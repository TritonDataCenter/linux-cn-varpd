/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>

#include "svp.h"
#include "link.h"

#define	LINUX_SYSFS_VNICS "/sys/devices/virtual/net"
#define	LINUX_PROCFS_VNICS_IPV4 "/proc/sys/net/ipv4/neigh"
#define	LINUX_PROCFS_VNICS_IPV6 "/proc/sys/net/ipv4/neigh"

static int32_t linktab_size = 0;	/* Same range as ifindex */
static fabric_link_t **linktab = NULL;
#define	LINKTAB_START_SIZE 64

static void
resize_linktab(int32_t newsize)
{
	fabric_link_t **newtab;
	size_t index;

	if (newsize <= linktab_size)
		return;	/* Never shrink. */
	if (newsize < 0) {
		errno = ERANGE;
		errx(-36, "new linktab size too big\n");
	}

	newtab = realloc(linktab, sizeof (fabric_link_t *) * newsize);
	if (newtab == NULL)
		errx(-30, "Can't grow linktab!");
	linktab = newtab;

	for (index = linktab_size; index < newsize; index++)
		newtab[index] = NULL;
	linktab_size = newsize;
}

static fabric_link_t *
update_link_entry(fabric_link_t *parent, const char *name, int32_t index,
	uint32_t id)
{
	fabric_link_t *dst;

	/* A while loop on the off chance we need to more-than-double. */
	while (index >= linktab_size) {
		errno = 0;
		warnx("Index %d forcing resize from %d to %d", index,
		    linktab_size, linktab_size * 2);
		resize_linktab(linktab_size * 2);
	}

	if (linktab[index] == NULL) {
		/* Just allocate, fill, and return. */
		dst = malloc(sizeof (*dst));
		if (dst == NULL)
			errx(-34, "Can't allocate new linktab entry!");
		linktab[index] = dst;
		dst->fl_vxlan = parent; /* Might be NULL... */
		dst->fl_ifindex = index;
		dst->fl_id = id;
		if (strlcpy(dst->fl_name, name, sizeof (dst->fl_name)) >=
		    sizeof (dst->fl_name)) {
			errno = EINVAL;
			errx(-35, "Oh wow, name(%s) is longer (%lu) than %lu",
			    name, strlen(name), sizeof (dst->fl_name));
		}
		return (dst);
	}

	dst = linktab[index];

	/*
	 * Verify things? Can use strcmp() because we have initialized ours
	 * properly.
	 */
	/*
	 * XXX KEBE ASKS do we ever think we'll change things w/o bumping the
	 * ifindex?
	 */
	if (dst->fl_vxlan != parent || dst->fl_ifindex != index ||
	    dst->fl_id != id || strcmp(dst->fl_name, name) != 0) {
		errno = EINVAL;
		err(-37, "OH NO MISMATCH");
	}

	return (dst);
}

static int32_t
nicdir_to_index(int nicfd)
{
	int indexfd;
	ssize_t indexlen;
	char indexstr[11];
	long index;

	indexfd = openat(nicfd, "ifindex", O_RDONLY);
	if (indexfd == -1)
		errx(-35, "openat(ifindex)");
	indexlen = read(indexfd, indexstr, sizeof (indexstr));
	if (indexlen == -1)
		errx(-35, "read(ifindex)");
	assert(indexstr[indexlen - 1] == '\n');
	indexstr[indexlen - 1] = '\0';
	errno = 0;
	index = strtol(indexstr, NULL, 10);
	if (errno != 0)
		errx(-35, "strtol(index)");
	/* Cheesy... */
	return ((int32_t)index);
}

void
scan_triton_fabrics(bool startup)
{
	DIR *sysfsd;
	struct dirent *vxlan;

	if (startup)
		resize_linktab(LINKTAB_START_SIZE);

	/*
	 * XXX KEBE ASKS if not startup, maybe need to check outstanding
	 * transactions for index values that no longer exist?
	 *
	 * It appears that linux ifindex values don't replace, they just
	 * monotonically increase.  For a long-lived CN with lots of
	 * bringups/teardowns I worry a bit, but let's assume MAX_INT32
	 * (0x7fffffff) is sufficient for now.
	 */

	sysfsd = opendir(LINUX_SYSFS_VNICS);
	if (sysfsd == NULL)
		errx(-22, "opendir(%s)", LINUX_SYSFS_VNICS);

	/* Okay, let's iterate... */
	errno = 0;	/* Start clean... */
	for (vxlan = readdir(sysfsd); vxlan != NULL; vxlan = readdir(sysfsd)) {
		DIR *vxland;
		struct dirent *uppers;
		int vxlanfd;
		uint32_t vnetid;
		int32_t index;
		fabric_link_t *vxlanlink;

		/*
		 * If sdcvxl*:
		 * - record $(dirent)/ifindex, name, .
		 */
		if (strncmp("sdcvxl", vxlan->d_name, 6) != 0)
			continue;
		vnetid = (uint32_t)strtoul(&(vxlan->d_name[6]), NULL, 10);
		if (vnetid == 0 || vnetid >= (16 * 1024 * 1024)) {
			warn("Parsing error on %s, got %u", vxlan->d_name,
			    vnetid);
			continue;
		}
		vxlanfd = openat(dirfd(sysfsd), vxlan->d_name, O_RDONLY);
		if (vxlanfd == -1) {
			warnx("openat(%s/%s) failed, continuing",
			    LINUX_SYSFS_VNICS, vxlan->d_name);
			continue;
		}
		vxland = fdopendir(vxlanfd);
		if (vxland == NULL) {
			warnx("fdopendir(%s/%s) failed, continuing",
			    LINUX_SYSFS_VNICS, vxlan->d_name);
			(void) close(vxlanfd);
		}
		/*
		 * Both nicdir_to_index() and update_link_entry() will bail on
		 * failure. If libc internals do bizarre things with vxlanfd,
		 * use dirfd() to be safe.
		 */
		index = nicdir_to_index(dirfd(vxland));
		vxlanlink =
		    update_link_entry(NULL, vxlan->d_name, index, vnetid);

		/* Find the lower layers. */
		for (uppers = readdir(vxland); uppers != NULL ;
		    uppers = readdir(vxland)) {
			uint32_t vid;
			int32_t vlanindex;
			char *vidstr;
			int vlanfd;

			/*
			 * - scan upper_* and hang 'em off
			 */
			if (strncmp("upper_vx", uppers->d_name, 8) != 0)
				continue;
			for (vidstr = &(uppers->d_name[8]); *vidstr != 'v';
			    vidstr++) {
			}
			vid = (uint32_t)strtoul(++vidstr, NULL, 10);
			if (vid == 0 || vid >= 1024) {
				warn("Parsing error on %s: got %u",
				    uppers->d_name, vid);
				continue;
			}
			vlanfd = openat(dirfd(vxland), uppers->d_name,
			    O_RDONLY);
			if (vlanfd == -1) {
				warnx("openat(.../%s) failed, continuing",
				    uppers->d_name);
				continue;
			}
			vlanindex = nicdir_to_index(vlanfd);
			/*
			 * Both nicdir_to_index() and update_link_entry() will
			 * bail on failure.
			 *
			 * Skip "upper_" part for name.
			 */
			(void) update_link_entry(vxlanlink,
			    &(uppers->d_name[6]), vlanindex, vid);
			if (close(vlanfd) == -1)
				errx(-32, "close(vlanfd)");
		}
		closedir(vxland);
	}
	if (vxlan != NULL || errno != 0)
		errx(-23, "readdir()");
	
}

fabric_link_t *
index_to_link(int32_t index)
{
	if (index >= linktab_size) {
		warnx("Index %d exceeds current table size %d", index,
		    linktab_size);
		return (NULL);
	}

	return (linktab[index]);
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
	 * NETLINK_ROUTE with the above RTMGRP_* flags should be sufficient to
	 * get everything we need.
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

	/* Should never reach... */
	(void) close(netlink_fd);
	return (-1);
}

/*
 * Extract ONE message from netlink.
 */
void
handle_netlink_inbound(int netlink_fd)
{
	uint8_t buf[4096];
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
		warn("DANGER: recvsize %ld != nlmsg_len %d, "
		    "dropping...\n", recvsize, nlmsg->nlmsg_len);
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
			warn("Unkown ndm_family %d", ndm->ndm_family);
			return;
		}
		/*
		 * Trigger SVP requests for both incomplete AND probe.
		 * XXX KEBE SAYS need to handle failures on both better.
		 */
		if (ndm->ndm_state != NUD_INCOMPLETE &&
		    ndm->ndm_state != NUD_PROBE) {
			/* Handle better? */
			warn("Unknown ndm_state 0x%x", ndm->ndm_state);
			return;
		}
		/* Right now assume NDA_DST is our only trigger. */
		if (ndm->ndm_type != NDA_DST) {
			/* Handle better? */
			warn("Unknown ndm_type 0x%x\n", ndm->ndm_type);
			return;
		}
		/* XXX KEBE ASKS WTF are the flags for?!? */

		/* Anyway, on to parsing the extensions. */
		readspot += sizeof (*nlmsg) + sizeof (*ndm);
		while (readspot < endspot) {
			struct rtattr *thisone = (struct rtattr *)readspot;

			rtas[thisone->rta_type] = thisone;
			warn("rta_type %d, rta_len = %d, advancing %u bytes",
			    thisone->rta_type, thisone->rta_len,
			    RTA_ALIGN(thisone->rta_len));
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
		 * We *do* need a way to convery the ifindex to a vnetid for
		 * SVP, but we let the SVP functions handle that themselves.
		 */
		switch (ndm->ndm_family) {
		case AF_INET: {
			struct in6_addr v6addr;

			/* Uggh, SVP requires v4mapped... do it here. */
			warn("Sending l3 req");
			IN6_INADDR_TO_V4MAPPED(
			    (struct in_addr *)RTA_DATA(rtas[RTA_DST]), &v6addr);
			send_l3_req(ndm->ndm_ifindex, AF_INET, v6addr.s6_addr);
			break;
		}
		case AF_INET6:
			warn("Sending l3 req (v6)");
			send_l3_req(ndm->ndm_ifindex, AF_INET6,
			    RTA_DATA(rtas[RTA_DST]));
			break;
		case AF_PACKET: {
			uint64_t arg = 0;

			warn("Sending l2 req");
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
	case RTM_NEWLINK:
	case RTM_DELLINK:
		/*
		 * Just rescan for now, but maybe call update_link_entry
		 * directly with message parms?
		 */
		scan_triton_fabrics(false);
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
