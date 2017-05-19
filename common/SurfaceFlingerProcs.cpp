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

#include "SurfaceFlingerProcs.h"

namespace intel {
namespace ufo {
namespace hwc {

static void fake_invalidate(SurfaceFlingerProcs::CallbackData)
{
    ALOGW( "Ignoring proc invalidate until registration has completed" );
}
static void fake_vsync(SurfaceFlingerProcs::CallbackData, SurfaceFlingerProcs::DisplayId, int64_t)
{
    ALOGW( "Ignoring proc invalidate until registration has completed" );
}
static void fake_hotplug(SurfaceFlingerProcs::CallbackData, SurfaceFlingerProcs::DisplayId, int32_t)
{
    ALOGW( "Ignoring proc invalidate until registration has completed" );
}

/**
 */
SurfaceFlingerProcs::SurfaceFlingerProcs()
{
    mHotplug = fake_hotplug;
    mRefresh = fake_invalidate;
    mVsync   = fake_vsync;
    mHotplugData = NULL;
    mRefreshData = NULL;
    mVsyncData   = NULL;
}

void SurfaceFlingerProcs::init(hwc_procs const* procs)
{
    ALOG_ASSERT(procs);
    ALOG_ASSERT(procs->invalidate);
    ALOG_ASSERT(procs->vsync);
    ALOG_ASSERT(procs->hotplug);

    mHotplug = procs->hotplug;
    mRefresh = procs->invalidate;
    mVsync = procs->vsync;

    mHotplugData = procs;
    mRefreshData = procs;
    mVsyncData = procs;
}

} // namespace hwc
} // namespace ufo
} // namespace intel
