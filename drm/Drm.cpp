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

#include "Hwc.h"
#include "Drm.h"
#include "DrmDisplay.h"
#include "DrmEventThread.h"
#include "DrmUEventThread.h"
#include "HwcService.h"
#include "Log.h"
#include "AbstractPlatform.h"
#include <cutils/properties.h>
#include <i915_drm.h>       //< For PASASBCA/DRM_PFIT_PROP/DRM_PRIMARY_DISABLE (if available)
#include <drm_fourcc.h>

namespace intel {
namespace ufo {
namespace hwc {


// TODO:
// These should be available in drm headers at some point.
#define DRM_DPMS_PROP "DPMS"
#define DRM_DRRS_PROP "drrs_capability"

#define DRM_PROBE_DEBUG ( HPLUG_DEBUG || MODE_DEBUG )

#if !defined(DRM_MODE_CONNECTOR_DSI)
// Currently needed for GMIN builds where libdrm doesnt define this
#define DRM_MODE_CONNECTOR_DSI 16
#endif

#if !defined(DRM_MODE_FB_AUX_PLANE)
#define DRM_MODE_FB_AUX_PLANE (1<<2)
#endif

#if !defined(DRM_CAP_RENDER_COMPRESSION)
#define DRM_CAP_RENDER_COMPRESSION 0x11
#endif

Drm::Drm() :
    mOptionPanel("panel", 1),
    mOptionExternal("external", 1),
    mOptionDisplayInternal("display0", ""),
    mOptionDisplayExternal("display1", ""),
    mpHwc( NULL ),
    mDrmFd(-1),
    mAcquiredPanelFitters(0ULL),
    mAcquiredCrtcs(0),
    mAcquiredPipes(0),
    mActiveDisplays(0),
    mActiveDisplaysMask(0),
    mbRegisterWithHwc(true),
    mbCapNuclear(false),
    mbCapUniversalPlanes(false),
    mbCapRenderCompression(false),
    mpModeRes(NULL)
{
    for (uint32_t i = 0; i < cMaxSupportedPhysicalDisplays; i++)
        mDisplay[i] = NULL;

    mDrmFd = AbstractPlatform::getDrmHandle();
    LOG_ALWAYS_FATAL_IF( mDrmFd == -1, "Unable to open private DRM handle");

#if defined(DRM_CLIENT_CAP_UNIVERSAL_PLANES) && defined(DRM_CLIENT_CAP_ATOMIC)
    // We currently only want to use universal planes on a kernel that supports drm atomic
    if (setClientCap(DRM_CLIENT_CAP_ATOMIC, 1) == 0)
    {
        mbCapNuclear = true;
    }

    if (mbCapNuclear)
    {
        if (setClientCap(DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) == 0)
        {
            mbCapUniversalPlanes = true;
            ALOGI( "Detected DMR/KMS Nuclear and Universal Plane support" );

        }
        else
        {
            // Disable atomic caps if universal isnt supported
            setClientCap(DRM_CLIENT_CAP_ATOMIC, 0);
            mbCapNuclear = false;
        }
    }
#endif

#if defined(DRM_CAP_RENDER_COMPRESSION)
    uint64_t value = 0;
    if (getCap(DRM_CAP_RENDER_COMPRESSION, value) == Drm::SUCCESS)
        mbCapRenderCompression = (value != 0);
#endif
    ALOGI( "%s DMR/KMS Render Compression support", mbCapRenderCompression ? "Detected" : "NOT AVAILABLE:" );
}

Drm::~Drm()
{
    if (mpModeRes)
    {
        freeResources( mpModeRes );
    }
}

void Drm::init(Hwc& hwc)
{
    mpHwc = &hwc;
    mpEventThread = new DrmEventThread();
    if (mpEventThread == NULL)
    {
        ALOGE("Composer::Drm Failed to initialize DrmEventThread.");
        return;
    }

    mpUEventThread = new DrmUEventThread(hwc, *this);
    if (mpUEventThread == NULL)
    {
        ALOGE("Composer::Drm Failed to initialize DrmUEventThread.");
        return;
    }

    return;
}

bool Drm::broadcastNumActiveDisplays( void )
{
    ALOG_ASSERT( mpHwc );
    // If there are *ANY* receivers then let the caller know.
    bool bReceiver = false;
    // TODO:
    //  Make this a generic notification.
    for ( uint32_t d = 0; d < cMaxSupportedPhysicalDisplays; ++d )
    {
        DrmDisplay* pDisplay = getDrmDisplay( d );
        if (pDisplay == NULL)
            continue;
        if ( pDisplay->notifyNumActiveDisplays( mActiveDisplays ) )
        {
            bReceiver = true;
        }
    }
    return bReceiver;
}

bool Drm::setActiveDisplay( uint32_t drmDisplay, bool bActive )
{
    ALOG_ASSERT( mpHwc );

    ALOGD_IF( DRM_PROBE_DEBUG || HPLUG_DEBUG, "Drm setActiveDisplay %u active %d", drmDisplay, bActive );

    Mutex::Autolock _l( mLockForCrtcMask );

    bool bSynchronize = false;
    bool bChange = false;
    uint32_t mask = mActiveDisplaysMask;
    uint32_t active = 0;

    if ( bActive )
    {
        mask |= (1<<drmDisplay);
    }
    else
    {
        mask &= ~(1<<drmDisplay);
    }

    if ( mActiveDisplaysMask != mask )
    {
        mActiveDisplaysMask = mask;
        bChange = true;
    }

    while ( mask )
    {
        if ( mask&1 )
        {
            ++active;
        }
        mask >>= 1;
    }

    if ( mActiveDisplays != active )
    {
        mActiveDisplays = active;
        bChange = true;
    }

    // If there is a change and at least one component that cares, then the caller should synchronize.
    if ( bChange && broadcastNumActiveDisplays() )
    {
        bSynchronize = true;
    }

    ALOGD_IF( DRM_PROBE_DEBUG || HPLUG_DEBUG, "Drm setActiveDisplay 0x%x (x%u) sync:%d",
        mActiveDisplaysMask, mActiveDisplays, bSynchronize );

    if ( bChange )
    {
        // Force update on any change.
        mpHwc->forceGeometryChangeAndRedraw();
    }

    return bSynchronize;
}

/** process hot plug event from hot-plug handler
 */
void Drm::onHotPlugEvent(UEvent eHPE)
{
    // Set of displays being plugged/unplugged (bit0=>display0).
    ALOG_ASSERT( cMaxSupportedPhysicalDisplays <= 32 );
    uint32_t plug = 0;
    uint32_t unplug = 0;

    for ( uint32_t display = 0; display < cMaxSupportedPhysicalDisplays; ++display )
    {
        DrmDisplay* pDisplay = getDrmDisplay(display);
        if (pDisplay == NULL)
            continue;

        // Run through all our displays and deliver the hotplug event to them if they claim to be hotpluggable
        if ( pDisplay->getDisplayType() == eDTExternal )
        {
            Log::alogd( HPLUG_DEBUG, "Drm HotPlugEvent to hotpluggable D%d(%s) Previously:%s Event:%u(%s)",
                display, pDisplay->getName(),
                pDisplay->isDrmConnected() ? "Connected" : "Disconnected",
                eHPE, UEventToString( eHPE ) );

            // The incoming event type (eHPE) is ignored.
            // Instead poll the display to discover the actual current status right now.
            UEvent ev = pDisplay->onHotPlugEvent( );

            // NOTE:
            //  A reconnect may be generated if the mode list changes.
            //  This is decomposed into an unplug/plug pair.
            if (  ( ev == UEvent::HOTPLUG_CONNECTED ) || ( ev == UEvent::HOTPLUG_RECONNECT ) )
            {
                plug |= (1<<display);
            }
            if (  ( ev == UEvent::HOTPLUG_DISCONNECTED ) || ( ev == UEvent::HOTPLUG_RECONNECT ) )
            {
                unplug |= (1<<display);
            }
        }
    }

    // Process unplugs first so that all resources are guaranteed to be released first.
    for ( uint32_t display = 0; display < cMaxSupportedPhysicalDisplays; ++display )
    {
        DrmDisplay* pDisplay = getDrmDisplay(display);
        if (pDisplay == NULL)
            continue;

        if ( unplug & (1<<display) )
        {
            pDisplay->issueHotUnplug( );
        }
    }

    // Process plugs.
    for ( uint32_t display = 0; display < cMaxSupportedPhysicalDisplays; ++display )
    {
        DrmDisplay* pDisplay = getDrmDisplay(display);
        if (pDisplay == NULL)
            continue;

        if ( plug & (1<<display) )
        {
            pDisplay->issueHotPlug( );
        }
    }

    return;
}

void Drm::onESDEvent( UEvent eEvent, uint32_t connectorID, uint32_t connectorType)
{
    if( eEvent != UEvent::ESD_RECOVERY )
    {
        ALOGE("Drm ESDEvent: skip since [%s] was passed into!", UEventToString(eEvent) );
        return;
    }

    for ( uint32_t display = 0; display < cMaxSupportedPhysicalDisplays; ++display )
    {
        DrmDisplay* pDisplay = getDrmDisplay(display);
        if ( pDisplay == NULL )
        {
            continue;
        }
        Log::alogd( HPLUG_DEBUG, "Drm ESDEvent to D%d(%s) Connect %d: %s(%d) ", display, pDisplay->getName(), connectorID, UEventToString(eEvent), eEvent );

        // Run through all our displays and deliver the ESD event to the specified one
        if ( ( pDisplay->getDrmConnectorType() == connectorType )
            &&( pDisplay->getDrmConnectorID() == connectorID ) )
        {
            pDisplay->onESDEvent(eEvent);
            break;
        }
    }

    return;
}


void Drm::disableHwcRegistration( void )
{
    mbRegisterWithHwc = false;
}

bool Drm::acquirePipe(uint32_t possible_crtcs, uint32_t& crtc_id, uint32_t& pipe_idx)
{
    Mutex::Autolock _l( mLockForCrtcMask );
    ALOG_ASSERT( mpModeRes );

    ALOGD_IF( DRM_PROBE_DEBUG || HPLUG_DEBUG, "Acquiring pipe from possible set 0x%x [Crtcs acquired mask 0x%x]", possible_crtcs, mAcquiredCrtcs );

    for (int i = 0; i < mpModeRes->count_crtcs; i++)
    {
        if ( ( possible_crtcs & (1 << i) ) && !( mAcquiredCrtcs & (1 << i) ) )
        {
            pipe_idx = i;
            crtc_id = mpModeRes->crtcs[pipe_idx];
            mAcquiredCrtcs |= (1 << i);
            ++mAcquiredPipes;
            ALOGD_IF( DRM_PROBE_DEBUG, "Acquired PipeIdx:%u CrtcID:%u. [Acquired Pipes %u, Crtcs Mask 0x%x]",
                pipe_idx, crtc_id, mAcquiredPipes, mAcquiredCrtcs );
            return true;
        }
    }
    ALOGW( "No pipes available [Crtcs acquired mask 0x%x]", mAcquiredCrtcs );
    return false;
}

void Drm::releasePipe(uint32_t pipe_idx)
{
    Mutex::Autolock _l( mLockForCrtcMask );
    ALOG_ASSERT( mpModeRes );
    ALOG_ASSERT( pipe_idx < (uint32_t)mpModeRes->count_crtcs );
    ALOG_ASSERT( mAcquiredCrtcs & (1 << pipe_idx) );
    ALOG_ASSERT( mAcquiredPipes > 0 );
    mAcquiredCrtcs &= ~(1 << pipe_idx);
    --mAcquiredPipes;
    ALOGD_IF( DRM_PROBE_DEBUG || HPLUG_DEBUG, "Released PipeIdx:%u. [Acquired Pipes %u, Crtcs Mask 0x%x]",
        pipe_idx, mAcquiredPipes, mAcquiredCrtcs );
}

int Drm::probe(Hwc& hwc)
{
    if (mpEventThread == NULL || mpUEventThread == NULL)
        return BAD_VALUE;

    ALOG_ASSERT( mpHwc );

    // Get the Drm mode resources and search for any appropriate
    // Update mode resource for every probe call
    if (mpModeRes)
    {
        freeResources( mpModeRes );
    }
    mpModeRes = getResources( );
    if (mpModeRes == NULL || mpModeRes->connectors == NULL) {
        ALOGE("probe FAILED to get modeset resources");
        return BAD_VALUE;
    }

    uint32_t displayIndex = 0;
    uint32_t internalIndex = 0;
    for (uint32_t i = 0; i < (uint32_t)mpModeRes->count_connectors; i++)
    {
        drmModeConnectorPtr pConnector = getConnector( mpModeRes->connectors[i] );
        if (pConnector == NULL)
        {
            ALOGI_IF( DRM_PROBE_DEBUG, "Invalid connector");
            continue;
        }

        // Skip supported but disabled internal display types.
        bool bIsInternal = isSupportedInternalConnectorType( pConnector->connector_type );
        if (bIsInternal && mOptionPanel == 0)
        {
            ALOGI_IF( DRM_PROBE_DEBUG, "DrmDisplay::probe() Skipping disabled internal connector type.");
            freeConnector( pConnector );
            continue;
        }

        // Skip supported but disabled external display types.
        bool bIsExternal = isSupportedExternalConnectorType( pConnector->connector_type );
        if (bIsExternal && mOptionExternal == 0)
        {
            ALOGI_IF( DRM_PROBE_DEBUG, "DrmDisplay::probe() Skipping disabled external connector type.");
            freeConnector( pConnector );
            continue;
        }

        ALOGD_IF( DRM_PROBE_DEBUG, "Opening display %d", displayIndex);
        DrmDisplay* pDisplay = new DrmDisplay( hwc, displayIndex );
        if (pDisplay == NULL)
        {
            freeConnector( pConnector );
            continue;
        }

        if (pDisplay->open(pConnector, mbRegisterWithHwc) == SUCCESS)
        {
            // The display now owns the connector allocation and its responsible for calling freeConnector
            if (bIsInternal)
            {
                // Insert internal displays ahead of external displays
                for (uint32_t tmp = displayIndex; tmp > internalIndex; tmp--)
                    mDisplay[tmp] = mDisplay[tmp-1];
                mDisplay[internalIndex] = pDisplay;
                internalIndex++;
            }
            else
            {
                mDisplay[displayIndex] = pDisplay;
            }
            displayIndex++;
        }
        else
        {
            delete pDisplay;
            freeConnector( pConnector );
        }
    }

    // Register all devices.
    // Plug connected.
    ALOGD_IF( DRM_PROBE_DEBUG, "DrmDisplay::probe() New mapping:" );
    PhysicalDisplayManager& pdm = hwc.getPhysicalDisplayManager( );
    for (uint32_t d = 0; d < cMaxSupportedPhysicalDisplays; d++)
    {
        if (mDisplay[d] != NULL)
        {
            mDisplay[d]->setDrmDisplayID(d);

            if ( mbRegisterWithHwc )
            {

                // TODO:
                // We may want to consider how we push priority out to the LogicalDisplayManager.
                // i.e. Currently we let Drm acquire pipes on a first come basis and only
                // make displays available once a pipe is acquired.

                // Add first N if connected.
                if (pdm.registerDisplay(mDisplay[d]) && ( mDisplay[d]->isDrmConnected()))
                {
                    // If the connector is connected, try to find a available pipe for it
                    uint32_t crtc_id, pipe_idx;
                    if ( acquirePipe( mDisplay[d]->getPossibleCrtcs(), crtc_id, pipe_idx ) )
                    {
                        ALOGD_IF ( DRM_PROBE_DEBUG,
                                   "Found an available pipe for physical display %u, crtc_id: %u, pipe_idx: %u",
                                   d, crtc_id, pipe_idx);

                        // Start the dipslay.
                        mDisplay[d]->start(crtc_id, pipe_idx);

                        // Plug the display if its in a valid SurfaceFlinger range, otherwise notify its available
                        pdm.notifyPhysicalAvailable(mDisplay[d]);
                    }
                    else
                    {
                        ALOGD_IF ( DRM_PROBE_DEBUG, "No available pipe found for display %u", d);
                    }
                }
            }
            // Summary log
            Timing initialTiming;
            if ( mDisplay[d]->isDrmConnected() )
            {
                mDisplay[d]->getTiming( initialTiming );
            }
            Log::alogd( DRM_PROBE_DEBUG, "  Drm D%u : pDisplay:%p desc:%s RPD:%u drm id:%d connector:%2d %s %s",
                d, mDisplay[d], mDisplay[d]->getName(),
                mDisplay[d]->getDisplayManagerIndex(),
                mDisplay[d]->getDrmDisplayID(),
                mDisplay[d]->getDrmConnectorID(),
                mDisplay[d]->isDrmConnected() ? "connected" : "disconnected",
                mDisplay[d]->isDrmConnected() ? initialTiming.dump().string() : "");
        }
    }

    // Broadcast start-of-day active displays.
    broadcastNumActiveDisplays();

    return OK;
}

bool Drm::enableVSync(DrmDisplay* pDisp)
{
    ALOG_ASSERT(mpEventThread != NULL);
    ALOG_ASSERT(pDisp != NULL);
    return mpEventThread->enableVSync(pDisp);
}

bool Drm::disableVSync(DrmDisplay* pDisp, bool bWait)
{
    ALOG_ASSERT(mpEventThread != NULL);
    ALOG_ASSERT(pDisp != NULL);
    return mpEventThread->disableVSync(pDisp, bWait);
}

int Drm::setCrtc( uint32_t crtc_id, uint32_t fb, uint32_t x, uint32_t y,
                  uint32_t* connector_id, uint32_t count, drmModeModeInfoPtr modeInfo )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ATRACE_INT_IF(DRM_CALL_TRACE, String8::format("HWC:D%d MP", crtc_id).string(), fb);
    Log::alogd( DRM_STATE_DEBUG,
              "drmModeSetCrtc( crtc_id %u, fb %u, x %u, y %u, connector_id %p, count %u, modeInfo %p )",
              crtc_id, fb, x, y, connector_id, count, modeInfo );
    int ret = drmModeSetCrtc( mDrmFd, crtc_id, fb, x, y, connector_id, count, modeInfo );
    Log::aloge( ret != SUCCESS,
             "Failed to set Crtc crtc_id %u, fb %u, x %u, y %u, connector_id %p, count %u, modeInfo %p  ret %d/%s",
             crtc_id, fb, x, y, connector_id, count, modeInfo, ret, strerror(errno) );
    return ret;
}

drmModeCrtcPtr Drm::getCrtc( uint32_t crtc_id )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeGetCrtc( crtc_id %u )", crtc_id );
    drmModeCrtcPtr ret = drmModeGetCrtc( mDrmFd, crtc_id );
    Log::aloge( ret == NULL, "Could not get Crtc crtc_id %u", crtc_id );
    return ret;
}

void Drm::freeCrtc( drmModeCrtcPtr ptr )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeFreeCrtc( ptr %p )", ptr );
    Log::aloge( !ptr, "Missing Crtc ptr" );
    drmModeFreeCrtc( ptr );
}

drmModeResPtr Drm::getResources( void )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeGetResources(  )" );
    drmModeResPtr ret = drmModeGetResources( mDrmFd);
    Log::aloge( ret == NULL, "Could not get resources" );
    return ret;
}

void Drm::freeResources( drmModeResPtr ptr )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeFreeResources( ptr %p )", ptr );
    Log::aloge( !ptr, "Missing resources ptr" );
    drmModeFreeResources( ptr );
}

drmModeEncoderPtr Drm::getEncoder( uint32_t encoder_id )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeGetEncoder( encoder_id %u )", encoder_id );
    drmModeEncoderPtr ret = drmModeGetEncoder( mDrmFd, encoder_id );
    Log::aloge( ret == NULL, "Could not get encoder encoder_id %u", encoder_id );
    return ret;
}

void Drm::freeEncoder( drmModeEncoderPtr ptr )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeFreeEncoder( ptr %p )", ptr );
    Log::aloge( !ptr, "Missing encoder ptr" );
    drmModeFreeEncoder( ptr );
}

drmModeConnectorPtr Drm::getConnector( uint32_t connector_id )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeGetConnector( connector_id %u )", connector_id );
    drmModeConnectorPtr ret = drmModeGetConnector( mDrmFd, connector_id );
    Log::aloge( ret == NULL, "Could not get connector connector_id %u", connector_id );
    return ret;
}

void Drm::freeConnector( drmModeConnectorPtr ptr )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeFreeConnector( ptr %p )", ptr );
    ALOGE_IF( !ptr, "Missing connector ptr" );
    drmModeFreeConnector( ptr );
}

drmModePlaneResPtr Drm::getPlaneResources( void )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeGetPlaneResources( )" );
    drmModePlaneResPtr ret = drmModeGetPlaneResources( mDrmFd );
    Log::aloge( ret == NULL, "Could not get plane resources" );
    return ret;
}

void Drm::freePlaneResources( drmModePlaneResPtr ptr )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeFreePlaneResources( ptr %p )", ptr );
    ALOGE_IF( !ptr, "Missing plane resources ptr" );
    drmModeFreePlaneResources( ptr );
}

drmModePlanePtr Drm::getPlane( uint32_t plane_id )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeGetPlane( plane_id %u )", plane_id );
    drmModePlanePtr ret = drmModeGetPlane( mDrmFd, plane_id );
    Log::aloge( ret == NULL, "Could not get plane plane_id %u", plane_id );
    return ret;
}

void Drm::freePlane( drmModePlanePtr ptr )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeFreePlane( ptr %p )", ptr );
    ALOGE_IF( !ptr, "Missing plane ptr" );
    drmModeFreePlane( ptr );
}

uint32_t Drm::getPanelFitterPropertyID( uint32_t connector_id )
{
    uint32_t propId = INVALID_PROPERTY;
#if VPG_DRM_HAVE_PANEL_FITTER
    return getConnectorPropertyID( connector_id, DRM_PFIT_PROP );
#else
    HWC_UNUSED( connector_id );
#endif
    ALOGW_IF( propId == INVALID_PROPERTY, "Panel fitter property not available" );
    return propId;
}

uint32_t Drm::getPanelFitterSourceSizePropertyID( uint32_t connector_id )
{
    uint32_t propId = INVALID_PROPERTY;
#if VPG_DRM_HAVE_PANEL_FITTER_SOURCE_SIZE
    propId = getConnectorPropertyID( connector_id, DRM_SCALING_SRC_SIZE_PROP );
#else
    HWC_UNUSED( connector_id );
#endif
    ALOGW_IF( sbInternalBuild && propId == INVALID_PROPERTY, "Panel fitter source size property not available" );
    return propId;
}

uint32_t Drm::getDPMSPropertyID( uint32_t connector_id )
{
    return getConnectorPropertyID( connector_id, DRM_DPMS_PROP );
}

uint32_t Drm::getDRRSPropertyID( uint32_t connector_id )
{
    return getConnectorPropertyID( connector_id, DRM_DRRS_PROP );
}

uint32_t Drm::getConnectorPropertyID( uint32_t connector_id, const char* pchPropName )
{
    return getPropertyID (connector_id, DRM_MODE_OBJECT_CONNECTOR, pchPropName);
}
uint32_t Drm::getPlanePropertyID( uint32_t plane_id, const char* pchPropName )
{
    return getPropertyID (plane_id, DRM_MODE_OBJECT_PLANE, pchPropName);
}

uint32_t Drm::getPropertyID( uint32_t obj_id, uint32_t obj_type, const char* pchPropName )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    uint32_t prop_id = INVALID_PROPERTY;

    drmModeObjectPropertiesPtr props;

    //Get the Connector property
    ALOGD_IF( DRM_STATE_DEBUG, "drmModeObjectGetProperties( obj_id %u, obj_type %u )", obj_id, obj_type );
    props = drmModeObjectGetProperties( mDrmFd, obj_id, obj_type );
    if ( !props )
    {
        ALOGE("Display enumPropertyID - could not get connector properties");
        return -1;
    }

    for (uint32_t j = 0; j < props->count_props; j++) {
        drmModePropertyPtr prop;
        ALOGD_IF( DRM_STATE_DEBUG, "drmModeGetProperty( property_id %u )", props->props[j] );
        prop = drmModeGetProperty( mDrmFd, props->props[j] );
        if(prop == NULL) {
            ALOGE("Get Property return NULL");
            drmModeFreeObjectProperties( props );
            return -1;
        }
        if (!strcmp(prop->name,pchPropName)) {
            Log::alogd( DRM_STATE_DEBUG, "drmModeGetProperty ( %s ) property_id %u", pchPropName, props->props[j] );
            prop_id = prop->prop_id;
            drmModeFreeProperty( prop );
            break;
        }
        ALOGD_IF( DRM_STATE_DEBUG,  "drmModeFreeProperty( ptr %p id:%d name:%s)", prop, prop->prop_id, prop->name );
        drmModeFreeProperty( prop );
    }

    Log::alogd( DRM_STATE_DEBUG, "drmModeFreeObjectProperties( ptr %p )", props );
    drmModeFreeObjectProperties( props );

    ALOGD_IF( sbInternalBuild && prop_id == INVALID_PROPERTY, "Drm property %s not found", pchPropName );

    return prop_id;
}

int Drm::acquirePanelFitter( uint32_t connector_id )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ALOG_ASSERT( connector_id < 64 );
    const uint64_t connectorMask = (1ULL<<connector_id);
    if ( mAcquiredPanelFitters & connectorMask )
    {
        Log::alogd( DRM_STATE_DEBUG,  "drm acquired panel fitter [connector_id %u, acquired 0x%llx ] [No Change]",
            connector_id, mAcquiredPanelFitters );
        return SUCCESS;
    }
    // Current implementation assumes one panel
    // fitter shared between all connectors.
    if ( mAcquiredPanelFitters )
    {
        Log::alogd( DRM_STATE_DEBUG,  "drm did not acquire panel fitter [connector_id %u, acquired 0x%llx ]",
            connector_id, mAcquiredPanelFitters );
        return BAD_VALUE;
    }
    mAcquiredPanelFitters |= connectorMask;
    Log::alogd( DRM_STATE_DEBUG,  "drm acquired panel fitter [connector_id %u, acquired 0x%llx ]",
        connector_id, mAcquiredPanelFitters );
    return SUCCESS;
}

int Drm::releasePanelFitter( uint32_t connector_id )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ALOG_ASSERT( connector_id < 64 );
    const uint64_t connectorMask = (1ULL<<connector_id);
    if ( mAcquiredPanelFitters & connectorMask )
    {
        mAcquiredPanelFitters &= ~connectorMask;
        Log::alogd( DRM_STATE_DEBUG,  "drm released panel fitter [connector_id %u, acquired 0x%llx ]",
            connector_id, mAcquiredPanelFitters );
        return SUCCESS;
    }
    ALOGE( "panel fitter not acquired for connector id %u", connector_id );
    return BAD_VALUE;
}

bool Drm::isPanelFitterAcquired( uint32_t connector_id )
{
    const uint64_t connectorMask = (1ULL<<connector_id);
    return ( ( mAcquiredPanelFitters & connectorMask ) != 0 );
}

int Drm::setPanelFitterProperty( uint32_t connector_id, int32_t pfit_prop_id, uint32_t mode,
                                 int32_t dstX, int32_t dstY, uint32_t dstW, uint32_t dstH )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
#if VPG_DRM_HAVE_PANEL_FITTER
    ALOG_ASSERT( connector_id < 64 );
    const uint64_t connectorMask = (1ULL<<connector_id);
    if ( !( mAcquiredPanelFitters & connectorMask ) )
    {
        ALOGE( "panel fitter not acquired for connector id %u", connector_id );
        return BAD_VALUE;
    }

    if ( pfit_prop_id == -1 )
    {
        ALOGE( "Panel fitter not available" );
        return BAD_VALUE;
    }

    Log::alogd( DRM_STATE_DEBUG,
        "drmModeObjectSetProperty( connector_id %u, object_type 0x%x, property_id %u[PFIT], mode %u[%s] "
        "dstX %d, dstY %d, dstW %d, dstH %d )",
        connector_id, DRM_MODE_OBJECT_CONNECTOR, pfit_prop_id,
        mode, getPanelFitterModeString( mode ),
        dstX, dstY, dstW, dstH );
#if VPG_DRM_HAVE_PANEL_FITTER_MANUAL
    if ( ( mode == DRM_PFIT_MANUAL ) && ( !dstW || dstH ) )
    {
        ALOGE( "Manual panel fitter mode requires explicit destination frame [%d,%d %dx%d]",
            dstX, dstY, dstW, dstH );
        return BAD_VALUE;
    }
    // TODO:
    // Manual mode requires implementation once KMD property support
    // for dest frame has been confirmed.
    ALOGE_IF( mode == DRM_PFIT_MANUAL, "Manual pannel fitter mode is not implemented." );
    return BAD_VALUE;
#endif
    if (drmModeObjectSetProperty( mDrmFd, connector_id, DRM_MODE_OBJECT_CONNECTOR, (uint32_t)pfit_prop_id, mode ))
    {
       ALOGE("set panel fitter property failed");
       return -1;
    }
    return 0;
#else // VPG_DRM_HAVE_PANEL_FITTER
    HWC_UNUSED( connector_id );
    HWC_UNUSED( pfit_prop_id );
    HWC_UNUSED( mode );
    HWC_UNUSED( dstX );
    HWC_UNUSED( dstY );
    HWC_UNUSED( dstW );
    HWC_UNUSED( dstH );
    ALOGE( "Panel fitter support missing" );
    return BAD_VALUE;
#endif
}

int Drm::setPanelFitterSourceSizeProperty( uint32_t connector_id, int32_t pfit_prop_id, uint32_t srcW, uint32_t srcH )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
#if VPG_DRM_HAVE_PANEL_FITTER_SOURCE_SIZE
    const uint64_t connectorMask = (1ULL<<connector_id);
    if ( !( mAcquiredPanelFitters & connectorMask ) )
    {
        ALOGE( "panel fitter not acquired for connector id %u", connector_id );
        return BAD_VALUE;
    }

    if ( pfit_prop_id == -1 )
    {
        ALOGE_IF( sbInternalBuild, "Panel fitter source size not available" );
        return BAD_VALUE;
    }

    srcW--;
    srcH--;
    ALOG_ASSERT( srcW<=0xFFFF );
    ALOG_ASSERT( srcH<=0xFFFF );
    const uint32_t val = (srcW<<16)|(srcH);

    Log::alogd( DRM_STATE_DEBUG,
        "drmModeObjectSetProperty( connector_id %u, object_type 0x%x, property_id %u[PFIT_SRC_SIZE], val %u[%ux%u] )",
        connector_id, DRM_MODE_OBJECT_CONNECTOR, pfit_prop_id, val, srcW+1, srcH+1 );

    if (drmModeObjectSetProperty( mDrmFd, connector_id, DRM_MODE_OBJECT_CONNECTOR, (uint32_t)pfit_prop_id, val ))
    {
        ALOGE("set panel fitter source size property failed");
        return -1;
    }
    return 0;
#else // VPG_DRM_HAVE_PANEL_FITTER_SOURCE_SIZE
    HWC_UNUSED( connector_id );
    HWC_UNUSED( pfit_prop_id );
    HWC_UNUSED( srcW );
    HWC_UNUSED( srcH );
    ALOGE( "Panel fitter source size support missing" );
    return BAD_VALUE;
#endif
}

const char* Drm::getPanelFitterModeString( uint32_t mode )
{
#if VPG_DRM_HAVE_PANEL_FITTER
#define DRM_PANEL_FITTER_MODE_TO_STRING(M) case M : return #M;
    switch( mode )
    {
        DRM_PANEL_FITTER_MODE_TO_STRING( DRM_PFIT_OFF  )
        DRM_PANEL_FITTER_MODE_TO_STRING( DRM_AUTOSCALE )
        DRM_PANEL_FITTER_MODE_TO_STRING( DRM_PILLARBOX )
        DRM_PANEL_FITTER_MODE_TO_STRING( DRM_LETTERBOX )
#if VPG_DRM_HAVE_PANEL_FITTER_MANUAL
        DRM_PANEL_FITTER_MODE_TO_STRING( DRM_PFIT_MANUAL )
#endif
        default:
            return "<?>";

    }
#undef DRM_PANEL_FITTER_MODE_TO_STRING
#else
    HWC_UNUSED(mode);
    return "<?>";
#endif
}

int Drm::getCap(uint64_t capability, uint64_t& value)
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    int ret = ::drmGetCap(mDrmFd, capability, &value);
    if (ret != Drm::SUCCESS)
    {
        Log::aloge( true, "Failed drmGetCap( %" PRIu64 " ), ret:%d", capability, value, ret);
        return ret;
    }
    Log::alogd( DRM_STATE_DEBUG, "drmGetCap( %" PRIu64 " ) = %" PRIu64 , capability, value);
    return ret;
}

int Drm::setClientCap(uint64_t capability, uint64_t value)
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    // Older drm versions do not support this call.
#if defined(DRM_CLIENT_CAP_UNIVERSAL_PLANES)
    Log::alogd( DRM_STATE_DEBUG, "drmSetClientCap( %" PRIu64 ", %" PRIu64 ")", capability, value);
    int ret = ::drmSetClientCap(mDrmFd, capability, value);
    Log::aloge( ret != Drm::SUCCESS, "Failed drmSetClientCap %" PRIu64 " value %" PRIu64 ", ret:%d", capability, value, ret);
    return ret;
#else
    Log::alogd( DRM_STATE_DEBUG, "drmSetClientCap( %" PRIu64 ", %" PRIu64 ") return INVALID_OPERATION", capability, value);
    return android::INVALID_OPERATION;
#endif
}


int Drm::setDPMSProperty( uint32_t connector_id, int32_t prop_id, uint32_t mode )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    if ( prop_id == -1 )
    {
        ALOGE( "DPMS not available" );
        return BAD_VALUE;
    }

    int res;
    Log::alogd( DRM_STATE_DEBUG || DRM_SUSPEND_DEBUG,
        "drmModeObjectSetProperty( connector_id %u, object_type 0x%x, property_id %u[DPMS], value %u[%s] )",
        connector_id, DRM_MODE_OBJECT_CONNECTOR, prop_id, mode, getDPMSModeString( mode ) );

    res = drmModeObjectSetProperty( mDrmFd, connector_id, DRM_MODE_OBJECT_CONNECTOR, (uint32_t) prop_id, mode );

    if ( res )
    {
       ALOGE("Set DPMS property failed");
       return -1;
    }

    return 0;
}

// helper functions for setting particular property types
int Drm::setConnectorProperty( uint32_t connector_id, int32_t prop_id, uint64_t value )
{
    return setProperty(connector_id, DRM_MODE_OBJECT_CONNECTOR, prop_id, value);
}
int Drm::setPlaneProperty( uint32_t plane_id, int32_t prop_id, uint64_t value )
{
    return setProperty(plane_id, DRM_MODE_OBJECT_PLANE, prop_id, value);
}

int Drm::setProperty( uint32_t obj_id, uint32_t obj_type, int32_t prop_id, uint64_t value )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    Log::alogd( DRM_STATE_DEBUG, "drmModeObjectSetProperty( obj_id %u, object_type 0x%x, prop_id %d, value %" PRIu64 " )", obj_id, obj_type, prop_id, value );
    int ret = drmModeObjectSetProperty( mDrmFd, obj_id, obj_type, (uint32_t) prop_id, value );
    Log::aloge(ret != Drm::SUCCESS, "drmModeObjectSetProperty( obj_id %u, object_type 0x%x, prop_id %d, value %" PRIu64 " ) FAILED ret %d, error: %s", obj_id, obj_type, prop_id, value, ret, strerror(errno));
    return ret;
}

// helper functions for getting particular property types
int Drm::getConnectorProperty( uint32_t connector_id, int32_t prop_id, uint64_t *pValue )
{
    ALOG_ASSERT( pValue );
    return getProperty(connector_id, DRM_MODE_OBJECT_CONNECTOR, prop_id, pValue);
}
int Drm::getPlaneProperty( uint32_t plane_id, int32_t prop_id, uint64_t *pValue )
{
    ALOG_ASSERT( pValue );
    return getProperty(plane_id, DRM_MODE_OBJECT_PLANE, prop_id, pValue);
}

int Drm::getProperty( uint32_t obj_id, uint32_t obj_type, int32_t prop_id, uint64_t *pValue )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ALOG_ASSERT( pValue );

    if ( prop_id == -1 )
    {
        return android::BAD_VALUE;
    }

    drmModeObjectPropertiesPtr pProps = drmModeObjectGetProperties( mDrmFd, obj_id, obj_type );
    if ( !pProps )
    {
        return android::BAD_VALUE;
    }

    int result = android::BAD_VALUE;
    for ( uint32_t p = 0; p < pProps->count_props; p++ )
    {
        if ( pProps->props[p] == (uint32_t)prop_id )
        {
            *pValue = pProps->prop_values[p];
            result = SUCCESS;
            break;
        }
    }
    drmModeFreeObjectProperties( pProps );

    return result;
}

int Drm::getDPMSProperty( uint32_t connector_id, int32_t prop_id )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    uint64_t value;
    if ( getConnectorProperty(connector_id, prop_id, &value) != SUCCESS )
    {
        ALOGE( "DPMS not available" );
        return -1;
    }

    int mode = (int)value;

    Log::alogd( DRM_STATE_DEBUG || DRM_SUSPEND_DEBUG,
        "drmModeObjectGetProperties( connector_id %u, object_type 0x%x )  property_id %u[DPMS] ==  value %u[%s]",
        connector_id, DRM_MODE_OBJECT_CONNECTOR, prop_id, mode, getDPMSModeString( mode ) );

    return mode;
}

const char* Drm::getDPMSModeString( int32_t mode )
{
#define DRM_DPMS_MODE_TO_STRING(M) case M : return #M;
    switch( mode )
    {
        DRM_DPMS_MODE_TO_STRING( DRM_MODE_DPMS_ON        )
        DRM_DPMS_MODE_TO_STRING( DRM_MODE_DPMS_STANDBY   )
        DRM_DPMS_MODE_TO_STRING( DRM_MODE_DPMS_SUSPEND   )
        DRM_DPMS_MODE_TO_STRING( DRM_MODE_DPMS_OFF       )
#if VPG_DRM_HAVE_ASYNC_DPMS
        DRM_DPMS_MODE_TO_STRING( DRM_MODE_DPMS_ASYNC_ON  )
        DRM_DPMS_MODE_TO_STRING( DRM_MODE_DPMS_ASYNC_OFF )
#endif
        default:
            return "<?>";

    }
#undef DRM_DPMS_MODE_TO_STRING
}

int Drm::getDRRSProperty( uint32_t connector_id, int32_t prop_id )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    uint64_t value;
    if ( getConnectorProperty(connector_id, prop_id, &value) != SUCCESS )
    {
        ALOGE( "DRRS not available" );
        return -1;
    }

    int drrs = (int)value;

    return drrs;
}

int Drm::setDecrypt( uint32_t objectType, uint32_t id, bool bEnable )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    // Filter attempts to set decrypt for main plane (not currently supported).
    if ( objectType == DRM_MODE_OBJECT_CRTC )
    {
        ALOGD_IF(DRM_STATE_DEBUG, "setDecrypt for main display plane skipped (unsupported)" );
        return OK;
    }

#ifdef DRM_IOCTL_I915_RESERVED_REG_BIT_2
    struct drm_i915_reserved_reg_bit_2 decrypt;
    decrypt.enable = bEnable ? 1: 0;
#if defined(INTEL_HWC_ANDROID_BYT_3_10)
    // Legacy implementation.
    decrypt.plane = id - 2;
#else
    // Current/future implementations should use planeID directly.
    decrypt.plane = id;
#endif

    Log::alogd( DRM_STATE_DEBUG,
              "drmIoctl( DRM_IOCTL_I915_RESERVED_REG_BIT_2[ plane %u enable %d ] )",
              decrypt.plane, decrypt.enable );
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_I915_RESERVED_REG_BIT_2, &decrypt );
    Log::aloge( ret != SUCCESS, "Failed to set dec plane %u, enable %d  ret %d/%s",
        decrypt.plane, decrypt.enable, ret, strerror(errno) );
    return ret;
#else // DRM_IOCTL_I915_RESERVED_REG_BIT_2
    Log::aloge(bEnable, "DRM_IOCTL_I915_RESERVED_REG_BIT_2 not defined - expect video corruption");
    return BAD_VALUE;
#endif
}

int Drm::moveCursor( uint32_t crtc_id, int32_t x, int32_t y)
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeMoveCursor( crtc_id %u, x %d, y %d )", crtc_id, x, y );
    int ret = drmModeMoveCursor( mDrmFd, crtc_id, x, y );
    Log::aloge( ret != SUCCESS, "Failed to move cursor crtc_id %u, x %d, y %d  ret %d/%s",
        crtc_id, x, y, ret, strerror(errno) );
    return ret;
}

int Drm::setCursor( uint32_t crtc_id, uint32_t bo, uint32_t w, uint32_t h )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmModeSetCursor( crtc_id %u, bo %u, w %u, h %u )", crtc_id, bo, w, h );
    int ret = drmModeSetCursor( mDrmFd, crtc_id, bo, w, h );
    Log::aloge( ret != SUCCESS, "Failed to set cursor crtc_id %u, bo %u, w %u, h %u  ret %d/%s",
        crtc_id, bo, w, h, ret, strerror(errno) );
    return ret;
}

int Drm::setZOrder( uint32_t crtc_id, uint32_t zorder )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
#if VPG_DRM_HAVE_ZORDER_API
    struct drm_i915_set_plane_zorder z;
    z.order = zorder;
#if defined(DRM_ZORDER_WITH_ID)
    z.obj_id = crtc_id;
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl( DRM_IOCTL_I915_SET_PLANE_ZORDER[ crtc_id %u, order %u ] )", z.obj_id, z.order );
#else
    HWC_UNUSED(crtc_id);
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl( DRM_IOCTL_I915_SET_PLANE_ZORDER[ order %u ] )", z.order );
#endif
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_I915_SET_PLANE_ZORDER, &z );
    Log::aloge( ret != SUCCESS, "Failed to set plane ZOrder %u  ret %d/%s", zorder, ret, strerror(errno) );
    return ret;
#else
    HWC_UNUSED(crtc_id);
    HWC_UNUSED(zorder);
    ALOGE( "Plane ZOrder support missing" );
    return ~SUCCESS;
#endif // VPG_DRM_HAVE_ZORDER_API
}

int Drm::setPlane( uint32_t plane_id, uint32_t crtc_id, uint32_t fb, uint32_t flags,
              uint32_t crtc_x, uint32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h,
              uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h, void *user_data )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ATRACE_INT_IF(DRM_CALL_TRACE, String8::format("HWC:D%d P%d", crtc_id, plane_id).string(), fb);

    ALOG_ASSERT( !(flags & DRM_MODE_PAGE_FLIP_EVENT) || (VPG_DRM_HAVE_MAIN_PLANE_DISABLE), "VPG_DRM_HAVE_MAIN_PLANE_DISABLE not enabled" );

    Log::alogd( DRM_STATE_DEBUG,
              "drmModeSetPlane( plane_id %u, crtc_id %u, fb %u, flags %u, "
              "x %u, y %u, w %u, h %u, sx %.1f, sy %.1f, sw %.1f, sh %.1f, ud %p )",
              plane_id, crtc_id, fb, flags,
              crtc_x, crtc_y, crtc_w, crtc_h,
              src_x / 65536.0f, src_y / 65536.0f, src_w / 65536.0f, src_h / 65536.0f, user_data );
    int ret= drmModeSetPlane( mDrmFd,
                    plane_id, crtc_id, fb, flags,
                    crtc_x, crtc_y, crtc_w, crtc_h,
                    src_x, src_y, src_w, src_h
#if defined(DRM_PRIMARY_DISABLE)
                    , user_data
#endif
                    );

    Log::aloge( ret != SUCCESS,
              "Failed to set plane plane_id %u, crtc_id %u, fb %u, flags %u, "
              "x %u, y %u, w %u, h %u, sx %u, sy %u, sw %u, sh %u, ud %p  ret %d/%s",
              plane_id, crtc_id, fb, flags,
              crtc_x, crtc_y, crtc_w, crtc_h,
              src_x, src_y, src_w, src_h, user_data,
              ret, strerror(errno) );
    return ret;
}

int Drm::pageFlip( uint32_t crtc_id, uint32_t fb, uint32_t flags, void* user_data )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG,
              "drmModePageFlip( crtc_id %u, fb %u, flags %u, user_data %p )",
              crtc_id, fb, flags, user_data );
    int ret;
    {
            ret = drmModePageFlip( mDrmFd, crtc_id, fb, flags, user_data );
    }
    Log::aloge( ret != SUCCESS,
              "Failed to page flip crtc_id %u, fb %u, flags %u, user_data %p  ret %d/%s",
              crtc_id, fb, flags, user_data,
              ret, strerror(errno) );

    return ret;
}

int Drm::screenCtl( uint32_t crtc_id, uint32_t enable )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
#if VPG_DRM_HAVE_SCREEN_CTL
    struct drm_i915_disp_screen_control screen_cntrl;
    screen_cntrl.crtc_id = crtc_id;
    screen_cntrl.on_off_cntrl = enable;
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl( DRM_IOCTL_I915_DISP_SCREEN_CONTROL[ crtc_id %u, on_off_cntrl %d ] )",
        screen_cntrl.crtc_id, screen_cntrl.on_off_cntrl);
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_I915_DISP_SCREEN_CONTROL, &screen_cntrl );
    // NOTE:
    //  Reduced ALOGE to ALOGD due to expected failures on builds where libdrm defines
    //  DRM_IOCTL_I915_DISP_SCREEN_CONTROL but the kernel does not implement it.
    ALOGD_IF( DRM_STATE_DEBUG && ret != SUCCESS, "Failed to set screen crtc_id %u, enable %d  ret %d/%s",
        crtc_id, enable, ret, strerror(errno) );
    return ret;
#else
    HWC_UNUSED(crtc_id);
    HWC_UNUSED(enable);
    return -ENOSYS;
#endif
}

int Drm::setTransform( uint32_t objectType, uint32_t id, ETransform transform )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    ALOG_ASSERT( ( objectType == DRM_MODE_OBJECT_CRTC )
              || ( objectType == DRM_MODE_OBJECT_PLANE ) );

#if VPG_DRM_HAVE_TRANSFORM_180
    ALOG_ASSERT( ( transform == ETransform::NONE ) || ( transform == ETransform::ROT_180 ) );
    struct drm_i915_plane_180_rotation plane_rotation;
    plane_rotation.obj_id = id;
    plane_rotation.obj_type = objectType;
    plane_rotation.rotate = ( transform == ETransform::ROT_180 ) ? 1 : 0;
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl( DRM_IOCTL_I915_SET_PLANE_180_ROTATION[ objType %x id %u rotate %u ] )",
        objectType, id, plane_rotation.rotate );
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_I915_SET_PLANE_180_ROTATION, &plane_rotation );
    Log::aloge( ret != SUCCESS, "Failed to set objType %x id %u rotation %u  ret %d/%s",
        objectType, id, plane_rotation.rotate, ret, strerror(errno) );
    return ret;
#else
    HWC_UNUSED(objectType);
    HWC_UNUSED(id);
    HWC_UNUSED(transform);
    ALOG_ASSERT(transform == 0);
    return 0;
#endif
}

// These should be defined in libdrm somewhere
#ifndef DRM_ROTATE_0
#define DRM_ROTATE_0    (1<<0)
#define DRM_ROTATE_90   (1<<1)
#define DRM_ROTATE_180  (1<<2)
#define DRM_ROTATE_270  (1<<3)
#define DRM_REFLECT_X   (1<<4)
#define DRM_REFLECT_Y   (1<<5)
#endif
uint32_t Drm::hwcTransformToDrm(ETransform hwcTransform)
{
    switch (hwcTransform)
    {
        case ETransform::NONE       : return DRM_ROTATE_0;
        case ETransform::FLIP_H     : return DRM_REFLECT_X;
        case ETransform::FLIP_V     : return DRM_REFLECT_Y;
        case ETransform::ROT_90     : return DRM_ROTATE_270;
        case ETransform::ROT_180    : return DRM_ROTATE_180;
        case ETransform::ROT_270    : return DRM_ROTATE_90;

        // Unsupported by libdrm, should never get here.
        case ETransform::FLIP_H_90  : break;
        case ETransform::FLIP_V_90  : break;
    }
    Log::aloge(true, "Drm::hwcTransformToDrm Failed to convert hwc transform %d", hwcTransform);
    ALOG_ASSERT(false);
    return DRM_ROTATE_0;
}

#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY

int Drm::drmSetDisplay( struct drm_mode_set_display& display )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl( DRM_IOCTL_MODE_SETDISPLAY[ %s ] )", drmDisplayPipeToString( display ).string( ) );
    for ( uint32_t p = 0; p < display.num_planes; ++p )
    {
        Log::alogd( DRM_STATE_DEBUG, "drmIoctl    %s", drmDisplayPlaneToString( display, p ).string( ) );
    }
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_MODE_SETDISPLAY, &display );
    if ( ret != SUCCESS )
    {
        Log::add( "Failed to set display %s", drmDisplayPipeToString( display ).string( ) );
        for ( uint32_t p = 0; p < display.num_planes; ++p )
        {
            Log::add( "  %s", drmDisplayPlaneToString( display, p ).string( ) );
        }
        Log::add( "  ret %d/%s", ret, strerror(errno) );
    }
    return ret;
}

#endif

int Drm::waitBufferObject( uint32_t boHandle, uint64_t timeoutNs )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    struct drm_i915_gem_wait wait;
    wait.bo_handle = boHandle;
#ifdef I915_WAIT_WRITES
    wait.flags = I915_WAIT_WRITES;
#else
    wait.flags = 0;
#endif
    wait.timeout_ns = timeoutNs;
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl( DRM_IOCTL_I915_GEM_WAIT[ boHandle %u, timeout %llu ] )",
        boHandle, timeoutNs );
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_I915_GEM_WAIT, &wait );
    Log::aloge( timeoutNs && (ret != SUCCESS), "Failed to wait boHandle %u, timeout %" PRIu64 "  ret %d/%s",
        boHandle, timeoutNs, ret, strerror(errno) );
    return ret;
}

int Drm::openPrimeBuffer( int primeFd, uint32_t* pHandle )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ALOG_ASSERT( pHandle );
    struct drm_prime_handle prime;
    memset( &prime, 0, sizeof(prime) );
    prime.fd = primeFd;
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl( DRM_IOCTL_PRIME_FD_TO_HANDLE[ primeFd %d ] )", primeFd );
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime );
    if ( ret == SUCCESS )
    {
        *pHandle = prime.handle;
    }
    else
    {
        *pHandle = 0;
        Log::aloge( true, "Failed to open primeFd %d ret %d/%s", primeFd, ret, strerror(errno) );
    }
    return ret;
}

int Drm::closeBuffer( uint32_t handle )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ALOG_ASSERT( handle );
    struct drm_gem_close close;
    memset( &close, 0, sizeof(close) );
    close.handle = handle;
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl( DRM_IOCTL_GEM_CLOSE[ handle %d ] )", handle );
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_GEM_CLOSE, &close );
    Log::aloge( ret != SUCCESS, "Failed to close handle %u ret %d/%s",
        handle, ret, strerror(errno) );
    return ret;
}

int Drm::registerBoAsDmaBuf( uint32_t boHandle, int* pDmaBuf )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ALOG_ASSERT( pDmaBuf );
    struct drm_prime_handle prime;
    prime.fd = -1;
    prime.flags = DRM_CLOEXEC;
    prime.handle = boHandle;
    Log::alogd( DRM_STATE_DEBUG, "drmPrimeDmaBuff( boHandle %u )", boHandle );
    int ret = drmIoctl( mDrmFd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime );
    Log::aloge( ret != SUCCESS, "Failed to get dma buf boHandle %u ret %d/%s",
        boHandle, ret, strerror(errno) );
    if ( ret == SUCCESS )
    {
        *pDmaBuf = prime.fd;
    }
    else
    {
        *pDmaBuf = -1;
    }
    return ret;
}

static uint32_t i915_gem_get_tiling(int fd, uint32_t boHandle)
{
    struct drm_i915_gem_get_tiling param;
    memset(&param, 0, sizeof(param));
    param.handle = boHandle;

    int ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_GET_TILING, &param);
    if (ret != Drm::SUCCESS)
    {
        Log::aloge( true, "Failed to get tiling bo:%u  ret %d/%s", boHandle, ret, strerror(errno) );
        return 0;
    }
    return param.tiling_mode;
}


ETilingFormat Drm::getTilingFormat(uint32_t boHandle)
{
    switch (i915_gem_get_tiling(mDrmFd, boHandle))
    {
        case I915_TILING_NONE:
            return TILE_LINEAR;
        case I915_TILING_X:
            return TILE_X;
        case I915_TILING_Y:
            return TILE_Y;
#if defined(I915_TILING_Yf)
        case I915_TILING_Yf:
            return TILE_Yf;
#endif
#if defined(I915_TILING_Ys)
        case I915_TILING_Ys:
            return TILE_Ys;
#endif
    }
    return TILE_UNKNOWN;
}

#if defined(DRM_MODE_FB_MODIFIERS)

#ifndef I915_FORMAT_MOD_X_TILED
// TODO: Remove later. Many current versions of external/drm/include/drm_fourcc.h do not define these
#define DRM_FORMAT_MOD_VENDOR_INTEL   0x01
#define fourcc_mod_code(vendor, val) \
	((((uint64_t)DRM_FORMAT_MOD_VENDOR_## vendor) << 56) | (val & 0x00ffffffffffffffULL))
#define I915_FORMAT_MOD_X_TILED	fourcc_mod_code(INTEL, 1)
#define I915_FORMAT_MOD_Y_TILED	fourcc_mod_code(INTEL, 2)
#define I915_FORMAT_MOD_Yf_TILED fourcc_mod_code(INTEL, 3)
#endif

static uint64_t lookupFbFormatMod(int fd, uint32_t boHandle)
{
    switch (i915_gem_get_tiling(fd, boHandle))
    {
        case I915_TILING_X:
            return I915_FORMAT_MOD_X_TILED;
        case I915_TILING_Y:
            return I915_FORMAT_MOD_Y_TILED;
#if defined(I915_TILING_Yf)
        case I915_TILING_Yf:
            return I915_FORMAT_MOD_Yf_TILED;
#endif
        default:
        case I915_TILING_NONE:
            return 0;
    }
}

static const char* fbModToString(uint64_t fbModifier)
{
    switch(fbModifier)
    {
        case I915_FORMAT_MOD_X_TILED:
            return "X";
        case I915_FORMAT_MOD_Y_TILED:
            return "Y";
        case I915_FORMAT_MOD_Yf_TILED:
            return "Yf";
        default:
            return "L";
    }

}

static int drmModeAddFB2WithModifier(int fd, uint32_t width, uint32_t height,
                                     uint32_t fbFormat, uint32_t handles[4],
                                     uint32_t pitches[4], uint32_t offsets[4],
                                     uint32_t *buf_id, uint32_t flags)
{
    struct drm_mode_fb_cmd2 f;
    int ret;

    memset(&f, 0, sizeof(f));
    f.width  = width;
    f.height = height;
    f.pixel_format = fbFormat;
    f.flags = flags | DRM_MODE_FB_MODIFIERS;
    for(int i = 0; i < 4; i++)
    {
        if (handles[i])
        {
            f.handles[i] = handles[i];
            f.pitches[i] = pitches[i];
            f.offsets[i] = offsets[i];
            f.modifier[i] = lookupFbFormatMod(fd, handles[i]);
        }
        else
        {
            f.handles[i] = 0;
            f.pitches[i] = 0;
            f.offsets[i] = 0;
            f.modifier[i] = 0;
        }
    }
    Log::alogd( DRM_STATE_DEBUG, "drmIoctl(DRM_IOCTL_MODE_ADDFB2 w:%u h:%u fmt:%x fl:%x, 0:%u:%u:%u:%s 1:%u:%u:%u:%s 2:%u:%u:%u:%s 3:%u:%u:%u:%s)",
            f.width, f.height, f.pixel_format, f.flags,
            f.handles[0], f.pitches[0], f.offsets[0], fbModToString(f.modifier[0]),
            f.handles[1], f.pitches[1], f.offsets[1], fbModToString(f.modifier[1]),
            f.handles[2], f.pitches[2], f.offsets[2], fbModToString(f.modifier[2]),
            f.handles[3], f.pitches[3], f.offsets[3], fbModToString(f.modifier[3]));

    if ((ret = drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f)))
        return ret;

    *buf_id = f.fb_id;
    return 0;
}

#endif

int Drm::addFb( uint32_t width, uint32_t height, uint32_t fbFormat, uint32_t boHandle, uint32_t pitch, uint32_t uvPitch, uint32_t uvOffset, uint32_t* pFb, uint32_t auxPitch, uint32_t auxOffset )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ALOG_ASSERT( width );
    ALOG_ASSERT( height );
    ALOG_ASSERT( pitch );
    ALOG_ASSERT( fbFormat );
    ALOG_ASSERT( boHandle );
    ALOG_ASSERT( pFb );
    uint32_t handles[4] = { boHandle, 0, 0, 0 };
    uint32_t pitches[4] = { pitch, pitch, pitch, pitch };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    uint32_t flags = 0;
    if (fbFormat == DRM_FORMAT_NV12)
    {
        ALOG_ASSERT( uvPitch );
        ALOG_ASSERT( uvOffset );
        handles[1] = boHandle;
        pitches[1] = uvPitch;
        offsets[1] = uvOffset;
    }
    else if (auxPitch)
    {
        handles[1] = boHandle;
        pitches[1] = auxPitch;
        offsets[1] = auxOffset;
        flags |= DRM_MODE_FB_AUX_PLANE;
    }

#if defined(DRM_MODE_FB_MODIFIERS)
    int ret = drmModeAddFB2WithModifier( mDrmFd, width, height, fbFormat, handles, pitches, offsets, pFb, flags);
#else
    int ret = drmModeAddFB2( mDrmFd, width, height, fbFormat, handles, pitches, offsets, pFb, flags);
#endif
    if ( ret == SUCCESS )
    {
        Log::alogd( DRM_STATE_DEBUG, "drmAddFb( width %u, height %u, fbFormat %x/%s, boHandle %u, pitch %u) = fb %u",
            width, height, fbFormat, fbFormatToString( fbFormat ).string(), boHandle, pitch, *pFb);
    }
    else
    {
        *pFb = 0;
        // This is expected for some formats such as NV12, dont report it as an error.
        Log::alogd( DRM_STATE_DEBUG, "drmAddFb failed with width %u, height %u, fbFormat %x/%s, boHandle %u, pitch %u, uvPitch %u, uvOffset %u, ret %d/%s",
            width, height, fbFormat, fbFormatToString( fbFormat ).string(),  boHandle, pitch, uvPitch, uvOffset, ret, strerror(-ret) );
    }
    return ret;
}

int Drm::removeFb( uint32_t fb )
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);
    ALOG_ASSERT( fb );
    Log::alogd( DRM_STATE_DEBUG, "drmRmFb( fb %u )", fb );
    int ret = drmModeRmFB( mDrmFd, fb );
    Log::aloge( ret != SUCCESS, "Failed to remove fb %u ret %d/%s", fb, ret, strerror(-ret) );
    return ret;
}

//define connector LookUp Table
typedef struct _ConnectorLUT
{
    uint32_t            mConnector;
    const char*         mNameString;
}ConnectorLUT;

static const ConnectorLUT ConnectorLookupTable[ ] =
{
    { DRM_MODE_CONNECTOR_eDP,          "eDP" },
    { DRM_MODE_CONNECTOR_DSI,          "DSI" },
    { DRM_MODE_CONNECTOR_DisplayPort,  "DP" },
    { DRM_MODE_CONNECTOR_HDMIA,        "HDMI-A" },
    { DRM_MODE_CONNECTOR_HDMIB,        "HDMI-B" },
    { DRM_MODE_CONNECTOR_DVII,         "DVI-I" },
    { DRM_MODE_CONNECTOR_DVID,         "DVI-D" },
    { DRM_MODE_CONNECTOR_DVIA,         "DVI-A" },
    { DRM_MODE_CONNECTOR_9PinDIN,      "DIN" },
    { DRM_MODE_CONNECTOR_VGA,          "VGA" },
    { DRM_MODE_CONNECTOR_LVDS,         "LVDS" },
    { DRM_MODE_CONNECTOR_Component,    "Component" },
    { DRM_MODE_CONNECTOR_TV,           "TV" },
    { DRM_MODE_CONNECTOR_Composite,    "Composite" },
    { DRM_MODE_CONNECTOR_SVIDEO,       "SVIDEO" },
    { DRM_MODE_CONNECTOR_Unknown,      "Unknown" },
#ifdef DRM_MODE_CONNECTOR_VIRTUAL
    { DRM_MODE_CONNECTOR_VIRTUAL,      "Virtual" },
#endif
};

const char* Drm::connectorTypeToString( uint32_t connectorType )
{
    const uint32_t numEntries = sizeof(ConnectorLookupTable) / sizeof(ConnectorLookupTable[0]);
    for ( uint32_t i = 0; i < numEntries; i++)
    {
        if( connectorType == ConnectorLookupTable[ i ].mConnector )
        {
            return ConnectorLookupTable[ i ].mNameString;
        }
     }
    return "Unknown";
}

uint32_t Drm::stringToConnectorType( const char * connectorString )
{

    const uint32_t numEntries = sizeof(ConnectorLookupTable) / sizeof(ConnectorLookupTable[0]);
    for ( uint32_t i = 0; i < numEntries; i++)
    {
        if( !strcmp( connectorString, ConnectorLookupTable[ i ].mNameString ) )
        {
            return ConnectorLookupTable[ i ].mConnector;
        }
     }
    return DRM_MODE_CONNECTOR_Unknown;
}

bool Drm::isSupportedInternalConnectorType( uint32_t connectorType ) const
{
    bool isSupported = false;
    int32_t optionValue = stringToConnectorType( mOptionDisplayInternal );
    if( optionValue == DRM_MODE_CONNECTOR_Unknown )
    {
        isSupported = ( ( connectorType == DRM_MODE_CONNECTOR_eDP )
                      || ( connectorType == DRM_MODE_CONNECTOR_DSI ) );
    }
    else
    {
        isSupported = ( optionValue == (int32_t)connectorType );
    }
    ALOGD_IF( DRM_STATE_DEBUG, "Drm::isSupportedInternalConnectorType, connectorType=%d, support=%s",connectorType, ( ( isSupported == true )? "Yes" : "No" ) );
    return isSupported;
}

bool Drm::isSupportedExternalConnectorType( uint32_t connectorType ) const
{
    bool isSupported = false;
    int32_t optionValue = stringToConnectorType( mOptionDisplayExternal );
    if( optionValue == DRM_MODE_CONNECTOR_Unknown )
    {
        // Currently default support HDMI and DP type.
        // This should be extended for DVI at some point (?)
        isSupported = ( ( connectorType == DRM_MODE_CONNECTOR_HDMIA )
                      || ( connectorType == DRM_MODE_CONNECTOR_HDMIB )
                      || ( connectorType == DRM_MODE_CONNECTOR_DisplayPort ) );
    }
    else
    {
        isSupported = ( optionValue == (int32_t)connectorType );
    }
    ALOGD_IF( DRM_STATE_DEBUG, "Drm::isSupportedExternalConnectorType, connectorType=%d, support=%s",connectorType, ( ( isSupported == true )? "Yes" : "No" ) );
    return isSupported;

}

uint32_t Drm::getDeviceID( void )
{
    drm_i915_getparam_t params;
    int            deviceID = 0;
    params.param = I915_PARAM_CHIPSET_ID;
    params.value = &deviceID;
    drmIoctl(Drm::get( ).getDrmHandle() , DRM_IOCTL_I915_GETPARAM, &params);
    return deviceID;
}

sp<Drm::Blob> Drm::Blob::create( Drm& drm, const void* pData, uint32_t size )
{
    sp<Blob> ret;
#ifdef DRM_IOCTL_MODE_CREATEPROPBLOB
    drm_mode_create_blob createBlob;
    createBlob.data = (__u64)pData;
    createBlob.length = size;
    int status = drmIoctl(drm.getDrmHandle() , DRM_IOCTL_MODE_CREATEPROPBLOB, &createBlob);
    if (status == SUCCESS)
    {
        ret = new Blob(drm, createBlob.blob_id);
    }
#else
    Log::aloge(true, "Failed to create DRM blob: DRM_IOCTL_MODE_CREATEPROPBLOB unknown");
    HWC_UNUSED(drm);
    HWC_UNUSED(pData);
    HWC_UNUSED(size);
#endif
    return ret;
}

Drm::Blob::~Blob()
{
#ifdef DRM_IOCTL_MODE_DESTROYPROPBLOB
    drm_mode_destroy_blob destroyBlob;
    destroyBlob.blob_id = mID;
    drmIoctl(mDrm.getDrmHandle() , DRM_IOCTL_MODE_DESTROYPROPBLOB, &destroyBlob);
#else
    HWC_UNUSED(mDrm);
#endif
}

sp<Drm::Blob> Drm::createBlob( const void* pData, uint32_t size )
{
    return Drm::Blob::create(*this, pData, size);
}

const char* Drm::UEventToString( UEvent eUE )
{
#define UE_TO_STRING( a ) case UEvent::a : return #a;
    switch ( eUE )
    {
        UE_TO_STRING( UNRECOGNISED            );
        UE_TO_STRING( HOTPLUG_CONNECTED       );
        UE_TO_STRING( HOTPLUG_DISCONNECTED    );
        UE_TO_STRING( HOTPLUG_RECONNECT       );
        UE_TO_STRING( HOTPLUG_CHANGED         );
        UE_TO_STRING( HOTPLUG_IMMINENT        );
        UE_TO_STRING( ESD_RECOVERY            );
        // No default to provoke warning for new enums.
    }
#undef UE_TO_STRING
    return "<?>";
};


const char* Drm::zOrderToString( uint32_t zorder )
{
#if VPG_DRM_HAVE_ZORDER_API
#define ZORDER_TO_STRING( a ) case (uint32_t)a : return #a;
    switch ( zorder )
    {
        // PipeA.
        ZORDER_TO_STRING( PASASBCA )
        ZORDER_TO_STRING( PASBSACA )
        ZORDER_TO_STRING( SBPASACA )
        ZORDER_TO_STRING( SBSAPACA )
        ZORDER_TO_STRING( SAPASBCA )
        ZORDER_TO_STRING( SASBPACA )
        // PipeB.
        ZORDER_TO_STRING( PBSCSDCB )
        ZORDER_TO_STRING( PBSDSCCB )
        ZORDER_TO_STRING( SDPBSCCB )
        ZORDER_TO_STRING( SDSCPBCB )
        ZORDER_TO_STRING( SCPBSDCB )
        ZORDER_TO_STRING( SCSDPBCB )
        default:
            break;
    }
#undef ZORDER_TO_STRING
#else
    HWC_UNUSED(zorder);
#endif // VPG_DRM_HAVE_ZORDER_API
    return "<?>";
}

const char* Drm::getObjectTypeString( uint32_t objType )
{
#define OBJECTTYPE_TO_STRING( a ) case DRM_MODE_OBJECT_##a : return #a;
    switch ( objType )
    {
        OBJECTTYPE_TO_STRING( CRTC );
        OBJECTTYPE_TO_STRING( PLANE );
        OBJECTTYPE_TO_STRING( CONNECTOR );
        default:
            break;
    }
#undef OBJECTTYPE_TO_STRING
    return "<?>";
}

String8 Drm::modeInfoToString( const drmModeModeInfo& m )
{
    String8 s;
    s = String8::format( "clock %u h[disp %u syncstart %u syncend %u total %u skew %u] "
                                  "v[disp %u syncstart %u syncend %u total %u scan %u] "
                                  " vrefresh %u flags 0x%x type %u name{%s}",
            m.clock,
            m.hdisplay, m.hsync_start, m.hsync_end, m.htotal, m.hskew,
            m.vdisplay, m.vsync_start, m.vsync_end, m.vtotal, m.vscan,
            m.vrefresh,
            m.flags,
            m.type,
            m.name );
    return s;
}

bool Drm::modeInfoCompare( const drmModeModeInfo& a, const drmModeModeInfo& b )
{
    return ( ( a.clock       == b.clock )
          && ( a.hdisplay    == b.hdisplay )
          && ( a.hsync_start == b.hsync_start )
          && ( a.hsync_end   == b.hsync_end )
          && ( a.htotal      == b.htotal )
          && ( a.hskew       == b.hskew )
          && ( a.vdisplay    == b.vdisplay )
          && ( a.vsync_start == b.vsync_start )
          && ( a.vsync_end   == b.vsync_end )
          && ( a.vscan       == b.vscan )
          && ( a.vrefresh    == b.vrefresh )
          && ( a.flags       == b.flags )
          && ( a.type        == b.type )
          && !strncmp( a.name, b.name, DRM_DISPLAY_MODE_LEN ) );
}

#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY

String8 Drm::drmDisplayToString( const struct drm_mode_set_display& display )
{
    return drmDisplayPipeToString( display ) + "\n" + drmDisplayPlaneToString( display );
}

String8 Drm::drmDisplayPipeToString( const struct drm_mode_set_display& display )
{
    return String8::format( "CRTC:%u UPDATE[0x%04x%s%s%s%s%s%s%s%s%s%s] STATE{Z:%u, PFIT:%u/%s S:%ux%u D:%d,%d %ux%u PLANES:%u}",
            display.crtc_id, display.update_flag,
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_ZORDER       ? " ZORDER" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PANEL_FITTER ? " PANELFITTER" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE(0)     ? " PLANE0" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE(1)     ? " PLANE1" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE(2)     ? " PLANE2" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE(3)     ? " PLANE3" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE(4)     ? " PLANE4" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE(5)     ? " PLANE5" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE(6)     ? " PLANE6" : "",
            display.update_flag & DRM_MODE_SET_DISPLAY_UPDATE_PLANE(7)     ? " PLANE7" : "",
            display.zorder,
            display.panel_fitter.mode,
#if VPG_DRM_HAVE_PANEL_FITTER
            display.panel_fitter.mode == DRM_PFIT_OFF    ? "OFF" :
            display.panel_fitter.mode == DRM_AUTOSCALE   ? "AUTO" :
#if VPG_DRM_HAVE_PANEL_FITTER_MANUAL
            display.panel_fitter.mode == DRM_PFIT_MANUAL ? "MANUAL" :
#endif
            display.panel_fitter.mode == DRM_PILLARBOX     ? "PILLARBOX" :
            display.panel_fitter.mode == DRM_LETTERBOX     ? "LETTERBOX" :
#endif
            "<?>",
            display.panel_fitter.src_w,
            display.panel_fitter.src_h,
            display.panel_fitter.dst_x,
            display.panel_fitter.dst_y,
            display.panel_fitter.dst_w,
            display.panel_fitter.dst_h,
            display.num_planes );
}

String8 Drm::drmDisplayPlaneToString( const struct drm_mode_set_display& display, int32_t plane )
{
    String8 str;
    uint32_t planes = 0;

    for ( uint32_t p = 0; p < display.num_planes; ++p )
    {
        if ( ( plane != -1 ) && ( (uint32_t)plane != p ) )
            continue;

        String8 idStr = String8::format(
            "%5s %02u",
            display.plane[p].obj_type == DRM_MODE_OBJECT_PLANE ? "PLANE" :
            display.plane[p].obj_type == DRM_MODE_OBJECT_CRTC  ? "CRTC"  : "<?>",
            display.plane[p].obj_id );

        String8 updateFlagStr = String8::format(
            "0x%04x:%s%s%s%s",
            display.plane[p].update_flag,
            display.plane[p].update_flag & DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT   ? " FLP"  : "",
            display.plane[p].update_flag & DRM_MODE_SET_DISPLAY_PLANE_UPDATE_RRB2      ? " RRB2" : "",
            display.plane[p].update_flag & DRM_MODE_SET_DISPLAY_PLANE_UPDATE_TRANSFORM ? " TX"   : "",
            display.plane[p].update_flag & DRM_MODE_SET_DISPLAY_PLANE_UPDATE_ALPHA     ? " BL"   : "" );

        String8 stateStr = String8::format(
            "FB:%3u, F:0x%04x, S:%7.2f,%7.2f %7.2fx%7.2f -> D:%4u,%4u %4ux%4u UD:0x%-8llx, RRB2:%u, TX:%u, BL:%d",
            display.plane[p].fb_id,
            display.plane[p].flags,
            (1.0f/65536.0f) * display.plane[p].src_x,
            (1.0f/65536.0f) * display.plane[p].src_y,
            (1.0f/65536.0f) * display.plane[p].src_w,
            (1.0f/65536.0f) * display.plane[p].src_h,
            display.plane[p].crtc_x,
            display.plane[p].crtc_y,
            display.plane[p].crtc_w,
            display.plane[p].crtc_h,
            display.plane[p].user_data,
            display.plane[p].rrb2_enable,
            display.plane[p].transform,
            display.plane[p].alpha );

        str += String8::format(
            "%s%s UPDATE[%-16s] STATE{%s}",
            planes ? "\n" : "",
            // Id.
            idStr.string(),
            // Dirty bits.
            updateFlagStr.string(),
            // Status.
            stateStr.string() );

        ++planes;
    }
    return str;
}

#endif

// For validation
extern "C" ANDROID_API void hwcSimulateHotPlug(bool connected)
{
    Drm::get().onHotPlugEvent(connected ? Drm::UEvent::HOTPLUG_CONNECTED : Drm::UEvent::HOTPLUG_DISCONNECTED);
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
