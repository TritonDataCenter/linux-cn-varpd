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

/* Because *^#@$-ing glibc. */
extern size_t strlcpy(char *, const char *, size_t);

typedef struct fabric_link_s {
	struct fabric_link_s *fl_vxlan;	/* Points to vlan's vxlan if a vlan. */
	char fl_name[16];		/* Name, Linux-capped at 15 + '\0' */
	int32_t fl_ifindex;		/* Linux interface index */
	uint32_t fl_id;			/* VID if vlan, vnetid if vxlan */
} fabric_link_t;

extern void scan_triton_fabrics(const char *, int32_t);
extern int new_netlink(void);
extern void handle_netlink_inbound(int);
extern fabric_link_t *index_to_link(int32_t);

#ifdef __cplusplus
}
#endif

#endif /* _LINK_H */
