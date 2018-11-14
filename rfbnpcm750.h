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
#include <sys/epoll.h>
#include <pthread.h>

#define RAWFB_MMAP 1
#define RAWFB_FILE 2

#define MAXEVENTS 64

struct ece_ioctl_cmd
{
    unsigned int framebuf;
    unsigned int gap_len;
    char *buf;
    int len;
    int x;
    int y;
    int w;
    int h;
    int lp;
};

#define ECE_IOC_MAGIC 'k'
#define ECE_IOCGETED _IOR(ECE_IOC_MAGIC, 1, struct ece_ioctl_cmd)
#define ECE_IOCSETFB _IOW(ECE_IOC_MAGIC, 2, struct ece_ioctl_cmd)
#define ECE_IOCSETLP _IOW(ECE_IOC_MAGIC, 3, struct ece_ioctl_cmd)
#define ECE_IOCGET_OFFSET _IOR(ECE_IOC_MAGIC, 4, unsigned int)
#define ECE_IOCCLEAR_OFFSET _IO(ECE_IOC_MAGIC, 5)
#define ECE_IOC_MAXNR 5

struct vcd_info
{
    unsigned int vcd_fb;
    unsigned int pixelClock;
    unsigned int line_pitch;
    int hdisp;
    int hfrontporch;
    int hsync;
    int hbackporch;
    int vdisp;
    int vfrontporch;
    int vsync;
    int vbackporch;
    int refresh_rate;
    int hpositive;
    int vpositive;
    int bpp;
    int r_max;
    int g_max;
    int b_max;
    int r_shift;
    int g_shift;
    int b_shift;
};

struct rect
{
    unsigned int x;
    unsigned int y;
    unsigned int w;
    unsigned int h;
};

struct nu_rfb
{
    struct vcd_info vcd_info;
    struct rect *rect_table;
    char *fake_fb;
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
    int refresh_cnt;
    int id;
    int nRects;
    int frame_size;
    int dumpfps;
    int fps_cnt;
    unsigned char hsync_mode;
};

struct nu_cl
{
    int id;
    struct nu_rfb *nurfb;
};

#define VCD_IOC_MAGIC     'v'
#define VCD_IOCGETINFO	_IOR(VCD_IOC_MAGIC,  1, struct vcd_info)
#define VCD_IOCSENDCMD	_IOW(VCD_IOC_MAGIC,  2, int)
#define VCD_IOCCHKRES	_IOR(VCD_IOC_MAGIC,  3, int)
#define VCD_IOCGETDIFF	_IOR(VCD_IOC_MAGIC,  4, struct rect)
#define VCD_IOCDIFFCNT	_IOR(VCD_IOC_MAGIC,  5, int)
#define VCD_IOCDEMODE	_IOR(VCD_IOC_MAGIC,  6, unsigned char)
#define VCD_IOC_MAXNR     6

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
void *rfbNuResEventThread(void *ptr);
#endif