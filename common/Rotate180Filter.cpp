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

#include "Common.h"
#include "AbstractFilter.h"
#include "Layer.h"
#include "FilterManager.h"
#include <vector>

namespace intel {
namespace ufo {
namespace hwc {

class Rotate180Filter : public AbstractFilter
{
public:
    Rotate180Filter();
    virtual ~Rotate180Filter();

    const char* getName() const { return "Rotate180Filter"; }
    const Content& onApply(const Content& ref);
    String8 dump();
private:
    Option              mOptionRotate180;
    Content             mContent;
    std::vector<Layer>  mLayers[cMaxSupportedSFDisplays];
};

// Factory instance
Rotate180Filter gRotate180Filter;

Rotate180Filter::Rotate180Filter() :
    mOptionRotate180( "rotate180", 0, false )
{
    // If the status control actually exists
    if (mOptionRotate180 != 0) {
        // Add this filter to the filter list
        FilterManager::getInstance().add(*this, FilterPosition::Rotate180);
    }
}

Rotate180Filter::~Rotate180Filter()
{
    // remove this filter
    FilterManager::getInstance().remove(*this);
}

const Content& Rotate180Filter::onApply(const Content& ref)
{
    mContent = ref;
    for (uint32_t d = 0; d < ref.size() && d < cMaxSupportedSFDisplays; d++)
    {
        Content::Display& display = mContent.editDisplay(d);
        if ( !display.isEnabled() )
            continue;

        if (mOptionRotate180 & (1<<d))
        {
            Content::LayerStack& layerStack = display.editLayerStack();
            mLayers[d].resize(layerStack.size());
            for (uint32_t ly = 0; ly < layerStack.size(); ++ly)
            {
                Layer& layer = mLayers[d][ly];
                const Layer& src = layerStack.getLayer(ly);
                layer = src;

                // As the transform is a bitfield, we can simply invert some bits to rotate the bitmap by 180
                uint32_t t = uint32_t(layer.getTransform());
                t ^= uint32_t(ETransform::ROT_180);
                layer.setTransform(ETransform(t));

                // We also need to rotate the coordinates, flip all 4 across X and Y
                hwc_rect_t r;
                r.left = display.getWidth() - layer.getDst().right;
                r.right = display.getWidth() - layer.getDst().left;
                r.top = display.getHeight() - layer.getDst().bottom;
                r.bottom = display.getHeight() - layer.getDst().top;
                layer.editDst() = r;

                // Update the layerstack with our pointer
                layerStack.setLayer(ly, &layer);
            }
        }
    }

    return mContent;
}

String8 Rotate180Filter::dump()
{
    String8 output;

    if (mOptionRotate180)
        output.appendFormat("Rotating %x", mOptionRotate180.get());
    else
        output.append("No Rotation");
    return output;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

