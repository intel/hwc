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

#ifndef INTEL_UFO_HWC_VISIBLERECTFILTER_H
#define INTEL_UFO_HWC_VISIBLERECTFILTER_H

#include <utils/List.h>
#include <ui/GraphicBuffer.h>
#include "AbstractFilter.h"

namespace intel {
namespace ufo {
namespace hwc {

class VisibleRectFilter : public AbstractFilter
{
public:
    VisibleRectFilter();
    virtual ~VisibleRectFilter();

    const char* getName() const { return "VisibleRectFilter"; }
    const Content& onApply(const Content& ref);
    String8 dump();
protected:
    bool displayStatePrepare( uint32_t d, uint32_t layerCount);

    // Figure out the smallest box that can cover all visible rects of this layer
    hwc_rect_t getVisibleRegionBoundingBox(const Layer& layer);

    // Private reference to hold modified state
    Content mReference;

    // Helper struct to contain per display state
    struct DisplayState
    {
        DisplayState() : mbWasModified(false) {}
        bool mbWasModified;
        std::vector<Layer>  mLayers;   // layer list for this display
    };
    DisplayState mDisplayState[cMaxSupportedSFDisplays];
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_VISIBLERECTFILTER_H
