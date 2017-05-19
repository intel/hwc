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

#ifndef INTEL_UFO_HWC_DEBUGFILTER_H
#define INTEL_UFO_HWC_DEBUGFILTER_H

#include "AbstractFilter.h"
#include "Singleton.h"

namespace intel {
namespace ufo {
namespace hwc {

class DebugFilter : public AbstractFilter, Singleton<DebugFilter>
{
public:
    static DebugFilter& get() { return getInstance(); }

    DebugFilter();
    virtual ~DebugFilter();

    // This returns the name of the filter.
    const char* getName() const { return "DebugFilter"; }
    const Content& onApply(const Content& ref);

    String8 dump();

    // Public API
    void enableDisplay(uint32_t d);
    void disableDisplay(uint32_t d, bool bBlank);
    void maskLayer(uint32_t d, uint32_t layer, bool bHide);
    void dumpFrames(uint32_t d, int32_t count);

    // Dump hardware output.
    void dumpHardwareFrame(uint32_t d, const Content::Display& out);

private:
    friend class Singleton<DebugFilter>;

    // Private reference to hold modified state
    Content    mReference;

    // Helper class to contain per display debug state
    class DisplayDebug
    {
    public:
        DisplayDebug() : mMask(0), mDumpFrames(0), mbGeometryChange(false), mbDisableDisplay(false), mbBlankDisplay(false) { }
        uint32_t    mMask;              // Mask of layers to disable
        int32_t     mDumpFrames;        // Dump next N frames to disk (-1 => continuous)
        uint32_t    mDumpFrameIdx;      // Incrementing count of dumped frames.
        uint32_t    mDumpHardwareFrame; // The frame index for next hardware output dump.
        bool        mbGeometryChange:1; // Force a geometry change at next frame
        bool        mbDisableDisplay:1; // Disable the display
        bool        mbBlankDisplay:1;   // Blank the display
    };

    // Handle up to 32 layers
    Vector<DisplayDebug> mDebugDisplay;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DEBUGFILTER_H
