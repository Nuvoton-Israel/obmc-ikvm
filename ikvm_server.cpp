#include "ikvm_server.hpp"

#include <linux/videodev2.h>
#include <rfb/rfbproto.h>

#include <boost/crc.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace ikvm
{

using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

Server::Server(const Args& args, Input& i, Video& v) :
    pendingResize(false), frameCounter(0), numClients(0), input(i), video(v),
    captureModeCounter(COMPLETE_FRAME_COUNT), fpsCounter(0)
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

void Server::checkClientFormat()
{
    rfbClientIteratorPtr it;
    rfbClientPtr cl;
    uint32_t format;

    it = rfbGetClientIterator(server);
    cl = rfbClientIteratorNext(it);

    // Use HW Hextile if client prefers Hextile encoding and supports 16bpp.
    // Otherwise, notify driver to return RGB565 raw data and use SW encoding.
    if (cl->preferredEncoding == rfbEncodingHextile && cl->format.bitsPerPixel == 16)
    {
        format = 0x4C545848; /* V4L2_PIX_FMT_HEXTILE Four-character-code */
    }
    else
    {
        format = V4L2_PIX_FMT_RGB565;
    }

    if (video.getWantedPixelFormat() != format)
    {
        video.setWantedPixelFormat(format);
        video.restart();
    }

    rfbReleaseClientIterator(it);
}

void Server::sendFrame()
{
    char* data = nullptr;
    rfbClientIteratorPtr it;
    rfbClientPtr cl;
    bool anyClientNeedUpdate = false;
    bool anyClientSkipFrame = false;
    unsigned int rectCount;
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
        ClientData* cd = (ClientData*)cl->clientData;

        if (!cd)
            continue;

        if (cd->skipFrame)
        {
            cd->skipFrame--;
            anyClientSkipFrame = true;
            continue;
        }

        if (cd->needUpdate)
            anyClientNeedUpdate = true;
    }

    rfbReleaseClientIterator(it);

    if (!anyClientSkipFrame && anyClientNeedUpdate)
    {
        video.getFrame();
        data = video.getData();

        if (!data)
            return;

        if(captureModeCounter == COMPLETE_FRAME_COUNT)
            video.setCaptureMode(true);

        if (captureModeCounter != 0)
            captureModeCounter--;
        else
            video.setCaptureMode(false);

        rectCount = video.getRectCount();

        if (!rectCount)
            return;

        // video.getFrame() may get the differences compared with last frame
        // so that all clients need to be updated simultaneously for synchronization
        it = rfbGetClientIterator(server);
        while ((cl = rfbClientIteratorNext(it)))
        {
            ClientData* cd = (ClientData*)cl->clientData;
            rfbFramebufferUpdateMsg* fu = (rfbFramebufferUpdateMsg*)cl->updateBuf;

            cd->needUpdate = false;

            if (cl->enableLastRectEncoding)
            {
                fu->nRects = 0xFFFF;
            }
            else
            {
                fu->nRects = Swap16IfLE(rectCount);
            }

            switch (video.getPixelFormat())
            {
                case V4L2_PIX_FMT_RGB565:
                    framebuffer.assign(data, data + video.getFrameSize());
                    rfbMarkRectAsModified(server, 0, 0, video.getWidth(),
                                          video.getHeight());
                    break;

                case 0x4C545848: /* V4L2_PIX_FMT_HEXTILE Four-character-code */
                    fu->type = rfbFramebufferUpdate;
                    cl->ublen = sz_rfbFramebufferUpdateMsg;
                    rfbSendUpdateBuf(cl);

                    rfbSendCompressedDataHextile(cl, data, video.getFrameSize());

                    if (cl->enableLastRectEncoding)
                    {
                        rfbSendLastRectMarker(cl);
                    }
                    rfbSendUpdateBuf(cl);
                    break;

                default:
                    break;
            }
        }

        rfbReleaseClientIterator(it);
        dumpFps();
    }
}

void Server::clientFramebufferUpdateRequest(
    rfbClientPtr cl, rfbFramebufferUpdateRequestMsg* furMsg)
{
    ClientData* cd = (ClientData*)cl->clientData;

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
}

enum rfbNewClientAction Server::newClient(rfbClientPtr cl)
{
    Server* server = (Server*)cl->screen->screenData;

    cl->clientData =
        new ClientData(server->video.getFrameRate(), &server->input);
    cl->clientGoneHook = clientGone;
    cl->clientFramebufferUpdateRequestHook = clientFramebufferUpdateRequest;
    server->captureModeCounter = COMPLETE_FRAME_COUNT;

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

void Server::rfbNuNewFramebuffer(rfbScreenInfoPtr screen, char* framebuffer,
                                 int width, int height, int bitsPerSample,
                                 int samplesPerPixel, int bytesPerPixel)
{
    rfbClientIteratorPtr iterator;
    rfbClientPtr cl;

    // Update information in the screenInfo structure
    if (width & 3)
        rfbErr("WARNING: New width (%d) is not a multiple of 4.\n", width);

    screen->width = width;
    screen->height = height;
    screen->bitsPerPixel = screen->depth = 8 * bytesPerPixel;
    screen->paddedWidthInBytes = width * bytesPerPixel;

    rfbNuInitRfbFormat(screen);

    screen->frameBuffer = framebuffer;

    // Adjust pointer position if necessary
    if (screen->cursorX >= width)
        screen->cursorX = width - 1;

    if (screen->cursorY >= height)
        screen->cursorY = height - 1;

    // For each client
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
    captureModeCounter = COMPLETE_FRAME_COUNT;
}

void Server::dumpFps()
{
    timespec end;

    if(!fpsCounter)
    {
        clock_gettime(CLOCK_MONOTONIC, &start);
        fpsCounter++;
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    if((end.tv_sec - start.tv_sec) >= FPS_DUMP_RATE)
    {
        rfbLog("Sent %d frames in %ds, fps: %d\n", fpsCounter, FPS_DUMP_RATE, fpsCounter / FPS_DUMP_RATE);
        fpsCounter = 0;
    }
    else
    {
        fpsCounter++;
    }
}

} // namespace ikvm
