/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

#include <sys/socket.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>
#include <stdio.h>
#include <string.h>

#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>

#include "svp.h"
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

/*
 * Extract ONE message from netlink.
 */
void
handle_netlink_inbound(int netlink_fd)
{
	uint8_t buf[1024];
	uint8_t *readspot = buf, *endspot;
	struct nlmsghdr *nlmsg = (struct nlmsghdr *)readspot;
	struct ndmsg *ndm;
	/* XXX KEBE ASKS overkill? Need we worry about more than one? */
	struct rtattr *rtas[RTA_MAX] = { NULL };
	ssize_t recvsize;

	/*
	 * We read this in all at once, since it's a kernel-originated
	 * datagram.
	 */
	recvsize = recv(netlink_fd, readspot, sizeof (buf), 0);
	if (recvsize == -1)
		errx(-7, "recv(netlink)");

	if (recvsize != nlmsg->nlmsg_len) {
		warn("DANGER: recvsize %d != nlmsg_len %d, "
		    "continuing...\n", recvsize, nlmsg->nlmsg_len);
		/* Continue for now... */
	}
	endspot = readspot;
	if (recvsize <= nlmsg->nlmsg_len)
		endspot += recvsize;
	else
		endspot += nlmsg->nlmsg_len;

	/*
	 * Right now we really only care about two kinds of messages:
	 * RTM_GETNEIGH and RTM_NEWNEIGH. RTM_NEWNEIGH indicates failure
	 * but keep listening for it just in case (including perhaps a
	 * neighbor going stale).
	 */
	switch (nlmsg->nlmsg_type) {
	case RTM_GETNEIGH:
		ndm = (struct ndmsg *)(nlmsg + 1);
		/*
		 * XXX KEBE SAYS reality-check index once we have a concept of
		 * what established VXLAN and VLAN-over-VXLAN state we cache.
		 */

		/* Reality check other ndm fields. */

		/* Only cope with these address requests... */
		if (ndm->ndm_family != AF_INET && ndm->ndm_family != AF_INET6 &&
		    ndm->ndm_family != AF_PACKET) {
		}
		/* Right now assume NUD_INCOMPLETE is our only trigger. */
		if (ndm->ndm_state != NUD_INCOMPLETE) {
			/* Handle better? */
			warn("Unknown ndm_state %0x%x\n", ndm->ndm_state);
			return;
		}
		/* Right now assume NDA_DST is our only trigger. */
		if (ndm->ndm_type != NDA_DST) {
			/* Handle better? */
			warn("Unknown ndm_type %0x%x\n", ndm->ndm_type);
			return;
		}
		/* XXX KEBE ASKS WTF are the flags for?!? */

		/* Anyway, on to parsing the extensions. */
		readspot = (uint8_t *)(sizeof (*nlmsg) + sizeof (*ndm));
		while (readspot < endspot) {
			struct rtattr *thisone = (struct rtattr *)readspot;

			rtas[thisone->rta_type] = thisone;
			warn("rta_type %d, rta_len = %d, advancing %d bytes\n",
			    thisone->rta_len, RTA_ALIGN(thisone->rta_len));
			readspot += RTA_ALIGN(thisone->rta_len);
		}

		/*
		 * Alright, I expect:
		 * - NDA_DST, a destination address (raw, use ndm_family above
		 *   for type)
		 * - NDA_CACHEINFO, stats on entries I don't need immediately.
		 * - NDA_PROBES, number of probes or probe number sent?!?
		 *
		 * Really I only need the NDA_DST to construct a portolan
		 * request.  Do the delayed ndm_family check here as well.
		 *
		 * UGGGH, XXX KEBE SAYS WE NEED A 
		 */
		switch (ndm->ndm_family) {
		case AF_INET: {
			struct in6_addr v6addr;

			/* Uggh, SVP requires v4mapped... do it here. */
			IN6_INADDR_TO_V4MAPPED(
			    (struct in_addr *)RTA_DATA(rtas[RTA_DST]), &v6addr);
			send_l3_req(ndm->ndm_ifindex, AF_INET, v6addr.s6_addr);
			break;
		}
		case AF_INET6:
			send_l3_req(ndm->ndm_ifindex, AF_INET6,
			    RTA_DATA(rtas[RTA_DST]));
			break;
		case AF_PACKET: {
			uint64_t arg = 0;

			memcpy(&arg, RTA_DATA(rtas[RTA_DST]), ETHERADDRL);
			/* Cheesy use of 64-bit ints for MAC. */
			send_l2_req(ndm->ndm_ifindex, arg);
			break;
		}
		default:
			/* Handle better? */
			warn("Unknown ndm_family %d\n", ndm->ndm_family);
		}
		break;
	case RTM_NEWNEIGH:
		break;
	default:
		/*
		 * Just ignore this one for now, whatever it is.  If we wanna
		 * get clever we can setup for interface/link events and
		 * construct internal state based on that instead of however
		 * we end up doing it via plumb_links() and replumb_links().
		 */
		break;
	}
}
