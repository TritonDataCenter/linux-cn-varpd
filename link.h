/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

#ifndef _LINK_H
#define	_LINK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool plumb_links(const char *);
extern void replumb_links(const char *);
extern int new_netlink(void);
extern void handle_netlink_inbound(int);

#ifdef __cplusplus
}
#endif

#endif /* _LINK_H */
