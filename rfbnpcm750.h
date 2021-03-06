#ifndef RFBNPCM750_H
#define RFBNPCM750_H

/*
 * rfbnpcm750.h
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
#include <stdint.h>
#include <rfb/rfb.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <rfb/rfbconfig.h>
#include "config.h"

#ifdef KEYBOARD_EVENT
#include <sys/epoll.h>
#include <pthread.h>
#endif

#define RAWFB_MMAP 1
#define RAWFB_FILE 2

#define FAKE_FB_WIDTH 320
#define FAKE_FB_HEIGHT 240
#define FAKE_FB_BPP 2

#define REFRESHCNT 10

#ifdef KEYBOARD_EVENT
#define MAXEVENTS 64
#endif

struct ece_ioctl_cmd
{
    uint32_t framebuf;
    uint32_t gap_len;
    uint8_t *buf;
    uint32_t len;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t lp;
};

#define ECE_IOC_MAGIC 'k'
#define ECE_IOCGETED _IOR(ECE_IOC_MAGIC, 1, struct ece_ioctl_cmd)
#define ECE_IOCSETFB _IOW(ECE_IOC_MAGIC, 2, struct ece_ioctl_cmd)
#define ECE_IOCSETLP _IOW(ECE_IOC_MAGIC, 3, struct ece_ioctl_cmd)
#define ECE_IOCGET_OFFSET _IOR(ECE_IOC_MAGIC, 4, unsigned int)
#define ECE_IOCCLEAR_OFFSET _IO(ECE_IOC_MAGIC, 5)
#define ECE_IOCENCADDR_RESET _IO(ECE_IOC_MAGIC, 6)
#define ECE_RESET _IO(ECE_IOC_MAGIC, 7)
#define ECE_IOC_MAXNR 7

struct vcd_info
{
    uint32_t vcd_fb;
    uint32_t pixelClock;
    uint32_t line_pitch;
    uint32_t hdisp;
    uint32_t hfrontporch;
    uint32_t hsync;
    uint32_t hbackporch;
    uint32_t vdisp;
    uint32_t vfrontporch;
    uint32_t vsync;
    uint32_t vbackporch;
    uint32_t refresh_rate;
    uint32_t hpositive;
    uint32_t vpositive;
    uint32_t bpp;
    uint32_t r_max;
    uint32_t g_max;
    uint32_t b_max;
    uint32_t r_shift;
    uint32_t g_shift;
    uint32_t b_shift;
    uint32_t mode;
    uint32_t reg;
    uint32_t reg_val;
};

struct rect
{
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

struct nu_rfb
{
    struct vcd_info vcd_info;
    struct rect *rect_table;
    uint8_t fake_fb;
    char *raw_fb_addr;
    char *raw_hextile_addr;
    int raw_fb_mmap;
    int raw_hextile_mmap;
    int raw_fb_fd;
    int hextile_fd;
    unsigned int vcd_fb;
    unsigned int line_pitch;
    unsigned int rect_cnt;
    int res_changed;
    int last_mode;
    int cl_cnt;
    int id;
    int nRects;
    int frame_size;
    int dumpfps;
    int fps_cnt;
    int hsync_mode;
    unsigned char do_cmd;
    unsigned int width;
    unsigned int height;
    char sock_start;
    unsigned int rcvdCount[10];
    unsigned int refreshCount[10];
};

#define VCD_IOC_MAGIC 'v'
#define VCD_IOCGETINFO _IOR(VCD_IOC_MAGIC, 1, struct vcd_info)
#define VCD_IOCSENDCMD _IOW(VCD_IOC_MAGIC, 2, unsigned int)
#define VCD_IOCCHKRES _IOR(VCD_IOC_MAGIC, 3, int)
#define VCD_IOCGETDIFF _IOR(VCD_IOC_MAGIC, 4, struct rect)
#define VCD_IOCDIFFCNT _IOR(VCD_IOC_MAGIC, 5, int)
#define VCD_IOCDEMODE _IOR(VCD_IOC_MAGIC, 6, int)
#define VCD_IOCRESET _IO(VCD_IOC_MAGIC, 7)
#define VCD_GETREG _IOR(VCD_IOC_MAGIC, 8, struct vcd_info)
#define VCD_SETREG _IOW(VCD_IOC_MAGIC, 9, struct vcd_info)
#define VCD_IOC_MAXNR 9

#define CAPTURE_FRAME 0
#define CAPTURE_TWO_FRAMES 1
#define COMPARE 2

#define BitsPerSample 5
#define SamplesPerPixel 1
#define BytesPerPixel 2

struct nu_rfb *rfbInitNuRfb(int hsync_mode);
void rfbClearNuRfb(struct nu_rfb *nurfb);
void rfbNuInitRfbFormat(rfbScreenInfoPtr screen);
void rfbNuRunEventLoop(rfbScreenInfoPtr screen, long usec, rfbBool runInBackground);
rfbBool rfbNuResetVCD(struct nu_rfb *nurfb);
rfbBool rfbNuResetECE(struct nu_rfb *nurfb);
#ifdef KEYBOARD_EVENT
void *rfbNuKeyEventThread(void *ptr);
#endif
#endif
