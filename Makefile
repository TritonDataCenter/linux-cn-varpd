#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

#
# Copyright 2023 MNX Cloud, Inc.
#

OBJECTS = link.o main.o svp.o strlcpy.o

CFLAGS += -m64 -Wall
#DEBUGFLAGS = -g
CFLAGS += $(DEBUGFLAGS)

all: varpd

varpd: $(OBJECTS)
	cc $(DEBUGFLAGS) -o varpd *.o

$(OBJECTS): %.o: %.c

varpd-trainer: varpd-trainer.c
	cc -o varpd-trainer varpd-trainer.c

clean clobber:
	/bin/rm -f varpd-trainer varpd *.o
