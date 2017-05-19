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
#include "LayerBlanker.h"

namespace intel {
namespace ufo {
namespace hwc {

bool LayerBlanker::clear(unsigned display, bool bGeometryChange)
{
    if (mDisplayInfo.size() <= display)
    {
        if (mDisplayInfo.resize(display + 1) < (ssize_t)(display +1))
        {
            return false;
        }
    }
    DisplayInfo& displayInfo = mDisplayInfo.editItemAt(display);
    displayInfo.clear(!bGeometryChange);
    return true;
}

bool LayerBlanker::DisplayInfo::blank(unsigned layer)
{
    uint32_t index = mCount;
    bool bChanged = true;
    if (mLayerInfo.size() <= index)
    {
        mLayerInfo.resize(index+1);

        if (mLayerInfo.size() <= index)
        {
            return false;
        }
    }
    else
    {
        LayerInfo &info = mLayerInfo[index];
        if (info.mLayerIdx == layer)
        {
            bChanged = false;
        }
    }
    LayerInfo &info = mLayerInfo[index];
    info.mLayerIdx = layer;
    info.mbChanged = bChanged;
    if (bChanged)
    {
        mbGeometryChanged = true;
    }
    ++mCount;
    return true;
}

bool LayerBlanker::blank(unsigned display, unsigned layer)
{
    return mDisplayInfo.editItemAt(display).blank(layer);
}

const Content& LayerBlanker::update(const Content& ref)
{
    // Check for changes
    bool bHaveWork = false;
    for (DisplayInfo& di : mDisplayInfo)
    {
        if (di.count() || di.isGeometryChanged())
        {
            bHaveWork = true;
            break;
        }
    }

    // If we aren't changing anything from the source then just return it
    if (!bHaveWork)
    {
        return ref;
    }

    // Put the displayInfo in a sensible state.
    for (unsigned di = 0; di < mDisplayInfo.size(); ++di)
    {
        mDisplayInfo.editItemAt(di).prune();
    }
    if (mDisplayInfo.size() > ref.size())
    {
        mDisplayInfo.resize(ref.size());
    }

    // TODO: only update selectively
    mReference = ref;

    // Substitute any layers required.
    for (unsigned di = 0; di < mDisplayInfo.size(); ++di)
    {
        DisplayInfo& displayInfo = mDisplayInfo.editItemAt(di);

        for (uint32_t blankIdx = 0; blankIdx < displayInfo.count(); ++blankIdx)
        {
            LayerInfo &info = displayInfo.mLayerInfo[blankIdx];
            Content::Display& display = mReference.editDisplay(di);
            Content::LayerStack& layerStack = display.editLayerStack();
            // Copy the dst rect and close the fences on the old layer
            {
                const Layer& oldLayer = layerStack.getLayer(info.mLayerIdx);
                if (info.mbChanged)
                {
                    info.mLayer.onUpdateAll(mpBlankBuffer->handle);
                    // TODO: Try and crop the buffer if large enough rather than scaling.
                    info.mLayer.setDst(oldLayer.getDst());
                    info.mLayer.setVisibleRegions(oldLayer.getVisibleRegions());
                    info.mLayer.onUpdateFlags();
                    info.mbChanged = false;
                }
                oldLayer.closeAcquireFence();
                oldLayer.returnReleaseFence(-1);
            }
            // Substitute our new layer.
            layerStack.setLayer(info.mLayerIdx, &info.mLayer);
        }
    }

    // Update any layer flags & report any geometry changes.
    for (unsigned di = 0; di < mDisplayInfo.size(); ++di)
    {
        bool bUpdateLayerFlags = mDisplayInfo[di].count();
        bool bGeometryChanged = mDisplayInfo[di].isGeometryChanged();
        if (bUpdateLayerFlags || bGeometryChanged)
        {
            Content::Display& display = mReference.editDisplay(di);
            if (bUpdateLayerFlags)
            {
                display.editLayerStack().updateLayerFlags();
            }
            if (bGeometryChanged)
            {
                display.setGeometryChanged(true);
            }
        }
    }

    return mReference;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
