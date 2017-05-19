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

#include <cutils/memory.h>

#include <utils/Log.h>

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include <utils/Trace.h>

#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>

#include <ui/DisplayInfo.h>

#include "HwcServiceApi.h"

#include "ufo/graphics.h"

#include <binder/IServiceManager.h>

// For Power Modes
#include <hardware/hwcomposer_defs.h>

using namespace android;


int main(int argc, char** argv)
{
    // Argument parameters
    bool bBlank = false;
    bool bPrintUsage = true;
    status_t res;
    int display=0;

    //0--surfaceflinger; 1--hwc service
    int blankHandleType=0;

    // process arguments
    int argIndex = 1;
    while (argIndex < argc)
    {
        if (strncmp(argv[argIndex], "--sf", 4) == 0)
        {
            blankHandleType = 0;
        }
        else if (strncmp(argv[argIndex], "--hwc", 9) == 0)
        {
            blankHandleType = 1;
        }
        if (strncmp(argv[argIndex], "--blank", 7) == 0)
        {
            bBlank = true;
            bPrintUsage = false;
        }
        else if (strncmp(argv[argIndex], "--unblank", 9) == 0)
        {
            bBlank = false;
            bPrintUsage = false;
        }
        else if(strncmp(argv[argIndex], "--display=", 10) == 0)
        {
            display = atoi( argv[argIndex] + 10);
            if( ( display < 0 )||(display >= 2 ) )
            {
                printf("Error: invalid display number %d\n", display);
                return 1;
            }
        }
        argIndex++;

    }

    if (bPrintUsage)
    {
        printf("Usage: %s --sf/hwc --unblank  --display=X\n"
               "       %s --sf/hwc --blank    --display=X\n", argv[0], argv[0]);
        return 1;
    }

    if( blankHandleType == 0 )
    {
        // create a client to surfaceflinger
        sp<SurfaceComposerClient> client = new SurfaceComposerClient();


        sp<IBinder> display = SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain);

        printf(" Blank through SurfaceFlinger:\n");
#if defined(HWC_DEVICE_API_VERSION_1_4)
        if (bBlank)
            client->setDisplayPowerMode(display, HWC_POWER_MODE_OFF);
        else
            client->setDisplayPowerMode(display, HWC_POWER_MODE_NORMAL);
#else
        if (bBlank)
            client->blankDisplay(display);
        else
            client->unblankDisplay(display);
#endif
        return 0;
    }

    //display = ISurfaceComposer::eDisplayIdMain;
    // display = ISurfaceComposer::eDisplayIdHdmi;

    printf("Do blank by HWC:\n");
    // Connect to HWC service
    HWCSHANDLE hwcs = HwcService_Connect();
    if(hwcs == NULL) {
        printf("Could not connect to service\n");
        return -1;
    }

    printf( "Setting bBlank=%u\n", bBlank );
    res = HwcService_Display_EnableBlank(hwcs, display, bBlank ? HWCS_TRUE : HWCS_FALSE);
    printf("res=%d\n",res);

    HwcService_Disconnect(hwcs);

    return 0;
}

