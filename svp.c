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
#define	svprr_l2r_vnetid svprr_l2r.l2req.l2r.sl2r_vnetid

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
		assert(transaction_tail == svpt);
		if (transaction_head == transaction_tail) {
			/* Final node out! */
			transaction_head = transaction_tail = NULL;
		} else {
			transaction_tail =
			    (svp_transaction_t *)(&svpt->svpt_ptpn);
		}
	}
	svpt->svpt_ptpn = NULL;
}

/* Remove from list before we return. */
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

/*
 * Extract ONE message from SVP
 */
void
handle_svp_inbound(int svp_fd)
{
	err(2, "send_l2_req() not yet built"); /* XXX KEBE SAYS FILL ME IN! */
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

	svprr->svprr_ver = SVP_CURRENT_VERSION;
	svprr->svprr_op = htons(SVP_R_VL3_REQ);
	svprr->svprr_size = htonl(sizeof (svp_vl3_req_t));
	if (our_svp_id == 0)
		our_svp_id = 1;
	svprr->svprr_id = our_svp_id++;

	memcpy(svprr->svprr_l3r_ip, addr, sizeof (struct in6_addr));
	/* Uggh, KEBE SAYS need index-to-vnetid, or passed in vnetid. */
	svprr->svprr_l3r_vnetid = htonl(4385813); /* Hardcode for now... */
	/* svprr->svprr_l3r_vnetid = index_to_vnetid(index); */
	svprr->svprr_l3r_type = (af == AF_INET6) ? SVP_VL3_IPV6 : SVP_VL3_IP;

	if (send(svp_fd, svpt, sizeof (svp_req_t) + sizeof (svp_vl3_req_t), 0)
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
