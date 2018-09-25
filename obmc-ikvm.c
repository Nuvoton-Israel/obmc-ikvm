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

struct nu_rfb *nurfb = NULL;

static void clientgone(rfbClientPtr cl)
{
    nurfb->cl_cnt--;
    free(cl->clientData);
    cl->clientData = NULL;
}

static enum rfbNewClientAction newclient(rfbClientPtr cl)
{
    struct nu_cl *nucl = (struct nu_cl *)malloc(sizeof(*nucl));

    nucl->nurfb = nurfb;
    nucl->id = ++nurfb->cl_cnt;
    if (nucl->id > 1)
        cl->viewOnly = 1;

    nurfb->refresh_cnt = 30;

    cl->clientData = nucl;
    cl->clientGoneHook = clientgone;
    cl->preferredEncoding = rfbEncodingHextile;

    return RFB_CLIENT_ACCEPT;
}

void usage()
{
	fprintf(stderr, "OpenBMC IKVM daemon\n");
	fprintf(stderr, "Usage: obmc-ikvm [options]\n");
	fprintf(stderr, "-f dump frame rate          dump frame rate\n");
	rfbUsage();
}

int main(int argc,char** argv)
{
    int ret = 0, dumpfps = 0, option;
    const char *opts = "h:f:";
    struct option lopts[] = {
        { "frame_rate", 1, 0, 'f' },
        { "help", 0, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((option = getopt_long(argc, argv, opts, lopts, NULL)) != -1) {
        switch (option) {
        case 'f':
        {
            dumpfps = (int)strtol(optarg, NULL, 0);
            if (dumpfps == 0)
                dumpfps = 60;
            break;
        }
        case 'h':
            usage();
            goto done;
            break;
        }
    }

    nurfb = rfbInitNuRfb();
    if (!nurfb)
        return 0;

    nurfb->dumpfps = dumpfps;

    ret = hid_init();
    if (ret)
        return 0;

    rfbScreenInfoPtr rfbScreen =
        rfbGetScreen(&argc, argv, nurfb->vcd_info.hdisp, nurfb->vcd_info.vdisp,
            BitsPerSample, SamplesPerPixel, BytesPerPixel);
    if(!rfbScreen)
        return 0;

    rfbNuInitRfbFormat(rfbScreen);

    rfbScreen->desktopName = "obmc iKVM";
    rfbScreen->frameBuffer = malloc(nurfb->vcd_info.hdisp * nurfb->vcd_info.vdisp * nurfb->vcd_info.bpp);//nurfb->raw_fb_addr;
    rfbScreen->alwaysShared = TRUE;
    rfbScreen->ptrAddEvent = pointer_event;
    rfbScreen->kbdAddEvent = keyboard;
    rfbScreen->newClientHook = newclient;

    /* initialize the server */
    rfbInitServer(rfbScreen);
    rfbNuRunEventLoop(rfbScreen, -1, FALSE);

    free(rfbScreen->frameBuffer);

    hid_close();
    rfbClearNuRfb(nurfb);
    rfbScreenCleanup(rfbScreen);
done:
    return(0);
}
