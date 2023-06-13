/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>

#include "svp.h"
#include "link.h"

#define	SVP_PORT 1296	/* Should be in svp.h or its includes... */

/* Can be overridden by `-f $FILE` argument... */
char *nicfile = "/var/varpd/fabric-nics.txt";

static int
usage(const char *prog)
{
	(void) fprintf(stderr,
	    "Usage:  %s -a server-addr [-f FILE] [-p port] \n", prog);
	exit(1);
}

static void
do_sighup(int sig)
{
	if (sig != SIGHUP)
		errx(-1, "WTF signal-handler?!?\n");

	/* XXX KEBE WARNS, MAYBE CHECK FOR OUTSTANDING SVP TRANSACTIONS... */

	replumb_links(nicfile);
}

/* Keep this global... */
int svp_fd, netlink_fd;

int
main(int argc, char *argv[])
{
	uint16_t newport;
	int optchar, pollrc;
	struct sockaddr_in svp_sin = {
		.sin_family = AF_INET,
		.sin_port = htons(SVP_PORT),
	};
	struct sigaction sigact = {
		.sa_handler = do_sighup,
		
	};
	struct pollfd fds[2];

	while ((optchar = getopt(argc, argv, "f:p:a:")) != EOF) {
		switch (optchar) {
		case 'f':
			nicfile = optarg; /* XXX KEBE ASKS strdup() ? */
			break;
		case 'p':
			newport = atoi(optarg);
			if (newport == 0xFFFF || newport == 0) {
				warnx("bad port value");
				usage(argv[0]);
			}
			svp_sin.sin_port = htons(newport);
			break;
		case 'a':
			if (!inet_aton(optarg, &svp_sin.sin_addr)) {
				warnx("Invalid address: %s", optarg);
				usage(argv[0]);
			}
			break;
		default:
			return (usage(argv[0]));
		}
	}

	if (svp_sin.sin_addr.s_addr == INADDR_ANY) {
		warnx("Needs -a <addr>");
		usage(argv[0]);
	}

	/* Read "nicfile" and plumb nics. */
	if (!plumb_links(nicfile))
		errx(-1, "plumb_links() failed.\n");

	/*
	 * Because of multiple failure modes, new_svp() will print
	 * diagnostics.
	 */
	svp_fd = new_svp(&svp_sin);
	if (svp_fd == -1)
		errx(-3, "SVP server failure. ");

	/*
	 * Because of multiple failure modes, netlink_fd() will print
	 * diagnostics.
	 */
	netlink_fd = new_netlink();
	if (netlink_fd == -1)
		errx(-4, "netlink failure");

	/* Set up HUP handler... */
	if (sigaction(SIGHUP, &sigact, NULL) == -1)
		err(-2, "sigaction(): ");

	/* Build poll() loop here on netlink_fd and svp_fd. */
	fds[0].fd = svp_fd;
	fds[0].events = POLLIN;
	fds[0].revents = 0;
	fds[1].fd = netlink_fd;
	fds[1].events = POLLIN;
	fds[1].revents = 0;
	do {
		pollrc = poll(fds, 2, 60);	/* 60sec timeout? */
		if (pollrc <= 0)
			continue;	/* Will hit while-end and stop if -1. */
		/* svp_fd */
		if (fds[0].revents != 0) {
			handle_svp_inbound(svp_fd);
			fds[0].revents = 0;
		}

		/* netlink_fd */
		if (fds[1].revents != 0) {
			handle_netlink_inbound(netlink_fd);
			fds[1].revents = 0;
		}
	} while (pollrc != -1);
	
	warnx("poll() failure");
	exit(1);
}
