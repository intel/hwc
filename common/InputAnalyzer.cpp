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
#include "LogicalDisplay.h"
#include "LogicalDisplayManager.h"
#include "InputAnalyzer.h"
#include "Log.h"
#include "DisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

InputAnalyzer::Display::Display() :
    mpSrcDisplayContents(NULL),
    mbForceGeometry(false)
{
}

InputAnalyzer::Display::~Display()
{
}

void InputAnalyzer::Display::onPrepare(hwc_display_contents_1_t* pDisplayContents, Content::Display& ref,
                                       uint32_t hwcFrameIndex, nsecs_t now, LogicalDisplay* pHwDisplay)
{
    const uint32_t dmIndex = pHwDisplay ? pHwDisplay->getDisplayManagerIndex() : (uint32_t)INVALID_DISPLAY_ID;

    ALOGD_IF( CONTENT_DEBUG,
        "InputAnalyzer::Display::onPrepare hwc_display_contents_1_t:%p Frame Hwc:%d Timestamp:%" PRIu64 " DmIndex:%u",
        pDisplayContents, hwcFrameIndex, now, dmIndex);

    LOG_FATAL_IF( pHwDisplay && (dmIndex == INVALID_DISPLAY_ID),
                 "InputAnalyzer::Display::onPrepare Frame %d %s",
                 hwcFrameIndex, pHwDisplay->dump().string() );

    ref.setFrameIndex( hwcFrameIndex );
    ref.setFrameReceivedTime( now );

    if (pDisplayContents == NULL || pDisplayContents->numHwLayers < 1)
    {
        if (ref.isEnabled())
        {
            // This is an indication to disable the display.
            mLayers.clear();
            mpSrcDisplayContents = NULL;
            ref.disable();
            ref.setGeometryChanged(true);
        }
        return;
    }

    if ( ref.getDisplayManagerIndex() != dmIndex )
    {
        ALOGD_IF( CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare dmIndex change %u->%u", ref.getDisplayManagerIndex(), dmIndex );
        pDisplayContents->flags |= HWC_GEOMETRY_CHANGED;
        ref.setDisplayManagerIndex( dmIndex );
    }

    Content::LayerStack& layerstack = ref.editLayerStack();

    // Update the pointer always, just in cases the pDisplayContents was reallocated since last time
    ref.setRetireFenceReturn(&pDisplayContents->retireFenceFd);

    if (isForceGeometryChange())
    {
        // This is required across mode changes to perform a back to back hotplug
        // Without it, Surfaceflinger corrupts its state.
        pDisplayContents->flags |= HWC_GEOMETRY_CHANGED;
        setForceGeometryChange(false);
    }

    uint32_t displayFormat = INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT;
    if (pHwDisplay)
    {
        // Query refresh from current display mode and set it
        // TODO:
        //  Should probably cache the refresh rate for each display locally
        //  when SF queries the config. Then we can be sure we are aligned
        //  with the SF mode change in which case the geometry change flag
        //  is likely to be set already.
        const uint32_t refresh = pHwDisplay->getRefresh();
        if ( ref.getRefresh() != refresh )
        {
            ALOGD_IF( CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare refresh change %u->%u", ref.getRefresh(), refresh );
            pDisplayContents->flags |= HWC_GEOMETRY_CHANGED;
            ref.setRefresh( refresh );
        }
        const EDisplayType displayType = pHwDisplay->getDisplayType();
        if ( ref.getDisplayType() != displayType )
        {
            ALOGD_IF( CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare display type change %d->%d", ref.getDisplayType(), displayType );
            pDisplayContents->flags |= HWC_GEOMETRY_CHANGED;
            ref.setDisplayType( displayType );
        }
        displayFormat = pHwDisplay->getDefaultOutputFormat();
        ALOGD_IF(CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare format = %s (default output)", getHALFormatShortString( displayFormat ));
    }
    else
    {
        ref.setDisplayType(eDTUnspecified);
        ALOGD_IF(CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare format = %s (INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT)", getHALFormatShortString(INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT));
    }

    // Handle display geometry change requests
    if (pDisplayContents->flags & HWC_GEOMETRY_CHANGED)
    {
        ALOGD_IF(CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare Geometry Changed Display type: %d", ref.getDisplayType());

        // Reset force flag and indicate that a geometry change is in progress
        ref.setEnabled(true);
        ref.setGeometryChanged(true);

        // Keep a pointer to the source display
        mpSrcDisplayContents = pDisplayContents;

        if (mLayers.size() != pDisplayContents->numHwLayers-1)
        {
            mLayers.resize(pDisplayContents->numHwLayers-1);

            if (mLayers.size() < pDisplayContents->numHwLayers-1)
            {
                // Note, as long as we always use mLayers.size() as a counter, this error condition is relatively harmless
                ALOGE("Failed to allocate new layer list. Corruption may occur");
            }
        }

        if (layerstack.size() != mLayers.size())
        {
            layerstack.resize(mLayers.size());
        }

        for ( uint32_t ly = 0; ly < mLayers.size(); ly++ )
        {
            bool bForceOpaque = (ly == 0);
            Layer& layer = mLayers[ly];
            layerstack.setLayer(ly, &layer);
            layer.onUpdateAll(pDisplayContents->hwLayers[ly], now, bForceOpaque);
        }

        // use outbuf for virtual display only
        if (pDisplayContents->outbuf && (ref.getDisplayType() == eDTVirtual) )
        {
            // Make sure our output layer is refreshed with current state
            mOutputLayer = Layer(pDisplayContents->outbuf);
            mOutputLayer.setAcquireFenceReturn(&pDisplayContents->outbufAcquireFenceFd);
            ref.setOutputLayer(&mOutputLayer);
            displayFormat = mOutputLayer.getBufferFormat();
            ALOGD_IF(CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare setOutputLayer %s", mOutputLayer.dump().string());
        }
        else
        {
            // Reset the output layer pointer
            ref.setOutputLayer(NULL);
        }
    }
    else
    {
        ALOGD_IF(CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare Geometry the same");

        // If this assert fails, it means that the caller has changed the number of layers
        // in the layer list without setting geometry changed.
        // This can lead to SEGVs (particularly if the number of layers is reduced).
        ALOG_ASSERT(mLayers.size() == pDisplayContents->numHwLayers - 1);

        ALOG_ASSERT(ref.isEnabled());

        // Clear the geometry change flag
        ref.setGeometryChanged(false);

        // Check the src layers to see if any handles have changed.
        for ( uint32_t layer = 0; layer < mLayers.size(); layer++ )
        {
            // Trap changes in dynamic state for which we want to re-analyze composition results.
            // We have to propagate a geometry change downstream for these states.
            bool bForceGeometryChange = false;

            // Current state.
            const bool bOldEncrypted = mLayers[layer].isEncrypted();
            const uint32_t oldBufferModeFlags = mLayers[layer].getBufferModeFlags();
            const ECompressionType oldCompression = mLayers[layer].getBufferCompression();

            // Update frame state.
            mLayers[layer].onUpdateFrameState(pDisplayContents->hwLayers[layer], now);

            // Encryption status change.
            const bool bNewEncrypted = mLayers[layer].isEncrypted();
            if ( bOldEncrypted != bNewEncrypted )
            {
                ALOGD_IF( sbInternalBuild, "Layer encryption change %d->%d, forcing geometry change",
                    bOldEncrypted, bNewEncrypted );
                bForceGeometryChange = true;
            }

            // Buffer mode flag change.
            const uint32_t newBufferModeFlags = mLayers[layer].getBufferModeFlags();
            if ( oldBufferModeFlags != newBufferModeFlags )
            {
                ALOGD_IF( sbInternalBuild, "Layer buffer mode change 0x%x->0x%x, forcing geometry change",
                    oldBufferModeFlags, newBufferModeFlags );
                bForceGeometryChange = true;
            }

            // Compression status change.
            const ECompressionType newCompression = mLayers[layer].getBufferCompression();
            if ( oldCompression != newCompression )
            {
                ALOGD_IF( sbInternalBuild, "Layer compression change %d->%d, forcing geometry change",
                    static_cast<int>(oldCompression), static_cast<int>(newCompression) );
                bForceGeometryChange = true;
            }

            if ( bForceGeometryChange )
            {
                // Reflect layer state changes into layer flags.
                mLayers[layer].onUpdateFlags();

                // And set geometry change.
                ref.setGeometryChanged(true);
            }
        }

        // use outbuf for virtual display only
        if (pDisplayContents->outbuf && (ref.getDisplayType() == eDTVirtual) )
        {
            // Make sure our output layer is refreshed with current state
            mOutputLayer.onUpdateFrameState(pDisplayContents->outbuf);
            mOutputLayer.setAcquireFenceReturn(&pDisplayContents->outbufAcquireFenceFd);
            ref.setOutputLayer(&mOutputLayer);
            displayFormat = mOutputLayer.getBufferFormat();
            ALOGD_IF(CONTENT_DEBUG, "InputAnalyzer::Display::onPrepare setOutputLayer %s", mOutputLayer.dump().string());
        }
    }

    if (ref.getFormat() != displayFormat)
    {
        // Make sure a geometry change is issued if the display format changes
        ALOGD_IF(CONTENT_DEBUG, "Content::Display output format changed from %s to %s, forcing geometry change",
                                getHALFormatShortString(ref.getFormat()), getHALFormatShortString(displayFormat));

        ref.setGeometryChanged(true);
        ref.setFormat(displayFormat);;
    }

    // The RenderTarget is useful at this point as it defines the output resolution of the display
    // However, we have no valid render target buffer handle yet.
    const hwc_layer_1_t& rt = pDisplayContents->hwLayers[mLayers.size()];
    ref.setWidth(rt.displayFrame.right - rt.displayFrame.left);
    ref.setHeight(rt.displayFrame.bottom - rt.displayFrame.top);

    layerstack.updateLayerFlags();
}

InputAnalyzer::InputAnalyzer()
{
}

InputAnalyzer::~InputAnalyzer()
{
}

void InputAnalyzer::onPrepare(size_t numDisplays, hwc_display_contents_1_t** ppDisplayContents,
                              uint32_t hwcFrameIndex, nsecs_t now, LogicalDisplayManager& displayManager)
{
    if (mContent.size() != numDisplays)
    {
        if (numDisplays > cMaxSupportedSFDisplays)
        {
            numDisplays = cMaxSupportedSFDisplays;
        }
        else
        {
            // Clear any state for displays that have gone away.
            for ( size_t d = numDisplays; d < cMaxSupportedSFDisplays; ++d )
            {
                mDisplays[d].disable();
            }
        }

        mContent.resize(numDisplays);
    }

    for ( size_t d = 0; d < numDisplays; d++ )
    {
        LogicalDisplay* pDisplay = displayManager.getSurfaceFlingerDisplay(d);
        mDisplays[d].onPrepare( ppDisplayContents[d],
                                mContent.editDisplay(d),
                                hwcFrameIndex,
                                now,
                                pDisplay );
    }

    Log::add( mContent, "InputAnalyzer::onPrepare SF" );

    ALOGD_IF(CONTENT_DEBUG, "%s", dump("InputAnalyzer::onPrepare this").string());
    ALOGD_IF(CONTENT_DEBUG, "%s", mContent.dump("InputAnalyzer::onPrepare ref").string());
}

void InputAnalyzer::forceGeometryChange()
{
    for (Display& d : mDisplays)
    {
        d.setForceGeometryChange(true);
    }
}

String8 InputAnalyzer::Display::dump(const char* pIdentifier) const
{
    if (!sbInternalBuild || mpSrcDisplayContents == NULL)
        return String8();

    // Debug - report to logcat the status of everything that we are being asked to set.
    String8 output = String8::format("%s retireFenceFd:%d outbuf:%p outbufAcquireFenceFd:%d flags:%x numHwLayers:%zd\n",
        pIdentifier, mpSrcDisplayContents->retireFenceFd, mpSrcDisplayContents->outbuf,
        mpSrcDisplayContents->outbufAcquireFenceFd, mpSrcDisplayContents->flags, mpSrcDisplayContents->numHwLayers);

    for (uint32_t ly = 0; ly < mLayers.size(); ly++)
    {
        output += mLayers[ly].dump(String8::format("%d", ly)) + "\n";
    }

    return output;
}

String8 InputAnalyzer::dump(const char* pIdentifier) const
{
    if (!sbInternalBuild)
        return String8();

    String8 output;
    for (size_t d = 0; d < cMaxSupportedSFDisplays; d++)
    {
        output += mDisplays[d].dump(String8::format("%s Display:%zd", pIdentifier, d).string());
    }

    return output;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
