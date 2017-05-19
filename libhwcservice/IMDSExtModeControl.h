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

#ifndef INTEL_UFO_HWC_IMDSEXTMODE_CONTROL_H
#define INTEL_UFO_HWC_IMDSEXTMODE_CONTROL_H

#include "HwcServiceHelper.h"
#include <utils/RefBase.h>

namespace intel {
namespace ufo {
namespace hwc {
namespace services {


using namespace android;

/*
 * DEPRECATED: This is now a compataibilty layer over the supported API
 * and will be removed!  NO additional entry points should be added here.
*/
class IMDSExtModeControl : public android::RefBase
{
public:
    status_t updateVideoState(int64_t videoSessionID, bool isPrepared)
    {
        return HwcService_MDS_UpdateVideoState(mHwcConn, videoSessionID, isPrepared ? HWCS_TRUE : HWCS_FALSE);
    }

    status_t updateVideoFPS(int64_t videoSessionID, int32_t fps)
    {
        return HwcService_MDS_UpdateVideoFPS(mHwcConn, videoSessionID, fps);
    }

    status_t updateInputState(bool state)
    {
        return HwcService_MDS_UpdateInputState(mHwcConn, state ? HWCS_TRUE : HWCS_FALSE);
    }

private:
    HwcServiceConnection mHwcConn;
};


} // namespace services
} // namespace hwc
} // namespace ufo
} // namespace intel

#endif // INTEL_UFO_HWC_IMDSEXTMODE_CONTROL_H
