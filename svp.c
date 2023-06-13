/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

#include <unistd.h>
#include <err.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <netinet/in.h>

#include "svp.h"
#include "crc32.h"

static uint32_t our_svp_id = 1;	/* Will never be 0 */
extern int svp_fd;

typedef union svp_remotereq {
	svp_req_t svprr_head;
	struct {
		svp_req_t l3head;
		svp_vl3_req_t l3req;
	} svprr_l3r;
	struct {
		svp_req_t l2head;
		union {
			svp_vl2_req64_t l2r64;
			svp_vl2_req_t l2r;
		} l2req;
	} svprr_l2r;
	struct {
		svp_req_t l3head;
		svp_vl3_ack_t l3ack;
	} svprr_l3a;
	struct {
		svp_req_t l2head;
		svp_vl2_ack_t l2ack;
	} svprr_l2a;
} svp_remotereq_t;

#define	svprr_ver svprr_head.svp_ver
#define	svprr_op svprr_head.svp_op
#define	svprr_size svprr_head.svp_size
#define	svprr_id svprr_head.svp_id
#define	svprr_crc32 svprr_head.svp_crc32

#define	svprr_l3r_ip svprr_l3r.l3req.sl3r_ip
#define	svprr_l3r_type svprr_l3r.l3req.sl3r_type
#define	svprr_l3r_vnetid svprr_l3r.l3req.sl3r_vnetid

#define	svprr_l2r_macandpad svprr_l2r.l2req.l2r64.sl2r64_mac_and_pad
#define	svprr_l2r_mac svprr_l2r.l2req.l2r.sl2r_mac
#define	svprr_l2r_vnetid svprr_l2r.l2req.l2r.sl2r_vnetid

#define	svprr_l3a_status svprr_l3a.l3ack.sl3a_status
#define	svprr_l3a_mac svprr_l3a.l3ack.sl3a_mac
#define	svprr_l3a_port svprr_l3a.l3ack.sl3a_uport
#define	svprr_l3a_ip svprr_l3a.l3ack.sl3a_uip

#define	svprr_l2a_status svprr_l2a.l2ack.sl2a_status
#define	svprr_l2a_port svprr_l2a.l2ack.sl2a_port
#define	svprr_l2a_ip svprr_l2a.l2ack.sl2a_addr

typedef struct svp_transaction {
	/* XXX BEGIN LINKAGE XXX */

	/* Sigh... linux has nothing good like libavl so just list it for now. */
	struct svp_transaction **svpt_ptpn;  /* Must be first! */
	struct svp_transaction *svpt_next;

	/* XXX END LINKAGE XXX */
	svp_remotereq_t svpt_rr;
	int32_t svpt_index;
} svp_transaction_t;
#define	svpt_id svpt_rr.svprr_head.svp_id

static uint32_t svp_crc32_tab[] = { CRC32_TABLE };

/*
 * Self-conntained return of a complete, but not wire-ready, CRC32 value.
 */
uint32_t
svp_crc(void *pkt, size_t len)
{
	/* Use -1 as the initial crc32 value at the beginning. */
	uint32_t crc_val = -1;

	assert(((svp_req_t *)pkt)->svp_crc32 == 0);

	CRC32(crc_val, (uint8_t *)pkt, len, crc_val, svp_crc32_tab);
	return (~crc_val);
}

svp_transaction_t *transaction_head = NULL, *transaction_tail = NULL;

/* Tail insert for now. */
static void
insert_transaction(svp_transaction_t *svpt)
{
	svpt->svpt_next = NULL;
	if (transaction_tail == NULL) {
		assert(transaction_head == NULL);
		svpt->svpt_ptpn = &transaction_head;
	} else {
		transaction_tail->svpt_next = svpt;
		svpt->svpt_ptpn = &transaction_tail->svpt_next;
	}
	*(svpt->svpt_ptpn) = svpt;
	transaction_tail = svpt;
}

static void
remove_transaction(svp_transaction_t *svpt)
{
	*(svpt->svpt_ptpn) = svpt->svpt_next;
	if (svpt->svpt_next != NULL) {
		svpt->svpt_next->svpt_ptpn = svpt->svpt_ptpn;
		svpt->svpt_next = NULL;
	} else {
		/* Removing from the end. */
		assert(transaction_tail == svpt);
		if (transaction_head == NULL) {
			/* Final node out! Cleared by first line already! */
			transaction_tail = NULL;
		} else {
			/* works only if ptpn is first element in struct. */
			transaction_tail =
			    (svp_transaction_t *)(&svpt->svpt_ptpn);
		}
	}
	svpt->svpt_ptpn = NULL;
}

/* Remove from list before we return. Match on un-swapped ID. */
static svp_transaction_t *
find_transaction(uint32_t svp_id)
{
	svp_transaction_t *svpt;

	for (svpt = transaction_head; svpt != NULL; svpt = svpt->svpt_next) {
		if (svpt->svpt_id == svp_id)
			break;
	}

	if (svpt != NULL)
		remove_transaction(svpt);

	return (svpt);
}

static void
set_overlay_mac(uint8_t *mac, uint16_t *port, uint8_t *addr)
{
	warn("Setting mac!");
}

static void
set_overlay_ip(uint8_t *ip, uint8_t *mac)
{
	warn("Setting IP!");
}

int
new_svp(struct sockaddr_in *svp_sin)
{
	int svp_fd;
	svp_req_t svp;
	/* Use -1 as the initial crc32 value at the beginning. */
	uint32_t crc_holder, crc_val;
	ssize_t sendrecv_rc;

	/* Open a TCP connection to Triton's "Portolan" SVP service. */
	svp_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (svp_fd == -1) {
		warnx("socket(SVP)");
		return (-1);
	}

	if (connect(svp_fd, (struct sockaddr *)svp_sin, sizeof (*svp_sin)) ==
	    -1) {
		warnx("connect(SVP)");
		(void) close(svp_fd);
		return (-1);
	}

	/* Send an SVP ping message to confirm things. */
	svp.svp_ver = htons(SVP_CURRENT_VERSION);
	svp.svp_op = htons(SVP_R_PING);
	svp.svp_size = 0;
	svp.svp_id = 0xffffffff;  /* Normal traffic starts at 1... */
	svp.svp_crc32 = 0;
	svp.svp_crc32 = htonl(svp_crc(&svp, sizeof (svp)));

	sendrecv_rc = send(svp_fd, &svp, sizeof (svp), 0);
	if (sendrecv_rc == -1) {
		warnx("send(SVP PING)");
		goto fail;
	}
	if (sendrecv_rc != sizeof (svp)) {
		warnx("send(SVP PING) sent %ld bytes not %ld",
		    sendrecv_rc, sizeof (svp));
		goto fail;
	}

	sendrecv_rc = recv(svp_fd, &svp, sizeof (svp), 0);
	if (sendrecv_rc == -1) {
		warnx("recv(SVP PING)");
		goto fail;
	}
	if (sendrecv_rc != sizeof (svp)) {
		warnx("recv(SVP PING) got %ld bytes not %ld",
		    sendrecv_rc, sizeof (svp));
		goto fail;
	}

	crc_holder = ntohl(svp.svp_crc32);
	svp.svp_crc32 = 0;
	crc_val = svp_crc(&svp, sizeof (svp));
	/* For now just reality check the op. */
	if (crc_holder != crc_val) {
		warnx("crc mismatch. Wire's == 0x%x, Ours == 0x%x",
		    crc_holder, crc_val);
		goto fail;
	}

	if (svp.svp_op == htons(SVP_R_PONG)) {
		/* (void) printf("All good to go!\n"); */
		return (svp_fd);
	} else {
		warnx("Message type mismatch, got %d, expected %d",
		    ntohs(svp.svp_op), SVP_R_PONG);
	}

fail:
	(void) close(svp_fd);
	return (-1);
}

/* Assuming input is off the wire. May want transaction too... */
static bool
status_check(uint32_t wire_status)
{
	switch (ntohl(wire_status)) {
	case SVP_S_FATAL:
		err(-19, "SVP server returned SVP_S_FATAL. Aborting.");
		break;
	case SVP_S_NOTFOUND:
		/* This should be nominally silent. */
		warn("Request not found...");
		break;
	case SVP_S_BADL3TYPE:
		err(-18, "We apparently send a bad L3 type: not IPv4 or IPv6.");
		break;
	case SVP_S_BADBULK:
		err(-17, "WTF are we doing with BADBULK?");
		break;
	case SVP_S_OK:
		return (true);
	default:
		err(-20, "Invalid status value given: 0x%x\n",
		    ntohl(wire_status));
	}
	return (false);
}

/*
 * Extract ONE message from SVP
 */
void
handle_svp_inbound(int svp_fd)
{
	uint8_t buf[2048];
	uint8_t *next = buf;
	ssize_t recvlen = 0, payloadlen, chunk;
	svp_remotereq_t *svprr = (svp_remotereq_t *)buf;
	svp_req_t *svp_req = &svprr->svprr_head;
	svp_transaction_t *svpt;

	while (recvlen < sizeof (*svp_req)) {
		chunk = recv(svp_fd, next, sizeof (*svp_req) - recvlen, 0);
		if (chunk == -1)
			errx(-13, "handle_svp_inbound: recv()");

		recvlen += chunk;
	}

	/* Will a compiler save an actual call? */
	assert(svp_req->svp_ver == ntohs(SVP_CURRENT_VERSION));
	payloadlen = ntohl(svp_req->svp_size);
	if (payloadlen + sizeof (*svp_req) > sizeof (buf)) {
		err(-1, "Protocol issue: payload len %lu is more than %lu",
		    payloadlen + sizeof (*svp_req), sizeof (buf));
	}
	next += sizeof (*svp_req);

	while (payloadlen > 0) {
		chunk = recv(svp_fd, next, payloadlen, 0);
		if (chunk == -1)
			errx(-13, "handle_svp_inbound: recv()");

		payloadlen -= chunk;
	}

	svpt = find_transaction(svp_req->svp_id);
	if (svpt == NULL) {
		warn("handle_svp_inbound(): Can't find transaction 0x%u\n",
		    svp_req->svp_id);
		return;
	}

	/* Exploit REC/ACK adjacency for fun & profit... */
	if (ntohs(svp_req->svp_op) - 1 != ntohs(svpt->svpt_rr.svprr_op)) {
		warn("handle_svp_inbound(): req(0x%x)/ack(0x%x) mismatch",
		    ntohs(svpt->svpt_rr.svprr_op), ntohs(svp_req->svp_op));
		return;
	}
	switch (ntohs(svp_req->svp_op)) {
	case SVP_R_VL2_ACK:
		if (status_check(svprr->svprr_l2a_status)) {
			set_overlay_mac(svpt->svpt_rr.svprr_l2r_mac,
			    &svprr->svprr_l2a_port, svprr->svprr_l2a_ip);
		}
		break;
	case SVP_R_VL3_ACK:
		/*
		 * Have answers for both:
		 * Overlay MAC -> Underlay IP/port
		 * AND
		 * Overylay IP -> Overlay MAC.
		 *
		 * Set the Overlay MAC first, however.
		 */
		if (!status_check(svprr->svprr_l3a_status))
			break;

		set_overlay_mac(svprr->svprr_l3a_mac, &svprr->svprr_l3a_port,
		    svprr->svprr_l3a_ip);
		if (svpt->svpt_rr.svprr_l3r_type == ntohl(SVP_VL3_IP)) {
			assert(
			    IN6_IS_ADDR_V4MAPPED(svpt->svpt_rr.svprr_l3r_ip));
		} else {
			assert(svpt->svpt_rr.svprr_l3r_type ==
			    ntohl(SVP_VL3_IPV6) &&
			    !IN6_IS_ADDR_V4MAPPED(svpt->svpt_rr.svprr_l3r_ip));
		}
		set_overlay_ip(svpt->svpt_rr.svprr_l3r_ip,
		    svprr->svprr_l3a_mac);
		break;
	default:
		errx(-15, "handle_svp_inbound(): Should never reach, ack 0x%x "
		    "unimplmented\n", ntohs(svp_req->svp_op));
		break;
	}

	free(svpt);	/* We're done with the outstanding transaction. */
}

/*
 * Send an SVP_R_VL3_REQ
 */
void
send_l3_req(int32_t index, uint8_t af, uint8_t *addr)
{
	svp_transaction_t *svpt;
	svp_remotereq_t *svprr;

	svpt = calloc(1, sizeof (*svpt));
	if (svpt == NULL)
		errx(-10, "send_l3_req() - allocation failed\n");

	svpt->svpt_index = index;
	svprr = &svpt->svpt_rr;

	svprr->svprr_ver = htons(SVP_CURRENT_VERSION);
	svprr->svprr_op = htons(SVP_R_VL3_REQ);
	svprr->svprr_size = htonl(sizeof (svp_vl3_req_t));
	if (our_svp_id == 0)
		our_svp_id = 1;
	svprr->svprr_id = our_svp_id++;

	memcpy(svprr->svprr_l3r_ip, addr, sizeof (struct in6_addr));
	/* Uggh, KEBE SAYS need index-to-vnetid, or passed in vnetid. */
	svprr->svprr_l3r_vnetid = htonl(4385813); /* Hardcode for now... */
	/* svprr->svprr_l3r_vnetid = index_to_vnetid(index); */
	svprr->svprr_l3r_type = (af == AF_INET6) ?
	    htons(SVP_VL3_IPV6) : htonl(SVP_VL3_IP);
	svprr->svprr_crc32 = 0;
	svprr->svprr_crc32 =
	    htonl(svp_crc(svprr, sizeof (svp_req_t) + sizeof (svp_vl3_req_t)));

	if (send(svp_fd, svprr, sizeof (svp_req_t) + sizeof (svp_vl3_req_t), 0)
	    == -1) {
		warnx("send_l3_req: send()");
		free(svpt);
		return;
	}
	insert_transaction(svpt);
}

/*
 * Send an SVP_R_VL2_REQ
 */
void
send_l2_req(int32_t index, uint64_t mac_and_pad)
{
	err(2, "send_l2_req() not yet built"); /* XXX KEBE SAYS FILL ME IN! */
}
