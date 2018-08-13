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

struct nu_rfb *nurfb_g;

void rfbNuInitRfbFormat(rfbScreenInfoPtr screen)
{
	struct vcd_info *info = &nurfb_g->vcd_info;
	rfbPixelFormat *format= &screen->serverFormat;

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
  screen->bitsPerPixel = screen->depth = 8*bytesPerPixel;
  screen->paddedWidthInBytes = width*bytesPerPixel;

  rfbNuInitRfbFormat(screen);

  screen->frameBuffer = framebuffer;

  /* Adjust pointer position if necessary */

  if (screen->cursorX >= width)
    screen->cursorX = width - 1;
  if (screen->cursorY >= height)
    screen->cursorY = height - 1;

  /* For each client: */
  iterator = rfbGetClientIterator(screen);
  while ((cl = rfbClientIteratorNext(iterator)) != NULL) {
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
    if (ioctl(nurfb->raw_fb_fd, VCD_IOCGETINFO, info) < 0) {
        printf("get info failed\n");
        return -1;
    }
    return 0;
}

static int rfbNuSetVCDCmd(struct nu_rfb *nurfb, int cmd)
{
    if (ioctl(nurfb->raw_fb_fd, VCD_IOCSENDCMD, &cmd) < 0) {
        printf("set cmd failed\n");
        return -1;
    }
    return 0;
}

static int rfbNuChkVCDRes(struct nu_rfb *nurfb, rfbClientRec *cl)
{
    struct nu_cl *nucl;

    if (!cl)
        goto check; /* init state*/

	nucl = (struct nu_cl *)cl->clientData;
    if (nucl->id != 1)
        goto done;

check:
    if (ioctl(nurfb->raw_fb_fd, VCD_IOCCHKRES, &nurfb->res_changed) < 0) {
        printf("VCD_IOCCHKRES failed\n");
        return -1;
    }

done:
    return nurfb->res_changed;
}

static int rfbNuGetDiffCnt(rfbClientRec *cl)
{
    struct nu_cl *nucl = (struct nu_cl *)cl->clientData;
	struct nu_rfb *nurfb = (struct nu_rfb *)nucl->nurfb;

    if (nucl->id == 1) {
        if (ioctl(nurfb->raw_fb_fd, VCD_IOCDIFFCNT, &nurfb->diff_cnt) < 0) {
            printf("get diff cnt failed\n");
            return -1;
        }

        if (nurfb->diff_table) {
            free(nurfb->diff_table);
            nurfb->diff_table = NULL;
        }

        nurfb->diff_table = (struct vcd_diff *)malloc(sizeof(struct vcd_diff) * (nurfb->diff_cnt + 1));
    }

    return nurfb->diff_cnt;
}


static int rfbNuInitVCD(struct nu_rfb *nurfb)
{
    struct vcd_info *vcd_info = &nurfb->vcd_info;
    struct ece_ioctl_cmd cmd;
    int size;

    if (nurfb->last_mode == RAWFB_MMAP) {
        if (!nurfb->fake_fb) {
            munmap(nurfb->raw_fb_addr, nurfb->raw_fb_mmap);
        } else {
            free(nurfb->fake_fb);
            nurfb->fake_fb = NULL;
        }

        close(nurfb->raw_fb_fd);
        munmap(nurfb->raw_hextile_addr, nurfb->raw_hextile_mmap);
        close(nurfb->hextile_fd);
        nurfb->last_mode = 0;
    }

    if (nurfb->last_mode == 0) {
        nurfb->raw_fb_fd = -1;
        nurfb->hextile_fd = -1;
    }

    nurfb->raw_fb_fd = open("/dev/vcd", O_RDWR);
    if (nurfb->raw_fb_fd < 0) {
        rfbLog("failed to open /dev/vcd \n");
        goto error;
    }

    if (rfbNuChkVCDRes(nurfb, NULL) < 0)
        goto error;

    if (rfbNuGetVCDInfo(nurfb, vcd_info) < 0)
        goto error;

    size = vcd_info->hdisp *  vcd_info->vdisp * (16 / 8);

    nurfb->hextile_fd = open("/dev/hextile", O_RDWR);
    if (nurfb->hextile_fd < 0) {
        rfbErr("failed to open /dev/hextile \n");
        goto error;
    }

    cmd.framebuf= vcd_info->vcd_fb;
    if (ioctl(nurfb->hextile_fd, ECE_IOCSETFB , &cmd) < 0) {
        rfbErr("hextile set fb address failed \n");
        goto error;
    }

    if (vcd_info->hdisp == 0
        || vcd_info->vdisp == 0
        || vcd_info->line_pitch == 0) {
        /* grapich is off, fake a FB */
        vcd_info->hdisp = 320;
        vcd_info->vdisp = 240;
        vcd_info->line_pitch = 1024;
        nurfb->fake_fb = malloc(vcd_info->hdisp  * vcd_info->vdisp  * 2);
        if (!nurfb->fake_fb)
            goto error;
    }

    cmd.w = vcd_info->hdisp;
    cmd.h = vcd_info->vdisp;
    cmd.lp = vcd_info->line_pitch;
    if (ioctl(nurfb->hextile_fd, ECE_IOCSETLP , &cmd) < 0) {
        rfbErr("hextile set line patch failed \n");
        goto error;
    }

    nurfb->raw_hextile_mmap = vcd_info->hdisp * vcd_info->vdisp * 2;
    nurfb->raw_hextile_addr = mmap(0, nurfb->raw_hextile_mmap, PROT_READ,
        MAP_SHARED, nurfb->hextile_fd, 0);
    if (!nurfb->raw_hextile_addr) {
        rfbErr("mmap raw_hextile_addr failed \n");
        goto error;
    }

    if (!nurfb->fake_fb) {
        nurfb->raw_fb_addr = mmap(0, size, PROT_READ, MAP_SHARED,
            nurfb->raw_fb_fd, 0);
        if (!nurfb->raw_fb_addr) {
            rfbErr("mmap raw_fb_addr failed \n");
            goto error;
        } else {
            nurfb->raw_fb_mmap = size;
            rfbLog("   w: %d h: %d b: %d addr: %p sz: %d\n", vcd_info->hdisp, vcd_info->vdisp,
                16, nurfb->raw_fb_addr, size);
        }
    } else
        nurfb->raw_fb_addr = nurfb->fake_fb;

    nurfb->last_mode = RAWFB_MMAP;
    return 0;

error:
    rfbClearNuRfb(nurfb);
    return -1;
}

static int rfbNuGetUpdate(rfbClientRec *cl)
{
    struct nu_cl *nucl = (struct nu_cl *)cl->clientData;
	struct nu_rfb *nurfb = (struct nu_rfb *)nucl->nurfb;

    if (rfbNuChkVCDRes(nurfb, cl)) {
        if (nucl->id == 1) {
            usleep(1000 * 600);
            rfbNuSetVCDCmd(nurfb, CAPTURE_FRAME);
            rfbNuInitVCD(nurfb);
            rfbNuNewFramebuffer(cl->screen, nurfb->raw_fb_addr, nurfb->vcd_info.hdisp, nurfb->vcd_info.vdisp, 5, 1, 2);
        }

        LOCK(cl->updateMutex);
        cl->useNewFBSize = 1;
        cl->newFBSizePending = 1;
        UNLOCK(cl->updateMutex);
        nurfb->refresh_cnt = 30;
        return 1;
    }

    if (nurfb->refresh_cnt)
        nurfb->refresh_cnt--;

    if (!cl->incremental || nurfb->refresh_cnt > 0) {
        if (nucl->id == 1)
            rfbNuSetVCDCmd(nurfb, CAPTURE_FRAME);
        return 1;
    } else {
        if (nucl->id == 1)
            rfbNuSetVCDCmd(nurfb, COMPARE);
        return rfbNuGetDiffCnt(cl);
    }
}

static rfbBool
rfbNuHextiles16HW(rfbClientPtr cl, int rx, int ry, int rw, int rh) {
    struct nu_cl *nucl = (struct nu_cl *)cl->clientData;
	struct nu_rfb *nurfb = (struct nu_rfb *)nucl->nurfb;
    int err = 0;
    struct ece_ioctl_cmd cmd;
    char *copy_addr = NULL;
    uint32_t padding_len = 0;
    uint32_t copy_len = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.x = rx;
    cmd.y = ry;
    cmd.w = rw;
    cmd.h = rh;

    if ((err = ioctl(nurfb->hextile_fd, ECE_IOCGETED , &cmd)) < 0) {
        rfbLog("X11vnc:GETED failed:%d\n",err);
        return FALSE;
    }

    copy_addr = nurfb->raw_hextile_addr;
    copy_addr += cmd.gap_len;

    if (cmd.len >= (UPDATE_BUF_SIZE - cl->ublen)) {
        padding_len = cmd.len - (UPDATE_BUF_SIZE - cl->ublen);
        memcpy(&cl->updateBuf[cl->ublen], copy_addr, (UPDATE_BUF_SIZE - cl->ublen));
        copy_addr += (UPDATE_BUF_SIZE - cl->ublen);
        cl->ublen += (UPDATE_BUF_SIZE - cl->ublen);
        do {
            if ((cl->ublen + 1 + (2 + 16 * 16) * (16/8)) > UPDATE_BUF_SIZE) {
                if (!rfbSendUpdateBuf(cl)){
                    rfbLog("rfbSendUpdateBuf FAIL\n");
                    return FALSE;
                }
            }

            copy_len = padding_len;
            if (padding_len > (UPDATE_BUF_SIZE - cl->ublen)) {
                padding_len -= (UPDATE_BUF_SIZE - cl->ublen);
                copy_len = (UPDATE_BUF_SIZE - cl->ublen);
            } else
                padding_len = 0;

            memcpy(&cl->updateBuf[cl->ublen], copy_addr, copy_len);
            cl->ublen += copy_len;
            copy_addr += copy_len;
        } while(padding_len != 0 );
    } else {
        memcpy(&cl->updateBuf[cl->ublen], copy_addr, cmd.len);
        cl->ublen += cmd.len;
        padding_len = 0;
        rfbStatRecordEncodingSentAdd(cl, rfbEncodingHextile, cmd.len);
    }
    return TRUE;
}

static int rfbNuGetDiffTable(rfbClientRec *cl, struct vcd_diff *diff, int i)
{
    struct nu_cl *nucl = (struct nu_cl *)cl->clientData;
	struct nu_rfb *nurfb = (struct nu_rfb *)nucl->nurfb;
    struct vcd_diff *diff_table = nurfb->diff_table;

    if (nucl->id == 1) {
        if (ioctl(nurfb->raw_fb_fd, VCD_IOCGETDIFF, diff) < 0) {
            printf("get diff table failed\n");
            return -1;
        }
        diff_table[i].x = diff->x;
        diff_table[i].y = diff->y;
        diff_table[i].w = diff->w;
        diff_table[i].h = diff->h;
    } else {
        if (!nurfb->incremental || nurfb->refresh_cnt > 0) {
            diff->x = 0;
            diff->y = 0;
            diff->w = cl->screen->width;
            diff->h = cl->screen->height;
        } else {
            diff->x = diff_table[i].x;
            diff->y = diff_table[i].y;
            diff->w = diff_table[i].w;
            diff->h = diff_table[i].h;
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
    rfbFramebufferUpdateRectHeader rect;

    if (cl->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    rect.r.x = Swap16IfLE(x);
    rect.r.y = Swap16IfLE(y);
    rect.r.w = Swap16IfLE(w);
    rect.r.h = Swap16IfLE(h);
    rect.encoding = Swap32IfLE(rfbEncodingHextile);

    memcpy(&cl->updateBuf[cl->ublen], (char *)&rect,
           sz_rfbFramebufferUpdateRectHeader);
    cl->ublen += sz_rfbFramebufferUpdateRectHeader;

    rfbStatRecordEncodingSent(cl, rfbEncodingHextile,
          sz_rfbFramebufferUpdateRectHeader,
          sz_rfbFramebufferUpdateRectHeader + w * (cl->format.bitsPerPixel / 8) * h);

    switch (cl->format.bitsPerPixel) {
    case 16:
        return rfbNuHextiles16HW(cl, x, y, w, h);
    }

    rfbLog("rfbSendRectEncodingHextile: bpp %d?\n", cl->format.bitsPerPixel);
    return FALSE;
}


static rfbBool
rfbNuSendFramebufferUpdate(rfbClientPtr cl, sraRegionPtr givenUpdateRegion)
{
    rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *)cl->updateBuf;
    int j, cnt;
    rfbBool sendCursorShape = FALSE;
    rfbBool sendCursorPos = FALSE;
    rfbBool sendKeyboardLedState = FALSE;
    rfbBool sendSupportedMessages = FALSE;
    rfbBool sendSupportedEncodings = FALSE;
    rfbBool sendServerIdentity = FALSE;
    rfbBool result = TRUE;
    struct nu_cl *nucl = (struct nu_cl *)cl->clientData;
	struct nu_rfb *nurfb = (struct nu_rfb *)nucl->nurfb;

	if(!rfbNuGetUpdate(cl))
		return result;

    if (cl->useNewFBSize && cl->newFBSizePending) {
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

    if (cl->enableCursorShapeUpdates) {
        if (cl->cursorWasChanged && cl->readyForSetColourMapEntries)
            sendCursorShape = TRUE;
    }

    if (cl->enableCursorPosUpdates && cl->cursorWasMoved)
        sendCursorPos = TRUE;

    if ((cl->enableKeyboardLedState) && (cl->screen->getKeyboardLedStateHook!=NULL)) {
        int x;
        x = cl->screen->getKeyboardLedStateHook(cl->screen);
        if (x != cl->lastKeyboardLedState) {
            sendKeyboardLedState = TRUE;
            cl->lastKeyboardLedState = x;
        }
    }

    if (cl->enableSupportedMessages) {
        sendSupportedMessages = TRUE;
        cl->enableSupportedMessages = FALSE;
    }

    if (cl->enableSupportedEncodings) {
        sendSupportedEncodings = TRUE;
        cl->enableSupportedEncodings = FALSE;
    }

    if (cl->enableServerIdentity) {
        sendServerIdentity = TRUE;
        cl->enableServerIdentity = FALSE;
    }

    rfbStatRecordMessageSent(cl, rfbFramebufferUpdate, 0, 0);
    fu->type = rfbFramebufferUpdate;
    cl->ublen = sz_rfbFramebufferUpdateMsg;

    cnt = rfbNuGetDiffCnt(cl);
    if (cnt < 0)
        goto updateFailed;

    if (cnt == 0)
        cnt = 1;

    fu->nRects = Swap16IfLE((uint16_t)(cnt +
        !!sendCursorShape + !!sendCursorPos + !!sendKeyboardLedState +
        !!sendSupportedMessages + !!sendSupportedEncodings + !!sendServerIdentity));

    for (j = 0 ; j < cnt ; j++) {
        struct vcd_diff diff;
        if (rfbNuGetDiffTable(cl, &diff, j) < 0)
            break;

        cl->scaledScreen->frameBuffer = nurfb->raw_fb_addr;
        switch (cl->preferredEncoding) {
            case -1:
            case rfbEncodingRaw:
                if (!rfbSendRectEncodingRaw(cl, diff.x, diff.y, diff.w, diff.h))
                    goto updateFailed;
                break;
            case rfbEncodingRRE:
                if (!rfbSendRectEncodingRRE(cl, diff.x, diff.y, diff.w, diff.h))
                    goto updateFailed;
                break;
            case rfbEncodingCoRRE:
                if (!rfbSendRectEncodingCoRRE(cl, diff.x, diff.y, diff.w, diff.h))
                    goto updateFailed;
                break;
            case rfbEncodingHextile:
                if (!rfbNuSendRectEncodingHextile(cl, diff.x, diff.y, diff.w, diff.h))
                    goto updateFailed;
                break;
            case rfbEncodingUltra:
                if (!rfbSendRectEncodingUltra(cl, diff.x, diff.y, diff.w, diff.h))
                    goto updateFailed;
                break;
#ifdef LIBVNCSERVER_HAVE_LIBZ
	case rfbEncodingZlib:
	    if (!rfbSendRectEncodingZlib(cl, diff.x, diff.y, diff.w, diff.h))
	        goto updateFailed;
	    break;
       case rfbEncodingZRLE:
       case rfbEncodingZYWRLE:
           if (!rfbSendRectEncodingZRLE(cl, diff.x, diff.y, diff.w, diff.h))
	       goto updateFailed;
           break;
#endif
#if defined(LIBVNCSERVER_HAVE_LIBJPEG) && (defined(LIBVNCSERVER_HAVE_LIBZ) || defined(LIBVNCSERVER_HAVE_LIBPNG))
	case rfbEncodingTight:
	    if (!rfbSendRectEncodingTight(cl, diff.x, diff.y, diff.w, diff.h))
	        goto updateFailed;
	    break;
#ifdef LIBVNCSERVER_HAVE_LIBPNG
	case rfbEncodingTightPng:
	    if (!rfbSendRectEncodingTightPng(cl, diff.x, diff.y, diff.w, diff.h))
	        goto updateFailed;
	    break;
#endif
#endif
        }
    }

    if (sendCursorShape) {
        cl->cursorWasChanged = FALSE;
        if (!rfbSendCursorShape(cl))
            goto updateFailed;
    }

    if (sendCursorPos) {
        cl->cursorWasMoved = FALSE;
        if (!rfbSendCursorPos(cl))
            goto updateFailed;
    }

    if (sendKeyboardLedState) {
        if (!rfbSendKeyboardLedState(cl))
            goto updateFailed;
    }

    if (sendSupportedMessages) {
        if (!rfbSendSupportedMessages(cl))
            goto updateFailed;
    }

    if (sendSupportedEncodings) {
        if (!rfbSendSupportedEncodings(cl))
            goto updateFailed;
    }

    if (sendServerIdentity) {
        if (!rfbSendServerIdentity(cl))
            goto updateFailed;
    }

    if (!rfbSendUpdateBuf(cl)) {
updateFailed:
        result = FALSE;
    }

    LOCK(cl->updateMutex);
    nurfb->fb_req = 0;
    UNLOCK(cl->updateMutex);
    return result;
}

static rfbBool
rfbNuUpdateClient(rfbClientPtr cl)
{
  struct timeval tv;
  rfbBool result=FALSE;
  rfbScreenInfoPtr screen = cl->screen;

  if (cl->sock >= 0 && !cl->onHold && cl->fb_req) {
      result=TRUE;
      if(screen->deferUpdateTime == 0) {
          rfbNuSendFramebufferUpdate(cl, cl->modifiedRegion);
      } else if(cl->startDeferring.tv_usec == 0) {
        gettimeofday(&cl->startDeferring,NULL);
        if(cl->startDeferring.tv_usec == 0)
          cl->startDeferring.tv_usec++;
      } else {
        gettimeofday(&tv,NULL);
        if(tv.tv_sec < cl->startDeferring.tv_sec /* at midnight */
           || ((tv.tv_sec-cl->startDeferring.tv_sec)*1000
               +(tv.tv_usec-cl->startDeferring.tv_usec)/1000)
             > screen->deferUpdateTime) {
          cl->startDeferring.tv_usec = 0;
          rfbNuSendFramebufferUpdate(cl, cl->modifiedRegion);
        }
      }
    }

    if (!cl->viewOnly && cl->lastPtrX >= 0) {
      if(cl->startPtrDeferring.tv_usec == 0) {
        gettimeofday(&cl->startPtrDeferring,NULL);
        if(cl->startPtrDeferring.tv_usec == 0)
          cl->startPtrDeferring.tv_usec++;
      } else {
        struct timeval tv;
        gettimeofday(&tv,NULL);
        if(tv.tv_sec < cl->startPtrDeferring.tv_sec /* at midnight */
           || ((tv.tv_sec-cl->startPtrDeferring.tv_sec)*1000
           +(tv.tv_usec-cl->startPtrDeferring.tv_usec)/1000)
           > cl->screen->deferPtrUpdateTime) {
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
rfbNuProcessEvents(rfbScreenInfoPtr screen,long usec)
{
  rfbClientIteratorPtr i;
  rfbClientPtr cl,clPrev;
  rfbBool result=FALSE;
  extern rfbClientIteratorPtr
    rfbGetClientIteratorWithClosed(rfbScreenInfoPtr rfbScreen);

  if(usec<0)
    usec=screen->deferUpdateTime*1000;

  rfbCheckFds(screen,usec);
  rfbHttpCheckFds(screen);

  i = rfbGetClientIteratorWithClosed(screen);
  cl = rfbClientIteratorHead(i);
  while(cl) {
    result = rfbNuUpdateClient(cl);
    clPrev=cl;
    cl=rfbClientIteratorNext(i);
    if(clPrev->sock==-1) {
      rfbClientConnectionGone(clPrev);
      result=TRUE;
    }
  }
  rfbReleaseClientIterator(i);

  return result;
}

void rfbNuRunEventLoop(rfbScreenInfoPtr screen, long usec, rfbBool runInBackground)
{
  while(rfbIsActive(screen))
    rfbNuProcessEvents(screen,usec);
}

void rfbClearNuRfb(struct nu_rfb *nurfb)
{
    if (nurfb->fake_fb) {
        free(nurfb->fake_fb);
        nurfb->fake_fb = NULL;
    } else if (nurfb->raw_fb_addr) {
        munmap(nurfb->raw_fb_addr, nurfb->raw_fb_mmap);
        nurfb->raw_fb_mmap = 0;
        nurfb->raw_fb_addr = NULL;
    }

    if (nurfb->raw_fb_fd > -1) {
        close(nurfb->raw_fb_fd);
        nurfb->raw_fb_fd = -1;
    }

    if (nurfb->raw_hextile_addr) {
        munmap(nurfb->raw_hextile_addr, nurfb->raw_hextile_mmap);
        nurfb->raw_hextile_mmap = 0;
        nurfb->raw_hextile_addr = NULL;
    }

    if (nurfb->hextile_fd > -1) {
        close(nurfb->hextile_fd);
        nurfb->hextile_fd = -1;
    }

    free(nurfb);
    nurfb = NULL;
	nurfb_g = NULL;
}

struct nu_rfb *rfbInitNuRfb(void)
{
    struct nu_rfb *nurfb = NULL;

    nurfb = malloc(sizeof(struct nu_rfb));
    if (!nurfb)
        return NULL;

    memset(nurfb, 0 , sizeof(struct nu_rfb));

    if (rfbNuInitVCD(nurfb) < 0)
        return NULL;

	nurfb_g = nurfb;

    return nurfb;
}
