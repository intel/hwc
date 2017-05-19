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

#include "DrmDisplay.h"
#include "DrmEventThread.h"
#include "Drm.h"
#include "Log.h"
#include "DrmFormatHelper.h"
#include "DrmModeHelper.h"
#include "DrmNuclearPageFlipHandler.h"
#include "AbstractPlatform.h"
#include "HwcService.h"
#include "DisplayState.h"
#include <drm_fourcc.h>
#include <cutils/properties.h>
#include <math.h>

enum drrs_support_type {
        DRRS_NOT_SUPPORTED       = 0,
        STATIC_DRRS_SUPPORT      = 1,
        SEAMLESS_DRRS_SUPPORT    = 2,
        SEAMLESS_DRRS_SUPPORT_SW = 3,
};

namespace intel {
namespace ufo {
namespace hwc {

// TODO:
// Currently limiting self-teardown to just SET until we can establish precise requirements.
// This can be forced ON using option "drmteardown" (see ESelfTeardownOptions).
#define INTEL_UFO_HWC_DRMDISPLAY_WANT_SELF_TEARDOWN SELF_TEARDOWN_SET
// Any event:
//  SELF_TEARDOWN_SET | SELF_TEARDOWN_RESET | SELF_TEARDOWN_SUSPEND |  SELF_TEARDOWN_RESUME

#define ENABLE_HARDWARE_VSYNC           1

#define DRMDISPLAY_ID_STR               "DrmDisplay %u/%p DrmConnector %u"
#define DRMDISPLAY_ID_PARAMS            getDrmDisplayID(), this, getDrmConnectorID()

#define FRAMES_TO_HOLD_BLANKING_BUFFER  10

// NOTES:
// The DrmDisplay uses DisplayQueue.
// Calls to onSet are queued and consumed from a worker.
// All calls to program Drm should be made prior to the worker running (start of day) or from the worker.
// For each DrmDisplay method, we can assert that a thread calling that method must be one of:
//  PRODUCER of a frame or event
//  CONSUMER of a frame or event
//  EXTERNAL e.g. hotplug, vsync, pageflip.
#if INTEL_HWC_INTERNAL_BUILD
#define DRMDISPLAY_ASSERT_PRODUCER_THREAD \
    ALOGD_IF( DRM_DISPLAY_DEBUG, "ASSERT PRODUCER: Worker:%u This:%u", getWorkerTid(), gettid() ); \
    ALOG_ASSERT( gettid() != getWorkerTid() );
#define DRMDISPLAY_ASSERT_CONSUMER_THREAD \
    ALOGD_IF( DRM_DISPLAY_DEBUG, "ASSERT CONSUMER: Worker:%u This:%u", getWorkerTid(), gettid() ); \
    ALOG_ASSERT( ( getWorkerTid() == 0 ) || ( gettid() == getWorkerTid() ) );
#define DRMDISPLAY_ASSERT_EXTERNAL_THREAD \
    ALOGD_IF( DRM_DISPLAY_DEBUG, "ASSERT EXTERNAL: Worker:%u This:%u", getWorkerTid(), gettid() ); \
    ALOG_ASSERT( gettid() != getWorkerTid() );
#else
#define DRMDISPLAY_ASSERT_PRODUCER_THREAD
#define DRMDISPLAY_ASSERT_CONSUMER_THREAD
#define DRMDISPLAY_ASSERT_EXTERNAL_THREAD
#endif

Option DrmDisplay::mOptionDefaultFrame( "drmdefaultframe", eDF_On );

DrmDisplay::Connection::Connection( ) :
    mDrm(Drm::get()),
    mpConnector( NULL ),
    mCrtcId( 0 ),
    mPipeIndex( 0 ),
    mbConnected( false ),
    mbHasPipe( false )
    { }

String8 DrmDisplay::Connection::dump( void ) const
{
    if ( !mbConnected )
        return String8::format( "Connector %p Disconnected", mpConnector );
    else if ( !mbHasPipe )
        return String8::format( "Connector %p Connected, No pipe", mpConnector );
    else
        return String8::format( "Connector %p Connected, CrtcID %u, PipeIdx %u", mpConnector, mCrtcId, mPipeIndex );
}

void DrmDisplay::Connection::set( Connection& other )
{
    setConnector( other.getConnector() );
    setPipe( other.getCrtcID(), other.getPipeIndex() );
}

void DrmDisplay::Connection::setConnector( drmModeConnector* pConnector )
{
    if ( mpConnector && ( mpConnector != pConnector ) )
        mDrm.freeConnector( mpConnector );
    mpConnector = pConnector;
    mbConnected = mpConnector
                   && (mpConnector->connection == DRM_MODE_CONNECTED)
                   && (mpConnector->count_modes > 0);
}

void DrmDisplay::Connection::setPipe( uint32_t crtcId, uint32_t pipeIndex )
{
    ALOG_ASSERT( mpConnector );
    mCrtcId = crtcId;
    mPipeIndex = pipeIndex;
    mbHasPipe = true;
}

void DrmDisplay::Connection::reset( void )
{
    setConnector(NULL);
    clearPipe();
}

void DrmDisplay::Connection::clearConnector( void )
{
    mpConnector = NULL;
}

void DrmDisplay::Connection::clearPipe( void )
{
    mCrtcId = 0;
    mPipeIndex = 0;
    mbHasPipe = false;
}

DrmDisplay::DrmDisplay( Hwc& hwc, uint32_t drmConnectorIndex ) :
    PhysicalDisplay(hwc),
    DisplayQueue( eBF_SYNC_BEFORE_FLIP ),
    mDrm(Drm::get()),
    mPageFlipHandler(*this),
    mName( String8::format( "DrmDisplay %u", drmConnectorIndex ) ),
    mOptionSelfTeardown( "drmteardown", INTEL_UFO_HWC_DRMDISPLAY_WANT_SELF_TEARDOWN ),
    mOptionPanelFitterMigration( "drmpfitmigrate", false ),
    // Immutable state esablished during open().
    mPossibleCrtcs( 0 ),
    mDrmConnectorIndex( drmConnectorIndex ),
    mDrmConnectorID( 0 ),
    mDrmConnectorType( 0 ),
    mbSeamlessDRRSSupported( false ),
    mbDynamicModeSupport( false ),
    mPropPanelFitterMode( -1 ),
    mPropPanelFitterSource( -1 ),
    mPropDPMS( -1 ),
    // Generic state.
    mDrmDisplay( INVALID_DISPLAY_ID ),
    meStatus( UNKNOWN ),
    mbBlankBufferPurged( false ),
    mBlankBufferFramesSinceLastUsed( 0 ),
    mDrmPanelFitterMode( -1 ),
    // DRRS and dynamic mode state.
    mFilterAppliedRefresh( 0 ),
    mSeamlessRequestedRefresh( 0 ),
    mSeamlessAppliedRefresh( 0 ),
    mDynamicAppliedTimingIndex( 0 ),
    // Queue.
    meQueueState( QUEUE_STATE_SHUTDOWN ),
    // Flags.
    mbSuspendDPMS( false ),
    mbSuspendDeactivated( false ),
    mbScreenCtlOn( true ),
    mbDrmVsyncEnabled( false ),
    mbVSyncGenEnabled( false ),
    mRecovering( false ),
    mOptionNuclearModeset( "nuclearmodeset", true )
{
}

DrmDisplay::~DrmDisplay()
{
    mActiveConnection.reset();
}

void DrmDisplay::releaseDrmResources( void )
{
    resetGlobalScaling( );
}

status_t DrmDisplay::open(drmModeConnector *pConnector, bool bRegisterWithHwc)
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD

    // Get supported crtc mask from encoder
    drmModeEncoderPtr pEncoder = mDrm.getEncoder( pConnector->encoders[0] );
    if (pEncoder == NULL)
    {
        ALOGE("Failed to get encoder for connector, skipping");
        return INVALID_OPERATION;
    }

    // Set immutable state/properties.
    mPossibleCrtcs = pEncoder->possible_crtcs;
    mDrmConnectorID = pConnector->connector_id;
    mDrmConnectorType = pConnector->connector_type;
    mbRegisterWithHwc = bRegisterWithHwc;
    uint32_t propIdDRRS = mDrm.getDRRSPropertyID( mDrmConnectorID );
    if (propIdDRRS != Drm::INVALID_PROPERTY)
    {
        int drrsCap = mDrm.getDRRSProperty( mDrmConnectorID, propIdDRRS );
        switch(drrsCap)
        {
        default:
            break;
        case SEAMLESS_DRRS_SUPPORT:
        case SEAMLESS_DRRS_SUPPORT_SW:
            mbDynamicModeSupport = true;
            mbSeamlessDRRSSupported = true;
            break;
        };
    }
    mPropPanelFitterMode = mDrm.getPanelFitterPropertyID( mDrmConnectorID );
    mPropPanelFitterSource = mDrm.getPanelFitterSourceSizePropertyID( mDrmConnectorID );
    mPropDPMS = mDrm.getDPMSPropertyID( mDrmConnectorID );
    ALOGE_IF( mPropDPMS == Drm::INVALID_PROPERTY, "Failed to get DPMS property ID" );
    // Set internal/external.
    setDisplayType( mDrm.isSupportedExternalConnectorType(pConnector->connector_type) ? eDTExternal : eDTPanel);

    // Set the start-of-day configuration
    // NOTE:
    //   Only the connector is known at this stage.
    //   If the display is connected then a subsequent call to start() will be
    //   made to complete the configuration.
    mCurrentConnection.setConnector( pConnector );
    setCurrentConnectionModes( pConnector );

    // Log summary of connector state.
    Log::alogd( sbLogViewerBuild, "DRM connector %u %s %s DynamicMode:%d SeamlessDRRS:%d PanelFitter:%d DPMS:%d",
        mDrmConnectorID,
        dumpDisplayType( getDisplayType() ).string(),
        isDrmConnected() ? "CONNECTED" : "DISCONNECTED",
        mbDynamicModeSupport,
        mbSeamlessDRRSSupported,
        (mPropPanelFitterMode != Drm::INVALID_PROPERTY) && (mPropPanelFitterSource != Drm::INVALID_PROPERTY),
        (mPropDPMS != Drm::INVALID_PROPERTY) );

    // free allocated resources
    mDrm.freeEncoder(pEncoder);

    return OK;
}

status_t DrmDisplay::start( uint32_t crtcID, uint32_t pipeIdx )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD

    // The pipe config is now known.
    // Set the connection immediately to ensure timings/mode have been set.
    mCurrentConnection.setPipe( crtcID, pipeIdx );
    if ( !setNewConnection( mCurrentConnection ) )
    {
        return INVALID_OPERATION;
    }

    // Log summary of the connection and mode.
    Log::alogd( true, "Selected mode for Connector:%d [%s] is Mode:%d %s",
        getDrmConnectorID(), mCurrentConnection.dump().string(), getRequestedTimingIndex(),
        mDisplayTimings[ getRequestedTimingIndex() ].dump().string());

    // Complete startup of the display via the worker.
    // The config has just been applied so we don't need to do it again.
    startupDisplay( mCurrentConnection, false );

    return OK;
}

void DrmDisplay::startupDisplay( Connection& newConnection, bool bNew )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD

    // TODO: Check this - check shutdown is covered too.
    // Update active displays and broadcast change.
    if ( mDrm.setActiveDisplay( getDrmDisplayID(), true ) )
    {
        // Synchronize required.
        mHwc.synchronize();
    }

    // Set DisplayQueue name.
    DisplayQueue::init( String8::format( "%s Pipe %d Crtc %d",
                                         mName.string(),
                                         newConnection.getPipeIndex(),
                                         newConnection.getCrtcID() ) );

    // Continue display programming asynchronously.
    // First work item will start DisplayQueue worker.
    queueStartup( newConnection, bNew );

    // Once the new connection has been sent to the worker queue
    // then we must drop the original connector's reference.
    newConnection.clearConnector();
}

// Note, this cannot be in the drmModeHelper.h, as drmModeModeInfoPtr is drm specific
// (and identical layout to struct drm_mode)
inline Timing::EAspectRatio getDrmModeAspectRatio(drmModeModeInfoPtr m)
{
#ifdef DRM_PICTURE_ASPECT_RATIO
    return getDrmModeAspectRatio(m->picture_aspect_ratio);
#else
    return getDrmModeAspectRatio(m->flags);
#endif
}

// This is called once on opening a device to populate a list of timings for the
// mode set routines.
void DrmDisplay::updateDisplayTimings( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );

    // Reset applied mode to 'unknown'.
    setAppliedTiming( UnknownDisplayTiming );
    cancelRequestedTiming( );

    // Update timings.
    {
        Mutex::Autolock _l( mDisplayTimingsLock );

        mDisplayTimings.clear();
        mTimingToConnectorMode.clear();

        // Store these so we can use them for future mode sets
        mWidthmm = getDrmConnector()->mmWidth;
        mHeightmm = getDrmConnector()->mmHeight;

        uint32_t preferredModes = 0;
        for (int32_t i = 0; i < getDrmConnector()->count_modes; i++)
        {
            drmModeModeInfoPtr m = &getDrmConnector()->modes[i];

            // It is an android policy decision to avoid supporting interlaced modes.
            if (m->flags & DRM_MODE_FLAG_INTERLACE)
                continue;

            // Construct a list of available timings
            uint32_t flags = 0;
            if (m->type & DRM_MODE_TYPE_PREFERRED)
                flags |= Timing::Flag_Preferred;
            if (m->flags & DRM_MODE_FLAG_INTERLACE)
                flags |= Timing::Flag_Interlaced;

            Timing t(m->hdisplay, m->vdisplay, m->vrefresh, m->clock, m->htotal, m->vtotal, getDrmModeAspectRatio(m), flags);
            if (m->type & DRM_MODE_TYPE_PREFERRED)
            {
                mDisplayTimings.insertAt(t, preferredModes);
                mTimingToConnectorMode.insertAt(i, preferredModes, 1);
                ++preferredModes;
            }
            else
            {
                mDisplayTimings.push_back(t);
                mTimingToConnectorMode.push_back(i);
            }
            ALOGD_IF( MODE_DEBUG, "DrmDisplay updateDisplayTimings %s", t.dump().string());
        }
    }

    // If we have dynamic modes then update the mode list to reflect that.
    if (mbDynamicModeSupport)
    {
        processDynamicDisplayTimings();
    }

    notifyTimingsModified( );
}

bool DrmDisplay::acquireGlobalScaling( uint32_t srcW, uint32_t srcH,
                                       int32_t dstX, int32_t dstY,
                                       uint32_t dstW, uint32_t dstH )
{
    DRMDISPLAY_ASSERT_PRODUCER_THREAD

    if ( isAvailable( ) )
    {
        // Acquire panel fitter and enable global scaling.
        // Panel fitter updates are programmed on the next call to applyGlobalScalingConfig( ).
        if ( mDrm.acquirePanelFitter( mDrmConnectorID ) == Drm::SUCCESS )
        {
            // set mGlobalScalingRequested, DisplayQueue will check it
            ALOGD_IF( GLOBAL_SCALING_DEBUG,
                      "DrmDisplay::acquireGlobalScaling " DRMDISPLAY_ID_STR " src:%ux%u dst:%u,%u %ux%u",
                      DRMDISPLAY_ID_PARAMS, srcW, srcH, dstX, dstY, dstW, dstH );
            mGlobalScalingRequested.mbEnabled = true;
            mGlobalScalingRequested.mSrcW = srcW;
            mGlobalScalingRequested.mSrcH = srcH;
            mGlobalScalingRequested.mDstX = dstX;
            mGlobalScalingRequested.mDstY = dstY;
            mGlobalScalingRequested.mDstW = dstW;
            mGlobalScalingRequested.mDstH = dstH;

            return true;
        }
    }
    ALOGD_IF( GLOBAL_SCALING_DEBUG, "DrmDisplay::acquireGlobalScaling " DRMDISPLAY_ID_STR " %s "
                                    "panel fitter not acquired for this display.",
                                    DRMDISPLAY_ID_PARAMS, mActiveConnection.dump().string() );
    return false;
}

bool DrmDisplay::releaseGlobalScaling( void )
{
    DRMDISPLAY_ASSERT_PRODUCER_THREAD

    ALOGD_IF( GLOBAL_SCALING_DEBUG,
            "DrmDisplay::releaseGlobalScaling " DRMDISPLAY_ID_STR, DRMDISPLAY_ID_PARAMS );
    // Disable global scaling.
    // Panel fitter updates are programmed on the next call to applyGlobalScalingConfig( ).
    // The panel fitter is not released until the changes are applied.
    mGlobalScalingRequested.mbEnabled = false;
    return true;
}

void DrmDisplay::resetGlobalScaling( void )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    SGlobalScalingConfig globalScalingReset;
    globalScalingReset.mbEnabled = false;
    mDrmPanelFitterMode = -1;

    // apply changes right now.
    applyGlobalScalingConfig( globalScalingReset );
}

uint32_t DrmDisplay::globalScalingToPanelFitterMode( const SGlobalScalingConfig& config )
{
    ALOG_ASSERT( getDisplayCaps().getGlobalScalingCaps().getFlags()
               & DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_SUPPORTED );
#if VPG_DRM_HAVE_PANEL_FITTER
    uint32_t mode = DRM_AUTOSCALE;
#if VPG_DRM_HAVE_PANEL_FITTER_MANUAL
    mode = DRM_PFIT_MANUAL;
#else
    if ( config.mDstX > 0 )
    {
        ALOG_ASSERT( getDisplayCaps().getGlobalScalingCaps().getFlags()
                   & DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_PILLARBOX );
        mode = DRM_PILLARBOX;
    }
    else if ( config.mDstY > 0 )
    {
        ALOG_ASSERT( getDisplayCaps().getGlobalScalingCaps().getFlags()
                   & DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_LETTERBOX );
        mode = DRM_LETTERBOX;
    }
#endif
    return mode;
#else
    return 0;
#endif
}

#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY

void DrmDisplay::issueGlobalScalingConfig( drm_mode_set_display& display, const SGlobalScalingConfig& globalScalingNew )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

#if VPG_DRM_HAVE_PANEL_FITTER
    // it is time to apply Global scaling config for this frame.
    ALOGD_IF( GLOBAL_SCALING_DEBUG, "DrmDisplay::issueGlobalScalingConfig " DRMDISPLAY_ID_STR "/%s, globalScalingNew.mbEnabled:%d, mGlobalScalingActive.mbEnabled:%d",
                    DRMDISPLAY_ID_PARAMS, getConnectionDesc().string(), globalScalingNew.mbEnabled, mGlobalScalingActive.mbEnabled );
    if ( globalScalingNew.mbEnabled )
    {
        if( ( !mGlobalScalingActive.mbEnabled) ||// currently not enabled
            ( ( mGlobalScalingActive.mbEnabled ) && // currently enabled but have different settings
              ( ( mGlobalScalingActive.mSrcW != globalScalingNew.mSrcW ) ||
                ( mGlobalScalingActive.mSrcH != globalScalingNew.mSrcH ) ||
                ( mGlobalScalingActive.mDstX != globalScalingNew.mDstX ) ||
                ( mGlobalScalingActive.mDstY != globalScalingNew.mDstY ) ||
                ( mGlobalScalingActive.mDstW != globalScalingNew.mDstW ) ||
                ( mGlobalScalingActive.mDstH != globalScalingNew.mDstH ) ) ) )
        {
            display.panel_fitter.mode  = globalScalingToPanelFitterMode( globalScalingNew );
            display.panel_fitter.src_w = globalScalingNew.mSrcW;
            display.panel_fitter.src_h = globalScalingNew.mSrcH;
            display.panel_fitter.dst_x = globalScalingNew.mDstX;
            display.panel_fitter.dst_y = globalScalingNew.mDstY;
            display.panel_fitter.dst_w = globalScalingNew.mDstW;
            display.panel_fitter.dst_h = globalScalingNew.mDstH;
            display.update_flag |= DRM_MODE_SET_DISPLAY_UPDATE_PANEL_FITTER;
        }

    }
    else if ( mGlobalScalingActive.mbEnabled )
    {
        display.panel_fitter.mode = DRM_PFIT_OFF;
        display.panel_fitter.src_w = getAppliedWidth();
        display.panel_fitter.src_h = getAppliedHeight();
        display.panel_fitter.dst_x = 0;
        display.panel_fitter.dst_y = 0;
        display.panel_fitter.dst_w = getAppliedWidth();
        display.panel_fitter.dst_h = getAppliedHeight();
        display.update_flag |= DRM_MODE_SET_DISPLAY_UPDATE_PANEL_FITTER;
    }
#else
    HWC_UNUSED( display );
    HWC_UNUSED( globalScalingNew );
#endif
}
void DrmDisplay::finalizeGlobalScalingConfig( const SGlobalScalingConfig& globalScalingNew )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    if ( mGlobalScalingActive.mbEnabled && !globalScalingNew.mbEnabled )
    {
        if ( mOptionPanelFitterMigration )
        {
            // release panel fitter so that it can be moved to different pipes
            mDrm.releasePanelFitter( mDrmConnectorID );
            ALOGD_IF( GLOBAL_SCALING_DEBUG, DRMDISPLAY_ID_STR " Panel fitter released.", DRMDISPLAY_ID_PARAMS );
        }
        else
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG, DRMDISPLAY_ID_STR " Panel fitter 'do not release'.", DRMDISPLAY_ID_PARAMS );
        }
    }

    // New panel fitter mode has been successfully set.
    // Set active to new state.
    mGlobalScalingActive = globalScalingNew;
}

#endif

bool DrmDisplay::setPanelFitter( uint32_t pfitMode,
                                 uint32_t srcW, uint32_t srcH,
                                 uint32_t dstX, uint32_t dstY,
                                 uint32_t dstW, uint32_t dstH )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD
#if VPG_DRM_HAVE_PANEL_FITTER
    ALOG_ASSERT( mPropPanelFitterMode != Drm::INVALID_PROPERTY );
    ALOG_ASSERT( mPropPanelFitterSource != Drm::INVALID_PROPERTY );
    if ( !mDrm.isPanelFitterAcquired( mDrmConnectorID ) )
    {
        ALOGE( DRMDISPLAY_ID_STR " Can not enable panel fitter - not acquired.", DRMDISPLAY_ID_PARAMS );
        return false;
    }
    if ( mDrm.setPanelFitterSourceSizeProperty( mDrmConnectorID,
                                                mPropPanelFitterSource,
                                                srcW, srcH ) )
    {
        ALOGE( DRMDISPLAY_ID_STR " Set panel fitter source size property failed.", DRMDISPLAY_ID_PARAMS );
        return false;
    }
    if ( ( mDrmPanelFitterMode < 0 ) || ((uint32_t)mDrmPanelFitterMode != pfitMode ) )
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG, DRMDISPLAY_ID_STR " Set PFIT Mode : %u", DRMDISPLAY_ID_PARAMS, pfitMode );
        if ( mDrm.setPanelFitterProperty( mDrmConnectorID,
                                          mPropPanelFitterMode,
                                          pfitMode,
                                          dstX, dstY,
                                          dstW, dstH ))
        {
            ALOGE( DRMDISPLAY_ID_STR " Set panel fitter property failed.", DRMDISPLAY_ID_PARAMS );
            return false;
        }
        mDrmPanelFitterMode = pfitMode;
    }
    return true;
#else
    HWC_UNUSED( pfitMode );
    HWC_UNUSED( srcW );
    HWC_UNUSED( srcH );
    HWC_UNUSED( dstX );
    HWC_UNUSED( dstY );
    HWC_UNUSED( dstW );
    HWC_UNUSED( dstH );
    return false;
#endif
}

bool DrmDisplay::resetPanelFitter( void )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD
#if VPG_DRM_HAVE_PANEL_FITTER
    ALOG_ASSERT( mPropPanelFitterMode != Drm::INVALID_PROPERTY );
    ALOG_ASSERT( mPropPanelFitterSource != Drm::INVALID_PROPERTY );
    mDrmPanelFitterMode = -1;
    if ( mDrm.isPanelFitterAcquired( mDrmConnectorID ) )
    {
        ALOGD_IF( GLOBAL_SCALING_DEBUG, DRMDISPLAY_ID_STR " Reset PFIT Mode : %u", DRMDISPLAY_ID_PARAMS, DRM_PFIT_OFF );
        if ( mDrm.setPanelFitterSourceSizeProperty( mDrmConnectorID,
                                                    mPropPanelFitterSource,
                                                    getAppliedWidth(), getAppliedHeight() ) )
        {
            ALOGE( DRMDISPLAY_ID_STR " Set panel fitter source size property failed.", DRMDISPLAY_ID_PARAMS );
            return false;
        }
        if ( mDrm.setPanelFitterProperty( mDrmConnectorID, mPropPanelFitterMode, DRM_PFIT_OFF ) )
        {
            ALOGE( DRMDISPLAY_ID_STR " Set panel fitter property failed.", DRMDISPLAY_ID_PARAMS );
            return false;
        }
        if ( mOptionPanelFitterMigration )
        {
            mDrm.releasePanelFitter( mDrmConnectorID );
            ALOGD_IF( GLOBAL_SCALING_DEBUG, DRMDISPLAY_ID_STR " Panel fitter released.", DRMDISPLAY_ID_PARAMS );
            return true;
        }
        else
        {
            ALOGD_IF( GLOBAL_SCALING_DEBUG, DRMDISPLAY_ID_STR " Panel fitter 'do not release'.", DRMDISPLAY_ID_PARAMS );
            return true;
        }
    }
    return true;
#else
    return false;
#endif
}

bool DrmDisplay::applyGlobalScalingConfig( const SGlobalScalingConfig& globalScalingNew )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    ALOGD_IF( GLOBAL_SCALING_DEBUG, "DrmDisplay::applyGlobalScalingConfig " DRMDISPLAY_ID_STR "/%s, globalScalingNew.mbEnabled:%d, mGlobalScalingActive.mbEnabled:%d",
                    DRMDISPLAY_ID_PARAMS, getConnectionDesc().string(), globalScalingNew.mbEnabled, mGlobalScalingActive.mbEnabled );
    if ( globalScalingNew.mbEnabled )
    {
        if( ( !mGlobalScalingActive.mbEnabled) ||// currently not enabled
            ( ( mGlobalScalingActive.mbEnabled ) && // currently enabled but have different settings
              ( ( mGlobalScalingActive.mSrcW != globalScalingNew.mSrcW ) ||
                ( mGlobalScalingActive.mSrcH != globalScalingNew.mSrcH ) ||
                ( mGlobalScalingActive.mDstX != globalScalingNew.mDstX ) ||
                ( mGlobalScalingActive.mDstY != globalScalingNew.mDstY ) ||
                ( mGlobalScalingActive.mDstW != globalScalingNew.mDstW ) ||
                ( mGlobalScalingActive.mDstH != globalScalingNew.mDstH ) ) ) )
        {
            uint32_t pfitMode = globalScalingToPanelFitterMode( globalScalingNew );
            // Enable/update.
            Log::add( DRMDISPLAY_ID_STR " Panel fitter scaling Enabled S:%ux%u F:%d,%d,%ux%u PFit:%u/%s",
                    DRMDISPLAY_ID_PARAMS,
                    globalScalingNew.mSrcW, globalScalingNew.mSrcH,
                    globalScalingNew.mDstX, globalScalingNew.mDstY,
                    globalScalingNew.mDstW, globalScalingNew.mDstH,
                    pfitMode, Drm::getPanelFitterModeString( pfitMode ) );
                if ( !setPanelFitter( pfitMode,
                                      globalScalingNew.mSrcW, globalScalingNew.mSrcH,
                                      globalScalingNew.mDstX, globalScalingNew.mDstY,
                                      globalScalingNew.mDstW, globalScalingNew.mDstH ) )
                {
                    return false;
                }
        }
        else
        {
            Log::add( DRMDISPLAY_ID_STR " Panel fitter scaling Enabled already, skipped(No Change).", DRMDISPLAY_ID_PARAMS );
        }
    }
    else if ( mGlobalScalingActive.mbEnabled )
    {
        Log::add( DRMDISPLAY_ID_STR " Panel fitter scaling Disabled", DRMDISPLAY_ID_PARAMS );
        if ( !resetPanelFitter( ) )
        {
            return false;
        }
    }
    else
    {
        Log::add( DRMDISPLAY_ID_STR " Panel fitter scaling Disabled Skipped (No Change)", DRMDISPLAY_ID_PARAMS );
    }
    // Set active to new state.
    mGlobalScalingActive = globalScalingNew;

    return true;
}

static uint32_t findBestRefresh(uint32_t refresh, uint32_t min, uint32_t max)
{
    // Try and find a refesh multiple that we like...
    if (refresh == 0)
    {
        return 0;
    }

    uint32_t result = refresh;
    while (result < min)
    {
        result += refresh;
    }
    if (result > max)
    {
        result = max;
    }
    return result;
}

void DrmDisplay::onSet(const Content::Display& display, uint32_t zorder, int* pRetireFenceFd)
{
    DRMDISPLAY_ASSERT_PRODUCER_THREAD

    ALOGD_IF( DRM_DEBUG, "DrmDisplay::onSet P%u zorder:%d %s", getDisplayManagerIndex(), zorder, display.dump().string());

    // Sanity check our display is aligned with scaling requirements.
    if ( display.isOutputScaled() )
    {
        const hwc_rect_t& dst = display.getOutputScaledDst();
        HWC_UNUSED( dst );
        ALOG_ASSERT( mGlobalScalingRequested.mbEnabled );
        ALOG_ASSERT( mGlobalScalingRequested.mSrcW == display.getWidth() );
        ALOG_ASSERT( mGlobalScalingRequested.mSrcH == display.getHeight() );
        ALOG_ASSERT( mGlobalScalingRequested.mDstX == dst.left );
        ALOG_ASSERT( mGlobalScalingRequested.mDstY == dst.top );
        ALOG_ASSERT( mGlobalScalingRequested.mDstW == (uint32_t)(dst.right - dst.left) );
        ALOG_ASSERT( mGlobalScalingRequested.mDstH == (uint32_t)(dst.bottom - dst.top) );
    }
    else
    {
        ALOG_ASSERT( !mGlobalScalingRequested.mbEnabled );
    }

    queueFrame( display, zorder, pRetireFenceFd );
}

void DrmDisplay::considerReleasingBuffers( void )
{

    if ( isSuspended() )
    {
        return;
    }

    if ( ( mpBlankBuffer != NULL ) && !mbBlankBufferPurged )
    {
        mBlankBufferFramesSinceLastUsed++;
        if (mBlankBufferFramesSinceLastUsed > FRAMES_TO_HOLD_BLANKING_BUFFER)
        {
            Log::alogd( DRM_DEBUG, DRMDISPLAY_ID_STR
                " Unpurged blanking buffer not used for %d frames - deleting blanking buffer.",
                DRMDISPLAY_ID_PARAMS, mBlankBufferFramesSinceLastUsed );
            mpBlankBuffer = NULL;
            mBlankLayer.clear();
        }
    }
}

int DrmDisplay::onVSyncEnable( bool bEnable )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD
    Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " set vsync %d", DRMDISPLAY_ID_PARAMS, bEnable );
    setVSync( bEnable );
    return OK;
}

int DrmDisplay::onBlank(bool bEnable, bool bIsSurfaceFlinger)
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD

    int ret = -1;

    ALOGD_IF( DRMDISPLAY_MODE_DEBUG, "%s%s", bEnable ? "Blank" : "Unblank", bIsSurfaceFlinger ? " (SF)" : "" );

    if ( bEnable )
    {
#if VPG_DRM_HAVE_POWERMANAGER
        // Targets that provide custom powermanager do not use DPMS for SurfaceFlinger blanking.
        const bool bUseDPMS = !bIsSurfaceFlinger;
#else
        const bool bUseDPMS = true;
#endif
        // Only deactivate the display if this is NOT suspending due to SF blank.
        // e.g. deactivate and release resources if blanking primary for extended video mode.
        const bool bDeactivateDisplay = !bIsSurfaceFlinger;
        ret = queueSuspend( bUseDPMS, bDeactivateDisplay );
    }
    else
    {
        ret = queueResume( );
    }

    return ret;
}

void DrmDisplay::allocateBlankingLayer( uint32_t width, uint32_t height )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    if ( !width && !height )
    {
        if ( mGlobalScalingActive.mbEnabled )
        {
            width = mGlobalScalingActive.mSrcW;
            height = mGlobalScalingActive.mSrcH;
            ALOGD_IF( DRM_DEBUG || GLOBAL_SCALING_DEBUG,
                      "" DRMDISPLAY_ID_STR " Sizing blanking from applied global scaling source size %ux%u",
                      DRMDISPLAY_ID_PARAMS, width, height );
        }
        else
        {
            width = getAppliedWidth();
            height = getAppliedHeight();
            ALOGD_IF( DRM_DEBUG || GLOBAL_SCALING_DEBUG,
                      "" DRMDISPLAY_ID_STR " Sizing blanking from applied display mode size %ux%u",
                      DRMDISPLAY_ID_PARAMS, width, height );
        }
    }

    ALOG_ASSERT( width );
    ALOG_ASSERT( height );

    // Allocate buffer if it does not exist or if the required size has changed.
    if ( ( mpBlankBuffer == NULL )
      || ( mBlankLayer.getBufferWidth() != width )
      || ( mBlankLayer.getBufferHeight() != height ) )
    {
        ATRACE_CALL_IF(DISPLAY_TRACE);

        mpBlankBuffer = NULL;
        mBlankLayer.clear();

        // (Re)create blanking if appropriate.
        if ( width && height )
        {
            ALOGD_IF( DRM_DEBUG || GLOBAL_SCALING_DEBUG,
                "" DRMDISPLAY_ID_STR " (Re)creating mpBlankBuffer %ux%u", DRMDISPLAY_ID_PARAMS, width, height );

            AbstractBufferManager& bm = AbstractBufferManager::get();
            mpBlankBuffer = bm.createPurgedGraphicBuffer( "BLANKING", width, height, INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT,
                    GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER, &mbBlankBufferPurged );

            if ( mpBlankBuffer != NULL )
            {
                // This is an opaque layer
                mBlankLayer.onUpdateAll(mpBlankBuffer->handle, true /* bOpaque */);
            }
        }
        else
        {
            ALOGE( "Can't allocate blanking (mode %ux%u)", width, height );
        }
    }
    mBlankBufferFramesSinceLastUsed = 0;
}

void DrmDisplay::vsyncEvent(unsigned int, unsigned int, unsigned int)
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD
    ATRACE_NAME("DrmDisplay::vsyncEvent");
    nsecs_t time = systemTime(SYSTEM_TIME_MONOTONIC);
    mPhysicalDisplayManager.notifyPhysicalVSync( this, time );
}

void DrmDisplay::dropAllFrames( void )
{
    ALOGD_IF( DRM_DEBUG, "DRMDisplay " DRMDISPLAY_ID_STR " dropAllFrames( )", DRMDISPLAY_ID_PARAMS );
    DisplayQueue::dropAllFrames();
}

void DrmDisplay::flush( uint32_t frameIndex, nsecs_t timeoutNs )
{
    ALOGD_IF( DRM_DEBUG, "DRMDisplay " DRMDISPLAY_ID_STR " flush( Frame:%u, Timeout:%" PRIi64 ")",
        DRMDISPLAY_ID_PARAMS, frameIndex, timeoutNs );
    DisplayQueue::flush( frameIndex, timeoutNs );
}

void DrmDisplay::synchronizeEvent( void )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD
    // Blocking sync with work queue (work items are removed only once fully consumed).
    ALOGD_IF( HPLUG_DEBUG, "DRMDisplay " DRMDISPLAY_ID_STR " synchronizeEvent flush", DRMDISPLAY_ID_PARAMS );
    flush( 0, 0 );
    // Enforce a full update (this is to cater for syncs across changes that require re-analysis).
    mHwc.forceGeometryChangeAndRedraw();
    // Blocking sync with HWC to ensure SF has a chance to pick up and process the
    // trailing unplug notification before handling any more events.
    ALOGD_IF( HPLUG_DEBUG, "DRMDisplay " DRMDISPLAY_ID_STR " synchronizeEvent HWC synchronize", DRMDISPLAY_ID_PARAMS );
    mHwc.synchronize( 0 );
    ALOGD_IF( HPLUG_DEBUG, "DRMDisplay " DRMDISPLAY_ID_STR " synchronizeEvent HWC synchronize complete", DRMDISPLAY_ID_PARAMS );

    // Forward notification of plug change completed.
    mHwc.notifyPlugChangeCompleted();
}

void DrmDisplay::synchronizeFromConsumer( void )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD
    consumerBlocked();
    mHwc.synchronize();
    consumerUnblocked();
}

void DrmDisplay::setCurrentConnectionModes( drmModeConnector* pNewConnector )
{
    mCurrentConnectionModes.clear();

    if (pNewConnector == NULL)
        return;

    ALOGD_IF( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug modes x%d", DRMDISPLAY_ID_PARAMS, pNewConnector->count_modes );
    for ( int32_t i = 0; i < pNewConnector->count_modes; i++ )
    {
        drmModeModeInfo& m = pNewConnector->modes[i];
        ALOGD_IF( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug mode %d : %s", DRMDISPLAY_ID_PARAMS, i, Drm::modeInfoToString( m ).string() );
        mCurrentConnectionModes.push_back( m );
    }
}

bool DrmDisplay::checkCurrentConnectionModes( drmModeConnector* pNewConnector )
{
    ALOGD_IF( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug check timings x%d", DRMDISPLAY_ID_PARAMS, pNewConnector->count_modes );
    if ( pNewConnector->count_modes != (int32_t)mCurrentConnectionModes.size() )
    {
        ALOGD_IF( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug (now) x%u != (was) x%zu",
            DRMDISPLAY_ID_PARAMS, pNewConnector->count_modes, mCurrentConnectionModes.size() );
        return false;
    }
    for ( int32_t i = 0; i < pNewConnector->count_modes; i++ )
    {
        drmModeModeInfo& m = pNewConnector->modes[i];
        if ( !Drm::modeInfoCompare( mCurrentConnectionModes[i], m ) )
        {
            ALOGD_IF( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug timing %d : (now) %s != (was) %s",
                DRMDISPLAY_ID_PARAMS, i, Drm::modeInfoToString( m ).string(), Drm::modeInfoToString( mCurrentConnectionModes[i] ).string() );
            return false;
        }
    }
    return true;
}

Drm::UEvent DrmDisplay::onHotPlugEvent( void )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD
    ATRACE_CALL_IF(DISPLAY_TRACE);

    Drm::UEvent eEv = Drm::UEvent::UNRECOGNISED;

    // Get pre-existing connected/pipe status.
    bool bWasConnected = mCurrentConnection.isConnected();
    bool bHadPipe = mCurrentConnection.hasPipe();

    // Get current/new connector.
    drmModeConnector* pNewConnector = mDrm.getConnector( getDrmConnectorID() );

    // Set new connection (clears pipe state, updates connected status).
    mCurrentConnection.setConnector( pNewConnector );

    if ( mCurrentConnection.isConnected() && pNewConnector )
    {
        // Keep a plug thread local record of timings so we can spot changes.
        bool bTimingChanges = false;
        if ( bWasConnected )
        {
            // Check for changes and set new timings if necessary.
            bTimingChanges = !checkCurrentConnectionModes( pNewConnector );
            if ( bTimingChanges )
            {
                setCurrentConnectionModes( pNewConnector );
            }
        }
        else
        {
            // Update connection timings.
            setCurrentConnectionModes( pNewConnector );
        }

        if ( bWasConnected && bHadPipe )
        {
            if ( bTimingChanges )
            {
                // If timings have changed then force a reconnect.
                Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug timing change [UEVENT_HOTPLUG_RECONNECT]", DRMDISPLAY_ID_PARAMS );
                eEv = Drm::UEvent::HOTPLUG_RECONNECT;
            }
            else
            {
                // Nothing to do.
                Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug still: %s",
                    DRMDISPLAY_ID_PARAMS, mCurrentConnection.dump().string() );
            }
        }
        else
        {
            // This is a new connection *OR* we didn't acquire a pipe last time.
            // Either way, (re)try to acquire a pipe for the connection now.
            // Previous contention for a pipe may now be resolved.
            Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug %stry plug [UEVENT_HOTPLUG_CONNECTED]", DRMDISPLAY_ID_PARAMS, bWasConnected ? "re" : "" );
            eEv = Drm::UEvent::HOTPLUG_CONNECTED;
        }
    }
    else
    {
        if ( bWasConnected )
        {
            // This is a disconnection.
            if ( bHadPipe )
            {
                Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug unplug [UEVENT_HOTPLUG_DISCONNECTED]", DRMDISPLAY_ID_PARAMS );
                eEv = Drm::UEvent::HOTPLUG_DISCONNECTED;
            }
            else
            {
                Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug unplug", DRMDISPLAY_ID_PARAMS );
            }

            // Reset current connection.
            mCurrentConnection.reset();
        }
        else
        {
            // Nothing to do.
            Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug still: %s",
                DRMDISPLAY_ID_PARAMS, mCurrentConnection.dump().string() );
        }
    }

    return eEv;
}

void DrmDisplay::issueHotPlug( void )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD
    ATRACE_CALL_IF(DISPLAY_TRACE);

    ALOG_ASSERT( mCurrentConnection.isConnected() );

    // TODO:
    // We may want to consider how we push priority out to the LogicalDisplayManager.
    // i.e. Currently we let Drm acquire pipes on a first come basis and only
    // make displays available once a pipe is acquired.

    uint32_t crtcID, pipeIdx;
    if ( mDrm.acquirePipe( mPossibleCrtcs, crtcID, pipeIdx ) )
    {
        // Set acquired pipe.
        mCurrentConnection.setPipe( crtcID, pipeIdx );

        Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug issuing plug", DRMDISPLAY_ID_PARAMS );

        // Update active displays and broadcast change.
        if ( mDrm.setActiveDisplay( getDrmDisplayID(), true ) )
        {
            // Synchronize required.
            mHwc.synchronize();
        }
        // Startup display.
        // This will call queueStartup to complete startup with the new connection.
        // NOTE: The mCurrentConnection connector will be cleared on return.
        startupDisplay( mCurrentConnection, true );

        // Synchronize to ensure all work is processed.
        // This is to avoid potential contention for pipes but also to
        // avoid races to access/update display timings during rapid
        // plug/unplug/plug sequences.
        synchronizeEvent( );

        Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug plug complete", DRMDISPLAY_ID_PARAMS );
    }
    else
    {
        Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug plug failed - no available pipes", DRMDISPLAY_ID_PARAMS );
    }
}

void DrmDisplay::issueHotUnplug( void )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD
    ATRACE_CALL_IF(DISPLAY_TRACE);

    Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug issuing unplug", DRMDISPLAY_ID_PARAMS );

    // Shutdown display.
    queueShutdown( );

    // Synchronize to ensure all work is processed.
    // This is to avoid potential contention for pipes and races to access/update
    // display timings during rapid plug/unplug/plug sequences.
    synchronizeEvent( );

    // Update active displays and broadcast change.
    // An enforced synchronize is not strictly required here.
    mDrm.setActiveDisplay( getDrmDisplayID(), false );

    Log::alogd( HPLUG_DEBUG, DRMDISPLAY_ID_STR " HotPlug unplug complete", DRMDISPLAY_ID_PARAMS );
}

void DrmDisplay::reconnect( void )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD
    ATRACE_CALL_IF(DISPLAY_TRACE);

    if( !mCurrentConnection.isConnected() )
    {
        ALOGD("It has been disconnected, not to reconnect.");
        return;
    }
    // Firstly, issue HotUnplug.
    issueHotUnplug();

    // Get current/new connector.
    drmModeConnector* pNewConnector = mDrm.getConnector( getDrmConnectorID() );
    // Set new connection (clears pipe state, updates connected status).
    mCurrentConnection.setConnector( pNewConnector );
    // Update connection timings.
    setCurrentConnectionModes( pNewConnector );

    // If it still be connected, issue HotPlug.
    if( mCurrentConnection.isConnected() )
    {
        issueHotPlug();
    }
    return;
}

void DrmDisplay::processRecovery( void )
{
    if (isInRecovery() && (meStatus == AVAILABLE))
    {
       Log::aloge( true, "Drm Processing Recovery, displayID = %d, CRTC = %d", getDrmDisplayID(), getDrmCrtcID() );

       // Exit recovery mode and then attempt recovery.
       // If recovery gets requested again while *this* recovery is being attempted
       // then processRecovery() will run again on the next frame.
       exitRecovery();

       // DPMS OFF
       mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_OFF );

       // Display is now 'suspended' since DPMS is OFF.
       setStatus( SUSPENDED );

       // Call DPMS_ON before set mode
       mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_ON );

       // Set display - re-establish current mode and do DPMS ON.
       // This will make it easier to recover even if some registers were reset.
       setDisplay( );

       // Force redraw the last frame to get rid of the blank frame in setDisplay().
       mHwc.forceRedraw();
       Log::aloge( true, "Drm Recovery finished, displayID = %d, CRTC = %d", getDrmDisplayID(), getDrmCrtcID() );
    }
}

void DrmDisplay::recover( void )
{
    // Enter recovery mode.
    // The next work on the display will process recovery.
    // Any work on the display will be filtered until the display is re-started.
    Log::aloge( true, "Drm Entering Recovery, displayID = %d, CRTC = %d", getDrmDisplayID(), getDrmCrtcID() );
    enterRecovery( );

    // Force a redraw to ensure at least once frame is queued
    // then processRecovery( ) can be called immediately.
    mHwc.forceRedraw();
}

void DrmDisplay::onESDEvent( Drm::UEvent eEvent )
{
    DRMDISPLAY_ASSERT_EXTERNAL_THREAD
    ATRACE_CALL_IF(DISPLAY_TRACE);

    // ESD recovery event.
    if( eEvent == Drm::UEvent::ESD_RECOVERY )
    {
        Log::alogd( HPLUG_DEBUG, "DrmDisplay %d Connector:%d Crtc:%d ESD: Recovery.", getDrmDisplayID(), getDrmConnectorID(), getDrmCrtcID( ) );
        recover( );
    }
    else
    {
        Log::alogd( true, "DrmDisplay %d Crtc:%d ESD: not recognised ESD event = %d.", getDrmDisplayID(), getDrmCrtcID( ), eEvent);
    }
    return;
}

void DrmDisplay::setAppliedTiming( uint32_t timingIndex )
{
    ALOGD_IF( MODE_DEBUG, "DrmDisplay setAppliedTiming timing index %u", timingIndex );
    PhysicalDisplay::setAppliedTiming( timingIndex );
    mSeamlessAppliedRefresh = 0;
    mFilterAppliedRefresh = 0;
    mDynamicAppliedTimingIndex = timingIndex;
}

void DrmDisplay::doSetDisplayMode(uint32_t mode)
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD
    ATRACE_CALL_IF(DISPLAY_TRACE);
    ALOG_ASSERT( mode < mDisplayTimings.size() );
    // Keep a reference to the current buffer until the mode change is complete.
    // This is to workaround an issue with pulling down buffers while they are in use.
    sp<GraphicBuffer> pOldBlanking = mpBlankBuffer;

    // Get the display mode timing.
    Timing t;
    bool bOK = copyDisplayTiming( mode, t );
    HWC_UNUSED( bOK );

    // Should always be valid at this stage.
    ALOG_ASSERT( bOK );
    ALOG_ASSERT( mode < mTimingToConnectorMode.size() );

    // Get the DrmDisplay mode index.
    uint32_t connectorModeIdx = mTimingToConnectorMode[mode];

    // Get the connector modeInfo.
    ALOG_ASSERT( connectorModeIdx < (uint32_t)getDrmConnector()->count_modes );
    drmModeModeInfo modeInfo = getDrmConnector()->modes[ connectorModeIdx ];

    // Sanity check that the logical and real display modes match size.
    LOG_FATAL_IF( ( modeInfo.hdisplay != t.getWidth() ) || ( modeInfo.vdisplay != t.getHeight() ),
               "Connector mode %u mismatches current display size (%ux%u v %ux%u)",
               connectorModeIdx, modeInfo.hdisplay, modeInfo.vdisplay, t.getWidth(), t.getHeight() );

    // Just present our holding buffer initially.
    allocateBlankingLayer();
    uint32_t fb = getBlankingLayer().getBufferDeviceId();
    Log::alogd( DRMDISPLAY_MODE_DEBUG, "Mode: %u, Blanking Layer: %s", mode, getBlankingLayer().dump().string());
    ALOGE_IF( fb == 0, "" DRMDISPLAY_ID_STR " : Missing blanking buffer framebuffer", DRMDISPLAY_ID_PARAMS );
    int32_t status = -1;
#if VPG_DRM_HAVE_ATOMIC_NUCLEAR
    if (mOptionNuclearModeset && mpNuclearHelper)
    {
        status = mpNuclearHelper->setCrtcNuclear( &modeInfo, &getBlankingLayer() );
    }
#endif
    if(status != Drm::SUCCESS)
        status = mDrm.setCrtc( getDrmCrtcID(), fb, 0, 0, &mDrmConnectorID, 1, &modeInfo );

    if ( status != Drm::SUCCESS )
    {
        ALOGE("" DRMDISPLAY_ID_STR " set mode - failed to set video mode %d: %s",
            DRMDISPLAY_ID_PARAMS, status, strerror(errno));
        // We cannot do a lot here if this failed as the calling functions have no way to handle an error.
        // Also, we have at least one kernel implementation that erroneously returns a fail if its the first
        // mode set post D3 resume.
        // If the failure is real, then all future drm flip calls should fail.
    }

    // Release previous blanking buffer.
    pOldBlanking = NULL;

    // Notify display timing change.
    notifyDisplayTimingChange( t );

    mHwc.forceGeometryChange();
}

bool DrmDisplay::getSeamlessMode( drmModeModeInfo &modeInfoOut )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    // Update the refresh if requested but not if there is a mode change on-going.
    uint32_t refresh = mSeamlessRequestedRefresh;
    const uint32_t appliedMode = getAppliedTimingIndex();

    if ( mbSeamlessDRRSSupported
     && (mSeamlessAppliedRefresh != refresh)
     && (appliedMode == getRequestedTimingIndex()) )
    {
        // Copy the drm mode and patch it with the correct refresh.
        ALOG_ASSERT( (uint32_t)getDrmConnector()->count_modes >= appliedMode );
        modeInfoOut = getDrmConnector()->modes[appliedMode];
        if (refresh != 0)
            modeInfoOut.vrefresh = refresh;

        return true;
    }

    return false;
}

void DrmDisplay::applySeamlessMode( const drmModeModeInfo &modeInfo )
{
    mSeamlessAppliedRefresh = modeInfo.vrefresh;
}

void DrmDisplay::legacySeamlessAdaptMode( const Layer* pLayer )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD
    ALOG_ASSERT( pLayer );
    drmModeModeInfo seamlesModeInfo;
    if (pLayer && getSeamlessMode(seamlesModeInfo))
    {
        ALOGD( "Setting video mode for Crtc %d, Display " DRMDISPLAY_ID_STR ", now %uHz",
            getDrmCrtcID(), DRMDISPLAY_ID_PARAMS, seamlesModeInfo.vrefresh );

        const uint32_t fb = pLayer->getBufferDeviceId();
        uint32_t connectorId = getDrmConnectorID();
        int32_t status = mDrm.setCrtc( getDrmCrtcID(), fb, 0, 0, &connectorId, 1, &seamlesModeInfo );
        if ( status != Drm::SUCCESS )
        {
            ALOGE("" DRMDISPLAY_ID_STR " set mode - failed to set video mode %d: %s",
                DRMDISPLAY_ID_PARAMS, status, strerror(errno));
        }
        else
        {
            applySeamlessMode(seamlesModeInfo);
        }
    }
}

bool DrmDisplay::defaultFrameRequired( void )
{
    if ( mOptionDefaultFrame == eDF_Auto )
    {
        ALOGW( "eDF_Auto not implemented" );
        // TBC: Add check through mDrmCaps here.
        return true;
    }
    return ( mOptionDefaultFrame == eDF_On );
}

void DrmDisplay::setDisplay( int32_t overrideMode )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mSetVSyncLock );
    Mutex::Autolock _l( mSetVSyncLock );

    if ( meStatus == AVAILABLE )
    {
        ALOGD( DRMDISPLAY_ID_STR " Already initialised", DRMDISPLAY_ID_PARAMS );
        return;
    }

    // Apply current mode or override if specified.
    uint32_t applyMode = ( overrideMode < 0 ) ? getAppliedTimingIndex( ) : (uint32_t)overrideMode;

    Log::alogd( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR " Initializing display with mode timing index %u (override %u, applied %u)",
        DRMDISPLAY_ID_PARAMS, applyMode, overrideMode, getAppliedTimingIndex() );

    // Set mode.
    doSetDisplayMode( applyMode );

    // Ensure DPMS is ON.
    // Do this after setting the display mode to ensure display starts up with correct mode.
    // If setDisplay() is being called from consumeResume() then this is redundant but harmless.
    ALOGD_IF( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR " Setting Drm Mode  (DPMS -> DRM_MODE_DPMS_ON)", DRMDISPLAY_ID_PARAMS );
    mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_ON );

    // Init page flip handler.
    mPageFlipHandler.init( );

    // Do we want/need to flip a default frame?
    if ( defaultFrameRequired( ) )
    {
        // Set blanking (synchronous).
        Log::alogd( DRM_DISPLAY_DEBUG, "Setting blanking as default frame" );
        setBlanking();
    }

    // Display is now 'available'.
    setStatus( AVAILABLE );

    // Enable vsync generation if required (do this *after* display is made available).
    doSetVSync( mbVSyncGenEnabled );
}

void DrmDisplay::resetDisplay( void )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mSetVSyncLock );
    Mutex::Autolock _l( mSetVSyncLock );

    if ( meStatus != AVAILABLE )
    {
        ALOGD( DRMDISPLAY_ID_STR " Already suspended", DRMDISPLAY_ID_PARAMS );
        return;
    }

    Log::alogd( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR " Uninitializing display", DRMDISPLAY_ID_PARAMS );

    // Stop vsync generation.
    doSetVSync( false );

    // Set blanking (synchronous).
    setBlanking( );

    // Uninit page flip handler.
    mPageFlipHandler.uninit( );

    // Release miscellaneous Drm resources.
    releaseDrmResources( );

    // Display is now 'suspended'.
    setStatus( SUSPENDED );
}

bool DrmDisplay::setNewConnection( Connection& newConnection )
{
    Log::alogd( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR "DRM New Connection %s -> %s",
        DRMDISPLAY_ID_PARAMS, mActiveConnection.dump().string(), newConnection.dump().string() );

    ALOG_ASSERT( newConnection.hasPipe() );

    // Test if we can get a valid crtc
    drmModeCrtc* pCrtc = mDrm.getCrtc( newConnection.getCrtcID() );
    if ( !pCrtc )
    {
        ALOGE( "Display start - get Crtc error [CrtcID %u]", newConnection.getCrtcID() );
        // Always "consume" the connection.
        newConnection.reset();
        return false;
    }
    mDrm.freeCrtc( pCrtc );

    // Set new connection.
    mActiveConnection.set( newConnection );

    // Options for this pipe.
    initializeOptions( "drm", getDrmPipeIndex() );

    // Create and register capability
    DisplayCaps* pDisplayCaps = DisplayCaps::create( getDrmPipeIndex(), Drm::get().getDeviceID());
    ALOG_ASSERT( pDisplayCaps );
    mDrmCaps.probe( getDrmCrtcID(), getDrmPipeIndex(), getDrmConnectorID(), pDisplayCaps );
    registerDisplayCaps( pDisplayCaps );

#if VPG_DRM_HAVE_ATOMIC_NUCLEAR
    mpNuclearHelper = std::make_shared<DrmNuclearHelper>(*this);
#endif

    // Update display times for new connector.
    updateDisplayTimings( );

    // Establish current mode.
    uint32_t initialMode = getDefaultDisplayTiming( );

    ALOGD_IF( DRMDISPLAY_MODE_DEBUG, "DRM New Connection initial mode is %u", initialMode );

    // Check mode is in range.
    LOG_FATAL_IF(
        ( initialMode >= (uint32_t)getDrmConnector()->count_modes ) || ( initialMode >= mDisplayTimings.size() ),
        "initialMode %u out-of-range (v getDrmConnector()->count_modes %u mDisplayTimings.size() %zu)",
        initialMode, getDrmConnector()->count_modes, mDisplayTimings.size() );

    // Apply the initial mode immediately.
    setInitialTiming( initialMode );

    Log::alogd( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR "DRM Set Connection %s", DRMDISPLAY_ID_PARAMS, dump().string() );
    return true;
}

void DrmDisplay::consumeStartup( Connection& newConnection, bool bNew )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    // Set connection.
    if ( bNew && !setNewConnection( newConnection ) )
    {
        return;
    }

    // Startup page flip handler so we can start queuing future frames.
    mPageFlipHandler.startupDisplay();

    //  Startup requires a mode set for which we use a blanking buffer - if we do this now we may have a black display
    //  for some time - at least until SF flips the first frame. For this reaseon, startup completion is held as pending
    //  (status == AVAILABLE_PENDING_START). This is processed on the next workitem (assumed first frame) via a call to
    //  processPending. In this way we minimise a black screen (since we only set the mode once content is available.)
    setStatus( AVAILABLE_PENDING_START );

    Log::alogd( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR "Started %s", DRMDISPLAY_ID_PARAMS, dump().string() );

    // Notify availability.
    if ( bNew && mbRegisterWithHwc )
    {
        notifyAvailable( );
    }

    // --------------
    // WORKAROUND:
    //  It is observed that calls to enable/disable vsync are made early (prior to a first frame). Because vsyncs are
    //  pipelined via the worker this completes the pending startup 'early'.
    //  JIRA: https://jira01.devtools.intel.com/browse/OAM-34003 requires that we make vsync enable/disable asynchronous
    //  to the frame flip (i.e. NOT pipelined via worker). This is done with gerrit change:
    //    https://vpg-git.iind.intel.com/#/c/14130/ "HWC - Next - OAM-23695 - Synchronous vsync enable/disable"
    //  This change has the secondary benefit of avoiding the early completion of the startup.
    //  Unfortunately, this also exposes an issue on BXT kernel where we see a stall in the call to drmModeSetCrtc().
    //  Specifically, in intel_set_mode_checked() -- __intel_set_mode appears() to succeed, but the following call to
    //  intel_modeset_check_state() triggers with "[drm:return_to_handler] *ERROR* mismatch in pch_pfit.enabled (expected 0, found 1)"
    //  This issue is not seen on BYT/CHT kernels; it may be specific to nuclear atomic API.
    //   Repro:  valhwch -t Smoke
    //  This workaround completes the pending startup immediately; so the change to make vsync enable/disable asynchronous
    //  can be merged. It should be removed once the BXT kernel bug is fully understood and resolved.
    processPending();
    // WORKAROUND END
    // --------------
}

void DrmDisplay::consumeShutdown( uint32_t timelineIndex )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    Log::alogd( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR "Shutting down %s", DRMDISPLAY_ID_PARAMS, dump().string() );

    // Reset display.
    // This will set blanking and SUSPENDED status.
    resetDisplay();

    // Advance the timeline.
    // This is to ensure all prior frames are released.
    mPageFlipHandler.releaseTo( timelineIndex );

    // Disable DPMS.
    ALOGD_IF( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR " Setting Drm Mode  (DPMS -> DRM_MODE_DPMS_OFF)", DRMDISPLAY_ID_PARAMS );
    mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_OFF );

    // Always ensure the mode has been reset.
    // This was originally to workaround an issue with some kernels
    // failing to clear the mode info on HDMI unplug.
    int32_t status = -1;
#if VPG_DRM_HAVE_ATOMIC_NUCLEAR
    if (mOptionNuclearModeset && mpNuclearHelper)
    {
        status = mpNuclearHelper->setCrtcNuclear( NULL, NULL );
    }
#endif
    if (status != Drm::SUCCESS)
        status = mDrm.setCrtc( getDrmCrtcID(), 0, 0, 0, NULL, 0, NULL );

    if ( status != Drm::SUCCESS )
    {
        ALOGE("Failed to reset video mode for Crtc %d, Display " DRMDISPLAY_ID_STR ", %d: %s",
            getDrmCrtcID(), DRMDISPLAY_ID_PARAMS, status, strerror(errno));
    }

    // Tell Drm the pipe is now available for other display
    mDrm.releasePipe( getDrmPipeIndex() );
    ALOGD_IF ( DRMDISPLAY_MODE_DEBUG, "Release pipe %u", getDrmPipeIndex() );

    // Reset the conection pipe/crtc.
    Log::alogd( DRMDISPLAY_MODE_DEBUG, "DRM Reset Connection %s", mActiveConnection.dump().string() );
    mActiveConnection.reset();
#if VPG_DRM_HAVE_ATOMIC_NUCLEAR
    mpNuclearHelper.reset();
#endif
    // Notify unavailability.
    if ( mbRegisterWithHwc )
    {
        notifyUnavailable( );
    }
}

void DrmDisplay::consumeSuspend( uint32_t timelineIndex, bool bUseDPMS, bool bDeactivateDisplay )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    if ( meStatus == AVAILABLE )
    {
        Log::alogd( DRM_SUSPEND_DEBUG,
                    "*************************** SUSPEND " DRMDISPLAY_ID_STR " DPMS:%d (Status:%u) *******************************",
                    DRMDISPLAY_ID_PARAMS, bUseDPMS, meStatus );

        Log::alogd( DRM_SUSPEND_DEBUG, DRMDISPLAY_ID_STR " -> SUSPENDED", DRMDISPLAY_ID_PARAMS );

        // Reset display.
        // This will set blanking and SUSPENDED status.
        resetDisplay( );

        // Advance the timeline.
        // This is to ensure all prior frames are released.
        mPageFlipHandler.releaseTo( timelineIndex );

#if VPG_DRM_HAVE_SCREEN_CTL
        // Disable screen.
        if ( mbScreenCtlOn )
        {
            ALOGD_IF( DRM_BLANKING_DEBUG, DRMDISPLAY_ID_STR " screen ctl = 0", DRMDISPLAY_ID_PARAMS );
            mDrm.screenCtl( getDrmCrtcID(), 0 );
            mbScreenCtlOn = false;
        }
#endif

        if ( bUseDPMS )
        {
            Log::alogd( DRM_SUSPEND_DEBUG, DRMDISPLAY_ID_STR " SUSPENDED DPMS_OFF", DRMDISPLAY_ID_PARAMS );
#if VPG_DRM_HAVE_ASYNC_DPMS
            bool bOK = ( mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_ASYNC_OFF ) == Drm::SUCCESS );
            const uint32_t wait25Ms = 25000;
            const uint32_t timeout300Ms = 300000;
            uint32_t totalWait = 0;
            for (;;)
            {
                if ( !bOK )
                {
                    ALOGW( "DRM_MODE_DPMS_ASYNC_OFF did not complete - forcing DRM_MODE_DPMS_OFF" );
                    mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_OFF );
                    break;
                }
                usleep( wait25Ms );
                int32_t dpms = mDrm.getDPMSProperty( mDrmConnectorID, mPropDPMS );
                if ( dpms == DRM_MODE_DPMS_OFF )
                {
                    break;
                }
                bOK = ( dpms >= 0 );
                totalWait += wait25Ms;
                if ( totalWait >= timeout300Ms )
                {
                    bOK = false;
                }
            }
#else
            mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_OFF );
#endif
            mbSuspendDPMS = true;
        }

        // Some optimisations are dependent on suspend mode.
        mHwc.forceGeometryChangeAndRedraw();

        // Optionally, deactivate display (releases all resources such as dbuf).
        if ( bDeactivateDisplay )
        {
            // Update active displays and broadcast change.
            mDrm.setActiveDisplay( getDrmDisplayID(), false );
            mbSuspendDeactivated = true;
        }

        ALOGD_IF( DRM_SUSPEND_DEBUG, "*************************************************************************" );
    }
}

void DrmDisplay::consumeResume( void )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    if ( meStatus != AVAILABLE )
    {
        Log::alogd( DRM_SUSPEND_DEBUG,
                    "*************************** RESUME " DRMDISPLAY_ID_STR " (Status:%u) *******************************",
                    DRMDISPLAY_ID_PARAMS, meStatus );

        if ( !mActiveConnection.isConnected() || !mActiveConnection.hasPipe() )
        {
            ALOGE( DRMDISPLAY_ID_STR " Can not resume display [isConnected:%d hasPipe:%d]",
                DRMDISPLAY_ID_PARAMS, mActiveConnection.isConnected(), mActiveConnection.hasPipe() );
            return;
        }

        if ( mbSuspendDeactivated )
        {
            // Update active displays and broadcast change.
            if ( mDrm.setActiveDisplay( getDrmDisplayID(), true ) )
            {
                // We must synchronize on platforms that need to adjust cross-pipe resources before this displays completes its resume.
                ALOGD_IF( DRM_BLANKING_DEBUG, DRMDISPLAY_ID_STR "Synchronizing pre-resume", DRMDISPLAY_ID_PARAMS );
                synchronizeFromConsumer();
                ALOGD_IF( DRM_BLANKING_DEBUG, DRMDISPLAY_ID_STR "Synchronizing pre-resume complete", DRMDISPLAY_ID_PARAMS );
            }
            mbSuspendDeactivated = false;
        }

        Log::alogd( DRM_SUSPEND_DEBUG, DRMDISPLAY_ID_STR " -> AVAILABLE", DRMDISPLAY_ID_PARAMS );

        if ( mbSuspendDPMS )
        {
            Log::alogd( DRM_SUSPEND_DEBUG, DRMDISPLAY_ID_STR " DPMS_ON", DRMDISPLAY_ID_PARAMS );
#if VPG_DRM_HAVE_ASYNC_DPMS
            bool bOK = ( mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_ASYNC_ON ) == Drm::SUCCESS );
            const uint32_t wait25Ms = 25000;
            const uint32_t timeout300Ms = 300000;
            uint32_t totalWait = 0;
            for (;;)
            {
                if ( !bOK )
                {
                    ALOGW( "DRM_MODE_DPMS_ASYNC_ON did not complete - forcing DRM_MODE_DPMS_ON" );
                    mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_ON );
                    break;
                }
                usleep( wait25Ms );
                int32_t dpms = mDrm.getDPMSProperty( mDrmConnectorID, mPropDPMS );
                if ( dpms == DRM_MODE_DPMS_ON )
                {
                    break;
                }
                bOK = ( dpms >= 0 );
                totalWait += wait25Ms;
                if ( totalWait >= timeout300Ms )
                {
                    bOK = false;
                }
            }
#else
            mDrm.setDPMSProperty( mDrmConnectorID, mPropDPMS, DRM_MODE_DPMS_ON );
#endif
            mbSuspendDPMS = false;
        }

        // Set display - establish current mode.
        // This will set blanking and AVAILABLE status.
        setDisplay( );

#if VPG_DRM_HAVE_SCREEN_CTL
        // Enable screen.
        if ( !mbScreenCtlOn )
        {
            ALOGD_IF( DRM_BLANKING_DEBUG, DRMDISPLAY_ID_STR " screen ctl = 1", DRMDISPLAY_ID_PARAMS );
            mDrm.screenCtl( getDrmCrtcID(), 1 );
            mbScreenCtlOn = true;
        }
#endif

        // Some optimisations are dependent on suspend mode.
        mHwc.forceGeometryChangeAndRedraw();

        ALOGD_IF( DRM_SUSPEND_DEBUG, "*************************************************************************" );
    }
}

void DrmDisplay::setBlanking( void )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD
    ALOGD_IF( DRM_DISPLAY_DEBUG, DRMDISPLAY_ID_STR " Set blanking", DRMDISPLAY_ID_PARAMS );

    int32_t zorder = -1;
    const uint32_t numZOrders = getDisplayCaps().getNumZOrders( );
    const DisplayCaps::ZOrderLUTEntry* pZOrders = getDisplayCaps().getZOrderLUT( );
    if ( numZOrders && pZOrders )
    {
        zorder = pZOrders[0].getDisplayEnum( );
    }

    if (numZOrders == 0)
    {
        zorder = 0;
    }

    // Set blanking stack to mode size.
    allocateBlankingLayer( getAppliedWidth(), getAppliedHeight() );
    Content::LayerStack stack( &getBlankingLayer( ), 1 );

    // We must avoid using a 'real' timeline index here because there may
    // already be other frames queued up behind this and that would put the
    // timeline out-of-order. Just use a placeholder frameId for the blank frame.
    // The flip completion will spot this and just release the previous frame when
    // it completes the flip for the blanking frame.
    DisplayQueue::FrameId frameId;

    // Create and set the custom frame.
    DisplayQueue::Frame* pBlankingFrame = new DisplayQueue::Frame( );
    if ( !pBlankingFrame )
    {
        ALOGE( "Failed to create blanking frame" );
        return;
    }

    const uint32_t w = getAppliedWidth();
    const uint32_t h = getAppliedHeight();

    // Reset global scaling to disabled/mode size.
    SGlobalScalingConfig scalingCfg;
    scalingCfg.mSrcW = w;
    scalingCfg.mSrcH = h;
    scalingCfg.mDstX = 0;
    scalingCfg.mDstY = 0;
    scalingCfg.mDstW = w;
    scalingCfg.mDstH = h;
    scalingCfg.mbEnabled = false;

    // Current mode/refresh.
    Frame::Config config( w, h, getRefresh(), scalingCfg );

    pBlankingFrame->set( stack, zorder, frameId, config );
    pBlankingFrame->setType( eFT_BlankingFrame );

    // Flip the frame synchronously.
    ALOGD_IF( DRM_DISPLAY_DEBUG, "Flip custom frame %p (type eFT_BlankingFrame)", pBlankingFrame );

    // Direct flip the custom frame to page flip handler.
    if ( !mPageFlipHandler.flip( pBlankingFrame ) )
    {
        // Delete the custom frame immediately if the flip was not applied.
        delete pBlankingFrame;
    }
}

void DrmDisplay::releaseFlippedFrame( Frame* pOldFrame )
{
    ALOG_ASSERT( pOldFrame );

    if ( pOldFrame->getType() == Frame::eFT_DISPLAY_QUEUE )
    {
        releaseFrame( pOldFrame );
        return;
    }

    // Handle remaining frames here.
    // The only custom frame type we expect is for blanking.
    ALOG_ASSERT( pOldFrame->getType() == eFT_BlankingFrame );

    // Delete the frame.
    ALOGD_IF( DRM_DISPLAY_DEBUG, "Delete custom frame %p (eFT_BlankingFrame)", pOldFrame );
    delete pOldFrame;
}

void DrmDisplay::consumeFrame( DisplayQueue::Frame* pNewDisplayFrame )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    // Display frame must be specified.
    ALOG_ASSERT( pNewDisplayFrame );

    // We only expect display queue frames via this API.
    ALOG_ASSERT( pNewDisplayFrame->getType() == Frame::eFT_DISPLAY_QUEUE );

    // If it is a display queue frame then it must have been marked as on display before being flipped.
    ALOG_ASSERT( pNewDisplayFrame->isLockedForDisplay() );

    bool mbFlipped = false;

    // Flip valid frames, retire invalid frames.
    if ( pNewDisplayFrame->isValid() )
    {
        // Issue any pending mode changes before flipping this next frame.
        updateTiming( *pNewDisplayFrame );
        // Attempt the flip.
        mbFlipped = mPageFlipHandler.flip( pNewDisplayFrame );
    }
    else
    {
        // Retire invalid frames.
        mPageFlipHandler.retire( pNewDisplayFrame );
    }

    // Release the DisplayQueue frame immediately if the flip failed or the frame was retired.
    if ( !mbFlipped )
    {
        releaseFrame( pNewDisplayFrame );
    }

    considerReleasingBuffers( );
}

void DrmDisplay::processPending( void )
{
    // Complete start.
    if ( meStatus == AVAILABLE_PENDING_START )
    {
        Log::alogd( DRM_DISPLAY_DEBUG, "Completing start" );
        setDisplay();
        // If setDisplay() flipped blanking itself then we MUST
        // sync here before trying to flip *this* frame.
        if ( defaultFrameRequired( ) )
        {
            Log::alogd( DRM_DISPLAY_DEBUG, "Syncing default frame" );
            mPageFlipHandler.sync( );
        }
    }
}

bool DrmDisplay::updateTiming( const DisplayQueue::Frame& frame )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    bool bRet = false;

    if (meStatus != SUSPENDED)
    {
        uint32_t timingIndex;

        // Process new timing requests (forward notification).
        notifyNewRequestedTiming();

        // Check if we are waiting to apply a previous notified timing change.
        if ( haveNotifiedTimingChange( timingIndex ) )
        {
            ALOGD_IF( DRMDISPLAY_MODE_DEBUG, "Waiting to change to notified timing %u", timingIndex );
            Timing t;
            if ( copyDisplayTiming( timingIndex, t ) )
            {
                // Apply the timing change once we receive the first frame that matches the desired frame size.
                if ( ( frame.getConfig().getWidth( ) == t.getWidth() )
                  && ( frame.getConfig().getHeight( ) == t.getHeight() ) )
                {
                    Log::alogd( DRMDISPLAY_MODE_DEBUG, DRMDISPLAY_ID_STR " timing change for new content size %ux%u (timing change %u %s)",
                        DRMDISPLAY_ID_PARAMS,
                        frame.getConfig().getWidth( ), frame.getConfig().getHeight( ),
                        timingIndex,
                        t.dump().string() );
                    // Reset display (current mode).
                    resetDisplay( );
                    // Apply requested mode.
                    setAppliedTiming( timingIndex );
                    // Set display (new mode).
                    setDisplay( );
                    bRet = true;
                }
            }
        }
        else
        {
            uint32_t filterRequestedRefresh = frame.getConfig().getRefresh();
            if (mFilterAppliedRefresh != filterRequestedRefresh)
            {
                if (mbSeamlessDRRSSupported)
                {
                    Timing t;
                    if ( copyDisplayTiming( getAppliedTimingIndex(), t ) )
                    {
                        mSeamlessRequestedRefresh = findBestRefresh(filterRequestedRefresh, t.getMinRefresh(),t.getRefresh());
                        Log::alogd( DRM_DISPLAY_DEBUG, DRMDISPLAY_ID_STR " seamless DRRS change to %u for content refresh change %u->%u",
                            DRMDISPLAY_ID_PARAMS, mSeamlessRequestedRefresh, mFilterAppliedRefresh, filterRequestedRefresh );
                    }
                }
                else if (getDisplayType() == eDTExternal)
                {
                    // Only go looking for a non user requested mode if we ask for a lower refresh.
                    int32_t timingIndex = getAppliedTimingIndex();
                    Timing t;
                    if ( copyDisplayTiming( timingIndex, t ) )
                    {
                        if (t.getRefresh() > filterRequestedRefresh)
                        {
                            Timing nt(t.getWidth(), t.getHeight(), filterRequestedRefresh,
                                      0, 0, 0, t.getRatio(), t.getFlags() & ~Timing::Flag_Preferred);
                            timingIndex = findDisplayTiming(nt, FIND_MODE_FLAG_CLOSEST_REFRESH_MULTIPLE);
                        }
                        if ((timingIndex >= 0) && ((uint32_t)timingIndex != mDynamicAppliedTimingIndex))
                        {
                            Log::alogd( DRM_DISPLAY_DEBUG, DRMDISPLAY_ID_STR " timing change to %u from %u for content refresh change %u->%u",
                                DRMDISPLAY_ID_PARAMS, timingIndex, mDynamicAppliedTimingIndex, mFilterAppliedRefresh, filterRequestedRefresh );
                            mDynamicAppliedTimingIndex = timingIndex;
                            resetDisplay( );
                            setDisplay( mDynamicAppliedTimingIndex );
                        }
                    }
                }
                else
                {
                    Log::alogd( DRM_DISPLAY_DEBUG, DRMDISPLAY_ID_STR " unhandled content refresh change %u->%u",
                        DRMDISPLAY_ID_PARAMS, mFilterAppliedRefresh, filterRequestedRefresh );
                }
                mFilterAppliedRefresh = filterRequestedRefresh;
            }
        }
    }

    return bRet;
}

void DrmDisplay::setVSync( bool bEnable )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mSetVSyncLock );

    // NOTE:
    //  This must be thread safe since it services both SF event
    //  control requests received via onVSyncEnable() and internal
    //  updates via startup/shutdown/suspend/resume events.
    Mutex::Autolock _l( mSetVSyncLock );
    doSetVSync( bEnable );
    mbVSyncGenEnabled = bEnable;
}

void DrmDisplay::doSetVSync( bool bEnable )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mSetVSyncLock );

    if ( bEnable )
    {
        bool bUseSoftwareVSync = true;

#if ENABLE_HARDWARE_VSYNC
        if ( isAvailable( ) )
        {
            disableSoftwareVSyncGeneration( );

            if ( mDrm.enableVSync( this ) )
            {
                ATRACE_INT_IF( VSYNC_DEBUG, String8::format( "HWC:P%u(" DRMDISPLAY_ID_STR  ") HW VSYNC", getDisplayManagerIndex(), DRMDISPLAY_ID_PARAMS ).string(), 1 );
                Log::alogd( VSYNC_DEBUG, "HWC:P%u(" DRMDISPLAY_ID_STR  ") HW VSYNC Enabled", getDisplayManagerIndex(), DRMDISPLAY_ID_PARAMS );
                mbDrmVsyncEnabled = true;
                bUseSoftwareVSync = false;
            }
        }
#endif
        if ( bUseSoftwareVSync )
        {
            createSoftwareVSyncGeneration( );
            enableSoftwareVSyncGeneration( );
        }
        else
        {
            destroySoftwareVSyncGeneration( );
        }
    }
    else
    {
#if ENABLE_HARDWARE_VSYNC
        if ( mbDrmVsyncEnabled )
        {
            mDrm.disableVSync( this, false );
            ATRACE_INT_IF( VSYNC_DEBUG, String8::format( "HWC:P%u(" DRMDISPLAY_ID_STR ") HW VSYNC", getDisplayManagerIndex(), DRMDISPLAY_ID_PARAMS ).string(), 0 );
            Log::alogd( VSYNC_DEBUG, "HWC:P%u(" DRMDISPLAY_ID_STR  ") HW VSYNC Disabled", getDisplayManagerIndex(), DRMDISPLAY_ID_PARAMS );
            mbDrmVsyncEnabled = false;
        }
#endif
        disableSoftwareVSyncGeneration( );
    }
}

void DrmDisplay::syncFlip( void )
{
    mPageFlipHandler.sync( );
}

// *****************************************************************************
// Display Queue
// *****************************************************************************

class DrmDisplay::EventStartup : public DisplayQueue::Event
{
public:
    EventStartup( const Connection& newConnection, bool bNew ) :
        Event( EVENT_STARTUP ), mNewConnection( newConnection ), mbNew( bNew ) { }
    virtual String8 dump( void ) const
    {
        return DisplayQueue::Event::dump()
                + String8::format( " EVENT_STARTUP[CONNECTION:%s, NEW:%d]",
                mNewConnection.dump().string(), mbNew );
    }
    Connection mNewConnection;
    bool mbNew:1;
};

class DrmDisplay::EventShutdown : public DisplayQueue::Event
{
public:
    EventShutdown( uint32_t timelineIndex ) :
        Event( EVENT_SHUTDOWN ),
        mTimelineIndex( timelineIndex )
    {
    }
    virtual String8 dump( void ) const
    {
        return DisplayQueue::Event::dump()
                + String8::format( " EVENT_SHUTDOWN[TIMELINE:%u]",
                    mTimelineIndex );
    }
    uint32_t mTimelineIndex;
};

class DrmDisplay::EventSuspend : public DisplayQueue::Event
{
public:
    EventSuspend( uint32_t timelineIndex, bool bUseDPMS, bool bDeactivateDisplay ) :
        Event( EVENT_SUSPEND ),
        mTimelineIndex( timelineIndex ),
        mbUseDPMS( bUseDPMS ),
        mbDeactivateDisplay( bDeactivateDisplay )
    {
    }
    virtual String8 dump( void ) const
    {
        return DisplayQueue::Event::dump()
                + String8::format( " EVENT_SUSPEND[TIMELINE:%u, DPMS:%d Deactivate:%d]",
                    mTimelineIndex, mbUseDPMS, mbDeactivateDisplay );
    }
    uint32_t mTimelineIndex;
    bool mbUseDPMS:1;
    bool mbDeactivateDisplay:1;
};

class DrmDisplay::EventResume : public DisplayQueue::Event
{
public:
    EventResume( ) : Event( EVENT_RESUME ) { }
    virtual String8 dump( void ) const
    {
        return DisplayQueue::Event::dump() + String8( " EVENT_RESUME" );
    }
};

void DrmDisplay::disableAllEncryptedSessions( void )
{
    Log::add( "DRM Display Self Teardown" );
    int64_t p;
    HwcService& hwcService = HwcService::getInstance();
    hwcService.notify(HwcService::ePavpDisableAllEncryptedSessions, 0, &p);
}

String8 DrmDisplay::queueStateDump( void )
{
    String8 stateDesc;
#define INTEL_HWC_DRMDISPLAY_CASE_QUEUE_STATE(A) case QUEUE_STATE_##A : return String8( #A );
    uint32_t state = meQueueState;
    switch ( state )
    {
        INTEL_HWC_DRMDISPLAY_CASE_QUEUE_STATE( SHUTDOWN );
        INTEL_HWC_DRMDISPLAY_CASE_QUEUE_STATE( STARTED );
        INTEL_HWC_DRMDISPLAY_CASE_QUEUE_STATE( SUSPENDED );
        // No default - all enums must be included.
    }
#undef INTEL_HWC_DRMDISPLAY_CASE_QUEUE_STATE
    return String8( "n/a" );
}

int DrmDisplay::queueStartup( const Connection& newConnection, bool bNew )
{
    int ret = -1;
    // Set will trigger mode set/reset.
    if ( mOptionSelfTeardown.get() & SELF_TEARDOWN_SET )
    {
        Log::add( "Drm Display Startup => Self Teardown" );
        disableAllEncryptedSessions();
    }
    Mutex::Autolock _l( mSyncQueue );
    if ( meQueueState == QUEUE_STATE_SHUTDOWN )
    {
        ret = queueEvent( new EventStartup( newConnection, bNew ) );
        if ( ret == OK )
        {
            meQueueState = QUEUE_STATE_STARTED;
        }
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " queue startup connection %s new %d %s[QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, newConnection.dump().string(), bNew,
                    ret == OK ? "" : "*FAILED* ",
                    queueStateDump().string() );
    }
    else
    {
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " not ready for startup [QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, queueStateDump().string() );
    }
    return ( meQueueState == QUEUE_STATE_STARTED ) ? OK : -1;
}

int DrmDisplay::queueShutdown( void )
{
    int ret = -1;
    if ( mOptionSelfTeardown.get() & SELF_TEARDOWN_RESET )
    {
        Log::add( "Drm Display Shutdown => Self Teardown" );
        disableAllEncryptedSessions();
    }
    Mutex::Autolock _l( mSyncQueue );
    if ( ( meQueueState == QUEUE_STATE_STARTED )
      || ( meQueueState == QUEUE_STATE_SUSPENDED ) )
    {
        // Create a timeline slot so we can be sure to release all frames queued prior to the shutdown.
        uint32_t timelineIndex = 0;
        Timeline::NativeFence fenceFd = mPageFlipHandler.registerNextFutureFrame( &timelineIndex );
        Timeline::closeFence( &fenceFd );
        ret = queueEvent( new EventShutdown( timelineIndex ) );
        if ( ret == OK )
        {
            meQueueState = QUEUE_STATE_SHUTDOWN;
        }
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " queue shutdown Timeline %u %s[QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, timelineIndex,
                    ret == OK ? "" : "*FAILED* ",
                    queueStateDump().string() );
    }
    else
    {
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " not ready for shutdown [QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, queueStateDump().string() );
    }
    return ( meQueueState == QUEUE_STATE_SHUTDOWN ) ? OK : -1;
}

int DrmDisplay::queueSuspend( bool bUseDPMS, bool bDeactivateDisplay )
{
    if ( mOptionSelfTeardown.get() & SELF_TEARDOWN_SUSPEND )
    {
        Log::add( "Drm Display Suspend => Self Teardown" );
        disableAllEncryptedSessions();
    }
    Mutex::Autolock _l( mSyncQueue );
    if ( meQueueState == QUEUE_STATE_STARTED )
    {
        // Create a timeline slot so we can be sure to release all frames queued prior to the suspend.
        uint32_t timelineIndex = 0;
        Timeline::NativeFence fenceFd = mPageFlipHandler.registerNextFutureFrame( &timelineIndex );
        Timeline::closeFence( &fenceFd );
        int ret = queueEvent( new EventSuspend( timelineIndex, bUseDPMS, bDeactivateDisplay ) );
        if ( ret == OK )
        {
            meQueueState = QUEUE_STATE_SUSPENDED;
        }
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " queue suspend Timeline %u UseDPMS %d DeactivateDisplay %d %s[QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, timelineIndex, bUseDPMS, bDeactivateDisplay,
                    ret == OK ? "" : "*FAILED* ",
                    queueStateDump().string() );
    }
    else
    {
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " not ready for suspend [QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, queueStateDump().string() );
    }
    return ( meQueueState == QUEUE_STATE_SUSPENDED ) ? OK : -1;
}

int DrmDisplay::queueResume( void )
{
    int ret = -1;
    if ( mOptionSelfTeardown.get() & SELF_TEARDOWN_RESUME )
    {
        Log::add( "Drm Display Resume => Self Teardown" );
        disableAllEncryptedSessions();
    }
    Mutex::Autolock _l( mSyncQueue );
    if ( meQueueState == QUEUE_STATE_SUSPENDED )
    {
        ret = queueEvent( new EventResume( ) );
        if ( ret == OK )
        {
            meQueueState = QUEUE_STATE_STARTED;
        }
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " queue resume %s[QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS,
                    ret == OK ? "" : "*FAILED* ",
                    queueStateDump().string() );
    }
    else
    {
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " not ready for resume [QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, queueStateDump().string() );
    }
    return ( meQueueState == QUEUE_STATE_STARTED ) ? OK : -1;
}

int DrmDisplay::queueFrame( const Content::Display& display, uint32_t zorder, int* pRetireFenceFd )
{
    DRMDISPLAY_ASSERT_PRODUCER_THREAD

    const Content::LayerStack& stack = display.getLayerStack();
    const uint32_t hwcFrameIndex = display.getFrameIndex();
    const nsecs_t hwcFrameReceivedTime = display.getFrameReceivedTime();
    const Frame::Config config( display, mGlobalScalingRequested );

    ALOGD_IF( DRM_DISPLAY_DEBUG, DRMDISPLAY_ID_STR " Queue frame %u", DRMDISPLAY_ID_PARAMS, hwcFrameIndex );

    ALOG_ASSERT( pRetireFenceFd );
    ALOG_ASSERT( *pRetireFenceFd == -1 );

    Mutex::Autolock _l( mSyncQueue );
    if ( meQueueState != QUEUE_STATE_STARTED )
    {
        // Drop all frame if not started or if suspended.
        // We still need to return a fence.
        // We return a fence that repeats the previous frame's timeline index.
        uint32_t timelineIndex;
        *pRetireFenceFd = mPageFlipHandler.registerRepeatFutureFrame( &timelineIndex );

        // Replicate frame retire fence to layers' release fences.
        stack.setAllReleaseFences( *pRetireFenceFd );

        DisplayQueue::FrameId frameId( timelineIndex, hwcFrameIndex, hwcFrameReceivedTime );

        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " drop %s, retire fence %s [QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, frameId.dump( ).string( ),
                    Timeline::dumpFence(pRetireFenceFd).string( ),
                    queueStateDump().string() );

        // Keep display queue state aligned.
        // Once the last queue work is consumed then we have effectively
        // issued this frame we are dropping.
        queueDrop( frameId );

        return OK;
    }


    // Get the new future frame retire fence.
    uint32_t timelineIndex;
    *pRetireFenceFd = mPageFlipHandler.registerNextFutureFrame( &timelineIndex );

    // Replicate frame retire fence to layers' release fences.
    stack.setAllReleaseFences( *pRetireFenceFd );

    DisplayQueue::FrameId frameId( timelineIndex, hwcFrameIndex, hwcFrameReceivedTime );

    // Queue the frame for consumption.
    if ( DisplayQueue::queueFrame( stack, zorder, frameId, config ) == OK )
    {
        Log::alogd( DRM_DISPLAY_DEBUG, "drm " DRMDISPLAY_ID_STR " queue %s, retire fence %s [QUEUE:%s]",
                    DRMDISPLAY_ID_PARAMS, frameId.dump( ).string( ),
                    Timeline::dumpFence(pRetireFenceFd).string( ),
                    queueStateDump().string() );
        return OK;
    }
    else
    {
        ALOGE( "Failed DisplayQueue::queueFrame" );
        Timeline::closeFence( pRetireFenceFd );
        return -1;
    }
}

void DrmDisplay::consumeWork( DisplayQueue::WorkItem* pWork )
{
    DRMDISPLAY_ASSERT_CONSUMER_THREAD

    ALOG_ASSERT( pWork );

    // Process generic pending work (if any).
    processPending();

    if ( pWork->getWorkItemType() == WorkItem::WORK_ITEM_FRAME )
    {
        // Consume a frame.
        DisplayQueue::Frame* pFrame = static_cast<DisplayQueue::Frame*>( pWork );
        consumeFrame( pFrame );
    }
    else if ( pWork->getWorkItemType() == WorkItem::WORK_ITEM_EVENT )
    {
        // Consume an event.
        DisplayQueue::Event* pEvent = static_cast<DisplayQueue::Event*>( pWork );
        switch ( pEvent->getId( ) )
        {
            case EVENT_STARTUP:
                {
                    Log::add( DRMDISPLAY_ID_STR " EVENT_STARTUP", DRMDISPLAY_ID_PARAMS );
                    EventStartup* pEvStartup = static_cast<EventStartup*>(pEvent);
                    consumeStartup( pEvStartup->mNewConnection, pEvStartup->mbNew );
                }
                break;

            case EVENT_SHUTDOWN:
                {
                    Log::add( DRMDISPLAY_ID_STR " EVENT_SHUTDOWN", DRMDISPLAY_ID_PARAMS );
                    EventShutdown* pEvShutdown = static_cast<EventShutdown*>(pEvent);
                    consumeShutdown( pEvShutdown->mTimelineIndex );
                }
                break;

            case EVENT_SUSPEND:
                if ( !isSuspended( ) )
                {
                    Log::add( DRMDISPLAY_ID_STR " EVENT_SUSPEND", DRMDISPLAY_ID_PARAMS );
                    EventSuspend* pEvSuspend = static_cast<EventSuspend*>(pEvent);
                    consumeSuspend( pEvSuspend->mTimelineIndex, pEvSuspend->mbUseDPMS, pEvSuspend->mbDeactivateDisplay );
                }
                break;

            case EVENT_RESUME:
                if ( isSuspended( ) )
                {
                    Log::add( DRMDISPLAY_ID_STR " EVENT_RESUME", DRMDISPLAY_ID_PARAMS );
                    consumeResume( );
                }
                break;
        }
    }
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
