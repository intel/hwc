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


#ifndef INTEL_UFO_HWC_DRMSETDISPLAYPAGEFLIPHANDLER_H
#define INTEL_UFO_HWC_DRMSETDISPLAYPAGEFLIPHANDLER_H

#include "DrmPageFlipHandler.h"
#include "Timeline.h"
#include "Drm.h"

#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY

namespace intel {
namespace ufo {
namespace hwc {

class Drm;
class DrmDisplay;

// Drm display flip handler class for atomic Drm.
class DrmSetDisplayPageFlipHandler : public DrmPageFlipHandler::AbstractImpl
{
public:

    DrmSetDisplayPageFlipHandler( DrmDisplay& display );
    virtual ~DrmSetDisplayPageFlipHandler( );

    // Tests whether atomic API is available for use by setting blanking.
    // Returns true if successful.
    static bool test( DrmDisplay& display );

protected:

    // Flip the next frame to the display.
    // Returns true if the flip event request is successfully issued.
    virtual bool doFlip( DisplayQueue::Frame* pNewFrame, bool bMainBlanked, uint32_t flipEvData );

private:

    // Update plane.
    bool updatePlane( drm_mode_set_display_plane* pPlane,
                      const Layer* pLayer,
                      uint32_t flipEventData,
                      bool* pbRequestedFlip,
                      bool bIsBlanking );

    // Initialise the page flip handler impl.
    void doInit( void );

    // Uninitialise the page flip handler impl.
    void doUninit( void );

#if INTEL_HWC_INTERNAL_BUILD
    // Asserts state is valid.
    void validateSetDisplay( void );
#endif

private:
    // Set display option settings
    enum
    {
        eSetDisplayDisabled = 0,
        eSetDisplayEnabled  = 1,
        eSetDisplayUnknown  = 3
    };

    // Enable atomic?
    static Option           sOptionSetDisplay;

    // Display.
    DrmDisplay&             mDisplay;

    // Drm.
    Drm&                    mDrm;

    // Number of planes.
    uint32_t                mNumPlanes;

    // Index for the main plane.
    // -1 if not found.
    int32_t                 mMainPlaneIndex;

    // Is main plane disable available?
    bool                    mbHaveMainPlaneDisable:1;

    // Display state.
    drm_mode_set_display    mSetDisplay;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif

#endif // INTEL_UFO_HWC_DRMSETDISPLAYPAGEFLIPHANDLER_H
