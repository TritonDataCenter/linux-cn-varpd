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

int
main(int argc, char *argv[])
{
	struct sockaddr_nl kernel_nladdr = {
		.nl_family = AF_NETLINK,
		.nl_groups = 0,
		.nl_pid = 0
	};

	netlink_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (netlink_fd == -1)
		err(-1, "socket(AF_NETLINK)");
	(void) printf("Wow, looks like we opened it as %d\n", netlink_fd);

	return (0);
}
