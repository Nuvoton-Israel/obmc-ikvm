#include "ikvm_server.hpp"

#include <rfb/rfbproto.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace ikvm
{

using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

Server::Server(const Args& args, Input& i, Video& v) :
    pendingResize(false), frameCounter(0), numClients(0), input(i), video(v)
{
    std::string ip("localhost");
    const Args::CommandLine& commandLine = args.getCommandLine();
    int argc = commandLine.argc;

    server = rfbGetScreen(&argc, commandLine.argv, video.getWidth(),
                          video.getHeight(), Video::bitsPerSample,
                          Video::samplesPerPixel, Video::bytesPerPixel);

    if (!server)
    {
        log<level::ERR>("Failed to get VNC screen due to invalid arguments");
        elog<InvalidArgument>(
            xyz::openbmc_project::Common::InvalidArgument::ARGUMENT_NAME(""),
            xyz::openbmc_project::Common::InvalidArgument::ARGUMENT_VALUE(""));
    }

    framebuffer.resize(
        video.getHeight() * video.getWidth() * Video::bytesPerPixel, 0);

    format = &server->serverFormat;

    format->redMax = 31;
    format->greenMax = 63;
    format->blueMax = 31;
    format->redShift = 11;
    format->greenShift = 5;
    format->blueShift = 0;

    server->screenData = this;
    server->desktopName = "OpenBMC IKVM";
    server->frameBuffer = framebuffer.data();
    server->newClientHook = newClient;
    server->cursor = rfbMakeXCursor(cursorWidth, cursorHeight, (char*)cursor,
                                    (char*)cursorMask);
    server->cursor->xhot = 1;
    server->cursor->yhot = 1;

    //rfbStringToAddr(&ip[0], &server->listenInterface);

    rfbInitServer(server);

    rfbMarkRectAsModified(server, 0, 0, video.getWidth(), video.getHeight());

    server->kbdAddEvent = Input::keyEvent;
    server->ptrAddEvent = Input::pointerEvent;

    processTime = (1000000 / video.getFrameRate()) - 100;

    video.setCompareMode(false);
}

Server::~Server()
{
    rfbScreenCleanup(server);
}

void Server::resize()
{
    if (frameCounter > video.getFrameRate())
    {
        doResize();
    }
    else
    {
        pendingResize = true;
    }
}

void Server::run()
{
    rfbProcessEvents(server, processTime);

    if (server->clientHead)
    {
        input.sendReport();

        frameCounter++;
        if (pendingResize && frameCounter > video.getFrameRate())
        {
            doResize();
            pendingResize = false;
        }
    }
}

void Server::sendFrame()
{
    char* data = video.getData();
    rfbClientIteratorPtr it;
    rfbClientPtr cl;

    if (!data || pendingResize)
    {
        return;
    }

    it = rfbGetClientIterator(server);

    while ((cl = rfbClientIteratorNext(it)))
    {
        unsigned int x, y, w, h, clipcount;

        ClientData* cd = (ClientData*)cl->clientData;
        rfbFramebufferUpdateMsg* fu = (rfbFramebufferUpdateMsg*)cl->updateBuf;
        rfbStatList *ptr = rfbStatLookupMessage(cl, rfbFramebufferUpdateRequest);

        if (!cd)
        {
            continue;
        }

        if (cd->skipFrame)
        {
            cd->skipFrame--;
            continue;
        }

     /*   if (!cd->needUpdate)
        {
            continue;
        }
        cd->needUpdate = false;
    */
        if (FullframeCounter > 0)
            FullframeCounter--;

        if (FullframeCounter == 0)
            video.setCompareMode(true);

        clipcount = video.getClip(&x, &y, &w, &h);
        if (clipcount <= 0)
            continue;

        if (cl->enableLastRectEncoding)
        {
            fu->nRects = 0xFFFF;
        }
        else
        {
            fu->nRects = Swap16IfLE(clipcount);
        }

        fu->type = rfbFramebufferUpdate;
        cl->ublen = sz_rfbFramebufferUpdateMsg;
        rfbSendUpdateBuf(cl);

		cl->scaledScreen->frameBuffer = data;

        do {
            rfbSendRectEncodingHextile(cl, x, y, w, h);
        } while (video.getClip(&x, &y, &w, &h) > 0);
/*
        cl->tightEncoding = rfbEncodingTight;
        rfbSendTightHeader(cl, 0, 0, video.getWidth(), video.getHeight());

        cl->updateBuf[cl->ublen++] = (char)(rfbTightJpeg << 4);
        rfbSendCompressedDataTight(cl, data, video.getFrameSize());
*/
        if (cl->enableLastRectEncoding)
        {
            rfbSendLastRectMarker(cl);
        }

        rfbSendUpdateBuf(cl);
    }

    rfbReleaseClientIterator(it);
}

void Server::clientFramebufferUpdateRequest(
    rfbClientPtr cl, rfbFramebufferUpdateRequestMsg *furMsg)
{
    ClientData *cd = (ClientData *)cl->clientData;

    if (!cd)
        return;

    // Ignore the furMsg info. This service uses full frame update always.
    (void)furMsg;

    cd->needUpdate = true;
}

void Server::clientGone(rfbClientPtr cl)
{
    Server* server = (Server*)cl->screen->screenData;

    delete (ClientData*)cl->clientData;
    cl->clientData = nullptr;
    if (server->numClients-- == 1)
    {
        rfbMarkRectAsModified(server->server, 0, 0, server->video.getWidth(),
                              server->video.getHeight());
    }
}

enum rfbNewClientAction Server::newClient(rfbClientPtr cl)
{
    Server* server = (Server*)cl->screen->screenData;

    cl->clientData =
        new ClientData(server->video.getFrameRate(), &server->input);
    cl->clientGoneHook = clientGone;
    cl->clientFramebufferUpdateRequestHook = clientFramebufferUpdateRequest;
    if (!server->numClients++)
    {
        server->pendingResize = false;
        server->frameCounter = 0;
        server->FullframeCounter = 5;
    }

    return RFB_CLIENT_ACCEPT;
}


void rfbNuInitRfbFormat(rfbScreenInfoPtr screen)
{
	rfbPixelFormat *format = &screen->serverFormat;

	screen->colourMap.count = 0;
	screen->colourMap.is16 = 0;
	screen->colourMap.data.bytes = NULL;
	format->bitsPerPixel = screen->bitsPerPixel;
	format->depth = screen->depth;
	format->bigEndian = FALSE;
	format->trueColour = TRUE;
    format->redMax = 31;
    format->greenMax = 63;
    format->blueMax = 31;
    format->redShift = 11;
    format->greenShift = 5;
    format->blueShift = 0;
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

	screen->frameBuffer = framebuffer;

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

void Server::doResize()
{
    rfbClientIteratorPtr it;
    rfbClientPtr cl;

    framebuffer.resize(
        video.getHeight() * video.getWidth() * Video::bytesPerPixel, 0);

    rfbNuNewFramebuffer(server, framebuffer.data(), video.getWidth(),
                      video.getHeight(), Video::bitsPerSample,
                      Video::samplesPerPixel, Video::bytesPerPixel);
    rfbMarkRectAsModified(server, 0, 0, video.getWidth(), video.getHeight());

    it = rfbGetClientIterator(server);

    while ((cl = rfbClientIteratorNext(it)))
    {
        ClientData* cd = (ClientData*)cl->clientData;

        if (!cd)
        {
            continue;
        }

        // delay video updates to give the client time to resize
        cd->skipFrame = video.getFrameRate();
    }

    rfbReleaseClientIterator(it);

    FullframeCounter = 5;
}

} // namespace ikvm
