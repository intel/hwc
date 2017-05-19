/*
// Copyright (c) 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include "../libhwcservice/IService.h"
#include "../common/Format.h"

#include "GrallocClient.h"

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include <cutils/memory.h>

#include <utils/Log.h>

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include <utils/Trace.h>

#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>

#include <ui/DisplayInfo.h>
#include "ufo/graphics.h"

// Drm
#include <xf86drm.h>

// Conditional build in FBR.
#define INTEL_UFO_HWC_HAVE_GRALLOC_FBR (defined(INTEL_UFO_GRALLOC_USAGE_PRIVATE_FBR) && 1)


using namespace intel::ufo::hwc::services;
using namespace android;
using namespace intel::ufo::hwc;
using namespace intel::ufo::gralloc;

#define MIN(a,b) ((a)<(b))?(a):(b)

uint32_t getBPP( uint32_t bufferFormat )
{

    uint32_t bpp;
    switch (bufferFormat)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
             bpp = 4;
             break;
        case HAL_PIXEL_FORMAT_RGB_888:
             bpp = 3;
             break;
        case HAL_PIXEL_FORMAT_RGB_565:
            bpp = 2;
            break;
        default:
            bpp = 1;
            break;
     }

    return bpp;
}

void setMaxPriority( void )
{
    pthread_t self = pthread_self();
    struct sched_param params;
    params.sched_priority = sched_get_priority_max( SCHED_FIFO );
    int err = pthread_setschedparam( self, SCHED_FIFO, &params );
    if ( err )
    {
        printf( "Failed set sched param [%d/%d/%s]\n", err, errno, strerror( errno ) );
        return;
    }

    int policy = 0;
    err = pthread_getschedparam( self, &policy, &params );
    if ( err  )
    {
        printf( "Failed get sched param [%d/%d/%s]\n", err, errno, strerror( errno ) );
        return;
    }
    printf( "Policy %u%s Priority %u\n",
        policy, ( policy == SCHED_FIFO ) ? " SCHED_FIFO" : "", params.sched_priority );
}

int main(int argc, char** argv)
{
    // Styles
    enum EStyle
    {
        STYLE_SOLID      = 0,
        STYLE_HORIZONTAL = 1,
        STYLE_VERTICAL   = 2
    };

    // Argument parameters
    uint32_t usleepTime = 0;
    uint32_t burstFrames = 0;
    uint32_t bufferFormat = HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL;
    uint32_t bufferCount = 3;
    uint32_t layerDepth = 250000;       // In front of the status bars and virtual secondary display but behind the cursor plane
    uint32_t bufferWidth = 0;
    uint32_t bufferHeight = 0;
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
    uint32_t usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_SW_WRITE_OFTEN;
    uint32_t surfaceFlags = 0;
    bool     bRandomColour = false;
    uint32_t solidColour = 0;
    bool     bQuiet = false;
    float    constantAlpha = 1;
    bool     bSingleBuffer = false;
    uint32_t style = STYLE_HORIZONTAL;
    uint32_t step = 1;
    uint32_t linewidth = 3;
    uint32_t syncvblank = 0;
    int      drmFd;

    bool bAnimate = false;
    int32_t x = 0;
    int32_t y = 0;
    int32_t xoff = 2;
    int32_t yoff = 2;

    // process arguments
    int argIndex = 1;
    while (argIndex < argc)
    {
        if (strncmp(argv[argIndex], "--sleep=", 8) == 0)
        {
            usleepTime = atoi(argv[argIndex] + 8);
            printf("sleep = %ds\n", usleepTime);
            usleepTime *= 1000000;
        }
        if (strncmp(argv[argIndex], "--msleep=", 9) == 0)
        {
            usleepTime = atoi(argv[argIndex] + 9);
            printf("msleep = %dms\n", usleepTime);
            usleepTime *= 1000;
        }
        if (strncmp(argv[argIndex], "--burst=", 8) == 0)
        {
            burstFrames = atoi(argv[argIndex] + 8);
            printf("burst = %d\n", burstFrames);
        }
        else if (strncmp(argv[argIndex], "--format=", 9) == 0)
        {
            bufferFormat = atoi(argv[argIndex] + 9);
            printf("bufferFormat = %d\n", bufferFormat);
        }
        else if (strncmp(argv[argIndex], "--count=", 8) == 0)
        {
            bufferCount = atoi(argv[argIndex] + 8);
            printf("bufferCount = %d\n", bufferCount);
            if ( bufferCount <= 1 )
            {
                bSingleBuffer = true;
                bufferCount = 1;
                printf("single buffer mode\n");
                style = STYLE_VERTICAL;
                linewidth = 10;
                step = 10;
                printf("prefer vertical style with width %u and x%u stepping\n", linewidth, step);
            }
        }
        else if (strncmp(argv[argIndex], "--depth=", 8) == 0)
        {
            layerDepth = atoi(argv[argIndex] + 8);
            printf("Layer depth = %d\n", layerDepth);
        }
        else if (strncmp(argv[argIndex], "--width=", 8) == 0)
        {
            bufferWidth = atoi(argv[argIndex] + 8);
            printf("buffer width = %d\n", bufferWidth);
        }
        else if (strncmp(argv[argIndex], "--height=", 9) == 0)
        {
            bufferHeight = atoi(argv[argIndex] + 9);
            printf("buffer height = %d\n", bufferHeight);
        }
        else if (strncmp(argv[argIndex], "--swidth=", 9) == 0)
        {
            screenWidth = atoi(argv[argIndex] + 9);
            printf("screen width = %d\n", screenWidth);
        }
        else if (strncmp(argv[argIndex], "--sheight=", 10) == 0)
        {
            screenHeight = atoi(argv[argIndex] + 10);
            printf("screen height = %d\n", screenHeight);
        }
        else if (strncmp(argv[argIndex], "--colour=", 9) == 0)
        {
            sscanf(argv[argIndex] + 9, "%x", &solidColour);
            printf("colour = 0x%x\n", solidColour);
            if (!(surfaceFlags & ISurfaceComposerClient::eNonPremultiplied))
            {
                uint32_t alpha = solidColour >> 24;
                solidColour = ((((solidColour & 0xFF) * alpha) / 255) & 0xFF)
                            | (((((solidColour >> 8) & 0xFF) * alpha) / 255) << 8)
                            | (((((solidColour >> 16) & 0xFF) * alpha) / 255) << 16)
                            | (solidColour & 0xFF000000);
                printf("pre-multiplied colour = 0x%x\n", solidColour);
            }
            style = STYLE_SOLID;
            printf("prefer solid style\n");
        }
        else if (strncmp(argv[argIndex], "--colour", 8) == 0)
        {
            printf("random colour\n");
            style = STYLE_SOLID;
            bRandomColour = true;
        }
        else if (strncmp(argv[argIndex], "--protected", 11) == 0)
        {
            usage |= GRALLOC_USAGE_PROTECTED;
        }
        else if (strncmp(argv[argIndex], "--secure", 8) == 0)
        {
            surfaceFlags |= ISurfaceComposerClient::eSecure;
        }
        else if (strncmp(argv[argIndex], "--nonpremult", 12) == 0)
        {
            surfaceFlags |= ISurfaceComposerClient::eNonPremultiplied;
        }
        else if (strncmp(argv[argIndex], "--opaque", 8) == 0)
        {
            surfaceFlags |= ISurfaceComposerClient::eOpaque;
        }
        else if (strncmp(argv[argIndex], "--quiet", 7) == 0)
        {
            bQuiet = true;
        }
        else if (strncmp(argv[argIndex], "--alpha=", 8) == 0)
        {
            sscanf(argv[argIndex] + 8, "%f", &constantAlpha);
            printf("alpha = %f\n", constantAlpha);
        }
        else if (strncmp(argv[argIndex], "--style=", 8) == 0)
        {
            style=atoi(argv[argIndex] + 8);
            printf("style = %d/%s\n", style,
                style==STYLE_HORIZONTAL ? "horizontal" :
                style==STYLE_VERTICAL ? "vertical" :
                "<?>");
        }
        else if (strncmp(argv[argIndex], "--step=", 7) == 0)
        {
            step = atoi(argv[argIndex] + 7);
            printf("step = %d\n", step);
        }
        else if (strncmp(argv[argIndex], "--linewidth=", 12) == 0)
        {
            linewidth = atoi(argv[argIndex] + 12);
            printf("linewidth = %d\n", linewidth);
        }
        else if (strncmp(argv[argIndex], "--syncvblank=", 13) == 0)
        {
            // us delay.
            syncvblank = atoi(argv[argIndex] + 13);
            printf("syncvblank = %dus\n", syncvblank);
        }


        argIndex++;
    }

    // set up the thread-pool
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();

    // Find and connect to HWC service
    sp<IService> hwcService = interface_cast<IService>(defaultServiceManager()->getService(String16(INTEL_HWC_SERVICE_NAME)));
    if(hwcService == NULL) {
        printf("Could not connect to service %s\n", INTEL_HWC_SERVICE_NAME);
        return -1;
    }

    hw_module_t const* module;
    // TODO: Use GrallocBufferMapper class.
    hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    struct gralloc_module_t* gralloc_module = (struct gralloc_module_t *)module;

    // create a client to surfaceflinger
    sp<SurfaceComposerClient> client = new SurfaceComposerClient();

    // Query the display state for surface size etc
    DisplayInfo dinfo;
    sp<IBinder> display = SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain);
    client->getDisplayInfo(display, &dinfo);

    if (bufferWidth == 0)     bufferWidth = dinfo.w;
    if (bufferHeight == 0)    bufferHeight = dinfo.h;
    if (screenWidth == 0)     screenWidth = bufferWidth;
    if (screenHeight == 0)    screenHeight = bufferHeight;

    // If neither width or height are fullscreen, then start animating
    if (screenWidth < dinfo.w && screenHeight < dinfo.h)
        bAnimate = true;


    // Single buffer mode requires direct passthrough of the buffer to the display.
    // Apply some config changes/overrides to ensure it can work.
    if (bSingleBuffer)
    {
        if (bufferFormat != HAL_PIXEL_FORMAT_RGBX_8888)
        {
            bufferFormat = HAL_PIXEL_FORMAT_RGBX_8888;
            printf("Forced HAL_PIXEL_FORMAT_RGBX_8888 for single buffer mode\n");
        }
        if ( syncvblank == 0 )
        {
            syncvblank = 1;
            printf("Forced vblank sync for single buffer mode\n");
        }
#if INTEL_UFO_HWC_HAVE_GRALLOC_FBR
        printf("Adding INTEL_UFO_GRALLOC_USAGE_PRIVATE_FBR usage for single buffer mode\n");
        usage |= (INTEL_UFO_GRALLOC_USAGE_PRIVATE_FBR);
#else
        printf("FBR not available\n");
#endif
    }

    if ( syncvblank )
    {
        // Open drm driver.
        drmFd = drmOpen( "i915", NULL );
        if ( drmFd == -1 )
        {
            printf( "*ERROR* Could not open Drm [%d/%s]\n", errno, strerror( errno ) );
        }
        // Set max priority for sync.
        setMaxPriority();
    }

    // Calculate the bpp for the format
    uint32_t bpp = getBPP(bufferFormat);
    printf("BufferSize=%dx%d ScreenSize=%dx%d usage=%x, format=0x%x\n", bufferWidth, bufferHeight, screenWidth, screenHeight, usage, bufferFormat);

    sp<SurfaceControl> surfaceControl = client->createSurface(
            String8(argv[0]), screenWidth, screenHeight, bufferFormat/*PIXEL_FORMAT_RGB_565*/, surfaceFlags);
    if (surfaceControl == NULL)
    {
        printf("Failed to create SurfaceControl\n");
        return 1;
    }

    SurfaceComposerClient::openGlobalTransaction();
    surfaceControl->setAlpha(constantAlpha);
    surfaceControl->setLayer(layerDepth);
    surfaceControl->setPosition(0,0);
    surfaceControl->setSize(screenWidth, screenHeight);
    SurfaceComposerClient::closeGlobalTransaction();

    ANativeWindow* window = surfaceControl->getSurface().get();
    if (window == NULL)
    {
        printf("Failed to get ANativeWindow\n");
        return 1;
    }

    window->perform(window, NATIVE_WINDOW_SET_BUFFERS_FORMAT, bufferFormat);
    window->perform(window, NATIVE_WINDOW_SET_BUFFER_COUNT, bufferCount);
    window->perform(window, NATIVE_WINDOW_SET_USAGE, usage);
    window->perform(window, NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS, bufferWidth, bufferHeight);
    window->perform(window, NATIVE_WINDOW_SET_SCALING_MODE, NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
    window->perform(window, NATIVE_WINDOW_API_CONNECT, NATIVE_WINDOW_API_CPU);

    //int err = native_window_set_buffer_count(window, 8);
    ANativeWindowBuffer* buffer = NULL;

    uint32_t burstCount = burstFrames;
    uint32_t iterations = 0;
    uint32_t f = 0;
    uint64_t timeRender = 0;
    uint32_t totalFrames = 0;
    nsecs_t startTime = systemTime(SYSTEM_TIME_MONOTONIC);
    uint32_t effectiveBufferCount = bSingleBuffer ? 1 : bufferCount;
    uint32_t vblankseq = 0;

    unsigned char *dstPtr = NULL;

    while (1)
    {
        if ( syncvblank )
        {
            // Blocking request for initial/next vblank.
            drmVBlank vbl;
            vbl.request.sequence = vblankseq;
            vbl.request.signal = 0;
            vbl.request.type = (drmVBlankSeqType)( DRM_VBLANK_ABSOLUTE
                                                | ( iterations == 0 ) ? DRM_VBLANK_NEXTONMISS : 0 );
            int err = drmWaitVBlank( drmFd, &vbl );
            if ( err )
            {
                printf( "failed sync vblank [%d/%d/%s]\n", err, errno, strerror( errno ) );
            }
            vblankseq = vbl.reply.sequence + 1;
            usleep( syncvblank );
        }

        const unsigned long timeBefore = (uint64_t)systemTime(SYSTEM_TIME_MONOTONIC);

        if (bAnimate)
        {
            x += xoff;
            y += yoff;

            if (x <= 0 || x + screenWidth >= dinfo.w)
                xoff = -xoff;
            if (y <= 0 || y + screenHeight >= dinfo.h)
                yoff = -yoff;

            SurfaceComposerClient::openGlobalTransaction();
            surfaceControl->setPosition(x, y);
            SurfaceComposerClient::closeGlobalTransaction();
        }

        const bool bDequeueQueue = (buffer == NULL) || (!bSingleBuffer);

        if (bDequeueQueue)
        {
            window->dequeueBuffer_DEPRECATED(window, &buffer);
        }
        if (buffer == NULL)
        {
            printf("Failed to dequeue buffer\n");
            return 1;
        }

        // Convert stride to bytes
        bpp = getBPP(bufferFormat);
        uint32_t stride = buffer->stride * bpp;

        if ( bDequeueQueue )
        {
            gralloc_module->lock(gralloc_module, buffer->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, 0, 0, bufferWidth, bufferHeight, (void **)&dstPtr);
            if (dstPtr == NULL)
            {
                printf("Failed to lock buffer\n");
                return 1;
            }
        }

        switch (style)
        {
            case STYLE_SOLID:
            {
                switch (bpp)
                {
                case 4:
                    {
                        uint32_t* pDst = (uint32_t*)dstPtr;
                        static uint32_t colour[] = {
                                                    0x00000000,
                                                    0x000000ff,
                                                    0x0000ff00,
                                                    0x0000ffff,
                                                    0x00ff0000,
                                                    0x00ff00ff,
                                                    0x00ffff00,
                                                    0x00ffffff
                                                    };
                        if (bRandomColour)
                        {
                            solidColour = colour[f & 7];
                        }

                        for (uint32_t y = 0; y < bufferHeight; y++)
                        {
                            for (uint32_t x = 0; x < bufferWidth; x++)
                            {
                                pDst[x] = solidColour;
                            }
                            pDst += buffer->stride;
                        }
                    }
                    break;
                case 2:
                    {
                        uint16_t* pDst = (uint16_t*)dstPtr;
                        for (uint32_t y = 0; y < bufferHeight; y++)
                        {
                            for (uint32_t x = 0; x < bufferWidth; x++)
                            {
                                pDst[x] = uint16_t(solidColour);
                            }
                            pDst += buffer->stride;
                        }
                    }
                    break;
                case 1:
                    {
                        uint8_t* pDst = (uint8_t*)dstPtr;
                        for (uint32_t y = 0; y < bufferHeight; y++)
                        {
                            for (uint32_t x = 0; x < bufferWidth; x++)
                            {
                                pDst[x] = uint8_t(solidColour);
                            }
                            pDst += buffer->stride;
                        }
                    }
                    break;
                }
            }
            break;

            case STYLE_HORIZONTAL:
            {
                // Replace line(s) preceding the current position to all pixel bits 0.
                // Write lines at current position to all pixel bits 1.
                // If a packed RGBX buffer then this will be black/white.
                // If a planar YUV buffer then this will be Y channel only, so darkgreen/brightgreen.
                uint32_t resetPos = ( (iterations-effectiveBufferCount) * step ) % bufferHeight;
                uint32_t setPos = ( iterations * step ) % bufferHeight;
                uint32_t resetWidth = (bufferHeight > resetPos+linewidth) ? linewidth : bufferHeight - resetPos;
                uint32_t setWidth = (bufferHeight > setPos+linewidth) ? linewidth : bufferHeight - setPos;
                if ( resetWidth )
                    memset(dstPtr + stride * resetPos, 0, stride * resetWidth);
                if ( setWidth )
                    memset(dstPtr + stride * setPos, 255, stride * setWidth);
            }
            break;

            case STYLE_VERTICAL:
            {
                // Write line(s) preceding the current position to all pixel bits 0.
                // Write lines at current position to all pixel bits 1.
                // If a packed RGBX buffer then this will be black/white.
                // If a planar YUV buffer then this will be Y channel only, so darkgreen/brightgreen.
                uint32_t resetPos = ( (iterations-effectiveBufferCount) * step ) % bufferWidth;
                uint32_t setPos = ( iterations * step ) % bufferWidth;
                uint32_t resetWidth = (bufferWidth > resetPos+linewidth) ? linewidth : bufferWidth - resetPos;
                uint32_t setWidth = (bufferWidth > setPos+linewidth) ? linewidth : bufferWidth - setPos;
                for (uint32_t y=0; y < bufferHeight; ++y)
                {
                    if ( resetWidth )
                    {
                        memset(dstPtr + stride * y + bpp*resetPos, 0, bpp*resetWidth);
                    }
                    if ( setWidth )
                    {
                        memset(dstPtr + stride * y + bpp*setPos, 255, bpp*setWidth);
                    }
                }
            }
            break;
        }

        if ( !bSingleBuffer )
        {
            gralloc_module->unlock(gralloc_module, buffer->handle);
        }

        const unsigned long timeAfter = (uint64_t)systemTime(SYSTEM_TIME_MONOTONIC);
        timeRender += (uint64_t)int64_t( timeAfter - timeBefore );

        if ( bDequeueQueue )
        {
            window->queueBuffer_DEPRECATED(window, buffer);
        }

        if (burstCount)
            burstCount--;

        if (usleepTime && burstCount == 0)
        {
            // Sleep for the requested number of seconds
            usleep (usleepTime);

            burstCount = burstFrames;
        }

        f++;
        if (!bQuiet && f > 120)
        {
            nsecs_t endTime = systemTime(SYSTEM_TIME_MONOTONIC);
            totalFrames += f;

            // Note: the first pass through the timing, this code has benefitted slightly from having an empty
            // buffer queue. This tends to report 60.1 FPS for the first frame and 60.0 FPS afterwards.
            fprintf(stderr, "Frame:%d FPS:%.1f AvgRender:%.2fms\n",
                totalFrames,
                ((double)f * 1000000000)/(endTime-startTime),
                ((float)timeRender / ( 1000000.0f * f ) ) );

            timeRender = 0;

            f = 0;
            startTime = endTime;
        }
        ++iterations;
    }

    return 0;
}
