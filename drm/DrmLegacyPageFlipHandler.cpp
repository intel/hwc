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
#include "DrmLegacyPageFlipHandler.h"
#include "Drm.h"
#include "DrmDisplay.h"
#include "DisplayCaps.h"
#include "Utils.h"
#include "Log.h"

#define DRM_PFH_NAME "DrmLegacyPageFlip"

namespace intel {
namespace ufo {
namespace hwc {

// *****************************************************************************
// DrmLegacyPageFlipHandler::Plane
// *****************************************************************************

DrmLegacyPageFlipHandler::Plane::Plane( ) :
    mDrm( Drm::get( ) ),
    mDrmCrtcID( 0 ),
    mDrmObjID( 0 ),
    mDrmObjType( 0 )
{
    reset( );
}

DrmLegacyPageFlipHandler::Plane::~Plane( )
{
    reset( );
}

uint32_t DrmLegacyPageFlipHandler::Plane::getDrmCrtcID( void )
{
    return mDrmCrtcID;
}

void DrmLegacyPageFlipHandler::Plane::reset( void )
{
    mbDirty  = true;
    mbDirtyTransform = true;
    mbDirtyDecrypt = true;
    mbEnabled = false;
}

void DrmLegacyPageFlipHandler::Plane::setDrmObject( uint32_t crtcID, uint32_t objectType, uint32_t objectID )
{
    ALOG_ASSERT( ( objectType != DRM_MODE_OBJECT_CRTC ) || ( objectID == crtcID ) );
    mDrmCrtcID = crtcID;
    mDrmObjType = objectType;
    mDrmObjID = objectID;
}

uint32_t DrmLegacyPageFlipHandler::Plane::getDrmObjectType( void ) const
{
    return mDrmObjType;
}

uint32_t DrmLegacyPageFlipHandler::Plane::getDrmObjectID( void ) const
{
    return mDrmObjID;
}

void DrmLegacyPageFlipHandler::Plane::flip( const Layer* pLayer, uint32_t flipEventData, bool* pbRequestedFlip )
{
    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc %u Flip %s %u",
        mDrmCrtcID, Drm::getObjectTypeString( mDrmObjType ), mDrmObjID );

    if ( pLayer )
    {
        if ( mbEnabled )
        {
            // Check for changes (or if we want to issue page flip event from this plane).
            if ( ( mLayer.getBufferDeviceId( ) != pLayer->getBufferDeviceId( ) )
              || ( mLayer.getSrc( )            != pLayer->getSrc( )            )
              || ( mLayer.getDst( )            != pLayer->getDst( )            )
              || ( mLayer.getPlaneAlpha( )     != pLayer->getPlaneAlpha( )     )
              || ( mLayer.getBlending( )       != pLayer->getBlending( )       )
              || ( flipEventData != 0 ) )
            {
                mbDirty = true;
            }

            if ( mLayer.getTransform( ) != pLayer->getTransform( ) )
            {
                mbDirty = true;
                mbDirtyTransform = true;
            }

            if ( mLayer.isEncrypted( ) != pLayer->isEncrypted( ) )
            {
                mbDirty = true;
                mbDirtyDecrypt = true;
            }
        }
        else
        {
            // Enable.
            mbDirty = true;
            mbDirtyDecrypt = true;
            mbDirtyTransform = true;
        }
    }
    else if ( mbEnabled )
    {
        // Disable.
        mbDirty = true;
        mbDirtyTransform = true;
        mbDirtyDecrypt = true;
    }

    if ( pLayer && pLayer->isEnabled( ) )
    {
        const hwc_frect_t& src = pLayer->getSrc();
        const hwc_rect_t&  dst = pLayer->getDst();

        // Enabled.
        if ( mbDirty )
        {
            if ( mbDirtyTransform )
            {
                if ( mDrm.setTransform( mDrmObjType, mDrmObjID, pLayer->getTransform( ) ) == Drm::SUCCESS )
                {
                    mbDirtyTransform = false;
                }
            }

            if ( mbDirtyDecrypt )
            {
                if ( mDrm.setDecrypt( mDrmObjType, mDrmObjID, pLayer->isEncrypted( ) ) == Drm::SUCCESS )
                {
                    mbDirtyDecrypt = false;
                }
            }

            uint32_t drmFlags = flipEventData ? DRM_MODE_PAGE_FLIP_EVENT : 0;

            int r;

            const uint32_t fb = pLayer->getBufferDeviceId( );

            bool bMainBlanking = false;
            if ( mDrmObjType == DRM_MODE_OBJECT_CRTC )
            {
                bMainBlanking = !flipEventData;
                r = mDrm.pageFlip( mDrmCrtcID, fb, drmFlags, (void*)(uintptr_t)flipEventData );
            }
            else
            {
                r = mDrm.setPlane( mDrmObjID, mDrmCrtcID,
                                   fb,
                                   drmFlags,
                                   dst.left,
                                   dst.top,
                                   dst.right - dst.left,
                                   dst.bottom - dst.top,
                                   floatToFixed16(src.left),
                                   floatToFixed16(src.top),
                                   floatToFixed16(src.right - src.left),
                                   floatToFixed16(src.bottom - src.top),
                                   (void*)(uintptr_t)flipEventData );
            }

            Log::alogd( DRM_PAGEFLIP_DEBUG,
                    DRM_PFH_NAME " %5s %u H:%p%s%s TX:%d S:%.1f,%.1f,%.1fx%.1f F:%d,%d,%dx%d %s%s%s",
                    Drm::getObjectTypeString( mDrmObjType ), mDrmObjID,
                    pLayer->getHandle(),
                    pLayer->isDisabled()                    ? ":DISABLE"   : "",
                    pLayer->isEncrypted()                   ? ":DECRYPT"   : "",
                    pLayer->getTransform(),
                    src.left, src.top, src.right - src.left, src.bottom - src.top,
                    dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                    drmFlags & DRM_MODE_PAGE_FLIP_EVENT     ? ":FLIPEVENT" : "",
                    bMainBlanking                           ? ":BLANKING" : "",
                    r == Drm::SUCCESS ? "" : "!ERROR!" );

            if ( r == Drm::SUCCESS )
            {
                // Only reset mbDirty when all state has been succesfully applied.
                // Only clear down dirty state flag when state has been succesfully applied.
                mbDirty = mbDirtyTransform || mbDirtyDecrypt;

                // Set new layer.
                mLayer = *pLayer;
                mbEnabled = true;

                if ( flipEventData )
                {
                    *pbRequestedFlip = true;
                }
            }
        }
        else
        {
            // Log the fact that we haven't changed anything on this plane.
            Log::alogd( DRM_PAGEFLIP_DEBUG,
                    DRM_PFH_NAME " %5s %u H:%p%s%s TX:%d S:%.1f,%.1f,%.1fx%.1f F:%d,%d,%dx%d Skipped (No Change)",
                    Drm::getObjectTypeString( mDrmObjType ), mDrmObjID,
                    pLayer->getHandle(),
                    pLayer->isDisabled()                    ? ":DISABLE"   : "",
                    pLayer->isEncrypted()                   ? ":DECRYPT"   : "",
                    pLayer->getTransform(),
                    src.left, src.top, src.right - src.left, src.bottom - src.top,
                    dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top );
        }
    }
    else
    {
        // Disabled.
        if ( mbDirty )
        {
            // We don't support disable for main planes.
            ALOG_ASSERT( mDrmObjType != DRM_MODE_OBJECT_CRTC );
            if ( mDrm.setPlane( mDrmObjID, mDrmCrtcID,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL ) == Drm::SUCCESS )
            {
                // Flip to disable.
                mbDirty = false;
                mbDirtyTransform = false;
                mbDirtyDecrypt = false;
                mbEnabled = false;
                Log::alogd( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Plane %u Disabled", mDrmObjID );
            }
        }
        else
        {
            // Log the fact that we haven't changed anything on this plane.
            Log::alogd( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Plane %u Disabled (No Change)", mDrmObjID );
        }
    }
}

// *****************************************************************************
// DrmLegacyPageFlipHandler
// *****************************************************************************

DrmLegacyPageFlipHandler::DrmLegacyPageFlipHandler( DrmDisplay& display ) :
    mDisplay( display ),
    mDrm( Drm::get( ) ),
    mFlips( 0 ),
    maPlanes( NULL ),
    mZOrder( 0 ),
    mbDirtyZOrder( true )
{
    doInit();
}

DrmLegacyPageFlipHandler::~DrmLegacyPageFlipHandler( )
{
    doUninit();
    delete [] maPlanes;
}

void DrmLegacyPageFlipHandler::doInit()
{
    const DisplayCaps& genCaps = mDisplay.getDisplayCaps( );
    const DrmDisplayCaps& drmCaps = mDisplay.getDrmDisplayCaps( );

    mNumPlanes = genCaps.getNumPlanes( );
    maPlanes = new Plane[ mNumPlanes ];

    if ( !maPlanes )
    {
        ALOGE( "Failed to create plane state" );
        return;
    }

    mMainPlaneIndex = -1;
    mbHaveMainPlaneDisable = drmCaps.isMainPlaneDisableSupported( );

    for ( uint32_t p = 0; p < mNumPlanes; ++p )
    {
        const DrmDisplayCaps::PlaneCaps& planeCaps = drmCaps.getPlaneCaps( p );

        uint32_t id = planeCaps.getDrmID( );
        bool bMain = planeCaps.getDrmPlaneType( ) == DrmDisplayCaps::PLANE_TYPE_MAIN;

        if ( bMain )
        {
            ALOG_ASSERT( mDisplay.getDrmCrtcID( ) == id );
            maPlanes[p].setDrmObject( mDisplay.getDrmCrtcID( ),
                                      DRM_MODE_OBJECT_CRTC,
                                      id );
            // NOTE:
            // flip() implementation assumes main planes will always be at slot 0.
            ALOG_ASSERT( p == 0 );
            mMainPlaneIndex = p;
        }
        else
        {
            maPlanes[p].setDrmObject( mDisplay.getDrmCrtcID( ),
                                      DRM_MODE_OBJECT_PLANE,
                                      id );
        }

        ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc %u Plane %u : Id %u (%s)",
            mDisplay.getDrmCrtcID(), p,
            maPlanes[p].getDrmObjectID( ),
            Drm::getObjectTypeString( maPlanes[p].getDrmObjectType( ) ) );
    }
}

void DrmLegacyPageFlipHandler::doUninit( void )
{
    // Reset planes (will also dirty all plane state).
    for ( uint32_t p = 0; p < mNumPlanes; ++p )
    {
        maPlanes[p].reset( );
    }
}

bool DrmLegacyPageFlipHandler::doFlip( DisplayQueue::Frame* pNewFrame, bool bMainBlanked, uint32_t flipEvData )
{
    if ( maPlanes == NULL )
        return false;

    if ( ((mFlips++) & 255) == 0 )
    {
        ALOGW( "WARNING: Non-atomic legacy drm in use, expect occasional flickers" );

    }

    // *************************************************************************
    // Panel fitter processing.
    // *************************************************************************
    if ( !mDisplay.applyGlobalScalingConfig( pNewFrame->getConfig().getGlobalScaling() ) )
    {
        // Not a lot we can do to recover here.
        // If we succeed the acquireGlobalScaling (during prepeare)
        // and fail the application here in set( ) then that is a bug.
        ALOGE( "Failed to apply global scaling changes (panel fitter fail)" );
    }

    // *************************************************************************
    // ZOrder processing.
    // *************************************************************************
    if ( ( mbDirtyZOrder )
      || ( mZOrder != pNewFrame->getZOrder() ) )
    {
        const uint32_t newZOrder = pNewFrame->getZOrder();
        mbDirtyZOrder = true;

        int r = mDrm.setZOrder( mDisplay.getDrmCrtcID(), newZOrder );

        Log::alogd( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc:%d Pipe:%u ZOrder:%u,%s%s",
            mDisplay.getDrmCrtcID(), mDisplay.getDrmPipeIndex(),
            newZOrder, mDrm.zOrderToString( newZOrder ),
            r ? " !ERROR!" : "" );

        if ( r == Drm::SUCCESS )
        {
            // Only clear down dirty state flag when state has been succesfully applied.
            mbDirtyZOrder = false;
            mZOrder = newZOrder;
        }
    }
    else
    {
        Log::alogd( DRM_PAGEFLIP_DEBUG,
            DRM_PFH_NAME " Crtc:%d ZOrder:%u,%s Skipped (No Change)",
            mDisplay.getDrmCrtcID(), mZOrder, mDrm.zOrderToString( mZOrder ) );
    }

    // *************************************************************************
    // Plane processing.
    // *************************************************************************

    // NOTES:
    //   MCG builds only support flip request on SpriteA.
    //   GMIN builds support flip request from any Sprite.
    //   In either case, we can assert:
    //    SpriteA will always be used if any sprite is used.
    const bool bHaveMainPlane = ( mMainPlaneIndex != -1 );

    // Do we want to disable the main?
    const bool bUseSpriteEv = !bHaveMainPlane || ( mbHaveMainPlaneDisable && bMainBlanked );

    bool bRequestedFlip = false;

    // The flip sprite index is 0 or 1 depending on whether we have a main plane.
    const uint32_t flipSpritePlane = bHaveMainPlane ? 1 : 0;

    // Plane processing is reversed so main is processed last.
    for( uint32_t p = mNumPlanes; p > 0; )
    {
        --p;

        const Layer* pLayer = NULL;
        if ( p < pNewFrame->getLayerCount( ) )
        {
            pLayer = &pNewFrame->getLayer( p )->getLayer( );
        }
        else
        {
            pLayer = NULL;
        }

        // Is this plane the main plane?
        const bool bMainPlane = bHaveMainPlane && ( p == (uint32_t)mMainPlaneIndex );

        // If this plane is the main plane and the main layer was blanked then swap in blanking layer.
        if ( bMainPlane && bMainBlanked )
        {
            pLayer = &mDisplay.getBlankingLayer( );
        }

        uint32_t flipEventData;

        if ( !bRequestedFlip && ( bMainPlane || ( bUseSpriteEv && ( p == flipSpritePlane ) ) ) )
        {
            flipEventData = flipEvData;
        }
        else
        {
            flipEventData = 0;
        }

        maPlanes[ p ].flip( pLayer, flipEventData, &bRequestedFlip );
    }

    // Must always request flip.
    ALOGE_IF( !bRequestedFlip, "Failed to issue flip event request for frame %s", pNewFrame->getFrameId().dump().string() );

    return bRequestedFlip;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

