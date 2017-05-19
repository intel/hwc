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

#include "IService.h"
#include "HwcServiceApi.h"
#include <utils/String8.h>

#include <binder/IServiceManager.h>

#include <cinttypes>

using namespace android;
using namespace intel::ufo::hwc::services;

int main(int argc, char** argv)
{

    // Argument parameters
    uint32_t display = 0;
    uint32_t xres = 0;
    uint32_t yres = 0;
    uint32_t hz = 0;
    uint32_t flags = 0;
    uint32_t ratio = 0;
    char* pExtVideoMode = NULL;
    char* pMdsMode = NULL;
    bool bPrintModes = true;
    bool bPreferred  = false;
    bool bGet        = false;

    int argIndex = 1;
    int nonOptions = 0;

    // Process options
    while (argIndex < argc)
    {
        if (strncmp(argv[argIndex], "--", 2) == 0)
        {
            if (strncmp(argv[argIndex], "--extvideomode=", 15) == 0)
            {
                pExtVideoMode = argv[argIndex]+15;
                printf("ExtendedVideo = %s\n", pExtVideoMode);
            }

            if (strncmp(argv[argIndex], "--mds=", 6) == 0)
            {
                pMdsMode = argv[argIndex]+6;
                printf("Mds = %s\n", pMdsMode);
            }
        }
        else
        {
            // Process any non options
            switch (nonOptions)
            {
            case 0:
                display = atoi(argv[argIndex]);
                bPrintModes = true;
                break;

            case 1:
                bPrintModes = false;
                if (strcmp(argv[argIndex], "get") == 0)
                {
                    bGet = true;
                }
                else if (strcmp(argv[argIndex], "pref") == 0)
                {
                    bPreferred = true;
                }
                else if (sscanf(argv[argIndex], "%" PRIu32 "x%" PRIu32 "@%" PRIu32, &xres, &yres, &hz) != 3)
                {
                    if (sscanf(argv[argIndex], "%" PRIu32 "x%" PRIu32, &xres, &yres) != 2)
                    {
                        xres = yres = 0;
                    }
                }
                break;
            case 2:
                int32_t a, b;
                if (sscanf(argv[argIndex], "%d:%d", &a, &b) == 2)
                {
                    ratio = (a << 16) | b;
                }
                else
                {
                    printf("Invalid aspect ratio\n");
                    return 1;
                }
                break;
            }
            nonOptions++;
        }
        argIndex++;
    }

    if (nonOptions == 0)
    {
            printf("Usage: %s [--extvideomode=<0|1>] <display> <mode> [Aspect]"
                   "\n\t Mode should be formatted like 1280x720 or 1024x768@60 or it can be pref or get"
                   "\n\t The optional aspect ratio should be formatted 4:3 or 16:9\n", argv[0]);
            return 1;
    }

    // Find and connect to HWC service
    sp<IService> hwcService = interface_cast<IService>(defaultServiceManager()->getService(String16(INTEL_HWC_SERVICE_NAME)));
    if(hwcService == NULL) {
        printf("Could not connect to service %s\n", INTEL_HWC_SERVICE_NAME);
        return -1;
    }

    // Connect to HWC service
    HWCSHANDLE hwcs = HwcService_Connect();
    if(hwcs == NULL) {
        printf("Could not connect to service\n");
        return -1;
    }

    if (pExtVideoMode)
    {
        hwcService->setOption(String8("extendedmcg"), String8(pExtVideoMode));
    }

    if (pMdsMode)
    {
        hwcService->setOption(String8("mds"), String8(pMdsMode));
    }

    status_t modeCount = HwcService_DisplayMode_GetAvailableModes(hwcs, display, 0, NULL);
    Vector<HwcsDisplayModeInfo> modes;
    modes.resize(modeCount);
    HwcService_DisplayMode_GetAvailableModes(hwcs, display, modeCount, modes.editArray());
    if (bPrintModes)
    {
        printf("Display %d\n", display);
        for (uint32_t i = 0; i < modes.size(); i++)
        {
            printf("\t%-2d %4dx%-4d %3dHz %2d:%1d %s%s%s%s\n", i, modes[i].width, modes[i].height, modes[i].refresh, modes[i].ratio>>16, modes[i].ratio&0xFFFF,
                modes[i].flags & HWCS_MODE_FLAG_PREFERRED    ? "PREFERRED "    : "",
                modes[i].flags & HWCS_MODE_FLAG_SECURE       ? "SECURE "       : "",
                modes[i].flags & HWCS_MODE_FLAG_INTERLACED   ? "INTERLACED "   : "",
                modes[i].flags & HWCS_MODE_FLAG_CURRENT      ? "CURRENT "      : "");
        }
    }
    else if (bGet)
    {
        HwcsDisplayModeInfo mode;
        HwcService_DisplayMode_GetMode(hwcs, display, &mode);
        printf("Display %d: Get Current Mode %dx%d %dHz Flags:0x%x Ratio:%2d:%1d\n", display, mode.width, mode.height, mode.refresh, mode.flags, mode.ratio>>16, mode.ratio&0xFFFF);
    }
    else if (bPreferred)
    {
        for (uint32_t i = 0; i < modes.size(); i++)
        {
            if (modes[i].flags & HWCS_MODE_FLAG_PREFERRED)
            {
                printf("Display %d: Setting Preferred Mode %u\n", display, i);
                HwcService_DisplayMode_SetMode(hwcs, display, &modes[i]);
            }
        }
    }
    else
    {
        HwcsDisplayModeInfo mode;
        mode.width = xres;
        mode.height = yres;
        mode.refresh = hz;
        mode.flags = flags;
        mode.ratio = ratio;
        printf("Display %d: Setting Mode %dx%d %dHz Flags:%x Ratio:%2d:%d\n", display, xres, yres, hz, flags, ratio>>16, ratio&0xFFFF);
        status_t ret = HwcService_DisplayMode_SetMode(hwcs, display, &mode);
        if (ret != OK)
        {
            printf("Mode set failed\n");
            HwcService_Disconnect(hwcs);
            return 1;
        }
    }
    HwcService_Disconnect(hwcs);
    return 0;
}
