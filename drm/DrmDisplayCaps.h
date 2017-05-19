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


#ifndef INTEL_UFO_HWC_DISPLAY_CAPS_DRM_H
#define INTEL_UFO_HWC_DISPLAY_CAPS_DRM_H

#include "DisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

// ********************************************************************
// Display Capabilities for DRM class devices.
// ********************************************************************
class DrmDisplayCaps
{
public:

    // PIPE Index
    enum EDrmPipeIndex
    {
        PIPE_A = 0,
        PIPE_B = 1,
        PIPE_C = 2
    };

    // DRM plane types.
    enum EDrmPlaneType
    {
        PLANE_TYPE_UNKNOWN = 0,
        PLANE_TYPE_MAIN,
        PLANE_TYPE_SPRITE
    };

    // DRM specific plane capabilities.
    class PlaneCaps
    {
    public:
        PlaneCaps();
        ~PlaneCaps();
            // Construct plane capabilities for the specified plane and with
            // the specified display format support.
            // No extended transforms or other capabilities are set.
        void probe( EDrmPlaneType eDrmPlaneType,          //< Drm plane Type (main v sprite).
                    uint32_t drmID,                       //< Drm plane ID.
                    uint32_t pipeIndex,                   //< Drm pipe.
                    uint32_t numFormats,                  //< Default display-format LUT.
                    const int32_t* pFormats );            //< Default display-format LUT.
        uint32_t getDrmID( void ) const { return mDrmID; }
        EDrmPlaneType getDrmPlaneType( void ) const { return meDrmPlaneType; }

        DisplayCaps::PlaneCaps* getDisplayPlaneCaps() { return mpDisplayPlaneCaps; }
        void setDisplayPlaneCaps(DisplayCaps::PlaneCaps*);

    protected:
        EDrmPlaneType               meDrmPlaneType;
        uint32_t                    mDrmID;
        DisplayCaps::PlaneCaps*     mpDisplayPlaneCaps;
    };

    DrmDisplayCaps();

    // Construct Drm capabilities for specific crtc/pipe index and populate baseline hardware caps
    // DrmDisplayCaps takes ownership of the pCaps - previous caps (if any) will be deleted.
    void probe( uint32_t crtcID, uint32_t pipeIndex, uint32_t connectorID, DisplayCaps* pCaps );

    // Get plane cap object.
    const PlaneCaps& getPlaneCaps( uint32_t plane ) const { return mPlanes[ plane ]; }

    // Test specific capabilities.
    bool isMainPlaneDisableSupported( void ) const  { return mbCapFlagMainPlaneDisable; }
    bool isFlagAsyncDPMS( void ) const              { return mbCapFlagAsyncDPMS; }
    bool isZOrderSupported( void ) const            { return mbCapFlagZOrder; }
    bool isScreenControlSupported( void ) const     { return mbCapFlagScreenControl; }
    bool isPanelFitterSupported( void ) const       { return mbCapFlagPanelFitter; }
    bool isPowerManagerSupported( void ) const      { return mbCapFlagPowerManager; }
    bool isSelfRefreshSupported( void ) const       { return mbCapFlagSelfRefresh; }
    bool isSpriteTxRotSupported( void ) const       { return mbCapFlagSpriteTxRot; }

    // Get display capabilities as human-readable string.
    String8 displayCapsString( void ) const;

protected:


    // Add the main plane.
    // The main plane is set up with default set of display formats.
    // No extended transforms or other capabilities are set.
    // Returns plane index or -1 on error.
    void addMainPlane( DisplayCaps& caps );

    // Add sprite planes.
    // The sprite planes are set up with enumerated display formats.
    // No extended transforms or other capabilities are set.
    // Returns first plane index and count or -1 on error.
    void addSpritePlanes( DisplayCaps& caps );

    Vector<PlaneCaps>   mPlanes;

    // Generic DisplayCaps, provided on (re)probe.
    DisplayCaps*        mpDisplayCaps;

    // Crtc ID.
    uint32_t mCrtcID;
    // Pipe index (0,1,...)
    uint32_t mPipeIndex;

    // Drm specific display capabilities.
    bool mbCapFlagMainPlaneDisable:1;       //< Main plane can be fully disabled (else must be faked).
    bool mbCapFlagAsyncDPMS:1;              //< Asynchronous DPMS.
    bool mbCapFlagZOrder:1;                 //< ZOrder.
    bool mbCapFlagScreenControl:1;          //< Screen control.
    bool mbCapFlagPanelFitter:1;            //< Panel Fitter.
    bool mbCapFlagPowerManager:1;           //< Powermanager is present.
    bool mbCapFlagSelfRefresh:1;            //< Self-refresh (PSR).
    bool mbCapFlagSpriteTxRot:1;            //< Sprite planes support transform rotation.

    // Indication as to whether this drm kernel supports universal planes.
    bool mbUniversalPlanes:1;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DISPLAY_CAPS_DRM_H
