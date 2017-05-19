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

#include <math.h>
#include "Common.h"
#include "Layer.h"
#include "Log.h"
#include "AbstractDisplay.h"
#include "DisplayCaps.h"
#include "HwcServiceApi.h"
#include "Transform.h"
#include "GlobalScalingFilter.h"

using namespace intel::ufo::hwc;

namespace intel {
namespace ufo {
namespace hwc {

GlobalScalingFilter::GlobalScalingFilter(PhysicalDisplayManager& pdm) :
    mPhysicalDisplayManager(pdm),
    mOptionGlobalScaling    ("globalscaling", GLOBAL_SCALING_OPTION_ENABLE
                                            | GLOBAL_SCALING_OPTION_KEEP_ENABLED_ALWAYS
                                            | GLOBAL_SCALING_OPTION_RESTRICT_MATCHING_AR),
    mOptionGlobalScalingMin ("globalscalemin", 66),     // 1080p downscale to 720p
    mOptionGlobalScalingMax ("globalscalemax", 150),    // 720p upscale to 1080p
    mOptionGlobalScalingEdge("globalscaleedje", 1),
    mOptionGlobalScalingVideoOnly("gsvideoonly", 1) // only enabling global scaling HW when we have full height or width single plane video
{
}

GlobalScalingFilter::~GlobalScalingFilter()
{

}

const Content& GlobalScalingFilter::onApply(const Content& ref)
{
    bool contentChanged = false;

    // TODO:
    //  A geometryChange will be generated if the filter transitions on/off or other state changes.
    //  But, it would be better to avoid costly processing (and therefore propagating state changes)
    //  until a frame is received for which the geometry change is already raised.

    // Copy the content for modification
    mContent = ref;

    for (uint32_t d = 0; d < mContent.size(); d++)
    {
        Content::Display& displayOut = mContent.editDisplay(d);

        bool currentDisplayChanged = false;

        // check if display is valid
        if (!displayOut.isEnabled())
        {
            // skip handling this invalid display
            continue;
        }

        const Content::Display& refDisplayOut = ref.getDisplay(d);

        const uint32_t phyIndex = mPhysicalDisplayManager.remap( refDisplayOut.getDisplayManagerIndex() );
        AbstractPhysicalDisplay* pPhys = mPhysicalDisplayManager.getPhysicalDisplay( phyIndex );
        if ( pPhys == NULL )
        {
            // skip handling unmapped display
            continue;
        }

        struct DisplayInfo& displayInfo = mDisplayInfo[phyIndex];

        // apply overscan + proxy display first
        currentDisplayChanged = applyAllScalings(displayInfo, *pPhys, displayOut);

        // check if there is global scaling and if we can use HW to do the global scaling
        if (enableGlobalScalingHW(displayInfo, *pPhys, displayOut))
        {
            currentDisplayChanged = true;
            displayInfo.mbGlobalScalingHwEnabled = true;
        }
        else
        {
            releaseGlobalScalingHW(*pPhys);
            displayInfo.mbGlobalScalingHwEnabled = false;
        }

        if (currentDisplayChanged)
        {
            // check if newly enabling scaling.
            if ( !displayInfo.mbGlobalScalingEnabled )
            {
                displayInfo.mbGlobalScalingEnabled = true;
                displayOut.setGeometryChanged( true );
            }
            // check if settings changed, trigger geometry change.
            if ( handleDisplaySettingsChanged( displayInfo ) )
            {
                displayOut.setGeometryChanged( true );
            }
        }
        else
        {
            // check if newly disabling scaling.
            if ( displayInfo.mbGlobalScalingEnabled )
            {
                displayInfo.mbGlobalScalingEnabled = false;
                // generate a geometry change if necessary.
                if ( !refDisplayOut.isGeometryChanged() )
                {
                    currentDisplayChanged = true;
                    displayOut.setGeometryChanged( true );
                }
            }
            // check if settings changed, trigger geometry change.
            if (handleDisplaySettingsChanged(displayInfo))
            {
                // generate a geometry change if necessary.
                if ( !refDisplayOut.isGeometryChanged() )
                {
                    currentDisplayChanged = true;
                    displayOut.setGeometryChanged( true );
                }
            }
        }

        // Any a display was changed then content should be changed
        contentChanged |= currentDisplayChanged;
    }

    if (contentChanged)
    {
        return mContent;
    }
    else
    {
        return ref;
    }

}

String8 GlobalScalingFilter::dump()
{
    String8 output;
    // TODO: add more dump info
    return output;
}

int GlobalScalingFilter::setActualOutputResolution(uint32_t phyIndex, uint32_t outputWidth, uint32_t outputHeight)
{

    // check if the display index is valid
    if (phyIndex >= cMaxSupportedPhysicalDisplays)
    {
        return BAD_VALUE;
    }
    Mutex::Autolock _l(mLock);

    struct DisplayInfo& displayInfo = mDisplayInfo[phyIndex];
    if ( displayInfo.mActualOutputWidth != outputWidth || displayInfo.mActualOutputHeight != outputHeight )
    {
        displayInfo.mbSettingsChanged = true;
    }
    displayInfo.mActualOutputWidth = outputWidth;
    displayInfo.mActualOutputHeight = outputHeight;
    // disable if any of the sizes zero - which is the way to switch it off
    if((outputWidth == 0) || (outputHeight == 0))
    {
        displayInfo.mbSetActualOutputResolution = false;
    }
    else
    {
        displayInfo.mbSetActualOutputResolution = true;
    }

    return OK;
}

bool GlobalScalingFilter::getActualOutputResolution(uint32_t phyIndex, uint32_t& outputWidth, uint32_t& outputHeight)
{
    if (phyIndex >= cMaxSupportedPhysicalDisplays)
    {
        return false;
    }

    Mutex::Autolock _l(mLock);

    if (mDisplayInfo[phyIndex].mbSetActualOutputResolution)
    {
        outputWidth = mDisplayInfo[phyIndex].mActualOutputWidth;
        outputHeight = mDisplayInfo[phyIndex].mActualOutputHeight;
    }

    return mDisplayInfo[phyIndex].mbSetActualOutputResolution;
}

int GlobalScalingFilter::setUserOverscan(uint32_t phyIndex, int32_t xOverscan, int32_t yOverscan)
{
    ALOG_ASSERT( xOverscan <= HWCS_MAX_OVERSCAN || xOverscan >= -HWCS_MAX_OVERSCAN);
    ALOG_ASSERT( yOverscan <= HWCS_MAX_OVERSCAN || yOverscan >= -HWCS_MAX_OVERSCAN);

    if (phyIndex >= cMaxSupportedPhysicalDisplays)
    {
        return BAD_VALUE;
    }

    Mutex::Autolock _l(mLock);

    struct DisplayInfo& displayInfo = mDisplayInfo[phyIndex];
    if ( displayInfo.mUserOverscanX != xOverscan || displayInfo.mUserOverscanY != yOverscan )
    {
        displayInfo.mbSettingsChanged = true;
    }

    displayInfo.mUserOverscanX = xOverscan;
    displayInfo.mUserOverscanY = yOverscan;
    if (xOverscan != 0 || yOverscan !=0)
    {
        displayInfo.mbHaveUserOverscan = true;
    }
    else
    {
        displayInfo.mbHaveUserOverscan = false;
    }

    return OK;
}

bool GlobalScalingFilter::getUserOverscan(uint32_t phyIndex, int32_t& xOverscan, int32_t& yOverscan)
{
    if (phyIndex >= cMaxSupportedPhysicalDisplays)
    {
        return false;
    }

    Mutex::Autolock _l(mLock);

    if (mDisplayInfo[phyIndex].mbHaveUserOverscan)
    {
        xOverscan = mDisplayInfo[phyIndex].mUserOverscanX;
        yOverscan = mDisplayInfo[phyIndex].mUserOverscanY;
    }

    return mDisplayInfo[phyIndex].mbHaveUserOverscan;
}

// set the user scaling mode
int GlobalScalingFilter::setUserScalingMode(uint32_t phyIndex, EHwcsScalingMode scalingMode)
{
    ALOG_ASSERT( scalingMode < HWCS_SCALE_MAX_ENUM );
    if (phyIndex >= cMaxSupportedPhysicalDisplays)
    {
        return BAD_VALUE;
    }

    Mutex::Autolock _l(mLock);

    struct DisplayInfo& displayInfo = mDisplayInfo[phyIndex];
    if (displayInfo.mUserScalingMode != scalingMode)
    {
        displayInfo.mbSettingsChanged = true;
    }

    displayInfo.mUserScalingMode = scalingMode;
    displayInfo.mbHaveUserScalingMode = true;
    ALOGD_IF( GLOBAL_SCALING_DEBUG, "setUserScalingMode: phyIndex:%d, scalingMode:%d.", phyIndex, scalingMode);

    return OK;
}

bool GlobalScalingFilter::getUserScalingMode(uint32_t phyIndex, EHwcsScalingMode& scalingMode)
{
    if (phyIndex >= cMaxSupportedPhysicalDisplays)
    {
        return false;
    }

    Mutex::Autolock _l(mLock);

    if (!mDisplayInfo[phyIndex].mbHaveUserScalingMode)
    {
        return false;
    }
    scalingMode = mDisplayInfo[phyIndex].mUserScalingMode;
    ALOGD_IF( GLOBAL_SCALING_DEBUG, "getUserScalingMode: phyIndex:%d, scalingMode:%d.", phyIndex, scalingMode);

    return true;
}

EHwcsScalingMode GlobalScalingFilter::getScalingMode(DisplayInfo& displayInfo)
{
    Mutex::Autolock _l(mLock);

    if (displayInfo.mbHaveUserScalingMode)
    {
        return displayInfo.mUserScalingMode;
    }

    // user does not set Scaling mode, use default Scaling mode
    // TODO: read default scaling mode from Option
    return HWCS_SCALE_FIT;
}

bool GlobalScalingFilter::isDisplaySettingsChanged(uint32_t phyIndex)
{
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays );

    Mutex::Autolock _l(mLock);
    return mDisplayInfo[phyIndex].mbSettingsChanged;
}

bool GlobalScalingFilter::handleDisplaySettingsChanged(DisplayInfo& displayInfo)
{
    Mutex::Autolock _l(mLock);
    if (displayInfo.mbSettingsChanged)
    {
        displayInfo.mbSettingsChanged = false;
        return true;
    }

    return false;
}


bool GlobalScalingFilter::applyAllScalings(DisplayInfo& displayInfo, AbstractPhysicalDisplay& phys, Content::Display& contentDisplay )
{
    // original size of the display (proxy display's size)
    uint32_t dispW = 0;
    uint32_t dispH = 0;
    // output size of the display.(actual output size of the display)
    uint32_t outputW = 0;
    uint32_t outputH = 0;
    // final effective frame rect on the actual display output
    uint32_t finalFrameW = 0;
    uint32_t finalFrameH = 0;
    int32_t finalFrameX = 0;
    int32_t finalFrameY = 0;

    uint32_t phyIndex = phys.getDisplayManagerIndex();

    // get display size
    dispW= contentDisplay.getWidth();
    dispH= contentDisplay.getHeight();
    // is have effective overscan?
    bool bHaveUserOverscan = false;
    int32_t userOverscanX = 0;
    int32_t userOverscanY = 0;
    bHaveUserOverscan = getUserOverscan( phyIndex, userOverscanX, userOverscanY );
    if (bHaveUserOverscan &&
        (userOverscanX == 0) && (userOverscanY == 0))
    {
        // user set overscan but all 0;
        bHaveUserOverscan = false;
    }

    // is proxy display and have different output size
    bool bHaveDifferentOutputResolution = false;
    bHaveDifferentOutputResolution = getActualOutputResolution( phyIndex, outputW, outputH );
    if (bHaveDifferentOutputResolution &&
        (dispW == outputW) && (dispH == outputH))
    {
        // user set actual  output size, but same as display size;
        bHaveDifferentOutputResolution = false;
    }

    // if on external display and scaling mode is different from "keep aspect ratio mode"(SF default).
    // currently we only support strech to full screen
    bool bHaveDifferentScalingMode = false;
    EHwcsScalingMode scalingMode = getScalingMode(displayInfo);
    if ((phys.getDisplayType() == eDTExternal) &&
        (scalingMode == HWCS_SCALE_STRETCH))
    {
        bHaveDifferentScalingMode = true;
    }

    if (!bHaveUserOverscan && !bHaveDifferentOutputResolution && !bHaveDifferentScalingMode)
    {
        // no overscan or no proxy display or no need to apply scaling mode, do nothing
        return false;
    }

    // If scale/overscan adjustments are required, then the following
    // variables will describe the adjustments.
    float overscanFactorW = 1.0f; // overscan percentage X * 2
    float overscanFactorH = 1.0f; // overscan percentage Y * 2
    float outputScalingFactorW = 1.0f; // outputW/dispW
    float outputScalingFactorH = 1.0f; // outputH/dispH
    float totalScalingFactorW = 1.0f;  //  overscanFactorW * outputScalingFactorW
    float totalScalingFactorH = 1.0f;  //  overscanFactorH * outputScalingFactorH

    if (bHaveUserOverscan)
    {
        //  [+/-HWCS_MAX_OVERSCAN]
        // which represents a range of +/-IDisplayOverscanControl::RANGE % pixels.
        const float maxOverscanPct = 0.01f * HWCS_OVERSCAN_RANGE;
        const float adjX = maxOverscanPct * (float)userOverscanX / (float)HWCS_MAX_OVERSCAN;
        const float adjY = maxOverscanPct * (float)userOverscanY / (float)HWCS_MAX_OVERSCAN;

        // Always adjust as a function of frame size so adjustments
        // are relative to displayed image and AR is maintained (for overscanX==overscanY).
        overscanFactorW = 1.0f - adjX * 2.0;
        overscanFactorH = 1.0f - adjY * 2.0;

        ALOGD_IF( GLOBAL_SCALING_DEBUG,
            "adjX:%f, adjY:%f, [MAX %d, RANGE%u%% disp %dx%d], overscanFactorW:%f,overscanFactorH:%f",
            adjX, adjX,
            HWCS_MAX_OVERSCAN,
            HWCS_OVERSCAN_RANGE,
            dispW, dispH, overscanFactorW,overscanFactorH );
    }

    // apply scaling mode on external display, only support strech to full screen now.
    float scalingModeFactorW = 1.0f;
    float scalingModeFactorH = 1.0f;
    if (bHaveDifferentScalingMode)
    {
        calculateScalingFactorFromScalingMode(displayInfo, contentDisplay, scalingModeFactorW, scalingModeFactorH);
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
            "calculateScalingFactorFromScalingMode: phyIndex:%d, dispW:%d, dispH:%d, scalingModeFactorW:%f, scalingModeFactorW:%f.",
            phyIndex, dispW, dispH, scalingModeFactorW, scalingModeFactorH);
    }

    // have different output resolution, need to scale the frame to actual output size
    if (bHaveDifferentOutputResolution)
    {
        calculateOutputScalingFactor(scalingMode, dispW, dispH, outputW, outputH, outputScalingFactorW,outputScalingFactorH);
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
            "calculateOutputScalingFactor: phyIndex:%d,dispW:%d, dispH:%d, outputW:%d, outputH:%d,OutputScalingFactorW:%f, OutputScalingFactorW:%f.",
            phyIndex, dispW, dispH, outputW, outputH,
            outputScalingFactorW, outputScalingFactorH);
    }
    else
    {
        outputW = dispW;
        outputH = dispH;
    }

    // calculate the final scaling factor
    totalScalingFactorW = overscanFactorW * outputScalingFactorW * scalingModeFactorW;
    totalScalingFactorH = overscanFactorH * outputScalingFactorH * scalingModeFactorH;
    ALOG_ASSERT( (totalScalingFactorW != 0) && (totalScalingFactorH != 0) );

    // if there is no scaling and no Output Resolution change, skip the transform
    if ((totalScalingFactorW == 1.0f) && (totalScalingFactorH == 1.0f) && !bHaveDifferentOutputResolution)
    {
        ALOGD_IF(GLOBAL_SCALING_DEBUG,
            "calculateOutputScalingFactor: no scaling on phyIndex:%d, skip the transform", phyIndex);
        return false;
    }

    // calculate the transform from orignal dispay frame to the region in actual output region
    // the frame should always be centered at display
    finalFrameW = totalScalingFactorW * dispW + 0.5f;
    finalFrameH = totalScalingFactorH * dispH + 0.5f;
    finalFrameX = ((int)outputW - (int)finalFrameW) / 2.0 + 0.5f;
    finalFrameY = ((int)outputH - (int)finalFrameH) / 2.0 + 0.5f;

    ALOGD_IF( GLOBAL_SCALING_DEBUG,
        "final transform:phyIndex:%d, totalScalingFactorW:%f,totalScalingFactorH:%f, finalFrameW:%d, finalFrameH:%d, finalFrameX:%d, finalFrameY:%d.",
        phyIndex, totalScalingFactorW, totalScalingFactorH,
        finalFrameW, finalFrameH, finalFrameX, finalFrameY);

    // adjust layer stack according to the transform of the whole frame
    Content::LayerStack& layerStack = contentDisplay.editLayerStack();
    // resize mDisplayInfo.mLayers to match with content
    uint32_t layerCount = layerStack.size();
    if (displayInfo.mLayers.size() != layerCount)
    {
        displayInfo.mLayers.resize(layerCount);

        if (displayInfo.mLayers.size() < layerCount)
        {
            // Note, as long as we always use mLayers.size() as a counter, this error condition is relatively harmless
            ALOGE("Failed to allocate new layer list. Corruption may occur");
            return false;
        }
    }

    // adjust each layer according to the total transform
    // dst.x = finalFrameX + dst.x * totalScalingFactorW;
    // dst.y = finalFrameY + dst.y * totalScalingFactorH;
    for (uint32_t i = 0; i < layerCount; i++)
    {
        // make a copy from content's layerStack
        displayInfo.mLayers[i] = layerStack.getLayer(i);
        displayInfo.mLayers[i].onUpdateFrameState(layerStack.getLayer(i));

        // apply the total scaling to the dst of the layer
        hwc_rect_t& dst = displayInfo.mLayers[i].editDst();
        dst.left   = finalFrameX + dst.left   * totalScalingFactorW + 0.5f;
        dst.top    = finalFrameY + dst.top    * totalScalingFactorH + 0.5f;
        dst.right  = finalFrameX + dst.right  * totalScalingFactorW + 0.5f;
        dst.bottom = finalFrameY + dst.bottom * totalScalingFactorH + 0.5f;

        // apply the total scaling to the visibleRegions of the layer
        Vector<hwc_rect_t>& visRegions = displayInfo.mLayers[i].editVisibleRegions();
        for (uint32_t r = 0; r < visRegions.size(); r++)
        {
            hwc_rect_t& visRect = visRegions.editItemAt(r);
            visRect.left   = finalFrameX + visRect.left   * totalScalingFactorW + 0.5f;
            visRect.top    = finalFrameY + visRect.top    * totalScalingFactorH + 0.5f;
            visRect.right  = finalFrameX + visRect.right  * totalScalingFactorW + 0.5f;
            visRect.bottom = finalFrameY + visRect.bottom * totalScalingFactorH + 0.5f;
        }

        ALOGD_IF( GLOBAL_SCALING_DEBUG,
            "final transform:phyIndex:%d, layer:%d, dst:(%d, %d, %d, %d).\n",
            phyIndex, i,
            dst.left, dst.top, dst.right, dst.bottom);

        // Clip layer final src/dst rect to display output region.
        // NOTE:
        //   The VPP handles -ve destination co-ordinates correctly, even where a
        //   transform is being applied. However, DRM does not, so it is best to
        //   always clip here.
        clipLayerToDisplay(&displayInfo.mLayers[i], outputW, outputH);
        const hwc_frect_t& src = displayInfo.mLayers[i].getSrc();
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
            "final transform:phyIndex:%d, layer:%d, after clip: src:(%f, %f, %f, %f), dst:(%d, %d, %d, %d).\n",
            phyIndex, i,
            src.left, src.top, src.right, src.bottom,
            dst.left, dst.top, dst.right, dst.bottom);

        // update layer flags
        displayInfo.mLayers[i].onUpdateFlags();
        // replace with our modified layer
        layerStack.setLayer(i, &displayInfo.mLayers[i]);
    }
    layerStack.updateLayerFlags();

     // if proxy display have different output size,change W/H of content display to the actual output size
    if (bHaveDifferentOutputResolution)
    {
        contentDisplay.setHeight(outputH);
        contentDisplay.setWidth(outputW);
    }

    // indicating content is changed
    return true;
}

// calculate the scaling factor from input size  to output size
void GlobalScalingFilter::calculateOutputScalingFactor(EHwcsScalingMode scalingMode, uint32_t inW, uint32_t inH,
                              uint32_t outputW, uint32_t outputH, float& outputScalingFactorW, float& outputScalingFactorH)
{
    uint32_t scaledDispW, scaledDispH;
    ALOG_ASSERT( (inW > 0) && (inH > 0) && (outputW > 0) && (outputH > 0) );

    switch ( scalingMode )
    {
        case HWCS_SCALE_CENTRE:
                //Present the content centred at 1:1 source resolution (maintaining source aspect ratio ).
                scaledDispW = inW;
                scaledDispH = inH;
                break;
        case HWCS_SCALE_FIT:// Preserve aspect ratio - scale to closest edge (may be letterboxed or pillarboxed).
        case HWCS_SCALE_FILL:// Preserve aspect ratio - scale to fill the display (may crop the content).
                // Fit to display (maintaining source aspect ratio).
                // Try expand width.
                scaledDispW = outputW;
                scaledDispH = ( outputW * inH ) / inW;
                if ( ((scaledDispH > outputH) && (scalingMode == HWCS_SCALE_FIT)) ||
                     ((scaledDispH < outputH) && (scalingMode == HWCS_SCALE_FILL)) )
                {
                    // Swap to expand height.
                    scaledDispH = outputH;
                    scaledDispW = ( outputH * inW ) / inH;
                }
                break;
            default:
            case HWCS_SCALE_STRETCH:
                // Do not preserve aspect ratio - scale to fill the display without cropping.
                scaledDispW = outputW;
                scaledDispH = outputH;
                break;
    }

    // calculate the scaling factor
    outputScalingFactorW = (float)scaledDispW / inW;
    outputScalingFactorH = (float)scaledDispH / inH;
}

// return true if the HW is enabled for Global scaling
bool GlobalScalingFilter::enableGlobalScalingHW(DisplayInfo& displayInfo, AbstractPhysicalDisplay& phys, Content::Display& display )
{
    ALOGD_IF( GLOBAL_SCALING_DEBUG, "enableGlobalScalingHW: phyIndex:%d, FrameIndex:%d.", phys.getDisplayManagerIndex(), display.getFrameIndex());

    uint32_t dispW = display.getWidth();
    uint32_t dispH = display.getHeight();

    // check if there is global scaling
    const Content::LayerStack& layerStack = display.getLayerStack();

    // Scaling factor in X/Y.
    float globalScalingFactorX = 1.0f;
    float globalScalingFactorY = 1.0f;

    // Source size (input display size).
    uint32_t inputW = dispW;
    uint32_t inputH = dispH;

    // Final frame (destination size/position).
    int32_t finalFrameX         = 0;
    int32_t finalFrameY         = 0;
    int32_t finalFrameW         = dispW;
    int32_t finalFrameH         = dispH;

    // Check global scaling.
    // Returns true if scaling is constant (for all layers).
    // If true, then also adjusts scaling factor, src size and dst frame.
    if ( !checkGlobalScalingFactor(layerStack,
                                   globalScalingFactorX, globalScalingFactorY,
                                   inputW, inputH,
                                   finalFrameX, finalFrameY, finalFrameW, finalFrameH) )
    {
        // no global scaling,do nothing
        return false;
    }
    ALOG_ASSERT( globalScalingFactorX != 0.0f );
    ALOG_ASSERT( globalScalingFactorY != 0.0f );

    // check if the final frame is supportted by the Global Scaling HW on this display
    if ( !isSupporttedByGlobalScalingHW(phys,
                                        dispW, dispH,
                                        inputW, inputH,
                                        finalFrameX, finalFrameY, finalFrameW, finalFrameH,
                                        globalScalingFactorX, globalScalingFactorY) )
    {
        // not supportted by HW, bail out
        return false;
    }

    // acquire Global Scaling HW
    if ( !acquireGlobalScalingHW( phys, display, inputW, inputH, finalFrameX, finalFrameY, finalFrameW, finalFrameH ) )
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG, "Failed to acquire global scaling HW on display:%d.\n", phys.getDisplayManagerIndex() );
        return false;
    }

    // modify content to undo the scaling
    // Transform (undo scaling) all layer co-ordinates to virtual resolution space (Source space) [0,0:srcW,srcH]
    transformContentsToVirtualResolution( displayInfo, phys.getDisplayManagerIndex(), display, inputW, inputH, globalScalingFactorX, globalScalingFactorY );

    // propogate the output scaling through display contents (informational).
    hwc_rect_t dst = { finalFrameX, finalFrameY, finalFrameX+finalFrameW, finalFrameY+finalFrameH };
    display.setOutputScaled( dst );

    return true;
}

bool GlobalScalingFilter::nearAspectPreserving( float globalScalingFactorX, float globalScalingFactorY )
{
    // Tolerance to match AR as absolute percentage difference.
    const float matchingARTolerance = 0.5f;  // 0.5%
    float pctDiff = 100 * fabs( globalScalingFactorX - globalScalingFactorY )
                   / ( 0.5f * (globalScalingFactorX + globalScalingFactorY) );
    return ( pctDiff < matchingARTolerance );
}

bool GlobalScalingFilter::checkGlobalScalingFactor( const Content::LayerStack& layerStack,
                                                    float& globalScalingFactorX, float& globalScalingFactorY,
                                                    uint32_t& inputW, uint32_t& inputH,
                                                    int32_t& finalFrameX, int32_t& finalFrameY, int32_t& finalFrameW, int32_t& finalFrameH )
{
    bool  bHasGlobalScaling = true;

    float factorX = 0.0f;
    float factorY = 0.0f;
    int32_t layerCount = layerStack.size();
    if ( layerCount <= 0 )
        return false;

    const Layer& layer = layerStack.getLayer(0);
    if ( mOptionGlobalScalingVideoOnly &&
        !(layerCount == 1 && layer.isVideo()
            && ((layer.getDstWidth() == (uint32_t)finalFrameW) || (layer.getDstHeight() == (uint32_t)finalFrameH) )) )
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG, "Current option only allows enabling global scaling HW when we have full height or width single plane video, skiped checking.");
        return false;
    }
    if ( layer.getSrcWidth() < 1.0f || layer.getSrcHeight() < 1.0f )
    {
        // src 0x0 layer, skip checking to avoid divide by 0
        return false;
    }

    if ( layer.getDstWidth() < 1.0f || layer.getDstHeight() < 1.0f )
    {
        // dst 0x0 layer
        return false;
    }

    // TODO: need to check transform? removed it for now
    ETransform transform = layer.getTransform();
    bool bTransposed = isTranspose( transform );
    float srcW = bTransposed ? layer.getSrcHeight() : layer.getSrcWidth();
    float srcH = bTransposed ? layer.getSrcWidth() : layer.getSrcHeight();
    if ( fabs( srcW - layer.getDstWidth() ) < 1.0f && fabs( srcH - layer.getDstHeight() ) < 1.0f )
    {
        // 1:1, no scaling
        ALOGD_IF( GLOBAL_SCALING_DEBUG, "scaling factor of first layer is X:%f, Y %f, no scaling, skip checking the rest.", factorX, factorY );
        return false;
    }

    factorX = layer.getDstWidth() / srcW;
    factorY = layer.getDstHeight() / srcH;
    ALOGD_IF( GLOBAL_SCALING_DEBUG, "scaling factor of first layer is X:%f, Y %f, transform:%d", factorX, factorY, transform );

    const float matchingScalingTolerance = 0.01f;
    for ( int32_t i = 1; i < layerCount; i++ )
    {
        const Layer& layer = layerStack.getLayer( i );
        transform = layer.getTransform();
        ALOGD_IF( GLOBAL_SCALING_DEBUG, "checking scaling factor for Layer %d,transform:%d, srcW:%f, srcH:%f, dstW:%d,dstH:%d",
             i, transform, layer.getSrcWidth(), layer.getSrcHeight(), layer.getDstWidth(), layer.getDstHeight() );
        bTransposed = isTranspose( transform );
        srcW = bTransposed ? layer.getSrcHeight() : layer.getSrcWidth();
        srcH = bTransposed ? layer.getSrcWidth() : layer.getSrcHeight();
        if ( srcW < 1.0f || srcH < 1.0f )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG, "checking scaling factor for Layer %d : invalid src size (%f, %f), stop checking.", i, srcW, srcH);
            bHasGlobalScaling = false;
            break;
        }
        // check if the layer's size have same scaling factor
        if ( fabs( factorX - layer.getDstWidth() / srcW ) > matchingScalingTolerance ||
             fabs( factorY - layer.getDstHeight() / srcH ) > matchingScalingTolerance )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG, "Layer %d has different scaling factor, dstW:%d,srcW:%f, dstH:%d, srcH:%f.",
                    i, layer.getDstWidth(), layer.getSrcWidth(), layer.getDstHeight(), layer.getSrcHeight() );
            bHasGlobalScaling = false;
            break;
        }
    }

    if (bHasGlobalScaling)
    {
        if ( nearAspectPreserving( factorX, factorY ) )
        {
            // Use same scaling factor for both axis.
            globalScalingFactorX = factorX;
            globalScalingFactorY = factorX;
        }
        else
        {
            // Separate scaling factors for each axis.
            globalScalingFactorX = factorX;
            globalScalingFactorY = factorY;
        }
        // Calculate the input size for the global scaling (invert frame by scaling factor).
        inputW = finalFrameW / globalScalingFactorX + 0.5f;
        inputH = finalFrameH / globalScalingFactorY + 0.5f;
        // Final frame remains full screen.
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
                  "has global scaling factor: x:%f, y:%f from inputW/H %ux%u to finalFrame %u,%u %ux%u",
                  globalScalingFactorX, globalScalingFactorY,
                  inputW, inputH, finalFrameX, finalFrameY, finalFrameW, finalFrameH );
    }

    return bHasGlobalScaling;
}

void GlobalScalingFilter::fixupFractionalFrame( uint32_t inputW, uint32_t inputH,
                                                int32_t& dx, int32_t& dy,
                                                int32_t& dw, int32_t& dh )
{
    // Rounding errors may lead to not-quite-true aspect preservation.
    // We can adjust for this by checking final results and modifying the destination frame
    // to insert some lines or columns (essentially converting global scaling to a
    // letterbox or pillarbox mode).

    // Check for precision errors in integer space.
    // If we have precise AR preservation in integer space then:
    //    inputW/inputH == finalFrameW/finalFrameH
    // =>
    //    inputW * finalFrameH == inputH * finalFrameW

    ALOG_ASSERT( dx == 0 );
    ALOG_ASSERT( dy == 0 );

    int32_t err = (inputW * dh) - (inputH * dw);
    ALOGD_IF( GLOBAL_SCALING_DEBUG,
              "Scaling input %dx%d -> dest %dx%d err=%d",
              inputW, inputH, dw, dh, err );

    if ( err < 0 )
    {
        // Precision error for srcW "too small"
        // => adjust frame to add columns (effectively pillarbox).
        uint32_t adj = (abs(err)+dh-1)/dh;          // Adjustment 1:N pixels.
        dw -= adj;                                  // Adjust width.
        dx = (adj+1)/2;                             // Centre with left offset 1:N pixels.
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
                  "inputW too small - err %d => adj %u => dx %d",
                  err, adj, dx );
    }
    else if ( err > 0 )
    {
        // Precision error for srcH "too small"
        // => adjust frame to add rows (effectively letterbox).
        uint32_t adj = (abs(err)+dw-1)/dw;          // Adjustment 1:N pixels.
        dh -= adj;                                  // Adjust height.
        dy = (adj+1)/2;                             // Centre with top offset 1:N pixels.
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
                  "inputH too small - err %d => adj %u => dy %d",
                  err, adj, dy );
    }
    else
    {
        // Final frame remains full screen.
        ALOGD_IF( GLOBAL_SCALING_DEBUG, "inputW/H matches final frame AR precisely" );
    }
}

bool GlobalScalingFilter::isSupporttedByGlobalScalingHW(AbstractPhysicalDisplay& phys,
                                                        int32_t dispW, int32_t dispH,
                                                        uint32_t inputW, uint32_t inputH,
                                                        int32_t& dx, int32_t& dy, int32_t& dw, int32_t& dh,
                                                        float globalScalingFactorX, float globalScalingFactorY)
{
    //   +--------------------------------[dispW]------------------------------------+
    //   |                                                                           |
    //   |                                                                           |
    //   |  (dx,dy)                                                                  |
    //   |      +---------------------------[dw]------------------------------+      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                            [dh] [dispH]
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      |                                                             |      |
    //   |      +-------------------------------------------------------------+      |
    //   |                                                                           |
    //   |                                                                           |
    //   |                                                                           |
    //   +---------------------------------------------------------------------------+
    uint32_t phyIndex = phys.getDisplayManagerIndex();
    const DisplayCaps::GlobalScalingCaps& globalScalingCaps = phys.getDisplayCaps().getGlobalScalingCaps();

    // Early-out for displays that don't support global scaling.
    if ( !(globalScalingCaps.getFlags() & DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_SUPPORTED ) )
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG, "D%d display global scaling not supported\n", phyIndex );
        return false;
    }

    const bool bPreservedAR = ( globalScalingFactorX == globalScalingFactorY );

    // Fixup frame to account for rounding errors in fullscreen aspect-preserving scaling.
    // Only do this on displays that actually support PILLARBOX/LETTERBOX modes,
    const uint32_t pillarLetterMask = DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_PILLARBOX
                                    | DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_LETTERBOX;
    if ( bPreservedAR
     && ( dx == 0 ) && ( dy == 0 )
     && ( dw == dispW ) && ( dh == dispH )
     && (( globalScalingCaps.getFlags() & pillarLetterMask ) == pillarLetterMask ) )
    {
        fixupFractionalFrame( inputW, inputH, dx, dy, dw, dh );
    }

    // Effective Source size.
    const uint32_t esw = inputW;
    const uint32_t esh = inputH;

    // All sizes must be specified (non-zero).
    ALOG_ASSERT( esw );
    ALOG_ASSERT( esh );
    ALOG_ASSERT( dw );
    ALOG_ASSERT( dh );

    const uint32_t gso = mOptionGlobalScaling;
    const bool bEnabled            = ( ( gso & GLOBAL_SCALING_OPTION_ENABLE ) != 0 )
                                  && ( ( gso & GLOBAL_SCALING_OPTION_ENABLE_AUX )
                                    || ( phyIndex == HWC_DISPLAY_PRIMARY ) );
    const bool bRestrictMatchingAR = ( ( gso & GLOBAL_SCALING_OPTION_RESTRICT_MATCHING_AR ) != 0 );
    const uint32_t allowMinScale   = mOptionGlobalScalingMin;
    const uint32_t allowMaxScale   = mOptionGlobalScalingMax;

    const float dstAR  = (float)dw / (float)dh;                 // Destination aspect ratio.
    const float srcAR  = (float)esw / (float)esh;               // Source aspect ratio.
    const float scalex = (float)dw / (float)esw;                  // Source scaling ratio (horizontal).
    const float scaley = (float)dh / (float)esh;                  // Source scaling ratio (vertical).
    const bool bInX  = ( dx > 0 ) || ( ( dx + dw ) < dispW );   // Is destination inside display on horizontal axis?
    const bool bInY  = ( dy > 0 ) || ( ( dy + dh ) < dispH );   // Is destination inside display on vertical axis?
    const bool bOutX = ( dx < 0 ) || ( ( dx + dw ) > dispW );   // Is destination outside display on horizontal axis?
    const bool bOutY = ( dy < 0 ) || ( ( dy + dh ) > dispH );   // Is destination outside display on vertical axis?
                                                                // Is aspect ratio preserved?
    const bool bPillarBox = bInX && !bInY && bPreservedAR;      // Does this represent a case of aspect-preserving pillarboxing?
    const bool bLetterBox = bInY && !bInX && bPreservedAR;      // Does this represent a case of aspect-preserving letterboxing?
    const bool bOverscan  = bInX || bInY;                       // Is destination inside the display in either axis?
    const bool bUnderscan = bOutX || bOutY;                     // Is destination outside the display in either axis?
    const bool bWindow    = bOverscan || bUnderscan;            // Is destination NOT fullscreen?

    ALOGD_IF( GLOBAL_SCALING_DEBUG,
        "D%d display %ux%u\n"
        " esw %u esh %u dx %d dy %d dw %u dh %u scalex %.2f scaley %.2f\n"
        " srcAR %.2f dstAR %.2f presAR %d pillar %d letter %d over %d under %d window %d\n"
        " enabled:%d allowMinScale:%u allowMaxScale:%u\n"
        " displayCAPS:0x%x x%.2f:%.2f %ux%u:%ux%u",
        phyIndex, dispW, dispH,
        esw, esh, dx, dy, dw, dh, scalex, scaley,
        srcAR, dstAR, bPreservedAR, bPillarBox, bLetterBox, bOverscan, bUnderscan, bWindow,
        bEnabled, allowMinScale, allowMaxScale,
        globalScalingCaps.getFlags(), globalScalingCaps.getMinScale(), globalScalingCaps.getMaxScale(),
        globalScalingCaps.getMinSourceWidth(), globalScalingCaps.getMinSourceHeight(),
        globalScalingCaps.getMaxSourceWidth(), globalScalingCaps.getMaxSourceHeight() );

    int32_t bOK = true;

    if ( !bEnabled )
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
            "D%d Rejected due to not enabled\n",
            phyIndex );
        bOK = false;
    }

    if ( bOK )
    {
        if ( allowMinScale )
        {
            if ( ( scalex < ( 0.01f * allowMinScale ) )
              || ( scaley < ( 0.01f * allowMinScale ) ) )
            {
                ALOGD_IF( GLOBAL_SCALING_DEBUG,
                    "D%d Rejected due to options minimum scaling limit",
                    phyIndex );
                bOK = false;
            }
        }
        if ( allowMaxScale )
        {
            if ( ( scalex > ( 0.01f * allowMaxScale ) )
              || ( scaley > ( 0.01f * allowMaxScale ) ) )
            {
                ALOGD_IF( GLOBAL_SCALING_DEBUG,
                    "D%d Rejected due to options maximum scaling limit",
                    phyIndex );
                bOK = false;
            }
        }
    }

    if ( bOK )
    {
        if ( bRestrictMatchingAR && !bPreservedAR )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to unmatched aspect-ratios\n",
                phyIndex );
            bOK = false;
        }
    }

    if ( bOK )
    {
        if ( bPillarBox && !(globalScalingCaps.getFlags() &
                            ( DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_PILLARBOX
                            | DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_WINDOW ) ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific destination restrictions [pillarbox]",
                phyIndex );
            bOK = false;
        }
        if ( bLetterBox && !(globalScalingCaps.getFlags() &
                            ( DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_LETTERBOX
                            | DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_WINDOW ) ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific destination restrictions [letterbox]",
                phyIndex );
            bOK = false;
        }
        if ( bWindow && !bPillarBox && !bLetterBox
                        && !(globalScalingCaps.getFlags() &
                            DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_WINDOW ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific destination restrictions [window]",
                phyIndex );
            bOK = false;
        }
        if ( bOverscan && !(globalScalingCaps.getFlags() &
                            DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_OVERSCAN ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific destination restrictions [overscan]",
                phyIndex );
            bOK = false;
        }
        if ( bUnderscan && !(globalScalingCaps.getFlags() &
                            DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_UNDERSCAN ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific destination restrictions [underscan]",
                phyIndex );
            bOK = false;
        }
    }

    if ( bOK )
    {
        if ( ( globalScalingCaps.getMaxScale() > 0.0f )
          && ( ( scalex > globalScalingCaps.getMaxScale() )
            || ( scaley > globalScalingCaps.getMaxScale() ) ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific supported scaling range [max scale]",
                phyIndex );
            bOK = false;
        }
        if ( ( globalScalingCaps.getMinScale() > 0.0f )
          && ( ( scalex < globalScalingCaps.getMinScale() )
            || ( scaley < globalScalingCaps.getMinScale() ) ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific supported scaling range [min scale]",
                phyIndex );
            bOK = false;
        }
    }

    if ( bOK )
    {
        if ( ( globalScalingCaps.getMinSourceWidth() > 0.0f )
          && ( esw < globalScalingCaps.getMinSourceWidth() ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific supported source size [min source width]",
                phyIndex );
            bOK = false;
        }
        if ( ( globalScalingCaps.getMaxSourceWidth() > 0.0f )
          && ( esw > globalScalingCaps.getMaxSourceWidth() ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific supported source size [max source width]",
                phyIndex );
            bOK = false;
        }
        if ( ( globalScalingCaps.getMinSourceHeight() > 0.0f )
          && ( esh < globalScalingCaps.getMinSourceHeight() ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific supported source size [min source height]",
                phyIndex );
            bOK = false;
        }
        if ( ( globalScalingCaps.getMaxSourceHeight() > 0.0f )
          && ( esh > globalScalingCaps.getMaxSourceHeight() ) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d Rejected due to display-specific supported source size [max source height]",
                phyIndex );
            bOK = false;
        }
    }

    if ( bOK )
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
                "D%d passed global scaling hw check.", phyIndex);
    }

    return bOK;
}

bool GlobalScalingFilter::acquireGlobalScalingHW(AbstractPhysicalDisplay& phys, Content::Display& contentDisplay, uint32_t sw, uint32_t sh,
                                        int32_t dx, int32_t dy, int32_t dw, int32_t dh)
{
    uint32_t dispW = contentDisplay.getWidth();
    uint32_t dispH = contentDisplay.getHeight();
    // clip the dst frame to display size
    float fDstLeft   = (float)dx;
    float fDstTop    = (float)dy;
    float fDstRight  = (float)(dx + dw);
    float fDstBottom = (float)(dy + dh);

    int32_t dstClippedLeft   = dx;
    int32_t dstClippedTop    = dy;
    int32_t dstClippedRight  = dx + dw;
    int32_t dstClippedBottom = dy + dh;

    clipToDisplay( &fDstLeft, &fDstTop, &fDstRight, &fDstBottom, ETransform::NONE,
                   &dstClippedLeft, &dstClippedTop, &dstClippedRight, &dstClippedBottom,
                   dispW, dispH );

    bool bAcquired = phys.acquireGlobalScaling( sw, sh, dstClippedLeft, dstClippedTop, dstClippedRight - dstClippedLeft, dstClippedBottom - dstClippedTop );
    if ( bAcquired )
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG,
                  "RPD%d Acquired global scaling for src:%ux%u dst:%d,%d %ux%u",
                  phys.getDisplayManagerIndex(), sw, sh, dstClippedLeft, dstClippedTop, dstClippedRight - dstClippedLeft, dstClippedBottom - dstClippedTop );
    }

    return bAcquired;
}

void GlobalScalingFilter::releaseGlobalScalingHW(AbstractPhysicalDisplay& phys)
{
    phys.releaseGlobalScaling();
}

void GlobalScalingFilter::transformContentsToVirtualResolution(DisplayInfo& displayInfo, uint32_t phyIndex, Content::Display& contentDisplay,
                                                            uint32_t srcW, uint32_t srcH, float scalingFactorX, float scalingFactorY)
{
    ALOGD_IF( GLOBAL_SCALING_DEBUG,
        "transform layers to virtual resolution : RPD%d, srcW:%d, srcH:%d, scalingFactorX:%f, scalingFactorY:%f.",
        phyIndex, srcW, srcH, scalingFactorX, scalingFactorY );

    ALOG_ASSERT( scalingFactorX != 0.0f );
    ALOG_ASSERT( scalingFactorY != 0.0f );

    Content::LayerStack& layerStack = contentDisplay.editLayerStack();
    uint32_t layerCount = layerStack.size();

    if (displayInfo.mLayers.size() < layerCount)
    {
        displayInfo.mLayers.resize(layerCount);

        if (displayInfo.mLayers.size() < layerCount)
        {
            // Note, as long as we always use mLayers.size() as a counter, this error condition is relatively harmless
            ALOGE("Failed to allocate new layer list. Corruption may occur");
            return;
        }
    }

    // transform each layer to the source space (virtual resolution) [0,0, srcW, srcH]
    // dst.x = dst.x / scalingFactorX;
    // dst.y = dst.y / ScalingFactorY;
    for ( uint32_t i = 0; i < layerCount; i++ )
    {
        // make a copy from content's layerStack
        displayInfo.mLayers[i] = layerStack.getLayer(i);
        displayInfo.mLayers[i].onUpdateFrameState(layerStack.getLayer(i));

        // transform layer's dst to the source space (virtual resolution)
        hwc_rect_t& dst = displayInfo.mLayers[i].editDst();
        const hwc_frect_t& src = displayInfo.mLayers[i].getSrc();
        dst.left   = dst.left   / scalingFactorX + 0.5f;
        dst.top    = dst.top    / scalingFactorY + 0.5f;
        dst.right  = dst.right  / scalingFactorX + 0.5f;
        dst.bottom = dst.bottom / scalingFactorY + 0.5f;

        // transform layer's visibleRegions to the source space (virtual resolution)
        Vector<hwc_rect_t>& visRegions = displayInfo.mLayers[i].editVisibleRegions();
        for (uint32_t r = 0; r < visRegions.size(); r++)
        {
            hwc_rect_t& visRect = visRegions.editItemAt(r);
            visRect.left   = visRect.left   / scalingFactorX + 0.5f;
            visRect.top    = visRect.top    / scalingFactorY + 0.5f;
            visRect.right  = visRect.right  / scalingFactorX + 0.5f;
            visRect.bottom = visRect.bottom / scalingFactorY + 0.5f;
        }

        ALOGD_IF( GLOBAL_SCALING_DEBUG,
            "transform to virtual resolution:phyIndex:%d, layer:%d, src:(%f, %f, %f, %f), dst:(%d, %d, %d, %d).\n",
            phyIndex, i,
            src.left, src.top, src.right, src.bottom,
            dst.left, dst.top, dst.right, dst.bottom );

        // update layer flags
        displayInfo.mLayers[i].onUpdateFlags();
        // replace with our modified layer
        layerStack.setLayer( i, &displayInfo.mLayers[i] );
    }
    layerStack.updateLayerFlags();

    // set content display's width/height to virtual resolution
    contentDisplay.setWidth( srcW );
    contentDisplay.setHeight( srcH );
}

    // calculate scaling factor for applying scaling mode
void GlobalScalingFilter::calculateScalingFactorFromScalingMode(DisplayInfo& displayInfo, const Content::Display& contentDisplay, float& scalingModeFactorW, float& scalingModeFactorH)
{
    scalingModeFactorW = 1.0;
    scalingModeFactorH = 1.0;
    EHwcsScalingMode scalingMode = getScalingMode(displayInfo);
    // we only support stretching to full screen
    if (scalingMode != HWCS_SCALE_STRETCH) return;

    hwc_rect_t boundaryRect;
    // get the boundary of layers
    getBoundaryOfLayerStack(contentDisplay.getLayerStack(), boundaryRect);

    uint32_t dispW = contentDisplay.getWidth();
    uint32_t dispH = contentDisplay.getHeight();
    uint32_t boundaryW = boundaryRect.right - boundaryRect.left;
    uint32_t boundaryH = boundaryRect.bottom - boundaryRect.top;
    // check if the boundary is full screen
    if (dispW == boundaryW && dispH == boundaryH)
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG, "calculateScalingFactorFromScalingMode: Already full screen.");
        return;
    }

    // if the boundary is smaller or bigger than full screen size, apply the scaling factor.
    // get the scaling factor due to applying the scaling mode
    calculateOutputScalingFactor(scalingMode, boundaryW, boundaryH,
                                 dispW, dispH, scalingModeFactorW, scalingModeFactorH);
}

void GlobalScalingFilter::getBoundaryOfLayerStack(const Content::LayerStack& layerStack, hwc_rect_t& boundaryRect)
{
    int32_t layerCount = layerStack.size();
    ALOG_ASSERT(layerCount);

    boundaryRect.right = 0;
    boundaryRect.bottom = 0;
    boundaryRect.left = 0xfffffff;
    boundaryRect.top = 0xffffffff;

    for ( int32_t i = 0; i < layerCount; i++ )
    {
        const Layer& layer = layerStack.getLayer( i );
        const hwc_rect_t& layerDst = layer.getDst();
        ALOGD_IF( GLOBAL_SCALING_DEBUG, "Scanning boundary for Layer %d, left:%d, top:%d, right:%d, bottom:%d",
             i, layerDst.left, layerDst.top, layerDst.right, layerDst.bottom );
        // the 0x0 layer(full screen dst) is added at the beginning of video playback then disaper after several seconds,
        // this will cause the SCALE_STRETCH scaling mode not applied and then applied again, we will see a small video for several seconds and then STRETCH to full screen width/height.
        // TODO: just skip the 0x0 layer temporarily, need further discuss how to handle the 0x0 layer with full screen dst, this layer will make the boundary full screen.
        // TODO: need to further check if we need handle the SKIP flag or buffer handled by SF?
        if ( (layer.getBufferWidth() == 0) && (layer.getBufferHeight() == 0) )
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG, "0x0 layer(flag:%d), skip this layer.", layer.getFlags());
            continue;
        }

        if ( i == 0 )
        {
            boundaryRect.left = layerDst.left;
            boundaryRect.right = layerDst.right;
            boundaryRect.top = layerDst.top;
            boundaryRect.bottom = layerDst.bottom;
            continue;
        }
        if ( boundaryRect.left > layerDst.left )
        {
            boundaryRect.left = layerDst.left;
        }
        if ( boundaryRect.right < layerDst.right )
        {
            boundaryRect.right = layerDst.right;
        }
        if ( boundaryRect.top > layerDst.top )
        {
            boundaryRect.top = layerDst.top;
        }
        if ( boundaryRect.bottom < layerDst.bottom )
        {
            boundaryRect.bottom = layerDst.bottom;
        }
    }

    ALOGD_IF( GLOBAL_SCALING_DEBUG, "boundary of Layerstack, left:%d, top:%d, right:%d, bottom:%d",
             boundaryRect.left, boundaryRect.top, boundaryRect.right, boundaryRect.bottom );
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
