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

#include "Common.h"
#include "VpgPlatform.h"
#include "VpgBufferManager.h"

#include "Drm.h"
#include "PhysicalDisplay.h"
#include "VppComposer.h"
#include "GlCellComposer.h"
#include "PartitionedComposer.h"
#include "GrallocClient.h"

#if !defined(DRM_CAP_KMS_CONTROL_NODE)
#define DRM_CAP_KMS_CONTROL_NODE 0x12
#endif

namespace intel {
namespace ufo {
namespace hwc {

using namespace intel::ufo::gralloc;
bool sControlCap = false;

AbstractPlatform& AbstractPlatform::get()
{
    return VpgPlatform::getInstance();
}

int AbstractPlatform::getDrmHandle()
{
    int fd;
#if defined DRM_NODE_CONTROL
    uint64_t value = 0;

    fd = drmOpenWithType("i915", NULL, DRM_NODE_CONTROL);
    if (fd == -1)
    {
        GrallocClient::getInstance().getFd(&fd);
    } else if(drmGetCap(fd, DRM_CAP_KMS_CONTROL_NODE, &value) != Drm::SUCCESS)
    {
        drmClose(fd);
        GrallocClient::getInstance().getFd(&fd);
    }
    sControlCap = true;
#else
        GrallocClient::getInstance().getFd(&fd);
#endif
    LOG_ALWAYS_FATAL_IF( fd == -1, "Unable to get DRM handle");
    return fd;
}

VpgPlatform::VpgPlatform() :
    mpHwc(NULL),
    mOptionVppComposer("vppcomposer", 1),
    mOptionPartGlComp("partglcomp", 1)
{
}

VpgPlatform::~VpgPlatform()
{
#if defined DRM_NODE_CONTROL
    int fd = -1;
    fd = Drm::get().getDrmHandle();
    if(sControlCap)
    {
        drmClose(fd);
        ALOGD("HWC Close Drm %d",fd);
    }
#endif
}

status_t VpgPlatform::open(Hwc* pHwc)
{
    ALOG_ASSERT(pHwc);
    mpHwc = pHwc;

    // Plug in some hardware displays if we have any.
    if ( !mpHwc->getPhysicalDisplays( ) )
    {
        // Try to initialise the Drm subsystem.
        Drm::get().init(*pHwc);

        // Check for DrmDevices
        Drm::get().probe(*mpHwc);
    }

    // Initialise our composers
    CompositionManager& compositionManager = CompositionManager::getInstance();
    if (mOptionVppComposer)
    {
        AbstractComposer* pComposer = new VppComposer();
        if (pComposer)
        {
            compositionManager.add(pComposer);
        }
        else
        {
            ALOGE("Failed to allocate VppComposer");
        }
    }

    if (mOptionPartGlComp)
    {
        auto pCellComposer= GlCellComposer::create();
        if (pCellComposer)
        {
            AbstractComposer* pComposer = new PartitionedComposer(pCellComposer);
            if (pComposer)
            {
                compositionManager.add(pComposer);
            }
            else
            {
                ALOGE("Failed to allocate PartitionedComposer");
            }
        }
        else
        {
            ALOGE("Failed to allocate PartitionedComposer");
        }
    }

   return OK;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
