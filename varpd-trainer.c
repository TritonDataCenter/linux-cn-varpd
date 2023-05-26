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
#include <linux/netlink.h>

/* Globals...  */
int netlink_fd;

static void
dump(uint8_t *msg, unsigned int nbytes)
{
	unsigned int i;

	for (i = 0; i < nbytes; i++) {
		(void) printf("0x%02x", msg[i]);
		if ((i & 0xf) == 0xf)
			(void) putchar('\n');
		else
			(void) putchar(' ');
	}
	(void) putchar('\n');
}

static void
msg_and_header(uint8_t *msg, size_t msgsize)
{
	time_t current_time = time(NULL);

	(void) printf("%s\n", ctime(&current_time));
	(void) printf("YES! Got netlink msg 0x%p, %d bytes, all bytes:\n",
	    msg, msgsize);
	dump(msg, msgsize);

	(void) printf("--> header bytes:\n");
	dump(msg, sizeof (struct nlmsghdr));

}

static void
process_netlink_msg(uint8_t *msg, size_t msgsize)
{
	struct nlmsghdr *nlmsg = (struct nlmsghdr *)msg;

	if (msgsize != nlmsg->nlmsg_len) {
		(void) printf("DANGER: msgsize %d != nlmsg_len %d, "
		    "continuing...\n", msgsize, nlmsg->nlmsg_len);
		/*
		 * XXX KEBE ASKS can we get two messages in one recv()?
		 * XXX KEBE ALSO ASKS can we get partial messages and need to
		 * recv()/read() more?
		 */
	}

	/* XXX KEBE ASKS perform additional reality checks here? */

	switch (nlmsg->nlmsg_type) {
#if 0
	case RTM_GETNEIGH:
		/*
		 * Steps to do:
		 *
		 * 1.) Check that ndm_ifindex is one of "ours".
		 * 2.) If it is, confirm its state is correct.
		 * 3.) Extract the destination IP address.
		 * 4.) Query SVP/portolan with an SVP_R_VL3_REQ.
		 * 5.) Feed the VLAN MAC in ("ip neigh add..." equivalent).
		 * 6.) Feed the VXLAN MAC2IP ("bridge fdb add..." equivalent).
		 *
		 * Some notes:
		 * - We will need to dynamically update which VLAN NICs are
		 *   fabrics, as well as their underlying corresponding VXLAN
		 *   NICs.
		 * - We will need to know the IP of the SVP/portolan server and
		 *   port.
		 * - We will need to have established a TCP connection to said
		 *   server.
		 * 
		 */
		break;
	case RTM_NEWNEIGH:
		/*
		 * Likely a resolution that we didn't resolve fast enough.
		 * Also likely for ones we got nothing back from SVP/portolan
		 * we'll see these (and maybe we should keep stats?).
		 * We might also see these for neighbor entries that go stale,
		 * and maybe we should re-query SVP/portolan?
		 */
		break;
#endif
	case RTM_NEWNEIGH:
	case RTM_GETNEIGH: {
		struct ndmsg *ndm = (struct ndmsg *)(nlmsg + 1);
		struct rtattr *rta1, *rta2, *rta3;
		
		/*
		 * XXX KEBE SAYS skip indexes less than 5 to avoid common
		 * NICs.
		 */
		if (ndm->ndm_ifindex < 5) {
			(void) printf("Ignoring index %d\n", ndm->ndm_ifindex);
			return;
		}

		/* Dump raw message and base header now. */
		msg_and_header(msg, msgsize);

		(void) printf("RTM_%sNEIGH (sizeof (*ndm) = %u)\n",
		    (nlmsg->nlmsg_type == RTM_NEWNEIGH ? "NEW" : "GET"),
		    sizeof (*ndm));
		(void) printf("ndm_family = %u, ndm_ifindex = %d, "
		    "ndm_state = %u, ndm_flags = 0x%x,\nndm_type = %d\n",
		    ndm->ndm_family, ndm->ndm_ifindex, ndm->ndm_state,
		    ndm->ndm_flags, ndm->ndm_type);
		dump((uint8_t *)ndm, sizeof (*ndm));
		/* XXX KEBE SAYS CONTINUE HERE WITH ATTRIBUTES... */
		rta1 = (struct rtattr *)((uint8_t *)ndm + sizeof (*ndm));
		(void) printf("#1 rta_len = %d, rta_type = %d\n",
		    rta1->rta_len, rta1->rta_type);
		dump((uint8_t *)rta1, RTA_ALIGN(rta1->rta_len));
		rta2 = (struct rtattr *)
		    ((uint8_t *)rta1 + RTA_ALIGN(rta1->rta_len));
		(void) printf("#2 rta_len = %d, rta_type = %d\n",
		    rta2->rta_len, rta2->rta_type);
		dump((uint8_t *)rta2, RTA_ALIGN(rta2->rta_len));
		rta3 = (struct rtattr *)
		    ((uint8_t *)rta2 + RTA_ALIGN(rta2->rta_len));
		(void) printf("#3 rta_len = %d, rta_type = %d\n",
		    rta3->rta_len, rta3->rta_type);
		dump((uint8_t *)rta3, RTA_ALIGN(rta3->rta_len));

		switch (ndm->ndm_type) {
		case NDA_DST:
			(void) printf("NDA_DST!!!!\n");
			break;
		default:
			(void) printf("Unknown %d\n", ndm->ndm_type);
			break;
		}
		break;
	}
	default:
		(void) printf("Message type %d/0x%x\n", nlmsg->nlmsg_type,
			nlmsg->nlmsg_type);
		break;
	}

	putchar('\n');
	putchar('\n');
	putchar('\n');
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
