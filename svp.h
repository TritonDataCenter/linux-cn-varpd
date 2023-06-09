/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright 2023 MNX Cloud, Inc.
 */

#ifndef _SVP_H
#define	_SVP_H

#include "svp_prot.h"	/* Happily includes a bunch of things we need. */

#ifdef __cplusplus
extern "C" {
#endif

extern int new_svp(struct sockaddr_in *);
extern void handle_svp_inbound(int);
extern void send_l3_req(int32_t, uint8_t, uint8_t *);
extern void send_l2_req(int32_t, uint64_t);
#ifdef __cplusplus
}
#endif

#endif /* _SVP_H */
