#include "ikvm_server.hpp"

#include <rfb/rfbproto.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <boost/crc.hpp>

namespace ikvm
{

using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

Server::Server(const Args& args, Input& i, Video& v) :
    pendingResize(false), frameCounter(0), numClients(0), input(i), video(v), FullframeCounter(5)
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

    rfbNuInitRfbFormat(server);

    server->screenData = this;
    server->desktopName = "OpenBMC IKVM";
    server->frameBuffer = framebuffer.data();
    server->newClientHook = newClient;
    server->cursor = rfbMakeXCursor(cursorWidth, cursorHeight, (char*)cursor,
                                    (char*)cursorMask);
    server->cursor->xhot = 1;
    server->cursor->yhot = 1;

#if 0 // remove the limitation of localhost connect so that we can test by VNC Viewer
    rfbStringToAddr(&ip[0], &server->listenInterface);
#endif

    rfbInitServer(server);

    rfbMarkRectAsModified(server, 0, 0, video.getWidth(), video.getHeight());

    server->kbdAddEvent = Input::keyEvent;
    server->ptrAddEvent = Input::pointerEvent;

    processTime = (1000000 / video.getFrameRate()) - 100;
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
        frameCounter++;
        if (pendingResize && frameCounter > video.getFrameRate())
        {
            doResize();
            pendingResize = false;
        }
    }
}

 rfbBool Server::rfbSendCompressedDataHextile(rfbClientPtr cl, char *buf,
                                    int compressedLen)
 {
     int i, portionLen;
     portionLen = UPDATE_BUF_SIZE;

     for (i = 0; i < compressedLen; i += portionLen) {
         if (i + portionLen > compressedLen) {
             portionLen = compressedLen - i;
         }
         if (cl->ublen + portionLen > UPDATE_BUF_SIZE) {
             if (!rfbSendUpdateBuf(cl))
                 return FALSE;
         }
         memcpy(&cl->updateBuf[cl->ublen], &buf[i], portionLen);
         cl->ublen += portionLen;
     }

     return TRUE;
 }

void Server::sendFrame()
{
    char* data = nullptr;
    rfbClientIteratorPtr it;
    rfbClientPtr cl;
#if 0
    int64_t frame_crc = -1;
#endif

    if (pendingResize)
    {
        return;
    }

    it = rfbGetClientIterator(server);

    while ((cl = rfbClientIteratorNext(it)))
    {
        unsigned int x, y, w, h, clipcount;

        ClientData* cd = (ClientData*)cl->clientData;
        Server* server = (Server*)cl->screen->screenData;
        rfbFramebufferUpdateMsg* fu = (rfbFramebufferUpdateMsg*)cl->updateBuf;

        if (!cd)
        {
            continue;
        }

        if (cd->skipFrame)
        {
            cd->skipFrame--;
            continue;
        }

        if (!cd->needUpdate)
        {
            continue;
        }

        if (!data) {
            video.getFrame();

            data = video.getData();
            if (!data)
            {
                return;
            }
        }

#if 0
        if (calcFrameCRC)
        {
            if (frame_crc == -1)
            {
                /* JFIF header contains some varying data so skip it for
                 * checksum calculation */
                frame_crc = boost::crc<32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF,
                                       true, true>(data + 0x30,
                                                   video.getFrameSize() - 0x30);
            }

            if (cd->last_crc == frame_crc)
            {
                continue;
            }

            cd->last_crc = frame_crc;
        }
#endif

        if (server->numClients == 1) {
            if (FullframeCounter > 0)
                FullframeCounter--;

            if (FullframeCounter == 0)
                video.setCompareMode(true);
        }

        clipcount = video.getClip(&x, &y, &w, &h);
        if (clipcount <= 0)
            continue;

        cd->needUpdate = false;

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
        rfbSendCompressedDataHextile(cl, data, video.getFrameSize());

#if 0
        cl->tightEncoding = rfbEncodingTight;
        rfbSendTightHeader(cl, 0, 0, video.getWidth(), video.getHeight());

        cl->updateBuf[cl->ublen++] = (char)(rfbTightJpeg << 4);
        rfbSendCompressedDataTight(cl, data, video.getFrameSize());
#endif

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
        server->input.disconnect();
        rfbMarkRectAsModified(server->server, 0, 0, server->video.getWidth(),
                              server->video.getHeight());
    }
    else if (server->numClients == 1)
    {
        server->FullframeCounter = 5;
    }
}

enum rfbNewClientAction Server::newClient(rfbClientPtr cl)
{
    Server* server = (Server*)cl->screen->screenData;

    cl->clientData =
        new ClientData(server->video.getFrameRate(), &server->input);
    cl->clientGoneHook = clientGone;
    cl->clientFramebufferUpdateRequestHook = clientFramebufferUpdateRequest;
    cl->preferredEncoding = rfbEncodingHextile;
    server->video.setCompareMode(false);

    if (!server->numClients++)
    {
        server->input.connect();
        server->pendingResize = false;
        server->frameCounter = 0;
    }

    return RFB_CLIENT_ACCEPT;
}

void Server::rfbNuInitRfbFormat(rfbScreenInfoPtr screen)
{
    rfbPixelFormat *format = &screen->serverFormat;
    format->redMax = 31;
    format->greenMax = 63;
    format->blueMax = 31;
    format->redShift = 11;
    format->greenShift = 5;
    format->blueShift = 0;
}

void Server::doResize()
{
    rfbClientIteratorPtr it;
    rfbClientPtr cl;

    framebuffer.resize(
        video.getHeight() * video.getWidth() * Video::bytesPerPixel, 0);

    rfbNewFramebuffer(server, framebuffer.data(), video.getWidth(),
                      video.getHeight(), Video::bitsPerSample,
                      Video::samplesPerPixel, Video::bytesPerPixel);

    rfbNuInitRfbFormat(server);

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
