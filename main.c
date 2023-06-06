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

int
main(int argc, char *argv[])
{
	uint16_t newport;
	int optchar, svp_fd, netlink_fd;
	struct sockaddr_in svp_sin = {
		.sin_family = AF_INET,
		.sin_port = htons(SVP_PORT),
	};
	struct sigaction sigact = {
		.sa_handler = do_sighup,
		
	};

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

	/*
	 * XXX KEBE SAYS eventually do select() loop here on netlink_fd and
	 * svp_fd.
	 */

	/* XXX KEBE SAYS cheesy placeholder... */
	(void) printf("Okay, we're good: svp_fd == %d, netlink_fd == %d\n",
	    svp_fd, netlink_fd);
	(void) gets(&optchar, sizeof (optchar), stdin);
	exit(0);
}
