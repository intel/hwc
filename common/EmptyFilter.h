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

#ifndef INTEL_UFO_HWC_EMPTYFILTER_H
#define INTEL_UFO_HWC_EMPTYFILTER_H

#include <utils/List.h>
#include "AbstractBufferManager.h"
#include "AbstractFilter.h"

namespace intel {
namespace ufo {
namespace hwc {

class EmptyFilter : public AbstractFilter
{
public:
    EmptyFilter();
    virtual ~EmptyFilter();

    const char* getName() const { return "EmptyFilter"; }
    const Content& onApply(const Content& ref);
    String8 dump();

protected:
    buffer_handle_t getBlankBuffer(uint32_t width, uint32_t height);
    void ageBlankBuffers();

    AbstractBufferManager& mBM;

    // Private reference to hold modified state
    Content mReference;

    // Helper struct to contain per display state
    struct DisplayState
    {
        DisplayState() : mbWasModified(false) {}
        bool mbWasModified;
        Layer mBlankLayer;
    };
    DisplayState mDisplayState[cMaxSupportedSFDisplays];

    // Helper struct to contain per buffer state
    struct BufferState
    {
        BufferState() : mFramesSinceLastUsed(0) {}
        sp<GraphicBuffer> mpBuffer;
        uint32_t mFramesSinceLastUsed;
    };
    List<BufferState> mBufferList;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_EMPTYFILTER_H
