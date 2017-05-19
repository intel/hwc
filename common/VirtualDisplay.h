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

#ifndef INTEL_HWC_VIRTUAL_DISPLAY_H
#define INTEL_HWC_VIRTUAL_DISPLAY_H

#include "PhysicalDisplay.h"
#include "SinglePlaneDisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

class VirtualDisplay : public PhysicalDisplay
{
public:
    VirtualDisplay(Hwc& hwc);
    virtual ~VirtualDisplay();

    virtual void updateOutputFormat( int32_t format );
    virtual bool updateMode( const Content::Display&  ) { return false; }
    virtual void onSet(const Content::Display& display, uint32_t zorder, int* pRetireFenceFd);

    // This returns the name of the display.
    const char* getName() const { return "VirtualDisplay"; }

protected:
    SinglePlaneDisplayCaps          mCaps;
    uint32_t                        mHandledLayerMask;
    Timeline                        mTimeline;              // Dummy timeline for pre-signalled fence.
    Timeline::NativeFence           mPreSignalledFence;     // Pre-signalled fence.
};

}   // hwc
}   // ufo
}   // intel
#endif // INTEL_HWC_VIRTUAL_DISPLAY_H
