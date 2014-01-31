/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

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

#include <config.h>
#include <strings.h>

#include "knot/nameserver/chaos.h"
#include "common/descriptor.h"
#include "libknot/packet/pkt.h"

/*!
 * \brief Get a string result for a given TXT query.
 */
static const char *get_txt_response_string(const knot_nameserver_t *nameserver,
                                           const knot_dname_t *qname)
{
	char *qname_str = knot_dname_to_str(qname);
	const char *response = NULL;

	/* id.server and hostname.bind should have similar meaning */
	if (strcasecmp("id.server.",     qname_str) == 0 ||
	    strcasecmp("hostname.bind.", qname_str) == 0) {
		response = nameserver->identity;
	/* allow both version version.{server, bind}. for compatibility */
	} else if (strcasecmp("version.server.", qname_str) == 0 ||
	           strcasecmp("version.bind.",   qname_str) == 0) {
		response = nameserver->version;
	}

	free(qname_str);

	return response;
}

/*!
 * \brief Create TXT RR with a given string content.
 *
 * \param owner     RR owner name.
 * \param response  String to be saved in RDATA. Truncated to 255 chars.
 *
 * \return Allocated RRset or NULL in case of error.
 */
static knot_rrset_t *create_txt_rrset(const knot_dname_t *owner,
                                      const char *response)
{
	// truncate response to one TXT label
	size_t response_len = strlen(response);
	if (response_len > KNOT_DNAME_MAXLEN) {
		response_len = KNOT_DNAME_MAXLEN;
	}

	knot_dname_t *rowner = knot_dname_copy(owner);
	if (!rowner) {
		return NULL;
	}

	knot_rrset_t *rrset;
	rrset = knot_rrset_new(rowner, KNOT_RRTYPE_TXT, KNOT_CLASS_CH, 0);
	if (!rrset) {
		return NULL;
	}

	uint8_t *rdata = knot_rrset_create_rdata(rrset, response_len + 1);
	if (!rdata) {
		knot_rrset_deep_free(&rrset, 1);
		return NULL;
	}

	rdata[0] = response_len;
	memcpy(&rdata[1], response, response_len);

	return rrset;
}

/*!
 * \brief Create a response for a TXT CHAOS query.
 *
 * \param return KNOT_RCODE_NOERROR if the response was succesfully created,
 *               otherwise an RCODE representing the failure.
 */
static int answer_txt(knot_nameserver_t *nameserver, knot_pkt_t *response)
{
	const knot_dname_t *qname = knot_pkt_qname(response);
	const char *response_str = get_txt_response_string(nameserver, qname);
	if (response_str == NULL || response_str[0] == '\0') {
		return KNOT_RCODE_REFUSED;
	}

	knot_rrset_t *rrset = create_txt_rrset(qname, response_str);
	if (!rrset) {
		return KNOT_RCODE_SERVFAIL;
	}

	int result = knot_pkt_put(response, 0, rrset, KNOT_PF_FREE);
	if (result != KNOT_EOK) {
		knot_rrset_deep_free(&rrset, 1);
		return KNOT_RCODE_SERVFAIL;
	}

	return KNOT_RCODE_NOERROR;
}

int knot_chaos_answer(knot_pkt_t *pkt, knot_nameserver_t *ns)
{
	if (knot_pkt_qtype(pkt) != KNOT_RRTYPE_TXT) {
		return KNOT_RCODE_REFUSED;
	}

	return answer_txt(ns, pkt);
}