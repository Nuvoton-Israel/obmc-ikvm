/*
 * usb_hid.h
 *
 * Copyright (C) 2018 NUVOTON
 *
 * KW Liu <kwliu@nuvoton.com>
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; If not, see <http://www.gnu.org/licenses/>
 */

#include <rfb/rfb.h>
#include <rfb/keysym.h>
#include <rfb/rfbproto.h>
#include "usb_hid_key.h"

#define WO       "w"
#define RO       "r"
#define RW       "rw"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define DESC(_path, _p, _s)			\
	{							\
		.path	=	(_path),			\
		.ptr	=	(_p),				\
		.size	=	(_s),				\
	}

struct hid_init_desc {
    char *path;
    const void *ptr;
    int size;
};

typedef struct {
    int keycode;		/* Macintosh keycode. */
    unsigned long keysym;		/* X windows keysym. */
} KeyInfo;

typedef enum {
    USAGE_PAGE     = 0x05,
    USAGE          = 0x09,
    COLLECTION     = 0xA1,
    USAGE_MIN      = 0x19,
    USAGE_MAX      = 0x29,
    LOGICAL_MIN    = 0x15,
    LOGICAL_MAX    = 0x25,
    REPORT_COUNT   = 0x95,
    REPORT_SIZE    = 0x75,
    INPUT          = 0x81,
    OUTPUT         = 0x91,
    END_COLLECTION = 0xC0,
    LAST_ITEM      = 0xFF
} HID_report_items_t;

int hid_init(void);
void hid_close(void);
void keyboard(rfbBool down, rfbKeySym keysym, rfbClientPtr client);
void pointer_event(int mask, int x, int y, rfbClientPtr client);
