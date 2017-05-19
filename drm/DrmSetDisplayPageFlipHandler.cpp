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
#include "DrmSetDisplayPageFlipHandler.h"
#include "Drm.h"
#include "DrmDisplay.h"
#include "DisplayCaps.h"
#include "Utils.h"
#include "Log.h"

#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY

#define DRM_PFH_NAME "DrmSetDisplayPageFlip"

namespace intel {
namespace ufo {
namespace hwc {

// *****************************************************************************
// DrmSetDisplayPageFlipHandler
// *****************************************************************************

// Start SetDisplay option as eSetDisplayUnknown.
// When this is eSetDisplayUnknown a first-use test of the API will be made to determine the
// availability of the SetDisplay API - following which the option will be self-updated to
// eSetDisplayEnabled or eSetDisplayDisabled.
// Set this to eSetDisplayDisabled or eSetDisplayEnabled to force behaviour and skip the test.
Option DrmSetDisplayPageFlipHandler::sOptionSetDisplay("setdisplay", eSetDisplayUnknown, false);

DrmSetDisplayPageFlipHandler::DrmSetDisplayPageFlipHandler( DrmDisplay& display ) :
    mDisplay( display ),
    mDrm( Drm::get() )
{
    doInit();
}

DrmSetDisplayPageFlipHandler::~DrmSetDisplayPageFlipHandler( )
{
    doUninit();
}

bool DrmSetDisplayPageFlipHandler::test( DrmDisplay& display )
{
    // Check if result of test is already known.
    if ( sOptionSetDisplay.get() == eSetDisplayDisabled )
    {
        ALOGI( "Drm atomic API is disabled" );
        return false;
    }
    else if ( sOptionSetDisplay.get() == eSetDisplayEnabled )
    {
        ALOGI( "Drm atomic API is enabled" );
        return true;
    }

    // Test atomic API by making a NOP call.
    const uint32_t setDisplayBytes = sizeof( drm_mode_set_display );
    drm_mode_set_display setDisplay;
    memset( &setDisplay, 0, setDisplayBytes );

    setDisplay.version = DRM_MODE_SET_DISPLAY_VERSION;
    setDisplay.size = setDisplayBytes;
    setDisplay.crtc_id = display.getDrmCrtcID( );

    ALOGD( "Testing Drm atomic API" );

    int err = Drm::get().drmSetDisplay( setDisplay );
    if ( err == Drm::SUCCESS )
    {
        ALOGD( "Drm atomic API is available" );
        sOptionSetDisplay.set(eSetDisplayEnabled);
        return true;
    }
    else
    {
        Log::alogd( true, "DrmDisplay atomic API errored:0x%x [err:%s]", setDisplay.errored, strerror( -err ) );
    }

    ALOGD( "Drm atomic API is not available" );
    sOptionSetDisplay.set(eSetDisplayDisabled);
    return false;
}

void DrmSetDisplayPageFlipHandler::doInit()
{
    // One-shot set up of planes.
    const DisplayCaps& genCaps = mDisplay.getDisplayCaps( );
    const DrmDisplayCaps& drmCaps = mDisplay.getDrmDisplayCaps( );

    mNumPlanes = genCaps.getNumPlanes( );

    mMainPlaneIndex = -1;
    mbHaveMainPlaneDisable = drmCaps.isMainPlaneDisableSupported( );

    const uint32_t setDisplayBytes = sizeof( drm_mode_set_display );
    memset( &mSetDisplay, 0, setDisplayBytes );

    mSetDisplay.version = DRM_MODE_SET_DISPLAY_VERSION;
    mSetDisplay.size = setDisplayBytes;
    mSetDisplay.crtc_id = mDisplay.getDrmCrtcID( );
    mSetDisplay.num_planes = genCaps.getNumPlanes( );

    // Force ZOrder set (Z:0).
    mSetDisplay.update_flag |= DRM_MODE_SET_DISPLAY_UPDATE_ZORDER;

#if VPG_DRM_HAVE_PANEL_FITTER
    // Force panel fitter update (PFIT:OFF).
    const uint32_t w = mDisplay.getAppliedWidth( );
    const uint32_t h = mDisplay.getAppliedHeight( );

    mSetDisplay.update_flag |= DRM_MODE_SET_DISPLAY_UPDATE_PANEL_FITTER;
    mSetDisplay.panel_fitter.mode = DRM_PFIT_OFF;
    mSetDisplay.panel_fitter.src_w = w;
    mSetDisplay.panel_fitter.src_h = h;
    mSetDisplay.panel_fitter.dst_w = w;
    mSetDisplay.panel_fitter.dst_h = h;
#endif

    for ( uint32_t p = 0; p < mNumPlanes; ++p )
    {
        const DrmDisplayCaps::PlaneCaps& planeCaps = drmCaps.getPlaneCaps( p );
        drm_mode_set_display_plane& plane = mSetDisplay.plane[ p ];

        // Set plane object type and id.
        plane.obj_id = drmCaps.getPlaneCaps( p ).getDrmID();
        if ( planeCaps.getDrmPlaneType( ) == DrmDisplayCaps::PLANE_TYPE_SPRITE )
        {
            plane.obj_type = DRM_MODE_OBJECT_PLANE;
        }
        else
        {
            plane.obj_type = DRM_MODE_OBJECT_CRTC;
            // NOTE:
            // flip() implementation assumes main planes will always be at slot 0.
            ALOG_ASSERT( p == 0 );
            mMainPlaneIndex = p;
        }

        // Force Plane update (to disabled).
        mSetDisplay.update_flag |= DRM_MODE_SET_DISPLAY_UPDATE_PLANE( p );
        plane.update_flag |= DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT;
    }
}

void DrmSetDisplayPageFlipHandler::doUninit( void )
{
}

bool DrmSetDisplayPageFlipHandler::doFlip( DisplayQueue::Frame* pNewFrame, bool bMainBlanked, uint32_t flipEvData )
{
    // *************************************************************************
    // Panel fitter processing.
    // *************************************************************************

    mDisplay.issueGlobalScalingConfig( mSetDisplay, pNewFrame->getConfig().getGlobalScaling() );

    // *************************************************************************
    // ZOrder processing.
    // *************************************************************************
    const uint32_t zorder = pNewFrame->getZOrder();
    if ( mSetDisplay.zorder != zorder )
    {
        mSetDisplay.zorder = zorder;
        mSetDisplay.update_flag |= DRM_MODE_SET_DISPLAY_UPDATE_ZORDER;
        Log::alogd( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc:%d Pipe:%u ZOrder:%u,%s",
            mDisplay.getDrmCrtcID(), mDisplay.getDrmPipeIndex(),
            zorder, mDrm.zOrderToString( zorder ) );
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

    // Can we use sprites for the flip event request?
    // Only if
    //   1/ We don't have any main planes.
    //OR 2/ We have main plane but it is blanked (using the sprite for flip event will fully disable main).
    const bool bUseSpriteEv = !bHaveMainPlane || ( mbHaveMainPlaneDisable && bMainBlanked );

    bool bRequestedFlip = false;

    // The flip sprite index is 0 or 1 depending on whether we have a main plane.
    const uint32_t flipSpritePlane = bHaveMainPlane ? 1 : 0;

    // Plane processing is reversed so main is processed last.
    for( uint32_t p = mNumPlanes; p > 0; )
    {
        --p;

        // Get layer.
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
        bool bIsBlanking;
        if ( bMainPlane && bMainBlanked )
        {
            pLayer = &mDisplay.getBlankingLayer( );
            bIsBlanking = true;
        }
        else
        {
            bIsBlanking = false;
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

        ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " updatePlane %u flipEventData 0x%x, bRequestedFlip %d",
                  p, flipEventData, bRequestedFlip );

        if ( updatePlane( &mSetDisplay.plane[ p ], pLayer, flipEventData, &bRequestedFlip, bIsBlanking ) )
        {
            mSetDisplay.update_flag |= DRM_MODE_SET_DISPLAY_UPDATE_PLANE( p );
        }
    }

    // Must always request flip.
    ALOGE_IF( !bRequestedFlip, "Failed to issue flip event request for frame %s", pNewFrame->getFrameId().dump().string() );

    // Issue display update.
    mSetDisplay.errored = 0;
    mSetDisplay.presented = 0;

    if ( mMainPlaneIndex != -1 )
    {
        // NOTE:
        // The atomic API will fail if we try to modify the RRB2 state for a main plane, even if
        // just to ensure it's disabled. So clear the RRB2 update flag for the main plane.
        ALOG_ASSERT( !mSetDisplay.plane[ mMainPlaneIndex ].rrb2_enable );
        mSetDisplay.plane[ mMainPlaneIndex ].update_flag &= ~DRM_MODE_SET_DISPLAY_PLANE_UPDATE_RRB2;
    }

    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " %s", Drm::drmDisplayPipeToString( mSetDisplay ).string( ) );
    for ( uint32_t p = 0; p < mSetDisplay.num_planes; ++p )
    {
        ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME "     %s", Drm::drmDisplayPlaneToString( mSetDisplay, p ).string( ) );
    }

#if INTEL_HWC_INTERNAL_BUILD
    validateSetDisplay( );
#endif

    // Issue the atomic display update.
    bool bAtomicDisplayUpdateOK = false;

    bAtomicDisplayUpdateOK = ( mDrm.drmSetDisplay( mSetDisplay ) == Drm::SUCCESS );

    // Process succesfully issued update.
    if ( bAtomicDisplayUpdateOK )
    {
        // Finalise panel fitter update.
        if ( mSetDisplay.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PANEL_FITTER )
        {
            mDisplay.finalizeGlobalScalingConfig(pNewFrame->getConfig().getGlobalScaling() );
        }
        // Reset update flags.
        mSetDisplay.update_flag = 0;
        for ( uint32_t p = 0; p < mNumPlanes; ++p )
        {
            mSetDisplay.plane[p].update_flag &= ~DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT;
            // Only reset remaining flags if the plane is actually enabled.
            if ( mSetDisplay.plane[p].fb_id )
                mSetDisplay.plane[p].update_flag = 0;
        }
    }

    return bAtomicDisplayUpdateOK;
}

bool DrmSetDisplayPageFlipHandler::updatePlane( drm_mode_set_display_plane* pPlane,
                                            const Layer* pLayer,
                                            uint32_t flipEventData,
                                            bool* pbRequestedFlip,
                                            bool bIsBlanking )
{
    ALOG_ASSERT( pPlane );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc %u Update %s %u",
        mDisplay.getDrmCrtcID(), Drm::getObjectTypeString( pPlane->obj_type ), pPlane->obj_id );

    bool bDisable = false;

    const uint64_t fb = pLayer ? pLayer->getBufferDeviceId() : 0;

    // Set/reset flags/event callback.
    // We can only request flip event if we have an fb.
    if ( flipEventData && fb )
    {
        pPlane->user_data   = flipEventData;
        pPlane->flags       = DRM_MODE_PAGE_FLIP_EVENT;
    }
    else
    {
        pPlane->user_data   = 0ULL;
        pPlane->flags       = 0;
    }

    if ( pLayer )
    {
        // Update plane state from layer state.

        // Get buffer ID - skip update if no buffer change.
        const hwc_frect_t& src = pLayer->getSrc();
        const hwc_rect_t&  dst = pLayer->getDst();

        // Property: Alpha
        const bool bAlpha = pLayer->isBlend();

        // Property: RRB2
        const bool bRrb2 = pLayer->isEncrypted();

        // NOTE:
        // The layer's HWC/HAL transform must be converted to a Drm API transform.
        // Current Drm APIs only support ROT180.
        const ETransform hwcTransform = pLayer->getTransform();
        ALOG_ASSERT( ( hwcTransform == ETransform::NONE ) || ( hwcTransform == ETransform::ROT_180 ) );

        // Property: Transform
        const uint32_t drmTransform = ( hwcTransform == ETransform::ROT_180 ) ?
                                       DRM_MODE_SET_DISPLAY_PLANE_TRANSFORM_ROT180
                                     : DRM_MODE_SET_DISPLAY_PLANE_TRANSFORM_NONE;

        bool bChange =
            ( pPlane->update_flag != 0 )
         || ( pPlane->fb_id  != fb )
         || ( pPlane->alpha != bAlpha )
         || ( pPlane->rrb2_enable != bRrb2 )
         || ( pPlane->transform != drmTransform )
         || ( pPlane->crtc_x != pLayer->getDstX() )
         || ( pPlane->crtc_y != pLayer->getDstY() )
         || ( pPlane->crtc_w != pLayer->getDstWidth() )
         || ( pPlane->crtc_h != pLayer->getDstHeight() )
         || ( pPlane->src_x  != (uint32_t)floatToFixed16(pLayer->getSrcX()) )
         || ( pPlane->src_y  != (uint32_t)floatToFixed16(pLayer->getSrcY()) )
         || ( pPlane->src_w  != (uint32_t)floatToFixed16(pLayer->getSrcWidth()) )
         || ( pPlane->src_h  != (uint32_t)floatToFixed16(pLayer->getSrcHeight()) );

        if ( !bChange && !flipEventData )
        {
            Log::alogd( DRM_PAGEFLIP_DEBUG,
                    DRM_PFH_NAME " %5s %u H:%p%s%s TX:%d S:%.1f,%.1f,%.1fx%.1f F:%d,%d,%dx%d Skipped (No Change)",
                    Drm::getObjectTypeString( pPlane->obj_type ), pPlane->obj_id,
                    pLayer->getHandle(),
                    pLayer->isDisabled()                    ? ":DISABLE"   : "",
                    pLayer->isEncrypted()                   ? ":DECRYPT"   : "",
                    pLayer->getTransform(),
                    src.left, src.top, src.right - src.left, src.bottom - src.top,
                    dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top );
            return false;
        }

        if ( fb )
        {
            // We have a buffer to present.

            // Update presentation (flip).
            pPlane->update_flag |= DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT;
            if ( !pPlane->fb_id )
            {
                // Force an update all properties when a plane transitions from disabled->enabled.
                pPlane->update_flag |= ( DRM_MODE_SET_DISPLAY_PLANE_UPDATE_ALPHA
                                       | DRM_MODE_SET_DISPLAY_PLANE_UPDATE_RRB2
                                       | DRM_MODE_SET_DISPLAY_PLANE_UPDATE_TRANSFORM );
            }

            // Update plane state for this flip.

            uint32_t drmFlags = flipEventData ? DRM_MODE_PAGE_FLIP_EVENT : 0;

            pPlane->fb_id       = fb;
            pPlane->crtc_x      = pLayer->getDstX();
            pPlane->crtc_y      = pLayer->getDstY();
            pPlane->crtc_w      = pLayer->getDstWidth();
            pPlane->crtc_h      = pLayer->getDstHeight();
            pPlane->src_x       = floatToFixed16(pLayer->getSrcX());
            pPlane->src_y       = floatToFixed16(pLayer->getSrcY());
            pPlane->src_w       = floatToFixed16(pLayer->getSrcWidth());
            pPlane->src_h       = floatToFixed16(pLayer->getSrcHeight());
            pPlane->user_data   = flipEventData;
            pPlane->flags       = drmFlags;

            ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc %u  fb -> %d src:%.2f,%.2f %.2fx%.2f -> dst:%d,%d %dx%d ud:0x%llx",
                mDisplay.getDrmCrtcID(),
                pPlane->fb_id,
                fixed16ToFloat(pPlane->src_x), fixed16ToFloat(pPlane->src_y),
                fixed16ToFloat(pPlane->src_w), fixed16ToFloat(pPlane->src_h),
                pPlane->crtc_x, pPlane->crtc_y, pPlane->crtc_w, pPlane->crtc_h,
                pPlane->user_data );

            if ( pPlane->flags & DRM_MODE_PAGE_FLIP_EVENT )
            {
                *pbRequestedFlip = true;
            }

            // Update properties.

            if ( pPlane->alpha != bAlpha )
            {
                // Update alpha.
                pPlane->update_flag |= DRM_MODE_SET_DISPLAY_PLANE_UPDATE_ALPHA;
                pPlane->alpha = bAlpha;
                ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc %u  alpha -> %d", mDisplay.getDrmCrtcID(), bAlpha );
            }

            if ( pPlane->rrb2_enable != bRrb2 )
            {
                // Update RRB2.
                pPlane->update_flag |= DRM_MODE_SET_DISPLAY_PLANE_UPDATE_RRB2;
                pPlane->rrb2_enable = bRrb2;
                ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc %u  rrb2 -> %d", mDisplay.getDrmCrtcID(), bRrb2 );
            }

            if ( pPlane->transform != drmTransform )
            {
                // Update transform.
                pPlane->update_flag |= DRM_MODE_SET_DISPLAY_PLANE_UPDATE_TRANSFORM;
                pPlane->transform = drmTransform;
                ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Crtc %u  transform -> %d", mDisplay.getDrmCrtcID(), drmTransform );
            }

            Log::alogd( DRM_PAGEFLIP_DEBUG,
                    DRM_PFH_NAME " %5s %u H:%p%s%s TX:%d S:%.1f,%.1f,%.1fx%.1f F:%d,%d,%dx%d %s%s",
                    Drm::getObjectTypeString( pPlane->obj_type ), pPlane->obj_id,
                    pLayer->getHandle(),
                    pLayer->isDisabled()                    ? ":DISABLE"   : "",
                    pLayer->isEncrypted()                   ? ":DECRYPT"   : "",
                    pLayer->getTransform(),
                    src.left, src.top, src.right - src.left, src.bottom - src.top,
                    dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                    drmFlags & DRM_MODE_PAGE_FLIP_EVENT     ? ":FLIPEVENT" : "",
                    bIsBlanking                             ? ":BLANKING" : "" );


            return true;
        }
        else
        {
            // We have set a NULL buffer => disable.
            bDisable = true;
        }
    }
    else
    {
        // We have no layer => disable.
        bDisable = ( pPlane->fb_id != 0 );
    }

    if ( bDisable )
    {
        // Clear state.
        pPlane->fb_id       = 0;
        pPlane->crtc_x      = 0;
        pPlane->crtc_y      = 0;
        pPlane->crtc_w      = 0;
        pPlane->crtc_h      = 0;
        pPlane->src_x       = 0;
        pPlane->src_y       = 0;
        pPlane->src_w       = 0;
        pPlane->src_h       = 0;
        pPlane->user_data   = 0;
        pPlane->flags       = 0;
        pPlane->alpha       = 0;
        pPlane->rrb2_enable = 0;
        pPlane->transform   = 0;
        // Update presentation (disable).
        pPlane->update_flag |= DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT;
        Log::alogd( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Plane %u Disabled", pPlane->obj_id );

        return true;
    }

    Log::alogd( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Plane %u Disabled (No Change)", pPlane->obj_id );
    return false;
}

#if INTEL_HWC_INTERNAL_BUILD
void DrmSetDisplayPageFlipHandler::validateSetDisplay( void )
{
    // Run some sanity checks.
    bool bHaveFlip = false;
    for( uint32_t p = 0; p < mNumPlanes; ++p )
    {
        if ( mSetDisplay.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE( p ) )
        {
            // If plane is marked for update then we must have some plane-specific state flagged.
            if ( !mSetDisplay.plane[p].update_flag )
            {
                ALOGE( "Plane %u has plane update flag set but no plane-specific dirty bits set", p );
                ALOG_ASSERT( false );
            }
            // If plane flags include flip event then we must have user data and also presentation flag.
            if ( mSetDisplay.plane[p].flags & DRM_MODE_PAGE_FLIP_EVENT )
            {
                if ( mSetDisplay.plane[p].user_data == 0ULL )
                {
                    ALOGE( "Plane %u has DRM_MODE_PAGE_FLIP_EVENT set but user data is not set", p );
                    ALOG_ASSERT( false );
                }
                if ( !(mSetDisplay.plane[p].update_flag & DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT ) )
                {
                    ALOGE( "Plane %u has DRM_MODE_PAGE_FLIP_EVENT set but presentation flag is not set", p );
                    ALOG_ASSERT( false );
                }
                bHaveFlip = true;
            }
        }
        else
        {
            // If plane is not marked for update then we must not have any plane-specific update flag.
            if ( mSetDisplay.plane[p].update_flag & DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT )
            {
                ALOGE( "Plane %u is not flagged for update but has plane-specific update flag set", p );
                ALOG_ASSERT( false );
            }
            // If plane is not marked for update then if it is enabled then we must not have any plane-specific state flaggged.
            if ( mSetDisplay.plane[p].fb_id && ( mSetDisplay.plane[p].update_flag & ~DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT ) )
            {
                ALOGE( "Plane %u is enabled but is not flagged for update and has plane-specific dirty bits set", p );
                ALOG_ASSERT( false );
            }
            if ( mSetDisplay.plane[p].flags & DRM_MODE_PAGE_FLIP_EVENT )
            {
                ALOGE( "Plane %u is not flagged for update but has DRM_MODE_PAGE_FLIP_EVENT set", p );
                ALOG_ASSERT( false );
            }
            if ( mSetDisplay.plane[p].user_data != 0ULL )
            {
                ALOGE( "Plane %u is not flagged for update but has user_data set", p );
                ALOG_ASSERT( false );
            }
        }
    }
    if ( !bHaveFlip )
    {
        // This can occur if we have no fbs.
        ALOGE( "Did not set DRM_MODE_PAGE_FLIP_EVENT for any active presented plane" );
    }
}
#endif

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif
