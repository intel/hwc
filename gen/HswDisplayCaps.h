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


#ifndef INTEL_UFO_HWC_HSWDISPLAYCAPS_H
#define INTEL_UFO_HWC_HSWDISPLAYCAPS_H

#include "DisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

// ********************************************************************
// Display Capabilities for DRM Haswell/Broadwell class devices.
// ********************************************************************
class HswPlaneCaps : public DisplayCaps::PlaneCaps
{
};

class HswDisplayCaps : public DisplayCaps
{
public:
    HswDisplayCaps(uint32_t pipe);
    void probe();
    static const unsigned int cPlaneCount = 4; // set to 4 temporary, to cover BXT planes
    DisplayCaps::PlaneCaps* createPlane(uint32_t planeIndex)
    {
        ALOG_ASSERT(planeIndex < cPlaneCount);
        return &mPlanes[planeIndex];
    }
private:
    HswPlaneCaps        mPlanes[cPlaneCount];
    uint32_t mPipe;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_HSWDISPLAYCAPS_H
