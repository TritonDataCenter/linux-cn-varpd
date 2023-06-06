/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

#include <err.h>

#include "svp.h"
#include "crc32.h"

static uint32_t svp_crc32_tab[] = { CRC32_TABLE };

int
new_svp(struct sockaddr_in *svp_sin)
{
	int svp_fd;
	svp_req_t svp;
	/* Use -1 as the initial crc32 value at the beginning. */
	uint32_t crc_val = -1, crc_holder;
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
	svp.svp_size = 0; /* XXX KEBE SAYS CONFIRM size excludes header... */
	svp.svp_id = htons(1);
	svp.svp_crc32 = 0;
	CRC32(crc_val, (uint8_t *)(&svp), sizeof (svp), crc_val, svp_crc32_tab);
	crc_val = ~crc_val;
	svp.svp_crc32 = htonl(crc_val);

	sendrecv_rc = send(svp_fd, &svp, sizeof (svp), 0);
	if (sendrecv_rc == -1) {
		warnx("send(SVP PING)");
		goto fail;
	}
	if (sendrecv_rc != sizeof (svp)) {
		warnx("send(SVP PING) sent %d bytes not %d",
		    sendrecv_rc, sizeof (svp));
		goto fail;
	}

	sendrecv_rc = recv(svp_fd, &svp, sizeof (svp), 0);
	if (sendrecv_rc == -1) {
		warnx("recv(SVP PING)");
		goto fail;
	}
	if (sendrecv_rc != sizeof (svp)) {
		warnx("recv(SVP PING) got %d bytes not %d",
		    sendrecv_rc, sizeof (svp));
		goto fail;
	}

	crc_holder = ntohl(svp.svp_crc32);
	svp.svp_crc32 = 0;
	crc_val = -1;
	CRC32(crc_val, (uint8_t *)(&svp), sizeof (svp), crc_val, svp_crc32_tab);
	crc_val = ~crc_val;
	/* For now just reality check the op. */
	if (crc_holder != crc_val) {
		warnx("crc mismatch. Wire's == 0x%x, Ours == 0x%x",
		    crc_holder, crc_val);
		goto fail;
	}

	if (svp.svp_op == htons(SVP_R_PONG)) {
		(void) printf("All good to go!\n");
		return (svp_fd);
	} else {
		warnx("Message type mismatch, got %d, expected %d",
		    ntohs(svp.svp_op), SVP_R_PONG);
	}

fail:
	(void) close(svp_fd);
	return (-1);
}
