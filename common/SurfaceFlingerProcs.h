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

#ifndef INTEL_UFO_HWC_SURFACEFLINGERPROCS_H
#define INTEL_UFO_HWC_SURFACEFLINGERPROCS_H

#include "Common.h"

namespace intel {
namespace ufo {
namespace hwc {


/**
 */
class SurfaceFlingerProcs : NonCopyable
{
public:
    SurfaceFlingerProcs();

    void init(hwc_procs const* procs);

    typedef const struct hwc_procs* CallbackData;
    typedef int DisplayId;
    typedef void (*CallBackHotplug)(CallbackData, DisplayId, int32_t);
    typedef void (*CallBackRefresh)(CallbackData);
    typedef void (*CallBackVsync)(CallbackData, DisplayId, int64_t);

    void refresh(DisplayId disp = 0) const
    {
        mRefresh(mRefreshData);
        HWC_UNUSED(disp);
    }

    void vsync(DisplayId disp, int64_t timestamp) const
    {
        mVsync(mVsyncData, disp, timestamp);
    }

    void hotplug(DisplayId disp, int connected) const
    {
        mHotplug(mHotplugData, disp, connected);
    }


private:

    CallBackHotplug mHotplug;
    CallBackRefresh mRefresh;
    CallBackVsync   mVsync;

    CallbackData    mHotplugData;
    CallbackData    mRefreshData;
    CallbackData    mVsyncData;
};

} // namespace hwc
} // namespace ufo
} // namespace intel

#endif // INTEL_UFO_HWC_SURFACEFLINGERPROCS_H
