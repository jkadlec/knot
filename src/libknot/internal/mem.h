/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*!
 * \brief Create a copy of a binary buffer.
 *
 * Like \c strdup, but for binary data.
 */
uint8_t *knot_memdup(const uint8_t *data, size_t data_size);

/*! \brief Allocate memory or die. */
void* xmalloc(size_t l);

/*! \brief Reallocate memory or die. */
void *xrealloc(void *p, size_t l);

/*!
 * \brief Reserve new or trim excessive memory.
 *
 * \param p Double-pointer to memory region.
 * \param tlen Memory unit (f.e. sizeof(int) for int* array)
 * \param min Minimum number of items required.
 * \param allow Maximum extra items to keep (for trimming).
 * \param reserved Pointer to number of already reserved items.
 *
 * \note Example usage:
 * char *buf = NULL; size_t len = 0;
 * if (mreserve(&buf, sizeof(char), 6, 0, &len) == 0) {
 *   memcpy(buf, "hello", strlen("hello");
 *   if (mreserve(&buf, sizeof(char), 20, 0, &len) == 0) {
 *     strncat(buf, "!", 1);
 *     mreserve(&buf, sizeof(char), strlen("hello!")+1, 0, &len);
 *   }
 * }
 * free(buf);
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note Memory region will be left untouched if function fails.
 */
int mreserve(char **p, size_t tlen, size_t min, size_t allow, size_t *reserved);

/*!
 * \brief Format string and take care of allocating memory.
 *
 * \note sprintf(3) manual page reference implementation.
 *
 * \param fmt Message format.
 * \return formatted message or NULL.
 */
char *sprintf_alloc(const char *fmt, ...);

/*!
 * \brief Create new string from a concatenation of s1 and s2.
 *
 * \param s1 First string.
 * \param s2 Second string.
 *
 * \retval Newly allocated string on success.
 * \retval NULL on error.
 */
char *strcdup(const char *s1, const char *s2);

/*!
 * \brief Create a copy of a string skipping leading and trailing white spaces.
 *
 * \return Newly allocated string, NULL in case of error.
 */
char *strstrip(const char *str);
