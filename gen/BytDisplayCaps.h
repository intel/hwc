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

#ifndef INTEL_UFO_HWC_BYTDISPLAYCAPS_H
#define INTEL_UFO_HWC_BYTDISPLAYCAPS_H

#include "DisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

// ********************************************************************
// Display Capabilities for DRM Baytrail/Cherrytrail class devices.
// ********************************************************************

class BytPlaneCaps : public DisplayCaps::PlaneCaps
{
};

class BytDisplayCaps : public DisplayCaps
{
public:


    BytDisplayCaps(uint32_t pipe, bool bCherrytrail);
    void probe();

    static const unsigned int cPlaneCount = 3;
    DisplayCaps::PlaneCaps* createPlane(uint32_t planeIndex)
    {
        ALOG_ASSERT(planeIndex < cPlaneCount);
        return &mPlanes[planeIndex];
    }
private:
    BytPlaneCaps        mPlanes[cPlaneCount];
    uint32_t            mPipe;
    bool                mbCherrytrail:1;
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_BYTDISPLAYCAPS_H
