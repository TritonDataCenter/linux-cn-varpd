/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>

#include "link.h"

bool
plumb_links(const char *filename)
{
	return (true);	/* XXX KEBE SAYS placeholder */
}

void
replumb_links(const char *filename)
{
	/* XXX KEBE SAYS quiesce things... */

	if (!plumb_links(filename))
		errx(-1, "plumb_links() failed.\n");
}

int
new_netlink(void)
{
	struct sockaddr_nl kernel_nladdr = {
	    .nl_family = AF_NETLINK,
	    .nl_groups = (RTMGRP_LINK | RTMGRP_IPV4_ROUTE | RTMGRP_NEIGH),
	    .nl_pid = getpid()
	};
	int netlink_fd;

	/*
	 * XXX KEBE ASKS - is NETLINK_ROUTE with the above RTMGRP_* flags
	 * sufficient to get everything we need?
	 */
	netlink_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (netlink_fd == -1) {
		warnx("socket(AF_NETLINK)");
		return (-1);
	}

	if (bind(netlink_fd, (struct sockaddr *)&kernel_nladdr,
	    sizeof (kernel_nladdr)) != -1) {
		return (netlink_fd);
	} else
		err(-1, "bind()");

fail:
	(void) close(netlink_fd);
	return (-1);
}
