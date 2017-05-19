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
#include "Layer.h"
#include "Log.h"

#include "EmptyFilter.h"
#include "FilterManager.h"
#include "AbstractBufferManager.h"

using namespace intel::ufo::hwc;

namespace intel {
namespace ufo {

namespace hwc {

const uint32_t cMaxBufferAge = 10; // Arbitrary guess for now.

// Factory instance
EmptyFilter gEmptyFilter;

EmptyFilter::EmptyFilter() :
    mBM( AbstractBufferManager::get() )
{
    // Add this filter to the filter list
    FilterManager::getInstance().add(*this, FilterPosition::Empty);
}

EmptyFilter::~EmptyFilter()
{
    // remove this filter
    FilterManager::getInstance().remove(*this);
}

const Content& EmptyFilter::onApply(const Content& ref)
{
    bool modified = false;
    for (uint32_t d = 0; d < ref.size(); d++)
    {
        const Content::Display& display = ref.getDisplay(d);
        DisplayState &dispState = mDisplayState[d];
        uint32_t layerCount = display.getNumEnabledLayers();
        if (display.isEnabled()
            && ( (0 == layerCount)
                // We need to signal a geometry change on going back to 'normal'
                || (!display.isGeometryChanged() && dispState.mbWasModified)))
        {
            if (!modified)
            {
                // Copy the content for modification
                mReference = ref;
                modified = true;
            }

            Content::Display& disp = mReference.editDisplay(d);
            Content::LayerStack& layerStack = disp.editLayerStack();
            bool modifiedLayers = false;
            if (0 == layerCount)
            {
                // Insert the blank layer
                dispState.mBlankLayer.onUpdateAll(getBlankBuffer(disp.getWidth(), disp.getHeight()));
                hwc_rect_t rect;
                rect.left = rect.top = 0;
                rect.right = disp.getWidth();
                rect.bottom = disp.getHeight();
                dispState.mBlankLayer.setSrc(rect);
                dispState.mBlankLayer.setDst(rect);
                dispState.mBlankLayer.onUpdateFlags();

                layerStack.resize(layerStack.size()+1);
                layerStack.setLayer(layerStack.size()-1, &dispState.mBlankLayer);
                layerStack.updateLayerFlags();
                modifiedLayers = true;
            }
            if (modifiedLayers != dispState.mbWasModified)
            {
                // Set Geometry changed if different from last frame
                layerStack.setGeometryChanged(true);
            }
            dispState.mbWasModified = modifiedLayers;
        }
        else
        {
            dispState.mbWasModified = false;
        }
    }

    ageBlankBuffers();

    if (!modified)
    {
        // No work to do so return the unmodified content.
        // Don't keep our (old) reference copy hanging around, we might not be
        // back for a while.
        if (mReference.size())
        {
            mReference.resize(0);
        }
        return ref;
    }

    return mReference;
}

String8 EmptyFilter::dump()
{
    String8 output;

    bool bBlanking = false;
    for (uint32_t d = 0; d < cMaxSupportedSFDisplays; ++d)
    {
        if (mDisplayState[d].mbWasModified)
        {
            if (!bBlanking)
                output.append("Blanking layers on displays:");
            bBlanking = true;
            output.append(" %d", d);
        }
    }
    if (!bBlanking)
    {
        output.append("No layers being provided");
    }

    return output;
}

buffer_handle_t EmptyFilter::getBlankBuffer(uint32_t width, uint32_t height)
{
    // Look for the biggest accomodating buffer.
    List<BufferState>::iterator i = mBufferList.begin(), m = mBufferList.end();
    for (; i != mBufferList.end(); ++i)
    {
        if (((uint32_t)i->mpBuffer->width >= width)
            && ((uint32_t)i->mpBuffer->height >= height))
        {
            if ( (m == mBufferList.end())
                    || (i->mpBuffer->width > m->mpBuffer->width)
                    || (i->mpBuffer->height > m->mpBuffer->height))
                m = i;
        }
    }
    // If we didn't find one then allocate one.
    if (m == mBufferList.end())
    {
        BufferState bs;
        bs.mpBuffer = mBM.createPurgedGraphicBuffer( "EMPTYFILTER", width, height,
                                                     INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT,
                                                     GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER );
        if (bs.mpBuffer == 0)
            return NULL;

        mBufferList.push_front(bs);
        m = mBufferList.begin();
    }

    // Mark as recently used and return the handle
    m->mFramesSinceLastUsed = 0;
    return m->mpBuffer->handle;
}

void EmptyFilter::ageBlankBuffers()
{
    // Age all buffers and destroy any that are too old.
    List<BufferState>::iterator i = mBufferList.begin();
    while (i != mBufferList.end())
    {
        i->mFramesSinceLastUsed++;
        if (i->mFramesSinceLastUsed > cMaxBufferAge)
            i = mBufferList.erase(i);
        else
            ++i;
    }
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

