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
#include "Transform.h"

#include "VisibleRectFilter.h"
#include "FilterManager.h"

using namespace intel::ufo::hwc;

namespace intel {
namespace ufo {

namespace hwc {

// Factory instance
VisibleRectFilter gVisibleRectFilter;

VisibleRectFilter::VisibleRectFilter()
{
    // Add this filter to the filter list
    FilterManager::getInstance().add(*this, FilterPosition::VisibleRect);
}

VisibleRectFilter::~VisibleRectFilter()
{
    // remove this filter
    FilterManager::getInstance().remove(*this);
}

const Content& VisibleRectFilter::onApply(const Content& ref)
{
    bool bModified = false;
    mReference = ref;
    for (uint32_t d = 0; d < mReference.size(); d++)
    {
        Content::Display& display = mReference.editDisplay(d);
        Content::LayerStack& layerStack = display.editLayerStack();
        struct DisplayState& displayState = mDisplayState[d];

        if ( display.isEnabled() )
        {
            displayStatePrepare( d, layerStack.size() );
            for ( uint32_t ly = 0; ly < layerStack.size(); )
            {
                const Layer& layer = layerStack.getLayer(ly);
                bool isVisibleLayer = true;

                // Get visible rect that can cover all visible rects of this layer
                const hwc_rect_t visibleRect = getVisibleRegionBoundingBox(layer);

                // Dst rect was the same with visible rect, early skip
                if( layer.getDst() == visibleRect )
                {
                    ly++;
                    continue;
                }
                ALOGD_IF ( VISIBLERECTFILTER_DEBUG, "\nBegin to clip layer in D%d: \n%s", d, layer.dump("").string());

                // Copy layer
                displayState.mLayers[ly] = layer;
                displayState.mLayers[ly].onUpdateFrameState(layer);

                // Clip src/dst with visible regions
                //    case 1: visible region is zero, remove this layer
                //    case 2: visible region is non-zero, clip dst and src rect to match visible region
                isVisibleLayer = clipLayerToDestRect( &displayState.mLayers[ly], visibleRect );
                ALOGD_IF ( VISIBLERECTFILTER_DEBUG, "Clipped layer to visible region: \n%s", displayState.mLayers[ly].dump("").string());
                if( isVisibleLayer )
                {
                   layerStack.setLayer(ly, &displayState.mLayers[ly]);
                   ly++;
                   ALOGD_IF ( VISIBLERECTFILTER_DEBUG,"Clip layer to visible region.");
                }
                else
                {
                    // Zero visible region layer
                    // If this layer is not removed, the src/dst rect can be set zero rect, and it would not sent to composer
                    // but to waste some CPU resources, so it is better to remove this layer
                    layerStack.removeLayer( ly );
                    ALOGD_IF ( VISIBLERECTFILTER_DEBUG,"Remove zero visible region layer.");
                 }
                 layerStack.updateLayerFlags();
                 bModified = true;
            }
        }

    }

    if ( bModified == false )
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

String8 VisibleRectFilter::dump()
{
    String8 output("VisibleRectFilter: ");

    return output;
}

bool VisibleRectFilter::displayStatePrepare( uint32_t d, uint32_t layerCount)
{
    // resize mDisplayState.mLayers to match with content
    struct DisplayState& displayState = mDisplayState[d];
    if (displayState.mLayers.size() != layerCount)
    {
        displayState.mLayers.resize(layerCount);
        if (displayState.mLayers.size() < layerCount)
        {
            ALOGE("Error in VisibleRectFilter: Failed to allocate new layer list, skip this filter!");
            return false;
        }
    }
    return true;
}

// Figure out the smallest box that can cover all visible rects of this layer
hwc_rect_t VisibleRectFilter::getVisibleRegionBoundingBox(const Layer& layer)
{
    const Vector<hwc_rect_t>& visibleRegions = layer.getVisibleRegions();
    hwc_rect_t visibleRect = visibleRegions[0];
    for (uint32_t r = 1; r < visibleRegions.size(); r++)
    {
         const hwc_rect_t& rect = visibleRegions[r];
         visibleRect.left   = min( visibleRect.left,   rect.left );
         visibleRect.top    = min( visibleRect.top,    rect.top );
         visibleRect.right  = max( visibleRect.right,  rect.right );
         visibleRect.bottom = max( visibleRect.bottom, rect.bottom );
    }
    return visibleRect;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

