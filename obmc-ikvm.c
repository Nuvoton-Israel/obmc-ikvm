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

#include "rfbnpcm750.h"
#include "usb_hid.h"

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
    return RFB_CLIENT_ACCEPT;
}

int main(int argc,char** argv)
{
    int ret = 0;

    nurfb = rfbInitNuRfb();
    if (!nurfb)
        return 0;

    ret = hid_init();
    if (ret)
        return 0;

    rfbScreenInfoPtr rfbScreen = rfbGetScreen(&argc, argv, nurfb->vcd_info.hdisp, nurfb->vcd_info.vdisp, 5, 1, 2);
    if(!rfbScreen)
        return 0;

    rfbNuInitRfbFormat(rfbScreen);

    rfbScreen->desktopName = "Obmc iKVM";
    rfbScreen->frameBuffer = nurfb->raw_fb_addr;
    rfbScreen->alwaysShared = TRUE;
    rfbScreen->ptrAddEvent = pointer_event;
    rfbScreen->kbdAddEvent = keyboard;
    rfbScreen->newClientHook = newclient;

    /* initialize the server */
    rfbInitServer(rfbScreen);
    rfbNuRunEventLoop(rfbScreen, -1, FALSE);

    hid_close();
    rfbClearNuRfb(nurfb);
    rfbScreenCleanup(rfbScreen);
    return(0);
}
