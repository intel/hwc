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


#ifndef INTEL_UFO_HWC_DRMLEGACYPAGEFLIPHANDLER_H
#define INTEL_UFO_HWC_DRMLEGACYPAGEFLIPHANDLER_H

#include "DrmPageFlipHandler.h"
#include "Timeline.h"

namespace intel {
namespace ufo {
namespace hwc {

class Drm;
class DrmDisplay;

// Drm display flip handler class for legacy Drm.
class DrmLegacyPageFlipHandler : public DrmPageFlipHandler::AbstractImpl
{
public:

    DrmLegacyPageFlipHandler( DrmDisplay& display );
    virtual ~DrmLegacyPageFlipHandler( );

protected:

    // Flip the next frame to the display.
    // Returns true if the flip event request is successfully issued.
    virtual bool doFlip( DisplayQueue::Frame* pNewFrame, bool bMainBlanked, uint32_t flipEvData );

private:

    // Plane.
    class Plane
    {
    public:
        // C'tor.
        Plane( );

        // D'tor.
        ~Plane( );

        // Reset state/drop all buffers.
        void reset( void );

        // Set the drm crtcID plus object type/ID.
        // If this object type is DRM_MODE_OBJECT_CRTC then objectID and crtcID should be the same.
        void setDrmObject( uint32_t crtcID, uint32_t objectType, uint32_t objectID );

        // Set support for FLIPEVENT (default is on).
        void setSupportsFlipEvent( bool bSupport );

        // Get drm object type.
        uint32_t getDrmObjectType( void ) const;

        // Get drm object ID.
        uint32_t getDrmObjectID( void ) const;

        // Flip a new layer to this plane.
        // FLIPEVENT will be requested if a callback is provided.
        // If a new buffer is succesfully flipped with FLIPEVENT then pbRequestedFlip will be set to true.
        void flip( const Layer* pLayer, uint32_t flipEventData, bool* pbRequestedFlip );

        // Call from the page flip event to complete the previous flip.
        void completeFlip( void );

    private:

        // Get Drm CrtcID.
        uint32_t getDrmCrtcID( void );

        // Drm instance reference.
        Drm&     mDrm;

        uint32_t mDrmCrtcID;            //< Drm crtc ID.
        uint32_t mDrmObjID;             //< Drm object ID.
        uint32_t mDrmObjType;           //< DRM_MODE_OBJECT_CRTC or DRM_MODE_OBJECT_PLANE

        bool     mbDirty:1;             //< State needs to be applied.
        bool     mbDirtyTransform:1;    //< State needs to be applied for transform.
        bool     mbDirtyDecrypt:1;      //< State needs to be applied for decrypt.

        bool     mbEnabled:1;           //< Is plane currently enabeld?
        Layer    mLayer;                //< Current state if enabled.
    };


    // Initialise the page flip handler impl.
    void doInit( void );

    // Uninitialise the page flip handler impl.
    void doUninit( void );

private:

    // Display.
    DrmDisplay&             mDisplay;

    // Drm.
    Drm&                    mDrm;

    // Flips.
    uint32_t                mFlips;

    // Number of planes.
    uint32_t                mNumPlanes;

    // Index for the main plane.
    // -1 if not found.
    int32_t                 mMainPlaneIndex;

    // Plane state.
    Plane*                  maPlanes;

    // ZOrder state.
    uint32_t                mZOrder;
    bool                    mbDirtyZOrder:1;

    // Is main plane disable available?
    bool                    mbHaveMainPlaneDisable:1;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DRMLEGACYPAGEFLIPHANDLER_H
