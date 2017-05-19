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
#include "IDiagnostic.h"
#include <binder/IServiceManager.h>

using namespace android;
using namespace intel::ufo::hwc::services;

int main(int argc, char** argv)
{

    // Find and connect to HWC service
    sp<IService> hwcService = interface_cast<IService>(defaultServiceManager()->getService(String16(INTEL_HWC_SERVICE_NAME)));
    if(hwcService == NULL)
    {
        printf("Could not connect to service %s\n", INTEL_HWC_SERVICE_NAME);
        return 1;
    }

    sp<IDiagnostic> pDiagnostic = hwcService->getDiagnostic();
    if (pDiagnostic == NULL)
    {
        printf("Could not connect to diagnostics %s\n", INTEL_HWC_SERVICE_NAME);
        return 1;
    }

    // process arguments
    int argIndex = 1;
    while (argIndex < argc)
    {
        if (argIndex+1 >= argc)
            goto usage;

        uint32_t d = atoi(argv[argIndex+1]);

        if (strcmp(argv[argIndex], "enable") == 0)
        {
            printf("enable %d\n", d);
            pDiagnostic->enableDisplay(d);
            argIndex += 2;
        }
        else if (strcmp(argv[argIndex], "disable") == 0)
        {
            printf("disable %d\n", d);
            pDiagnostic->disableDisplay(d, false);
            argIndex += 2;
        }
        else if (strcmp(argv[argIndex], "blank") == 0)
        {
            printf("blank %d\n", d);
            pDiagnostic->disableDisplay(d, true);
            argIndex += 2;
        }
        else if (strcmp(argv[argIndex], "hide") == 0)
        {
            if (argIndex+2 >= argc)
                goto usage;
            uint32_t layer = atoi(argv[argIndex+2]);
            printf("hide %d %d\n", d, layer);
            pDiagnostic->maskLayer(d, layer, true);
            argIndex += 3;
        }
        else if (strcmp(argv[argIndex], "unhide") == 0)
        {
            if (argIndex+2 >= argc)
                goto usage;
            uint32_t layer = atoi(argv[argIndex+2]);
            printf("unhide %d %d\n", d, layer);
            pDiagnostic->maskLayer(d, layer, false);
            argIndex += 3;
        }
        else if (strcmp(argv[argIndex], "dump") == 0)
        {
            if (argIndex+3 >= argc)
                goto usage;
            int32_t frames = atoi(argv[argIndex+2]);
            bool bSync = atoi(argv[argIndex+3]);
            printf("dump %d %d %d\n", d, frames, bSync);
            pDiagnostic->dumpFrames(d, frames, bSync);
            argIndex += 4;
        }
        else
            goto usage;
    }

    return 0;

usage:
    printf("Usage: %s enable <display>\n", argv[0]);
    printf("          disable <display>\n");
    printf("          blank <display>\n");
    printf("          hide <display> <layer>\n");
    printf("          unhide <display> <layer>\n");
    printf("          dump <display> <frames> <sync>\n");
    printf("                dumps to /data/hwc/ which must already exist.\n");
    printf("                frames -1 => continuous.\n");
    printf("                sync    1 => force at least one frame before returning.\n");
    printf("\n");

    return 0;
}
