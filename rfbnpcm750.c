/*
 * rfbnpcm750.c
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

#include "rfbnpcm750.h"
#include "rfbusbhid.h"

struct nu_rfb *nurfb_g;
static struct timespec start;

static int timediff(struct timespec *start, struct timespec *end)
{
	int diff = end->tv_sec - start->tv_sec;

    return diff;
}

rfbBool rfbNuResetVCD(struct nu_rfb *nurfb)
{
	int err;

	if ((err = ioctl(nurfb->raw_fb_fd, VCD_IOCRESET)) < 0)
	{
		rfbLog("vnc: vcd reset failed:%d\n", err);
		return FALSE;
	}


	return TRUE;
}

rfbBool rfbNuResetECE(struct nu_rfb *nurfb)
{
	int err;

	if ((err = ioctl(nurfb->hextile_fd, ECE_RESET)) < 0)
	{
		rfbLog("vnc: ece reset failed:%d\n", err);
		return FALSE;
	}

	return TRUE;
}

static rfbBool rfbNuResetENCAddr(struct nu_rfb *nurfb)
{
	int err;

	if ((err = ioctl(nurfb->hextile_fd, ECE_IOCENCADDR_RESET)) < 0)
	{
		rfbLog("vnc: enc addr reset failed:%d\n", err);
		return FALSE;
	}

	return TRUE;
}

rfbBool
rfbNuSendUpdateBuf(rfbClientPtr cl, char *buf, int len)
{
	if (cl->sock < 0)
		return FALSE;

	if (rfbWriteExact(cl, buf, len) < 0)
	{
		rfbErr("rfbNuSendUpdateBuf: write \n");
		rfbCloseClient(cl);
		return FALSE;
	}

	return TRUE;
}

void rfbNuInitRfbFormat(rfbScreenInfoPtr screen)
{
	struct vcd_info *info = &nurfb_g->vcd_info;
	rfbPixelFormat *format = &screen->serverFormat;

	screen->colourMap.count = 0;
	screen->colourMap.is16 = 0;
	screen->colourMap.data.bytes = NULL;
	format->bitsPerPixel = screen->bitsPerPixel;
	format->depth = screen->depth;
	format->bigEndian = FALSE;
	format->trueColour = TRUE;
	format->redMax = info->r_max;
	format->greenMax = info->g_max;
	format->blueMax = info->b_max;
	format->redShift = info->r_shift;
	format->greenShift = info->g_shift;
	format->blueShift = info->b_shift;
}

void rfbNuNewFramebuffer(rfbScreenInfoPtr screen, char *framebuffer,
						 int width, int height,
						 int bitsPerSample, int samplesPerPixel,
						 int bytesPerPixel)
{
	rfbClientIteratorPtr iterator;
	rfbClientPtr cl;

	/* Update information in the screenInfo structure */

	if (width & 3)
		rfbErr("WARNING: New width (%d) is not a multiple of 4.\n", width);

	screen->width = width;
	screen->height = height;
	screen->bitsPerPixel = screen->depth = 8 * bytesPerPixel;
	screen->paddedWidthInBytes = width * bytesPerPixel;

	rfbNuInitRfbFormat(screen);

	free(screen->frameBuffer);

	screen->frameBuffer = malloc(width * height * 2);//framebuffer;

	/* Adjust pointer position if necessary */

	if (screen->cursorX >= width)
		screen->cursorX = width - 1;

	if (screen->cursorY >= height)
		screen->cursorY = height - 1;

	/* For each client: */
	iterator = rfbGetClientIterator(screen);
	while ((cl = rfbClientIteratorNext(iterator)) != NULL)
	{
		/* Mark the screen contents as changed, and schedule sending
	NewFBSize message if supported by this client. */

		LOCK(cl->updateMutex);

		if (cl->useNewFBSize)
			cl->newFBSizePending = TRUE;

		TSIGNAL(cl->updateCond);
		UNLOCK(cl->updateMutex);
	}
	rfbReleaseClientIterator(iterator);
}

static int rfbNuGetVCDInfo(struct nu_rfb *nurfb, struct vcd_info *info)
{
	if (ioctl(nurfb->raw_fb_fd, VCD_IOCGETINFO, info) < 0)
	{
		rfbErr("get info failed\n");
		return -1;
	}

	return 0;
}

static int rfbNuSetVCDCmd(struct nu_rfb *nurfb, unsigned int cmd)
{
	if (ioctl(nurfb->raw_fb_fd, VCD_IOCSENDCMD, &cmd) < 0) {
		//rfbErr("vcd status: 0x%x \n", cmd);
		return -1;
	}

	return 0;
}

static int rfbNuChkVCDRes(struct nu_rfb *nurfb, rfbClientRec *cl)
{
	if (!cl)
		goto check; /* init state*/

	if (!nurfb->do_cmd)
		goto done;

check:
	if (ioctl(nurfb->raw_fb_fd, VCD_IOCCHKRES, &nurfb->res_changed) < 0)
	{
		rfbErr("VCD_IOCCHKRES failed\n");
		return -1;
	}

done:
	return nurfb->res_changed;
}

static int rfbNuSetVCDMode(struct nu_rfb *nurfb, int de_mode)
{
	if (ioctl(nurfb->raw_fb_fd, VCD_IOCDEMODE, &de_mode) < 0)
	{
		rfbErr("set vcd mode failed\n");
		return -1;
	}
	return 0;
}

static int rfbNuGetDiffCnt(rfbClientRec *cl)
{
	struct nu_rfb *nurfb = (struct nu_rfb*)cl->clientData;

	if (nurfb->do_cmd)
	{
		if (ioctl(nurfb->raw_fb_fd, VCD_IOCDIFFCNT, &nurfb->rect_cnt) < 0)
		{
			rfbErr("get rect cnt failed\n");
			return -1;
		}

		if (nurfb->rect_table)
		{
			free(nurfb->rect_table);
			nurfb->rect_table = NULL;
		}

		nurfb->rect_table = (struct rect *)malloc(sizeof(struct rect) * (nurfb->rect_cnt + 1));
	}

	return nurfb->rect_cnt;
}

rfbBool
rfbNuClearHextieDataOffset(struct nu_rfb *nurfb)
{
	int err;

	if ((err = ioctl(nurfb->hextile_fd, ECE_IOCCLEAR_OFFSET)) < 0)
	{
		rfbLog("vnc: clear offset failed:%d\n", err);
		return FALSE;
	}


	return TRUE;
}

static int
rfbNuGetHextieDataOffset(struct nu_rfb *nurfb)
{
	int err;
	uint32_t offset = 0;

	if ((err = ioctl(nurfb->hextile_fd, ECE_IOCGET_OFFSET, &offset)) < 0)
	{
		rfbLog("vnc: get offset failed:%d\n", err);
		return -1;
	}

	return offset;
}


static int rfbNuInitVCD(struct nu_rfb *nurfb, int first)
{
	struct vcd_info *vcd_info = &nurfb->vcd_info;
	struct ece_ioctl_cmd cmd;

	if (nurfb->last_mode == RAWFB_MMAP)
	{
		if (!nurfb->fake_fb)
		{
			munmap(nurfb->raw_fb_addr, nurfb->raw_fb_mmap);
		}
		else
		{
			free(nurfb->fake_fb);
			nurfb->fake_fb = NULL;
		}

		munmap(nurfb->raw_hextile_addr, nurfb->raw_hextile_mmap);

		nurfb->last_mode = 0;
	}

	if (nurfb->last_mode == 0 && first)
	{
		nurfb->raw_fb_fd = -1;
		nurfb->hextile_fd = -1;
	}

	if (first) {
		nurfb->raw_fb_fd = open("/dev/vcd", O_RDWR);
		if (nurfb->raw_fb_fd < 0)
		{
			rfbLog("failed to open /dev/vcd\n");
			goto error;
		}

		if (rfbNuSetVCDMode(nurfb, !nurfb->hsync_mode) < 0)
			goto error;
	}

	if (rfbNuGetVCDInfo(nurfb, vcd_info) < 0)
		goto error;

	nurfb->frame_size = vcd_info->hdisp * vcd_info->vdisp * vcd_info->bpp;

	if (first) {
		nurfb->hextile_fd = open("/dev/hextile", O_RDWR);
		if (nurfb->hextile_fd < 0)
		{
			rfbErr("failed to open /dev/hextile\n");
			goto error;
		}
	}

	rfbNuClearHextieDataOffset(nurfb);

	cmd.framebuf = vcd_info->vcd_fb;
	if (ioctl(nurfb->hextile_fd, ECE_IOCSETFB, &cmd) < 0)
	{
		rfbErr("hextile set fb address failed\n");
		goto error;
	}

	if (vcd_info->hdisp == 0 || vcd_info->vdisp == 0)
	{
		/* grapich is off, fake a FB */
		vcd_info->hdisp = FAKE_FB_WIDTH;
		vcd_info->vdisp = FAKE_FB_HEIGHT;
		nurfb->fake_fb = malloc(vcd_info->hdisp * vcd_info->vdisp * 2);
		if (!nurfb->fake_fb)
			goto error;
	}

	cmd.lp = vcd_info->line_pitch;
	if (ioctl(nurfb->hextile_fd, ECE_IOCSETLP, &cmd) < 0)
	{
		rfbErr("hextile set line patch failed\n");
		goto error;
	}


	nurfb->raw_hextile_mmap = vcd_info->hdisp * vcd_info->vdisp * (vcd_info->bpp + 1);
	nurfb->raw_hextile_addr = mmap(0, nurfb->raw_hextile_mmap, PROT_READ,
								   MAP_SHARED, nurfb->hextile_fd, 0);
	if (!nurfb->raw_hextile_addr)
	{
		rfbErr("mmap raw_hextile_addr failed\n");
		goto error;
	}

	if (!nurfb->fake_fb)
	{
		nurfb->raw_fb_mmap = vcd_info->line_pitch * vcd_info->vdisp;
		nurfb->raw_fb_addr = mmap(0, nurfb->raw_fb_mmap, PROT_READ, MAP_SHARED,
								  nurfb->raw_fb_fd, 0);
		if (!nurfb->raw_fb_addr)
		{
			rfbErr("mmap raw_fb_addr failed\n");
			goto error;
		}
		else
		{
			rfbLog("   w: %d h: %d b: %d addr: %p sz: %d\n", vcd_info->hdisp, vcd_info->vdisp,
				   16, nurfb->raw_fb_addr, nurfb->frame_size);
		}
	}
	else
		nurfb->raw_fb_addr = nurfb->fake_fb;

	nurfb->last_mode = RAWFB_MMAP;
	return 0;

error:
	rfbClearNuRfb(nurfb);
	return -1;
}

static int rfbNuGetUpdate(rfbClientRec *cl)
{
	struct nu_rfb *nurfb = (struct nu_rfb *)cl->clientData;
	int index = cl->sock - nurfb->sock_start;

	if (rfbNuChkVCDRes(nurfb, cl))
	{
		int i;

		if (nurfb->do_cmd)
		{
			rfbNuInitVCD(nurfb, 0);
			rfbNuNewFramebuffer(cl->screen,
								nurfb->raw_fb_addr, nurfb->vcd_info.hdisp,
								nurfb->vcd_info.vdisp, BitsPerSample, SamplesPerPixel, BytesPerPixel);
			if (nurfb->dumpfps)
				nurfb->fps_cnt = 0;
		}


		if ((nurfb->width == nurfb->vcd_info.hdisp) &&
			(nurfb->height == nurfb->vcd_info.vdisp))
			return 0;

		LOCK(cl->updateMutex);
		cl->newFBSizePending = TRUE;
		UNLOCK(cl->updateMutex);
		for (i = 0 ; i < nurfb->cl_cnt; i++)
			nurfb->refreshCount[i] = REFRESHCNT;
		nurfb->width = nurfb->vcd_info.hdisp;
		nurfb->height = nurfb->vcd_info.vdisp;

		return 1;
	}

	if (nurfb->refreshCount[index])
		nurfb->refreshCount[index]--;

	if (nurfb->refreshCount[index] > 0)
	{
		if (nurfb->do_cmd) {
			if (rfbNuSetVCDCmd(nurfb, CAPTURE_FRAME) < 0)
				return -1;
		}

		return 1;
	}
	else
	{
		if (nurfb->do_cmd) {
			if (rfbNuSetVCDCmd(nurfb, COMPARE) < 0)
				return -1;
		}
		return rfbNuGetDiffCnt(cl);
	}
}

static rfbBool
rfbNuHextiles16HW(rfbClientPtr cl, int rx, int ry, int rw, int rh)
{
	struct nu_rfb *nurfb = (struct nu_rfb *)cl->clientData;
	int err = 0;
	struct ece_ioctl_cmd cmd;
	char *copy_addr = NULL;
	uint32_t padding_len = 0;
	uint32_t copy_len = 0;
	uint32_t offset = 0;
	rfbFramebufferUpdateRectHeader rect;

retry:
	offset = rfbNuGetHextieDataOffset(nurfb);

	cmd.x = rx;
	cmd.y = ry;
	cmd.w = rw;
	cmd.h = rh;
	if ((err = ioctl(nurfb->hextile_fd, ECE_IOCGETED, &cmd)) < 0)
	{
		rfbErr("vnc: get encoding data failed:%d\n", err);
		rfbNuClearHextieDataOffset(nurfb);
		rfbNuResetECE(nurfb);
		goto retry;
	}

#if DBG
	rfbLog("x %d y %d %dx%d \n", rx, ry, rw, rh);
	rfbLog("offset %d cmd.len %d cmd.gap_len %d \n", offset,  cmd.len, cmd.gap_len);
	rfbLog("frame size %d map size %d \n",  nurfb->frame_size,  nurfb->raw_hextile_mmap);
#endif

	if (((cmd.gap_len + offset + cmd.len) >= nurfb->raw_hextile_mmap) || (cmd.len <= 1))
	{
		rfbNuClearHextieDataOffset(nurfb);
		goto retry;
	}

	copy_addr = nurfb->raw_hextile_addr + cmd.gap_len + offset;

	rect.r.x = Swap16IfLE(rx);
	rect.r.y = Swap16IfLE(ry);
	rect.r.w = Swap16IfLE(rw);
	rect.r.h = Swap16IfLE(rh);
	rect.encoding = Swap32IfLE(rfbEncodingHextile);
	if (!rfbNuSendUpdateBuf(cl, (char *)&rect, sz_rfbFramebufferUpdateRectHeader))
		return FALSE;

	if (cmd.len > UPDATE_BUF_SIZE)
	{
		padding_len = cmd.len - (UPDATE_BUF_SIZE);

		if (!rfbNuSendUpdateBuf(cl, copy_addr, (UPDATE_BUF_SIZE)))
			return FALSE;

		copy_addr += UPDATE_BUF_SIZE;

		do
		{
			copy_len = padding_len;
			if (padding_len > UPDATE_BUF_SIZE)
			{
				padding_len -= UPDATE_BUF_SIZE;
				copy_len = UPDATE_BUF_SIZE;
			}
			else
				padding_len = 0;

			if (!rfbNuSendUpdateBuf(cl, copy_addr, copy_len))
				return FALSE;
			copy_addr += copy_len;
		} while (padding_len != 0);
	}
	else
		rfbNuSendUpdateBuf(cl, copy_addr, cmd.len);

	return TRUE;
}

static int rfbNuGetDiffTable(rfbClientRec *cl, struct rect *rect, int i)
{
	struct nu_rfb *nurfb = (struct nu_rfb *)cl->clientData;
	struct rect *rect_table = nurfb->rect_table;
	int index = cl->sock - nurfb->sock_start;

	if (nurfb->do_cmd)
	{
		if (ioctl(nurfb->raw_fb_fd, VCD_IOCGETDIFF, rect) < 0)
		{
			rfbErr("get rect table failed\n");
			return -1;
		}
		rect_table[i].x = rect->x;
		rect_table[i].y = rect->y;
		rect_table[i].w = rect->w;
		rect_table[i].h = rect->h;
	}
	else
	{
		if (nurfb->refreshCount[index] > 0)
		{
			rect->x = 0;
			rect->y = 0;
			rect->w = cl->screen->width;
			rect->h = cl->screen->height;
		}
		else
		{
			rect->x = rect_table[i].x;
			rect->y = rect_table[i].y;
			rect->w = rect_table[i].w;
			rect->h = rect_table[i].h;
		}
	}
	return 0;
}

static rfbBool
rfbNuSendRectEncodingHextile(rfbClientPtr cl,
							 int x,
							 int y,
							 int w,
							 int h)
{
	struct nu_rfb *nurfb  = (struct nu_rfb *)cl->clientData;

	if (cl->ublen > 0)
		if (!rfbSendUpdateBuf(cl))
			return FALSE;

	return rfbNuHextiles16HW(cl, x, y, w, h);

	rfbLog("rfbSendRectEncodingHextile: bpp %d?\n", cl->format.bitsPerPixel);
	return FALSE;
}

static rfbBool
rfbDumpFPS(rfbClientPtr cl)
{
	struct nu_rfb *nurfb = (struct nu_rfb *)cl->clientData;
	struct timespec end;

	if (nurfb->dumpfps)
	{
		if (nurfb->fps_cnt == 0) {
			clock_gettime(CLOCK_MONOTONIC, &start);
			nurfb->fps_cnt++;
		} else {
			clock_gettime(CLOCK_MONOTONIC, &end);
			if (timediff(&start, &end) >= nurfb->dumpfps) {
				rfbLog("Avg. FPS = %d \n", nurfb->fps_cnt/nurfb->dumpfps);
				nurfb->fps_cnt = 0;
			} else
				nurfb->fps_cnt++;
		}
	}
}

static rfbBool
rfbNuSendFakeFramebufferUpdate(rfbClientPtr cl)
{
	rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *)cl->updateBuf;
	rfbBool result = TRUE;
	struct nu_rfb *nurfb= (struct nu_rfb *)cl->clientData;

	fu->nRects = Swap16IfLE(1);
	fu->type = rfbFramebufferUpdate;
	cl->ublen = sz_rfbFramebufferUpdateMsg;

	memcpy(cl->scaledScreen->frameBuffer, nurfb->fake_fb, FAKE_FB_WIDTH * FAKE_FB_HEIGHT * FAKE_FB_BPP);

	if (!rfbSendRectEncodingHextile(cl, 0, 0, FAKE_FB_WIDTH, FAKE_FB_HEIGHT))
		goto updateFailed;

	if (!rfbSendUpdateBuf(cl))
	{
	updateFailed:
		result = FALSE;
	}

	return result;
}

static rfbBool
rfbNuSendFramebufferUpdate(rfbClientPtr cl)
{
	rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *)cl->updateBuf;
	rfbBool result = TRUE;
	int ret = 0;
	struct nu_rfb *nurfb= (struct nu_rfb *)cl->clientData;

	ret = rfbNuGetUpdate(cl);

	if (cl->useNewFBSize == TRUE
		&& cl->newFBSizePending == TRUE)
	{
		LOCK(cl->updateMutex);
		cl->newFBSizePending = FALSE;
		UNLOCK(cl->updateMutex);
		fu->type = rfbFramebufferUpdate;
		fu->nRects = Swap16IfLE(1);
		cl->ublen = sz_rfbFramebufferUpdateMsg;

		if (!rfbSendNewFBSize(cl, cl->scaledScreen->width, cl->scaledScreen->height))
			return FALSE;
		return rfbSendUpdateBuf(cl);
	}


	if (nurfb->fake_fb) {
		rfbNuSendFakeFramebufferUpdate(cl);
		return result;
	}

	if (ret <= 0)
		return FALSE;

	nurfb->nRects = rfbNuGetDiffCnt(cl);
	if (nurfb->nRects < 0)
		goto updateFailed;

	if (nurfb->nRects == 0)
		nurfb->nRects = 1;

	fu->nRects = Swap16IfLE(nurfb->nRects);
	fu->type = rfbFramebufferUpdate;
	cl->ublen = sz_rfbFramebufferUpdateMsg;

	if (cl->enableLastRectEncoding)
		fu->nRects = 0xFFFF;

	if (cl->format.bitsPerPixel != 16)
		for (int i = 0 ; i < nurfb->vcd_info.vdisp; i++) {
			unsigned int hbytes = nurfb->vcd_info.hdisp * 2;
			unsigned int dest_of = i * hbytes;
			unsigned int src_of = i * nurfb->vcd_info.line_pitch;
			memcpy(
				cl->scaledScreen->frameBuffer + dest_of,
				nurfb->raw_fb_addr + src_of,
				hbytes);
		}

	for (int i = 0; i < nurfb->nRects; i++)
	{
		struct rect rect;

		if (rfbNuGetDiffTable(cl, &rect, i) < 0)
			break;

		if (cl->format.bitsPerPixel == 16)
		{
			if (!rfbNuSendRectEncodingHextile(cl, rect.x, rect.y, rect.w, rect.h))
				goto updateFailed;
		}
		else
		{
			if (!rfbSendRectEncodingHextile(cl, rect.x, rect.y, rect.w, rect.h))
				goto updateFailed;
		}
	}

	if (cl->enableLastRectEncoding)
		rfbSendLastRectMarker(cl);

	if (!rfbSendUpdateBuf(cl))
	{
	updateFailed:
		result = FALSE;
	}

	return result;
}

static rfbBool
rfbNuUpdateClient(rfbClientPtr cl)
{
	struct timeval tv;
	rfbBool result = FALSE;
	rfbScreenInfoPtr screen = cl->screen;
	rfbStatList *ptr = rfbStatLookupMessage(cl, rfbFramebufferUpdateRequest);
	struct nu_rfb *nurfb= (struct nu_rfb *)cl->clientData;
	int index = cl->sock - nurfb->sock_start;

	if (cl->sock >= 0 && !cl->onHold && (ptr->rcvdCount > 0))
	{

		result = TRUE;

		if (screen->deferUpdateTime == 0)
		{
			if (nurfb->rcvdCount[index] != ptr->rcvdCount) {
				if (rfbNuSendFramebufferUpdate(cl) == TRUE)
					nurfb->rcvdCount[index]= ptr->rcvdCount;
				rfbDumpFPS(cl);
			}
		}
		else if (cl->startDeferring.tv_usec == 0)
		{
			gettimeofday(&cl->startDeferring, NULL);
			if (cl->startDeferring.tv_usec == 0)
				cl->startDeferring.tv_usec++;
		}
		else
		{
			gettimeofday(&tv, NULL);
			if (tv.tv_sec < cl->startDeferring.tv_sec /* at midnight */
				|| ((tv.tv_sec - cl->startDeferring.tv_sec) * 1000 + (tv.tv_usec - cl->startDeferring.tv_usec) / 1000) > screen->deferUpdateTime)
			{
				cl->startDeferring.tv_usec = 0;
				if (nurfb->rcvdCount[index] != ptr->rcvdCount) {
					if (rfbNuSendFramebufferUpdate(cl) == TRUE)
						nurfb->rcvdCount[index] = ptr->rcvdCount;
					rfbDumpFPS(cl);
				}
			}
		}
	}

	if (!cl->viewOnly && cl->lastPtrX >= 0)
	{
		if (cl->startPtrDeferring.tv_usec == 0)
		{
			gettimeofday(&cl->startPtrDeferring, NULL);
			if (cl->startPtrDeferring.tv_usec == 0)
				cl->startPtrDeferring.tv_usec++;
		}
		else
		{
			struct timeval tv;

			gettimeofday(&tv, NULL);
			if (tv.tv_sec < cl->startPtrDeferring.tv_sec /* at midnight */
				|| ((tv.tv_sec - cl->startPtrDeferring.tv_sec) * 1000 + (tv.tv_usec - cl->startPtrDeferring.tv_usec) / 1000) > cl->screen->deferPtrUpdateTime)
			{
				cl->startPtrDeferring.tv_usec = 0;
				cl->screen->ptrAddEvent(cl->lastPtrButtons,
										cl->lastPtrX,
										cl->lastPtrY, cl);
				cl->lastPtrX = -1;
			}
		}
	}

	return result;
}

static rfbBool
rfbNuProcessEvents(rfbScreenInfoPtr screen, long usec)
{
	rfbClientIteratorPtr i;
	rfbClientPtr cl, clPrev;
	rfbBool result = FALSE;
	struct nu_rfb *nurfb;

	extern rfbClientIteratorPtr
	rfbGetClientIteratorWithClosed(rfbScreenInfoPtr rfbScreen);

	if (usec < 0)
		usec = screen->deferUpdateTime * 1000;

	rfbCheckFds(screen, usec);
	rfbHttpCheckFds(screen);

	i = rfbGetClientIteratorWithClosed(screen);
	cl = rfbClientIteratorNext(i);

	if (cl) {
		nurfb = (struct nu_rfb *)cl->clientData;
		nurfb->do_cmd = TRUE;
	} else
		goto release;

	while (cl)
	{
		result = rfbNuUpdateClient(cl);
		nurfb->do_cmd = FALSE;

		clPrev = cl;
		cl = rfbClientIteratorNext(i);
		if (clPrev->sock == -1)
		{
			rfbClientConnectionGone(clPrev);
			result = TRUE;
		}
	}

release:
	rfbReleaseClientIterator(i);

	return result;
}

void rfbNuRunEventLoop(rfbScreenInfoPtr screen, long usec, rfbBool runInBackground)
{
	while (rfbIsActive(screen))
		rfbNuProcessEvents(screen, usec);
}

void rfbClearNuRfb(struct nu_rfb *nurfb)
{
	if (nurfb->fake_fb)
	{
		free(nurfb->fake_fb);
		nurfb->fake_fb = NULL;
	}
	else if (nurfb->raw_fb_addr)
	{
		munmap(nurfb->raw_fb_addr, nurfb->raw_fb_mmap);
		nurfb->raw_fb_mmap = 0;
		nurfb->raw_fb_addr = NULL;
	}

	if (nurfb->raw_fb_fd > -1)
	{
		close(nurfb->raw_fb_fd);
		nurfb->raw_fb_fd = -1;
	}

	if (nurfb->raw_hextile_addr)
	{
		munmap(nurfb->raw_hextile_addr, nurfb->raw_hextile_mmap);
		nurfb->raw_hextile_mmap = 0;
		nurfb->raw_hextile_addr = NULL;
	}

	if (nurfb->hextile_fd > -1)
	{
		close(nurfb->hextile_fd);
		nurfb->hextile_fd = -1;
	}

	free(nurfb);
	nurfb = NULL;
	nurfb_g = NULL;
}

struct nu_rfb *rfbInitNuRfb(int hsync_mode)
{
	struct nu_rfb *nurfb = NULL;

	nurfb = malloc(sizeof(struct nu_rfb));
	if (!nurfb)
		return NULL;

	memset(nurfb, 0, sizeof(struct nu_rfb));

	nurfb->hsync_mode = hsync_mode;

    sendWakeupPacket();

	if (rfbNuInitVCD(nurfb, 1) < 0)
		return NULL;

	nurfb_g = nurfb;

	return nurfb;
}
