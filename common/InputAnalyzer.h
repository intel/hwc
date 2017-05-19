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

#ifndef INTEL_UFO_HWC_CONTENT_OWNER_H
#define INTEL_UFO_HWC_CONTENT_OWNER_H

#include "Content.h"
#include "Layer.h"

namespace intel {
namespace ufo {
namespace hwc {

class LogicalDisplay;
class LogicalDisplayManager;

// This class analyzes the input layer state and creates internal layer state objects that
// represent that state..
class InputAnalyzer
{
public:
    InputAnalyzer();
    ~InputAnalyzer();

    class Display
    {
    public:
        Display();
        ~Display();

        bool                        isForceGeometryChange()  const      { return mbForceGeometry; }
        void                        setForceGeometryChange(bool force)  { mbForceGeometry = force; }

        // Initialise on an onPrepare call
        void                        onPrepare(hwc_display_contents_1_t* pDisplayContents, Content::Display& ref, uint32_t hwcFrameIndex, nsecs_t timestamp, LogicalDisplay* pHwDisplay);

        // Clear the state to disabled
        void                        disable() { mLayers.clear(); mpSrcDisplayContents = NULL; setForceGeometryChange(true); }

        // Dump display to logcat
        String8                     dump(const char* pIdentifier = "InputAnalyzer") const;

    private:
        std::vector<Layer>          mLayers;
        Layer                       mOutputLayer;                       // Space to store any output layer passed into the HWC.
        hwc_display_contents_1_t*   mpSrcDisplayContents;               // Pointer to original source display

        bool                        mbForceGeometry:1;                  // Geometry changed on this frame
    };


    const Content&              getContent() const                  { return mContent; }

    void                        onPrepare(size_t numDisplays, hwc_display_contents_1_t** ppDisplayContents, uint32_t hwcFrameIndex, nsecs_t timestamp, LogicalDisplayManager& displayManager);
    void                        forceGeometryChange();

    // Dump the content to a String8 object
    String8                     dump(const char* pIdentifier = "Content") const;

private:
    Content                     mContent;                                  // Lightweight array of pointers to the layers managed by this class
    Display                     mDisplays[cMaxSupportedSFDisplays];        // All the displays in the class
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_CONTENT_OWNER_H
