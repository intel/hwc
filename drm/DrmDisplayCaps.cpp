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
#include "Drm.h"
#include "DrmDisplayCaps.h"
#include "DrmFormatHelper.h"
#include "Log.h"
#include "DisplayState.h"

namespace intel {
namespace ufo {
namespace hwc {


DrmDisplayCaps::PlaneCaps::PlaneCaps() :
    meDrmPlaneType(PLANE_TYPE_UNKNOWN),
    mDrmID( -1 ),
    mpDisplayPlaneCaps(NULL)
{
}

DrmDisplayCaps::PlaneCaps::~PlaneCaps()
{
}

void DrmDisplayCaps::PlaneCaps::probe( EDrmPlaneType eDrmPlaneType, uint32_t drmID,
                            uint32_t pipeIndex, uint32_t numFormats, const int32_t* pFormats )
{
    meDrmPlaneType = eDrmPlaneType;
    mDrmID = drmID;
    mpDisplayPlaneCaps->setName(String8::format("Drm%d:%d", pipeIndex, drmID));
    static const ETransform transforms[] =
    {
        ETransform::NONE,
    };
    mpDisplayPlaneCaps->setTransforms( DISPLAY_CAPS_COUNT_OF( transforms ), transforms );
    mpDisplayPlaneCaps->setDisplayFormats( numFormats, pFormats );

}

void DrmDisplayCaps::PlaneCaps::setDisplayPlaneCaps(DisplayCaps::PlaneCaps* pCaps)
{
    ALOG_ASSERT(pCaps);
    mpDisplayPlaneCaps = pCaps;
}

DrmDisplayCaps::DrmDisplayCaps() :
    mpDisplayCaps( NULL ),
    mCrtcID( -1 ),
    mPipeIndex( -1 ),
    mbCapFlagMainPlaneDisable(false),
    mbCapFlagAsyncDPMS(false),
    mbCapFlagZOrder(false),
    mbCapFlagScreenControl(false),
    mbCapFlagPanelFitter(false),
    mbCapFlagPowerManager(false),
    mbCapFlagSelfRefresh(false),
    mbCapFlagSpriteTxRot(false),
    mbUniversalPlanes(false)
{
}

void DrmDisplayCaps::probe( uint32_t crtcID, uint32_t pipeIndex, uint32_t connectorID, DisplayCaps* pCaps )
{
    mCrtcID = crtcID;
    mPipeIndex = pipeIndex;
    delete mpDisplayCaps;
    mpDisplayCaps = pCaps;

    DisplayCaps& caps = *pCaps;

    // Configure Drm display caps.
    // NOTE:
    //   These are currently fixed based on build-time switches.
    // TODO:
    //   Implement a run-time mechanism for these.
#if VPG_DRM_HAVE_MAIN_PLANE_DISABLE
    mbCapFlagMainPlaneDisable = true;
#endif
#if VPG_DRM_HAVE_ASYNC_DPMS
    mbCapFlagAsyncDPMS = true;
#endif
#if VPG_DRM_HAVE_ZORDER_API
    mbCapFlagZOrder = true;
#endif
#if VPG_DRM_HAVE_SCREEN_CTL
    mbCapFlagScreenControl = true;
#endif
#if VPG_DRM_HAVE_PANEL_FITTER
    mbCapFlagPanelFitter = true;
#endif
#if VPG_DRM_HAVE_POWERMANAGER
    mbCapFlagPowerManager = true;
#endif
#if VPG_DRM_HAVE_PSR
    mbCapFlagSelfRefresh = true;
#endif
#if VPG_DRM_HAVE_TRANSFORM_180 // or any other rotations when become available
    mbCapFlagSpriteTxRot = true;
#endif

    caps.setName(String8::format("Drm%d", pipeIndex));

    // Empty out all planes first (to support re-probe).
    mPlanes.clear();

    // Set up all state that can be probed from DRM
    // Universal planes are only supported on newer libdrm implementations
    mbUniversalPlanes = Drm::get().useUniversalPlanes();
    if (!mbUniversalPlanes)
    {
        // If universal planes arnt supported by kernel, then we need to explictly add the main plane
        addMainPlane(caps);
    }

    addSpritePlanes(caps);

    caps.probe();

    if (Drm::get().useNuclear())
    {
        // Nuclear does not support Z-order or panel fitter at present.
        mbCapFlagZOrder = false;
        caps.editGlobalScalingCaps().setFlags(0);
    }
    else
    {
        // The legacy page flip paths have a minimum 4x4 src size for fb creation
        for ( uint32_t s = 1; s < caps.getNumPlanes(); ++s )
        {
            DisplayCaps::PlaneCaps& p = caps.editPlaneCaps( s );
            p.setMinSourceWidth( 4 );
            p.setMinSourceHeight( 4 );
        }
    }

    // Now is the time to disable any hardware features that the DRM subsystem cannot support

    if (!mbCapFlagMainPlaneDisable)
    {
        // This means that plane 0's disable state isnt supported in DRM
        caps.editPlaneCaps( 0 ).enableDisable(false);
    }

    if (!isZOrderSupported())
    {
        // Empty the z order lut
        Vector<DisplayCaps::ZOrderLUTEntry> emptyLUT;
        caps.setZOrderLUT(emptyLUT);
    }

    if (!isSpriteTxRotSupported())
    {
        for ( uint32_t s = 1; s < caps.getNumPlanes(); ++s )
        {
            DisplayCaps::PlaneCaps& p = caps.editPlaneCaps( s );
            // Override transforms to NONE
            static const ETransform transforms[] =
            {
                ETransform::NONE,
            };
            p.setTransforms( DISPLAY_CAPS_COUNT_OF( transforms ), transforms );
        }
    }

    // Enable panel fitter only when we have both panel fitter mode and source size properties.
    uint32_t panelFitterModePropId = Drm::get().getPanelFitterPropertyID( connectorID );
    uint32_t panelFitterSourceSizePropId = Drm::get().getPanelFitterSourceSizePropertyID( connectorID );
    DisplayCaps::GlobalScalingCaps& globalScalingCaps = caps.editGlobalScalingCaps();
    uint32_t globalScalingFlags =globalScalingCaps.getFlags();

    // Bitmask of pipes for which Drm supports panel fitter (BIT0=>Pipe0)
    // Enable for first pipe only due to issue with using other pipes (seen on CHT FFD OAM)
    // JIRA: https://jira01.devtools.intel.com/browse/OAM-9753
    // JIRA: https://jira01.devtools.intel.com/browse/VAH-214
    Option optionDrmPfitPipes( "drmpfitpipes", 1 );

    if ( ( panelFitterModePropId == Drm::INVALID_PROPERTY )
      || ( panelFitterSourceSizePropId == Drm::INVALID_PROPERTY )
      || ( ( optionDrmPfitPipes & (1<<pipeIndex) ) == 0 ) )
    {
        globalScalingFlags &= ~DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_SUPPORTED;
    }
#ifndef VPG_DRM_HAVE_PANEL_FITTER_MANUAL
    globalScalingFlags &= ~DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_WINDOW;
#endif
    globalScalingCaps.setFlags( globalScalingFlags );


    // Probe Drm active display state.
    DisplayState* pState = caps.editState();
    if ( pState )
    {
        pState->setNumActiveDisplays( Drm::get().getNumActiveDisplays() );
    }

    caps.dump();
}

void DrmDisplayCaps::addMainPlane( DisplayCaps& caps )
{
    // Default supported main plane display formats.
    static const int32_t defaultMainPlaneFormats[] =
    {
        HAL_PIXEL_FORMAT_RGBA_8888,
        HAL_PIXEL_FORMAT_RGBX_8888,
        HAL_PIXEL_FORMAT_RGB_565,
        HAL_PIXEL_FORMAT_BGRA_8888
    };

    mPlanes.resize(1);
    PlaneCaps& p = mPlanes.editItemAt(0);
    p.setDisplayPlaneCaps(caps.createPlane(0));
    p.probe( PLANE_TYPE_MAIN, mCrtcID, mPipeIndex,
             DISPLAY_CAPS_COUNT_OF( defaultMainPlaneFormats ), defaultMainPlaneFormats );
    caps.add( p.getDisplayPlaneCaps() );
    return;
}

void DrmDisplayCaps::addSpritePlanes( DisplayCaps& caps )
{
    // Enumerate Drm sprite planes.
    Drm& drm = Drm::get( );
    drmModePlaneRes *pPlaneResources = drm.getPlaneResources( );
    if ( pPlaneResources == NULL)
        return;

    if (pPlaneResources->planes)
    {
        for ( uint32_t ov = 0, ovIndex = caps.getNumPlanes(); ov < pPlaneResources->count_planes; ov++ )
        {
            drmModePlane *pDrmPlane = drm.getPlane( pPlaneResources->planes[ov] );
            if(pDrmPlane)
            {
                bool bSupportedPlaneType = true;

#if defined(DRM_CLIENT_CAP_UNIVERSAL_PLANES)
                if (mbUniversalPlanes)
                {
                    // Universal planes (and hence plane types) are only supported on newer libdrm implementations
                    uint32_t propType = drm.getPlanePropertyID(pDrmPlane->plane_id, "type");
                    uint64_t planeType = DRM_PLANE_TYPE_OVERLAY;
                    if (drm.getPlaneProperty(pDrmPlane->plane_id, propType, &planeType) == Drm::SUCCESS)
                    {
                        switch (planeType)
                        {
                            case DRM_PLANE_TYPE_CURSOR :
                                ALOGD_IF(DRM_DEBUG, "DRM_PLANE_TYPE_CURSOR ");
                                bSupportedPlaneType = false; // TODO: Add cursor plane support
                                break;
                            case DRM_PLANE_TYPE_OVERLAY:
                                ALOGD_IF(DRM_DEBUG, "DRM_PLANE_TYPE_OVERLAY");
                                break;
                            case DRM_PLANE_TYPE_PRIMARY:
                                ALOGD_IF(DRM_DEBUG, "DRM_PLANE_TYPE_PRIMARY");
                                break;
                            default: ALOGD_IF(DRM_DEBUG, "UNKNOWN_PLANE_TYPE(%" PRIu64")", planeType); break;
                        }
                    }
                    else
                    {
                        ALOGE("getPlaneProperty(""type"", %d) FAILED", pDrmPlane->plane_id);
                    }
                }
#endif

                if ( bSupportedPlaneType &&
                     pDrmPlane->possible_crtcs & (1<<mPipeIndex) && pDrmPlane->formats)
                {

                    LOG_DISPLAY_CAPS( ( "DRM PlaneID %u, CrtcID %u, FB %u, Crtc[ %d,%d ] Plane[ %d,%d ] possible crts=0x%x",
                         pDrmPlane->plane_id, pDrmPlane->crtc_id, pDrmPlane->fb_id,
                         pDrmPlane->crtc_x, pDrmPlane->crtc_y,
                         pDrmPlane->x, pDrmPlane->y,
                         pDrmPlane->possible_crtcs ) );

                    // Convert the DRM formats into HAL formats
                    Vector<int32_t> halFormats;
                    for (uint32_t i = 0; i < pDrmPlane->count_formats; i++)
                    {
                        int halFormat = convertDrmFormatToHalFormat(pDrmPlane->formats[i]);
                        if (halFormat > 0)
                            halFormats.push(halFormat);

                        // There are several NV12 internal formats:
                        //    HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL       = 0x100
                        //    HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL        = 0x101
                        //    HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL = 0x103
                        //    HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL       = 0x105
                        //    HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL = 0x10F
                        //
                        // If this plane supports NV12 format, we think it should support above internal formats.
                        if (pDrmPlane->formats[i] == DRM_FORMAT_NV12)
                        {
                            halFormats.push(HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL);
                            halFormats.push(HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL);
                            halFormats.push(HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL);
                            halFormats.push(HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL);
                        }
                    }

                    PlaneCaps p;
                    p.setDisplayPlaneCaps(caps.createPlane(ovIndex++));
                    p.probe( PLANE_TYPE_SPRITE, pDrmPlane->plane_id, mPipeIndex, halFormats.size(), halFormats.array() );
                    caps.add( p.getDisplayPlaneCaps() );
                    mPlanes.push_back(p);
                }
                drmModeFreePlane( pDrmPlane );
            }
        }
    }
    drm.freePlaneResources(pPlaneResources);

    return;
}


String8 DrmDisplayCaps::displayCapsString( void ) const
{
    String8 str( "" );
#define CHECK_LOG_CAP( C ) if ( mbCapFlag##C ) {    \
        if ( str.length( ) )                        \
            str += "|";                             \
        str += #C ; }
    CHECK_LOG_CAP( MainPlaneDisable )
    CHECK_LOG_CAP( AsyncDPMS )
    CHECK_LOG_CAP( ZOrder )
    CHECK_LOG_CAP( ScreenControl )
    CHECK_LOG_CAP( PanelFitter )
    CHECK_LOG_CAP( PowerManager )
    CHECK_LOG_CAP( SelfRefresh )
    CHECK_LOG_CAP( SpriteTxRot )
#undef CHECK_LOG_CAP
    if ( str.length( ) == 0 )
        str = "N/A";
    return str;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
