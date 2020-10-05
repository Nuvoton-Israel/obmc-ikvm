/*
 * obmc-ikvm.c
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
#include <getopt.h>
#include "rfbnpcm750.h"
#include "rfbusbhid.h"

#define MAX_CL 5

struct nu_rfb *nurfb = NULL;

static void clientgone(rfbClientPtr cl)
{
    int index = cl->sock - nurfb->sock_start;

    nurfb->rcvdCount[index] = 0;

    nurfb->cl_cnt--;

    if (nurfb->cl_cnt == 0)
    {
        rfbNuResetVCD(nurfb);
        rfbNuResetECE(nurfb);
    }

    cl->clientData = NULL;
}

static enum rfbNewClientAction newclient(rfbClientPtr cl)
{
    if ((nurfb->cl_cnt + 1) > MAX_CL)
        return RFB_CLIENT_REFUSE;

    nurfb->cl_cnt++;

    if (nurfb->cl_cnt == 1)
        nurfb->sock_start = cl->sock;

    nurfb->refreshCount[cl->sock - nurfb->sock_start] = REFRESHCNT;

    cl->clientData = nurfb;
    cl->clientGoneHook = clientgone;
    cl->preferredEncoding = rfbEncodingHextile;

    return RFB_CLIENT_ACCEPT;
}

void usage()
{
    fprintf(stderr, "OpenBMC IKVM daemon\n");
    fprintf(stderr, "Usage: obmc-ikvm [options]\n");
    fprintf(stderr, "-f dump fps per seconds\n");
    rfbUsage();
}

int main(int argc, char **argv)
{
    int ret = 0, dump_fps = 0, option;
    unsigned char hsync_mode = 0;
    const char *opts = "hsf:";
#ifdef KEYBOARD_EVENT
    pthread_t rfb;
#endif
    struct option lopts[] = {
        {"help", 0, 0, 'h'},
        {"hsync mode", 0, 0, 's'},
        {"dump_fps", 1, 0, 'f'},
        {0, 0, 0, 0}};

    while ((option = getopt_long(argc, argv, opts, lopts, NULL)) != -1)
    {
        switch (option)
        {
        case 'f':
            dump_fps = (int)strtol(optarg, NULL, 0);
            if (dump_fps < 0 || dump_fps > 60)
                dump_fps = 30;
            break;
        case 's':
            hsync_mode = 1;
            break;
        case 'h':
            usage();
            goto done;
            break;
        }
    }

    nurfb = rfbInitNuRfb(hsync_mode);
    if (!nurfb)
        return 0;

    nurfb->dumpfps = dump_fps;

    ret = hid_init();
    if (ret)
        return 0;

    rfbScreenInfoPtr rfbScreen =
        rfbGetScreen(&argc, argv, nurfb->vcd_info.hdisp, nurfb->vcd_info.vdisp,
                     BitsPerSample, SamplesPerPixel, BytesPerPixel);
    if (!rfbScreen)
        return 0;

    rfbNuInitRfbFormat(rfbScreen);

    rfbScreen->desktopName = "obmc iKVM";
    rfbScreen->frameBuffer = malloc(nurfb->vcd_info.hdisp * nurfb->vcd_info.vdisp * nurfb->vcd_info.bpp); //nurfb->raw_fb_addr;
    rfbScreen->alwaysShared = TRUE;
    rfbScreen->ptrAddEvent = pointer_event;
    rfbScreen->kbdAddEvent = keyboard;
    rfbScreen->newClientHook = newclient;

    /* initialize the server */
    rfbInitServer(rfbScreen);
#ifdef KEYBOARD_EVENT
    pthread_create(&rfb, NULL, rfbNuKeyEventThread, nurfb);
#endif
    rfbNuRunEventLoop(rfbScreen, -1, FALSE);

#ifdef KEYBOARD_EVENT
    pthread_join(rfb, NULL);
#endif
    free(rfbScreen->frameBuffer);

    hid_close();
    rfbClearNuRfb(nurfb);
    rfbScreenCleanup(rfbScreen);
done:
    return (0);
}
