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

#ifndef INTEL_UFO_HWC_LAYERBLANKER_H
#define INTEL_UFO_HWC_LAYERBLANKER_H

#include "Layer.h"
#include "Content.h"
#include <utils/RefBase.h>
#include <utils/Vector.h>
#include <ui/GraphicBuffer.h>

namespace intel {
namespace ufo {
namespace hwc {

// Helper class for tracking and managing the state for replacing layers
// with the contents of a buffer (e.g. black, icon, image, etc.).

class LayerBlanker
{
public:
    // Clear the list of layers to replace on a display.
    // Optional if the list has not changed EXCEPT on a geometry change.
    bool clear(unsigned display, bool bGeometryChange);

    // Have the specified layer on a display replaced.
    // Should be called between 'clear' and 'update'.
    bool blank(unsigned display, unsigned layer);

    // Specify the buffer to replace layers with.
    void setBlankingBuffer(sp<GraphicBuffer> buffer) { mpBlankBuffer = buffer; }

    // Update and return the modified content ref.  Should be called every frame.
    const Content& update(const Content& ref);

private:

    // Per-layer tracking information (including the replacement layer.
    struct LayerInfo {
        static const unsigned INVALID_INDEX = UINT_MAX;
        LayerInfo() { clear(); }
        void clear() { mLayerIdx = INVALID_INDEX; mbChanged = true; }
        Layer    mLayer;
        unsigned mLayerIdx;
        bool     mbChanged;
    };

    // Per-display tracking information.
    // Includes a list of layers that will be replaced.
    struct DisplayInfo {
        DisplayInfo() { clear(); }
        void clear(bool partial = false) { if (!partial) { mLayerInfo.clear(); } mCount = 0; mbGeometryChanged = false; }

        unsigned count() const { return mCount; }
        bool isGeometryChanged() const { return mbGeometryChanged || (mLayerInfo.size() != mCount); }
        void prune() { if (mLayerInfo.size() > mCount) { mbGeometryChanged = true; mLayerInfo.resize(mCount); } }
        bool blank(unsigned layer);
        std::vector<LayerInfo> mLayerInfo;
    private:
        unsigned mCount;
        bool mbGeometryChanged;
    };

    sp<GraphicBuffer>   mpBlankBuffer;  // The graphics buffer to replace the layers with.
    Vector<DisplayInfo> mDisplayInfo;   // Per display data on what is to be replaced.
    Content             mReference;     // Private reference to hold modified state.
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel


#endif // INTEL_UFO_HWC_LAYERBLANKER_H
