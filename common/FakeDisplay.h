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

#ifndef INTEL_UFO_HWC_FAKE_H
#define INTEL_UFO_HWC_FAKE_H

#include "PhysicalDisplay.h"
#include "SinglePlaneDisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

class FakeDisplay : public PhysicalDisplay
{
public:
    FakeDisplay(Hwc& hwc, uint32_t x, uint32_t y);
    virtual ~FakeDisplay();

    // *************************************************************************
    // This class implements these AbstractPhysicalDisplay APIs.
    // *************************************************************************
    virtual void onSet(const Content::Display& display, uint32_t zorder, int* pRetireFenceFd);

    const char* getName() const { return "FakeDisplay"; }
private:
    SinglePlaneDisplayCaps          mCaps;

    // Timeline to generate fake fence for every frames.
    Timeline    mTimeline;
    uint32_t    mLastTimelineIndex;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_FAKE_H
