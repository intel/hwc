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

#ifndef INTEL_UFO_HWC_GLOBAL_SCALLING_FILTER_H
#define INTEL_UFO_HWC_GLOBAL_SCALLING_FILTER_H

#include "AbstractFilter.h"
#include "HwcServiceApi.h"
#include "PhysicalDisplayManager.h"
#include "Option.h"

namespace intel {
namespace ufo {
namespace hwc {

class GlobalScalingFilter : public AbstractFilter
{
public:
    GlobalScalingFilter(PhysicalDisplayManager& pdm);
    virtual ~GlobalScalingFilter();

    // Flags for global scaling option.
    enum EGlobalScalingOptions
    {
        GLOBAL_SCALING_OPTION_ENABLE                 = (1<<0),   // Global scaling enabled (primary only unless AUX is also set).
        GLOBAL_SCALING_OPTION_RESTRICT_MATCHING_AR   = (1<<1),   // Only use when AR is matching.
        GLOBAL_SCALING_OPTION_KEEP_ENABLED_FOR_VIDEO = (1<<2),   // Keep enabled while video is present.
        GLOBAL_SCALING_OPTION_KEEP_ENABLED_ALWAYS    = (1<<3),   // Keep enabled in all cases.
        GLOBAL_SCALING_OPTION_ENABLE_AUX             = (1<<4)    // Enable also for auxiliary displays.
    };

    // This returns the name of the filter.
    const char* getName() const { return "GlobalScalingFilter"; }
    bool outputsPhysicalDisplays() const { return true; }
    const Content& onApply(const Content& ref);

    String8 dump();

    // Public API
    // set the actual output resolution of the display. this is needed for ProxyDisplay case
    // which have a different display resolution from the resolution reported to SF.
    // and also used by widi display extended mode.
    // passing 0 in outputWidth or outputHeight parameter reset(disable) ActualOutputResolution scaling.
    int setActualOutputResolution(uint32_t phyIndex, uint32_t outputWidth, uint32_t outputHeight);
    bool getActualOutputResolution(uint32_t phyIndex, uint32_t& outputWidth, uint32_t& outputHeight);

    // Implements service set/getOverscan.
    int setUserOverscan(uint32_t phyIndex, int32_t xOverscan, int32_t yOverscan);
    bool getUserOverscan(uint32_t phyIndex, int32_t& xOverscan, int32_t& yOverscan);

    // set the user scaling mode
    int setUserScalingMode(uint32_t phyIndex, EHwcsScalingMode scalingMode);
    bool getUserScalingMode(uint32_t phyIndex, EHwcsScalingMode& scalingMode);

private:
    // Helper struct to contain per display settings
    struct DisplayInfo
    {
        DisplayInfo() : mbSetActualOutputResolution(false), mActualOutputWidth(0), mActualOutputHeight(0),
            mbHaveUserOverscan(false), mUserOverscanX(0), mUserOverscanY(0),
            mbHaveUserScalingMode(false), mUserScalingMode(HWCS_SCALE_FIT),
            mbSettingsChanged(false), mbGlobalScalingHwEnabled(false) { }

        bool            mbSetActualOutputResolution;        // Set actual output resolution this display?
        uint32_t        mActualOutputWidth;                 // actual output size of the display, for proxy display
        uint32_t        mActualOutputHeight;                // actual output size of the display, for proxy display
        bool            mbHaveUserOverscan;                 // Has user-specified overscan been set for this display?
        int32_t         mUserOverscanX;                     // User-specified overscan (See IDisplayOverscanControl).
        int32_t         mUserOverscanY;                     // User-specified overscan (See IDisplayOverscanControl).
        bool            mbHaveUserScalingMode;              // Has user-specified scaling mode?
        EHwcsScalingMode    mUserScalingMode; // User-specified scaling mode.
        bool            mbSettingsChanged;                  // true when one of the settings changed
        std::vector<Layer>  mLayers;                        // layer list for this display
        bool            mbGlobalScalingEnabled:1;           // GlobalScaling is enabled for this display
        bool            mbGlobalScalingHwEnabled:1;         // GlobalScalingHW is enabled for this display
    };

    bool isDisplaySettingsChanged(uint32_t phyIndex);   // Unused
    bool handleDisplaySettingsChanged(DisplayInfo& displayInfo);

    EHwcsScalingMode getScalingMode(DisplayInfo& displayInfo);
    // apply scaling by overscan and proxy display, return true if the contentDisplay is changed
    bool applyAllScalings(DisplayInfo& displayInfo, AbstractPhysicalDisplay& phys, Content::Display& contentDisplay);
    // calculate scaling factor for different output resolution
    void calculateOutputScalingFactor(EHwcsScalingMode scalingMode, uint32_t inW, uint32_t inH, uint32_t outputW, uint32_t outputH,
                                      float& outputScalingFactorW, float& outputScalingFactorH);

    // enable HW to achieve the global scaling
    bool enableGlobalScalingHW(DisplayInfo& displayInfo, AbstractPhysicalDisplay& phys, Content::Display& display);
    // check if scaling in X/Y is near aspect preserving.
    bool nearAspectPreserving( float globalScalingFactorX, float globalScalingFactorY );
    // check if there is global scaling and return the scaling factors, input size and final frame if there is
    bool checkGlobalScalingFactor(const Content::LayerStack& layerStack,
                                  float& globalScalingFactorX, float& globalScalingFactorY,
                                  uint32_t& inputW, uint32_t &inputH,
                                  int32_t& finalFrameX, int32_t& finalFrameY, int32_t& finalFrameW, int32_t& finalFrameH);
    // assuming fullframe AR scaling, check scaling has no fractional component in either axis.
    // If there is, then modify the frame to indicate pillarbox or letterbox instead of fullframe.
    void fixupFractionalFrame( uint32_t inputW, uint32_t inputH,
                               int32_t& dx, int32_t& dy,
                               int32_t& dw, int32_t& dh );
    // check if the global scaling can be supported by display HW
    bool isSupporttedByGlobalScalingHW(AbstractPhysicalDisplay& phys,
                                       int32_t dispW, int32_t dispH,
                                       uint32_t inputW, uint32_t inputH,
                                       int32_t& dx, int32_t& dy, int32_t& dw, int32_t& dh,
                                       float globalScalingFactorX, float globalScalingFactorY);
    bool acquireGlobalScalingHW(AbstractPhysicalDisplay& phys, Content::Display& contentDisplay, uint32_t sw, uint32_t sh,
                                        int32_t dx, int32_t dy, int32_t dw, int32_t dh);
    void releaseGlobalScalingHW(AbstractPhysicalDisplay& phys);
    // transform display content to virtual resolution[0, 0, srcW, srcH]
    void transformContentsToVirtualResolution(DisplayInfo& displayInfo, uint32_t phyIndex, Content::Display& contentDisplay,
                                           uint32_t srcW, uint32_t srcH, float scalingFactorX, float scalingFactorY);
    // calculate scaling factor for applying scaling mode
    void calculateScalingFactorFromScalingMode(DisplayInfo& displayInfo, const Content::Display& contentDisplay, float& scalingModeFactorW, float& scalingModeFactorH);
    // get the boundary of layers.
    void getBoundaryOfLayerStack(const Content::LayerStack& layerStack, hwc_rect_t& boundaryRect);

    DisplayInfo mDisplayInfo[cMaxSupportedPhysicalDisplays];


    Content                     mContent;                   //< private copy of the content.
    Mutex                       mLock;                      //< Lock for async filter updates.
    PhysicalDisplayManager&     mPhysicalDisplayManager;    //< Physical display manager.

    Option mOptionGlobalScaling;       //< Global scaling flags (see EGlobalScalingFlags).
    Option mOptionGlobalScalingMin;    //< Global scaling down-scale limit as a percentage (or zero if no limit).
    Option mOptionGlobalScalingMax;    //< Global scaling up-scale limit as a percentage (or zero if no limit).
    Option mOptionGlobalScalingEdge;   //< Global scaling clamp layer horizontally or vertically to display edges.
    Option mOptionGlobalScalingVideoOnly;   //< only enabling global scaling HW when we have full height or width single plane video
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_GLOBAL_SCALLING_FILTER_H
