/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

/*
 * Let's establish a netlink listener...
 */

/* XXX KEBE SAYS CARGO CULT INCLUDES... */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <err.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>

/* Globals...  */
int netlink_fd;

static void
dump16(uint8_t *msg)
{
	int i;

	for (i = 0; i < 16; i++) {
		(void) printf("0x%02x", msg[i]);
		if (i < 15)
			(void) putchar(' ');
	}
	(void) putchar('\n');
}

static void
process_netlink_msg(uint8_t *msg, size_t msgsize)
{
	(void) printf("YES! Got netlink msg 0x%p, %d bytes, first 16 bytes:\n",
	    msg, msgsize);
	dump16(msg);
}


int
main(int argc, char *argv[])
{
	struct sockaddr_nl kernel_nladdr = {
	    .nl_family = AF_NETLINK,
	    .nl_groups = (RTMGRP_LINK | RTMGRP_IPV4_ROUTE | RTMGRP_NEIGH),
	    .nl_pid = getpid()
	};
	uint8_t msg[8192];	/* Better size? */
	ssize_t msgsize;

	/*
	 * XXX KEBE ASKS - is NETLINK_ROUTE with the above RTMGRP_* flags
	 * sufficient to get everything we need?
	 */
	netlink_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (netlink_fd == -1)
		err(-1, "socket(AF_NETLINK)");

	if (bind(netlink_fd, (struct sockaddr *)&kernel_nladdr,
	    sizeof (kernel_nladdr)) == -1) {
		err(-1, "bind()");
	}
	(void) printf("Wow, looks like we opened %d and bound it!\n",
	    netlink_fd);

	for (; ;) {
		msgsize = recv(netlink_fd, &msg, sizeof (msg), 0);
		if (msgsize == -1)
			err(-1, "recv()");

		process_netlink_msg(msg, msgsize);
	}

	return (0);
}
