/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

#include <stdbool.h>
#include <err.h>

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
	return (-1);  /* XXX KEBE SAYS placeholder */
}
