#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

#
# Copyright 2023 MNX Cloud, Inc.
#

varpd: varpd.o
	cc -o varpd varpd.o

varpd.o: varpd.c
	cc -c varpd.c

clean clobber:
	/bin/rm -f varpd varpd.o
