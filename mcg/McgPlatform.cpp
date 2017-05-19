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
#include "McgPlatform.h"
#include "Drm.h"

namespace intel {
namespace ufo {
namespace hwc {

AbstractPlatform& AbstractPlatform::get()
{
    return McgPlatform::getInstance();
}

int AbstractPlatform::getDrmHandle()
{
    // TODO: Need to obtain the DRM master handle
    int fd = 0;
    return fd;
}


McgPlatform::McgPlatform() :
    mpHwc(NULL)
{
}


McgPlatform::~McgPlatform()
{
}

status_t McgPlatform::open(Hwc* pHwc)
{
    ALOG_ASSERT(pHwc);

    mpHwc = pHwc;

    // Try to initialise the display subsystem.
    // This should plug in some hardware displays if we have any.
    // ProxyDisplay should take over and noop the display if we dont.
    Drm::get().init(*pHwc);
    Drm::get().probe(*mpHwc);

    return OK;
}


}; // namespace hwc
}; // namespace ufo
}; // namespace intel


