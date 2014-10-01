/*  Copyright (C) 2014 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_LMDB

#include <lmdb.h>

#include "common/namedb/namedb_lmdb.h"
#include "libknot/errcode.h"

struct lmdb_env
{
	MDB_dbi dbi;
	MDB_env *env;
	mm_ctx_t *pool;
};

static int dbase_open(struct lmdb_env *env, const char *handle)
{
	int ret = mdb_env_create(&env->env);
	if (ret != 0) {
		return ret;
	}

	ret = mdb_env_open(env->env, handle, 0, 0644);
	if (ret != 0) {
		mdb_env_close(env->env);
		return ret;
	}

	MDB_txn *txn = NULL;
	ret = mdb_txn_begin(env->env, NULL, 0, &txn);
	if (ret != 0) {
		mdb_env_close(env->env);
		return ret;
	}

	ret = mdb_open(txn, NULL, 0, &env->dbi);
	if (ret != 0) {
		mdb_txn_abort(txn);
		mdb_env_close(env->env);
		return ret;
	}

	ret = mdb_txn_commit(txn);
	if (ret != 0) {
		mdb_env_close(env->env);
		return ret;
	}

	return 0;
}

static void dbase_close(struct lmdb_env *env)
{
	mdb_close(env->env, env->dbi);
	mdb_env_close(env->env);
}

static knot_namedb_t *init(const char *handle, mm_ctx_t *mm)
{
	struct lmdb_env *env = mm_alloc(mm, sizeof(struct lmdb_env));
	if (env == NULL) {
		return NULL;
	}
	memset(env, 0, sizeof(struct lmdb_env));

	int ret = dbase_open(env, handle);
	if (ret != 0) {
		mm_free(mm, env);
		return NULL;
	}

	env->pool = mm;
	return env;
}

static void deinit(knot_namedb_t *db)
{
	if (db) {
		struct lmdb_env *env = db;

		dbase_close(env);
		mm_free(env->pool, env);
	}
}

static int txn_begin(knot_namedb_t *db, knot_txn_t *txn, unsigned flags)
{
	txn->db = db;
	txn->txn = NULL;

	unsigned txn_flags = 0;
	if (flags & KNOT_NAMEDB_RDONLY) {
		txn_flags |= MDB_RDONLY;
	}

	struct lmdb_env *env = db;
	int ret = mdb_txn_begin(env->env, NULL, txn_flags, (MDB_txn **)&txn->txn);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	return KNOT_EOK;
}

static int txn_commit(knot_txn_t *txn)
{
	int ret = mdb_txn_commit((MDB_txn *)txn->txn);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	return KNOT_EOK;
}

static void txn_abort(knot_txn_t *txn)
{
	mdb_txn_abort((MDB_txn *)txn->txn);
}

static int count(knot_txn_t *txn)
{
	struct lmdb_env *env = txn->db;

	MDB_stat stat;
	int ret = mdb_stat(txn->txn, env->dbi, &stat);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	return stat.ms_entries;
}

static int find(knot_txn_t *txn, knot_val_t *key, knot_val_t *val, unsigned flags)
{
	struct lmdb_env *env = txn->db;
	MDB_val db_key = { key->len, key->data };
	MDB_val data = { 0, NULL };


	int ret = mdb_get(txn->txn, env->dbi, &db_key, &data);
	if (ret != 0) {
		if (ret == MDB_NOTFOUND) {
			return KNOT_ENOENT;
		} else {
			return KNOT_ERROR;
		}
	}

	val->data = data.mv_data;
	val->len  = data.mv_size;
	return KNOT_EOK;
}

static int insert(knot_txn_t *txn, knot_val_t *key, knot_val_t *val, unsigned flags)
{
	struct lmdb_env *env = txn->db;

	MDB_cursor *cursor = NULL;
	int ret = mdb_cursor_open(txn->txn, env->dbi, &cursor);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	MDB_val db_key = { key->len, key->data };
	MDB_val data = { val->len, val->data };

	ret = mdb_cursor_get(cursor, &db_key, NULL, MDB_SET);
	if (ret != 0) {
		mdb_cursor_close(cursor);
		if (ret == MDB_NOTFOUND) {
			// Insert new item
			ret = mdb_put(txn->txn, env->dbi, &db_key, &data, 0);
			if (ret != 0) {
				return KNOT_ERROR;
			}

			return KNOT_EOK;
		} else {
			return ret;
		}
	}

	ret = mdb_cursor_put(cursor, &db_key, &data, MDB_CURRENT);
	mdb_cursor_close(cursor);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	return KNOT_EOK;
}

static int del(knot_txn_t *txn, knot_val_t *key)
{
	struct lmdb_env *env = txn->db;
	MDB_val db_key = { key->len, key->data };
	MDB_val data = { 0, NULL };

	int ret = mdb_del(txn->txn, env->dbi, &db_key, &data);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	return KNOT_EOK;
}

static knot_iter_t *iter_begin(knot_txn_t *txn, unsigned flags)
{
	struct lmdb_env *env = txn->db;
	MDB_cursor *cursor = NULL;

	int ret = mdb_cursor_open(txn->txn, env->dbi, &cursor);
	if (ret != 0) {
		return NULL;
	}

	ret = mdb_cursor_get(cursor, NULL, NULL, MDB_FIRST);
	if (ret != 0) {
		mdb_cursor_close(cursor);
		return NULL;
	}

	return cursor;
}

static knot_iter_t *iter_next(knot_iter_t *iter)
{
	MDB_cursor *cursor = iter;

	int ret = mdb_cursor_get(cursor, NULL, NULL, MDB_NEXT);
	if (ret != 0) {
		mdb_cursor_close(cursor);
		return NULL;
	}

	return cursor;
}

static int iter_key(knot_iter_t *iter, knot_val_t *key)
{
	MDB_cursor *cursor = iter;

	MDB_val mdb_key, mdb_val;
	int ret = mdb_cursor_get(cursor, &mdb_key, &mdb_val, MDB_GET_CURRENT);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	key->data = mdb_key.mv_data;
	key->len  = mdb_key.mv_size;
	return KNOT_EOK;
}

static int iter_val(knot_iter_t *iter, knot_val_t *val)
{
	MDB_cursor *cursor = iter;

	MDB_val mdb_key, mdb_val;
	int ret = mdb_cursor_get(cursor, &mdb_key, &mdb_val, MDB_GET_CURRENT);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	val->data = mdb_val.mv_data;
	val->len  = mdb_val.mv_size;
	return KNOT_EOK;
}

static void iter_finish(knot_iter_t *iter)
{
	if (iter == NULL) {
		return;
	}

	MDB_cursor *cursor = iter;
	mdb_cursor_close(cursor);
}

struct namedb_api *namedb_lmdb_api(void)
{
	static struct namedb_api api = {
		"lmdb",
		init, deinit,
		txn_begin, txn_commit, txn_abort,
		count, find, insert, del,
		iter_begin, iter_next, iter_key, iter_val, iter_finish
	};

	return &api;
}

#else

#include <stdlib.h>

struct namedb_api *namedb_lmdb_api(void)
{
	return NULL;
}

#endif