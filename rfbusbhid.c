/*
 * usb_hid.c
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

#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "rfbnpcm750.h"
#include "rfbusbhid.h"

#define IDVENDOR "/sys/kernel/config/usb_gadget/hid/idVendor"
#define IDPRODUCT "/sys/kernel/config/usb_gadget/hid/idProduct"
#define BCDDEVICE "/sys/kernel/config/usb_gadget/hid/bcdDevice"
#define BCDUSB "/sys/kernel/config/usb_gadget/hid/bcdUSB"
#define BMAXPACKERSIZE0 "/sys/kernel/config/usb_gadget/hid/bMaxPacketSize0"
#define SERIALNUMBER "/sys/kernel/config/usb_gadget/hid/strings/0x409/serialnumber"
#define MANUFACTURER "/sys/kernel/config/usb_gadget/hid/strings/0x409/manufacturer"
#define PRODUCT "/sys/kernel/config/usb_gadget/hid/strings/0x409/product"
#define ATTRIBUTES "/sys/kernel/config/usb_gadget/hid/configs/c.1/bmAttributes"
#define MAXPOWER "/sys/kernel/config/usb_gadget/hid/configs/c.1/MaxPower"
#define USB0 "/sys/kernel/config/usb_gadget/hid/functions/hid.usb0"
#define USB1 "/sys/kernel/config/usb_gadget/hid/functions/hid.usb1"
#define CONF0 "/sys/kernel/config/usb_gadget/hid/configs/c.1/hid.usb0"
#define CONF1 "/sys/kernel/config/usb_gadget/hid/configs/c.1/hid.usb1"
#define K_PROROCOL "/sys/kernel/config/usb_gadget/hid/functions/hid.usb0/protocol"
#define K_SUBCLASS "/sys/kernel/config/usb_gadget/hid/functions/hid.usb0/subclass"
#define K_REPORTLENGTH "/sys/kernel/config/usb_gadget/hid/functions/hid.usb0/report_length"
#define K_REPORTDESC "/sys/kernel/config/usb_gadget/hid/functions/hid.usb0/report_desc"
#define M_PROROCOL "/sys/kernel/config/usb_gadget/hid/functions/hid.usb1/protocol"
#define M_SUBCLASS "/sys/kernel/config/usb_gadget/hid/functions/hid.usb1/subclass"
#define M_REPORTLENGTH "/sys/kernel/config/usb_gadget/hid/functions/hid.usb1/report_length"
#define M_REPORTDESC "/sys/kernel/config/usb_gadget/hid/functions/hid.usb1/report_desc"
#define CONFIGURATION "/sys/kernel/config/usb_gadget/hid/configs/c.1/strings/0x409/configuration"
#define UDC "/sys/kernel/config/usb_gadget/hid/UDC"

#define MOUSE_ABS_RES 2032

#define KB_DEV "/dev/hidg0"
#define MS_DEV "/dev/hidg1"
#define USB_DEV_NAME "f0830000.udc"

static int mouse_fd = -1;
static int keyboard_fd = -1;

static const unsigned char hid_report_mouse[] =
    {
        0x05, 0x01,       // USAGE_PAGE (Generic Desktop)
        0x09, 0x02,       // USAGE (Mouse)
        0xa1, 0x01,       // COLLECTION (Application)
        0x09, 0x01,       //   USAGE (Pointer)
        0xa1, 0x00,       //   COLLECTION (Physical)
        0x05, 0x09,       //     USAGE_PAGE (Button)
        0x19, 0x01,       //     USAGE_MINIMUM (Button 1)
        0x29, 0x03,       //     USAGE_MAXIMUM (Button 3)
        0x15, 0x00,       //     LOGICAL_MINIMUM (0)
        0x25, 0x01,       //     LOGICAL_MAXIMUM (1)
        0x95, 0x03,       //     REPORT_COUNT (3)
        0x75, 0x01,       //     REPORT_SIZE (1)
        0x81, 0x02,       //     INPUT (Data,Var,Abs)
        0x95, 0x01,       //     REPORT_COUNT (1)
        0x75, 0x05,       //     REPORT_SIZE (5)
        0x81, 0x03,       //     INPUT (Cnst,Var,Abs)
        0x05, 0x01,       //     USAGE_PAGE (Generic Desktop)
        0x09, 0x30,       //     USAGE (X)
        0x09, 0x31,       //     USAGE (Y)
        0x35, 0x00,       //     PHYSICAL_MINIMUM (0)
        0x46, 0xff, 0x7f, //     PHYSICAL_MAXIMUM (32767)
        0x15, 0x00,       //     LOGICAL_MINIMUM (0)
        0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
        0x65, 0x11,       //     UNIT (SI Lin:Distance)
        0x55, 0x00,       //     UNIT_EXPONENT (0)
        0x75, 0x10,       //     REPORT_SIZE (16)
        0x95, 0x02,       //     REPORT_COUNT (2)
        0x81, 0x02,       //     INPUT (Data,Var,Abs)
        0x09, 0x38,       //     USAGE (Wheel)
        0x15, 0xff,       //     LOGICAL_MINIMUM (-1)
        0x25, 0x01,       //     LOGICAL_MAXIMUM (1)
        0x35, 0x00,       //     PHYSICAL_MINIMUM (0)
        0x45, 0x00,       //     PHYSICAL_MAXIMUM (0)
        0x75, 0x08,       //     REPORT_SIZE (8)
        0x95, 0x01,       //     REPORT_COUNT (1)
        0x81, 0x06,       //     INPUT (Data,Var,Rel)
        0xc0,             //   END_COLLECTION
        0xc0,             // END_COLLECTION
    };

/* keyboard Report descriptor */
static const unsigned char hid_report_keyboard[] =
    {
        0x05, 0x01,       // Usage Page (Generic Desktop Control)
        0x09, 0x06,       // Usage (Keyboard)
        0xA1, 0x01,       // Collection (Application)
        0x05, 0x07,       // Usage Page (Keyboard)
        0x19, 0xE0,       // Usage Minimum (224) (Left Control)
        0x29, 0xE7,       // Usage Maximum (231) (Right Control)
        0x15, 0x00,       // Logical Minimum (0)
        0x25, 0x01,       // Logical Maximum (1)
        0x75, 0x01,       // Report Size (1)
        0x95, 0x08,       // Report Count (8)
        0x81, 0x02,       // Input (Data, Variable, Absolute, Bit Field)
        0x95, 0x01,       // Report Count (1)
        0x75, 0x08,       // Report Size (8)
        0x81, 0x01,       // Input (Constant, Array, Absolute, Bit Field)
        0x95, 0x05,       // Report Count (5)
        0x75, 0x01,       // Report Size (1)
        0x05, 0x08,       // Usage Page (LEDs)
        0x19, 0x01,       // Usage Minimum (1) (Num Lock)
        0x29, 0x05,       // Usage Maximum (5) (Kana)
        0x91, 0x02,       // Output (Data, Value, Absolute, Non-volatile, Bit Field)
        0x95, 0x01,       // Report Count (1)
        0x75, 0x03,       // Report Size (3)
        0x91, 0x01,       // Output (Constant, Array, Absolute, Non-volatile, Bit Field)
        0x95, 0x06,       // Report Count (6)
        0x75, 0x08,       // Report Size (8)
        0x15, 0x00,       // Logical Minimum (0)
        0x26, 0xFF, 0x00, // Logical Maximum (255)
        0x05, 0x07,       // Usage Page (Keyboard)
        0x19, 0x00,       // Usage Minimum (0)
        0x2A, 0xFF, 0x00, // Usage Maximum (255)
        0x81, 0x00,       // Input (Data, Array, Absolute, Bit Field)
        0xC0              // End Collection
};

static KeyInfo keyArray[] = {
    {KEY_A, XK_A},
    {KEY_B, XK_B},
    {KEY_C, XK_C},
    {KEY_D, XK_D},
    {KEY_E, XK_E},
    {KEY_F, XK_F},
    {KEY_G, XK_G},
    {KEY_H, XK_H},
    {KEY_I, XK_I},
    {KEY_J, XK_J},
    {KEY_K, XK_K},
    {KEY_L, XK_L},
    {KEY_M, XK_M},
    {KEY_N, XK_N},
    {KEY_O, XK_O},
    {KEY_P, XK_P},
    {KEY_Q, XK_Q},
    {KEY_R, XK_R},
    {KEY_S, XK_S},
    {KEY_T, XK_T},
    {KEY_U, XK_U},
    {KEY_V, XK_V},
    {KEY_W, XK_W},
    {KEY_X, XK_X},
    {KEY_Y, XK_Y},
    {KEY_Z, XK_Z},
    {KEY_1, XK_exclam},
    {KEY_2, XK_at},
    {KEY_3, XK_numbersign},
    {KEY_4, XK_dollar},
    {KEY_5, XK_percent},
    {KEY_6, XK_asciicircum},
    {KEY_7, XK_ampersand},
    {KEY_8, XK_asterisk},
    {KEY_9, XK_parenleft},
    {KEY_0, XK_parenright},
    {KEY_1, XK_1},
    {KEY_2, XK_2},
    {KEY_3, XK_3},
    {KEY_4, XK_4},
    {KEY_5, XK_5},
    {KEY_6, XK_6},
    {KEY_7, XK_7},
    {KEY_8, XK_8},
    {KEY_9, XK_9},
    {KEY_0, XK_0},
    {KEY_F1, XK_F1},
    {KEY_F2, XK_F2},
    {KEY_F3, XK_F3},
    {KEY_F4, XK_F4},
    {KEY_F5, XK_F5},
    {KEY_F6, XK_F6},
    {KEY_F7, XK_F7},
    {KEY_F8, XK_F8},
    {KEY_F9, XK_F9},
    {KEY_F10, XK_F10},
    {KEY_F11, XK_F11},
    {KEY_F12, XK_F12},
    {KEY_SEMICOLON, XK_colon},
    {KEY_SEMICOLON, XK_semicolon},
    {KEY_APOSTROPHE, XK_apostrophe},
    {KEY_APOSTROPHE, XK_quotedbl},
    {KEY_COMMA, XK_comma},
    {KEY_COMMA, XK_less},
    {KEY_SLASH, XK_slash},
    {KEY_SLASH, XK_question},
    {KEY_DOT, XK_period},
    {KEY_DOT, XK_greater},
    {KEY_BACKSLASH, XK_backslash},
    {KEY_BACKSLASH, XK_bar},
    {KEY_LEFTBRACE, XK_bracketleft},
    {KEY_LEFTBRACE, XK_braceleft},
    {KEY_RIGHTBRACE, XK_bracketright},
    {KEY_RIGHTBRACE, XK_braceright},
    {KEY_MINUS, XK_minus},
    {KEY_MINUS, XK_underscore},
    {KEY_EQUAL, XK_equal},
    {KEY_EQUAL, XK_plus},
    {KEY_SPACE, XK_space},
    {KEY_GRAVE, XK_grave},
    {KEY_GRAVE, XK_asciitilde},
    {KEY_ENTER, XK_Return},
    {KEY_BACKSPACE, XK_BackSpace},
    {KEY_ESC, XK_Escape},
    {KEY_HOME, XK_Home},
    {KEY_END, XK_End},
    {KEY_INSERT, XK_Insert},
    {KEY_DELETE, XK_Delete},
    {KEY_TAB, XK_Tab},
    {KEY_CAPSLOCK, XK_Caps_Lock},
    {KEY_LEFTSHIFT, XK_Shift_L},
    {KEY_RIGHTSHIFT, XK_Shift_R},
    {KEY_LEFTALT, XK_Alt_L},
    {KEY_RIGHTALT, XK_Alt_R},
    {KEY_LEFTCTRL, XK_Control_L},
    {KEY_RIGHTCTRL, XK_Control_R},
    {KEY_SYSRQ, XK_Print},
    {KEY_UP, XK_Up},
    {KEY_LEFT, XK_Left},
    {KEY_DOWN, XK_Down},
    {KEY_RIGHT, XK_Right},
    {KEY_PAGEUP, XK_Prior},
    {KEY_PAGEDOWN, XK_Next},
    {KEY_NUMLOCK, XK_Num_Lock},
    {KEY_SCROLLLOCK, XK_Scroll_Lock},
    {KEY_PAUSE, XK_Pause},
    {KEY_KP0, XK_KP_0},
    {KEY_KP1, XK_KP_1},
    {KEY_KP2, XK_KP_2},
    {KEY_KP3, XK_KP_3},
    {KEY_KP4, XK_KP_4},
    {KEY_KP5, XK_KP_5},
    {KEY_KP6, XK_KP_6},
    {KEY_KP7, XK_KP_7},
    {KEY_KP8, XK_KP_8},
    {KEY_KP9, XK_KP_9},
    {KEY_KP0, XK_KP_Insert},
    {KEY_KP1, XK_KP_End},
    {KEY_KP2, XK_KP_Down},
    {KEY_KP3, XK_KP_Page_Down},
    {KEY_KP4, XK_KP_Left},
    {KEY_KP6, XK_KP_Right},
    {KEY_KP7, XK_KP_Home},
    {KEY_KP8, XK_KP_Up},
    {KEY_KP9, XK_KP_Page_Up},
    {KEY_KPMINUS, XK_KP_Subtract},
    {KEY_KPPLUS, XK_KP_Add},
    {KEY_KPENTER, XK_KP_Enter},
    {KEY_KPDOT, XK_KP_Delete},
    {KEY_KPDOT, XK_KP_Decimal},
    {KEY_KPEQUAL, XK_KP_Equal},
    {KEY_KPASTERISK, XK_KP_Multiply},
    {KEY_KPSLASH, XK_KP_Divide},
    {KEY_COMPOSE, XK_Menu},
    {0x08, XK_Super_L},
    {0x80, XK_Super_R},
    {0, 0}};

static struct hid_init_desc _hid_init_desc[] = {
    DESC(IDVENDOR, "0x046d", 8),
    DESC(IDPRODUCT, "0xc077", 8),
    DESC(BCDDEVICE, "0x7200", 8),
    DESC(BCDUSB, "0x0200", 8),
    DESC(BMAXPACKERSIZE0, "0x08", 4),
    DESC(SERIALNUMBER, "OBMC0001", 8),
    DESC(MANUFACTURER, "OpenBMC", 8),
    DESC(PRODUCT, "virtual_input", 16),
    DESC(ATTRIBUTES, "0xe0", 4),
    DESC(MAXPOWER, "0xc8", 4),
    DESC(K_PROROCOL, "0x01", 4),
    DESC(K_SUBCLASS, "0x01", 4),
    DESC(K_REPORTLENGTH, "0x40", 4),
    DESC(K_REPORTDESC, &hid_report_keyboard, sizeof(hid_report_keyboard)),
    DESC(M_PROROCOL, "0x02", 4),
    DESC(M_SUBCLASS, "0x01", 4),
    DESC(M_REPORTLENGTH, "0x06", 4),
    DESC(M_REPORTDESC, &hid_report_mouse, sizeof(hid_report_mouse)),
    DESC(CONFIGURATION, "Conf 1", 6),
};

static unsigned char keyboard_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static int last_write = 2;
static unsigned char mod = 0;
static uint8_t KEY_REPORT_LENGTH = 8;

void sendWakeupPacket(void)
{
    uint8_t wakeupReport[KEY_REPORT_LENGTH];

    memset(wakeupReport, 0, sizeof(wakeupReport));

    if (keyboard_fd >= 0)
    {
        memset(&wakeupReport[0], 0, KEY_REPORT_LENGTH);

        wakeupReport[0] = 0x02; //XK_Shift_L

        if (write(keyboard_fd, wakeupReport, KEY_REPORT_LENGTH) !=
            KEY_REPORT_LENGTH)
        {
            printf("Failed to write keyboard report");
            return;
        }

        wakeupReport[0] = 0;

        if (write(keyboard_fd, wakeupReport, KEY_REPORT_LENGTH) !=
            KEY_REPORT_LENGTH)
        {
            printf("Failed to write keyboard report");
        }
    }
}

static int keyboard_iow(int down, unsigned long keysym)
{
    int i = 0;
    unsigned char code = 0;
    int retryCount = 5;
    KeyInfo *kPtr;

    if (keysym >= 0x61 && keysym <= 0x7a)
        keysym -= 0x20;

    for (kPtr = keyArray; kPtr->keycode != 0; kPtr++)
        if (kPtr->keysym == keysym)
        {
            code = kPtr->keycode;
            break;
        }

    if (kPtr->keycode == 0)
        return 0;

    switch (keysym)
    {
    case XK_Control_L:
        mod = down ? mod | 0x01 : mod & ~0x01;
        break;
    case XK_Shift_L:
        mod = down ? mod | 0x02 : mod & ~0x02;
        break;
    case XK_Alt_L:
        mod = down ? mod | 0x04 : mod & ~0x04;
        break;
    case XK_Control_R:
        mod = down ? mod | 0x10 : mod & ~0x10;
        break;
    case XK_Shift_R:
        mod = down ? mod | 0x20 : mod & ~0x20;
        break;
    case XK_Alt_R:
        mod = down ? mod | 0x40 : mod & ~0x40;
        break;
    case XK_Super_L:
        mod = down ? mod | 0x08 : mod & ~0x08;
        break;
    case XK_Super_R:
        mod = down ? mod | 0x80 : mod & ~0x80;
        break;
    default:
        break;
    }

    keyboard_data[0] = mod;

    if (keysym < XK_Shift_L ||
        keysym > XK_Hyper_R ||
        keysym == XK_Caps_Lock)
    {
        if (down)
        {
            for (i = 2; i < 8; i++)
                if (keyboard_data[i] == 0)
                {
                    keyboard_data[i] = code;
                    last_write = i;
                    break;
                }
        }
        else
        {
            for (i = 2; i < 8; i++)
                if (keyboard_data[i] == code)
                {
                    keyboard_data[i] = 0;
                    break;
                }
        }
    }

    if (keyboard_fd < 0) {
        keyboard_fd = open(KB_DEV, O_RDWR);
    }

    if (keyboard_fd > -1) {
        while (retryCount > 0)
        {
            if (write(keyboard_fd, &keyboard_data, 8) == 8)
            {
                break;
            }

            if (errno != EAGAIN)
            {
                if (errno != ESHUTDOWN)
                {
                    printf("error: %s ", strerror(errno));
                }
                break;
            }
            retryCount--;
        }
    }

    return 0;
}

static int mouse_iow(int mask, int x, int y, int w, int h)
{
    uint8_t report[6] = {0, 0, 0, 0, 0, 0};
    int retryCount = 5;
    uint8_t wheel = 0;

    report[0] = ((mask & 0x4) >> 1) |
                ((mask & 0x2) << 1) | (mask & 0x1);

    if (x >= 0 && x < w)
    {
        uint16_t xx = (uint16_t)(x * (SHRT_MAX + 1) / w);
        memcpy(&report[1], &xx, 2);
    }

    if (y >= 0 && y < h)
    {
        uint16_t yy = (uint16_t)(y * (SHRT_MAX + 1) / h);
        memcpy(&report[3], &yy, 2);
    }

    if (mask == 8) {
        wheel = 1;
    }
    else if (mask == 16) {
        wheel = 0xff;
    }

    memcpy(&report[5], &wheel, 1);

    if (mouse_fd < 0) {
        mouse_fd = open(MS_DEV, O_WRONLY | O_NONBLOCK);
    }

    if (mouse_fd > -1)
    {
        while (retryCount > 0)
        {
            if (write(mouse_fd, &report, 6) == 6)
            {
                break;
            }

            if (errno != EAGAIN)
            {
                if (errno != ESHUTDOWN)
                {
                    printf("error: %s ", strerror(errno));
                }
                break;
            }
            retryCount--;
        }
    }


    return 0;
}

static int hid_f_write(char *path, const void *ptr, size_t size)
{
    FILE *pFile;
    pFile = fopen(path, WO);
    if (pFile < 0)
    {
        printf("failed to open %s \n", path);
        return -1;
    }
    fwrite(ptr, size, 1, pFile);
    fclose(pFile);
    return 0;
}

int hid_init(void)
{
    int i = 0;
    int nr_set = ARRAY_SIZE(_hid_init_desc);
    struct hid_init_desc *desc = _hid_init_desc;
    struct stat st = {0};

    if (stat("/sys/kernel/config/usb_gadget/hid", &st) == -1)
    {
        mkdir("/sys/kernel/config/usb_gadget/hid", 0755);
        mkdir("/sys/kernel/config/usb_gadget/hid/strings/0x409", 0755);
        mkdir("/sys/kernel/config/usb_gadget/hid/functions/hid.usb0", 0755);
        mkdir("/sys/kernel/config/usb_gadget/hid/functions/hid.usb1", 0755);
        mkdir("/sys/kernel/config/usb_gadget/hid/configs/c.1", 0755);
        mkdir("/sys/kernel/config/usb_gadget/hid/configs/c.1/strings/0x409", 0755);
    }

    for (i = 0; i < nr_set; i++)
    {
        hid_f_write(desc->path, desc->ptr, desc->size);
        desc++;
    }

    if (symlink(USB0, CONF0) < 0) {
        printf("symlink error: %s ", strerror(errno));
    }

    if (symlink(USB1, CONF1) < 0) {
        printf("symlink error: %s ", strerror(errno));
    }

    hid_f_write(UDC, USB_DEV_NAME, strlen(USB_DEV_NAME));

    keyboard_fd = open(KB_DEV, O_RDWR);
    if (keyboard_fd < 0) {
        printf("can not open %s error: %s\n", KB_DEV, strerror(errno));
    }

    mouse_fd = open(MS_DEV, O_WRONLY | O_NONBLOCK);
    if (mouse_fd < 0) {
        printf("can not open %s error: %s\n", MS_DEV, strerror(errno));
    }

    return 0;
}

void hid_close(void)
{
    close(mouse_fd);
    mouse_fd = -1;

    close(keyboard_fd);
    keyboard_fd = -1;
}

void keyboard(rfbBool down, rfbKeySym keysym, rfbClientPtr client)
{
    static rfbKeySym last_keysym = 0L;
    static int release_key = 0;

    if (keysym <= 0)
    {
        rfbLog("keyboard: skipping 0x0 keysym\n");
        return;
    }

    if (!down)
        release_key = 1;

    if (down)
    {
        int skip = 0;
        if ((release_key && last_keysym == keysym) ||
            last_keysym != keysym)
            skip = 0;
        else
            skip = 1;

        release_key = 0;

        if (skip)
        {
            last_keysym = keysym;
            return;
        }
    }
    keyboard_iow(down, keysym);
    last_keysym = keysym;
}

void pointer_event(int mask, int x, int y, rfbClientPtr client)
{
    struct nu_rfb *nurfb = (struct nu_rfb *)client->clientData;

    mouse_iow(mask, x, y, nurfb->vcd_info.hdisp, nurfb->vcd_info.vdisp);
    rfbDefaultPtrAddEvent(mask, x, y, client);
}
#ifdef KEYBOARD_EVENT
void *rfbNuKeyEventThread(void *ptr)
{
    struct nu_rfb *nurfb = (struct nu_rfb *)ptr;
    int ret, efd;
    struct epoll_event event;
    struct epoll_event *events;

    efd = epoll_create1(0);
    if (efd == -1)
    {
        rfbErr("epoll_create fail \n");
        return NULL;
    }

    event.data.fd = keyboard_fd;
    event.events = EPOLLIN | EPOLLET | EPOLLPRI;
    ret = epoll_ctl(efd, EPOLL_CTL_ADD, keyboard_fd, &event);
    if (ret == -1)
    {
        rfbErr("epoll_ctl fail\n");
        return NULL;
    }

    /* Buffer where events are returned */
    events = calloc(MAXEVENTS, sizeof(event));

    while (1)
    {
        int n, i;

        n = epoll_wait(efd, events, MAXEVENTS, -1);
        for (i = 0; i < n; i++)
        {
            if (keyboard_fd == events[i].data.fd)
            {
                char buffer[1];
                int nbytes;

                if (events[i].events & EPOLLIN)
                {
                    nbytes = read(keyboard_fd, buffer, sizeof(buffer));
                    rfbErr("nbytes %d byte0 %d\n", nbytes, buffer[0]);
                }
            }
        }
    }

    free(events);

    return NULL;
}
#endif
