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
#include "DrmPageFlipHandler.h"
#include "Drm.h"
#include "DrmDisplay.h"
#include "DisplayCaps.h"
#include "Utils.h"
#include "Log.h"
#include "DrmEventThread.h"
#include "DrmLegacyPageFlipHandler.h"
#if VPG_DRM_HAVE_ATOMIC_NUCLEAR
#include "DrmNuclearPageFlipHandler.h"
#endif
#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY
#include "DrmSetDisplayPageFlipHandler.h"
#endif
#include "OptionManager.h"

#define DRM_PFH_NAME "DrmPageFlip"

#define DRMDISPLAY_ID_STR               "DrmDisplay %u DrmConnector %u [Crtc %u]"
#define DRMDISPLAY_ID_PARAMS            mDisplay.getDrmDisplayID(), mDisplay.getDrmConnectorID(), mDisplay.getDrmCrtcID()

namespace intel {
namespace ufo {
namespace hwc {

// *****************************************************************************
// DrmPageFlipHandler
// *****************************************************************************

DrmPageFlipHandler::DrmPageFlipHandler( DrmDisplay& display ) :
    mDrm( Drm::get( ) ),
    mDisplay( display ),
    mpImpl( NULL ),
    mbInit( false ),
    mNumPlanes( 0 ),
    mMainPlaneIndex( -1 ),
    mLastPageFlipTime( 0ULL ),
    mpLastFlippedFrame( NULL ),
    mpCurrentFrame( NULL )
{
}

DrmPageFlipHandler::~DrmPageFlipHandler( )
{
}

void DrmPageFlipHandler::startupDisplay( void )
{
    // Initialise the display's retirement timeline.
    String8 name = String8::format( "HWC.DRM%d", mDisplay.getDrmDisplayID() );
    if ( !mTimeline.init( name ) )
    {
        ALOGE( "Failed to create sync timeline for %s", name.string() );
    }
}

void DrmPageFlipHandler::init()
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Initialising", DRMDISPLAY_ID_PARAMS );
    Mutex::Autolock _l( mLockPageFlip );

    if ( mbInit )
    {
        return;
    }

    const DisplayCaps& genCaps = mDisplay.getDisplayCaps( );
    const DrmDisplayCaps& drmCaps = mDisplay.getDrmDisplayCaps( );
    mNumPlanes = genCaps.getNumPlanes( );
    mMainPlaneIndex = -1;
    for ( uint32_t p = 0; p < mNumPlanes; ++p )
    {
        const DrmDisplayCaps::PlaneCaps& planeCaps = drmCaps.getPlaneCaps( p );
        if ( planeCaps.getDrmPlaneType( ) == DrmDisplayCaps::PLANE_TYPE_MAIN )
        {
            mMainPlaneIndex = p;
            break;
        }
    }

    delete mpImpl;
    mpImpl = NULL;

#if VPG_DRM_HAVE_ATOMIC_NUCLEAR
    if ( !mpImpl && DrmNuclearPageFlipHandler::test( mDisplay ) )
    {
        mpImpl = new DrmNuclearPageFlipHandler( mDisplay );
    }
#endif
#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY
    if ( !mpImpl && DrmSetDisplayPageFlipHandler::test( mDisplay ) )
    {
        mpImpl = new DrmSetDisplayPageFlipHandler( mDisplay );
    }
#endif

    if (!mpImpl)
    {
        // Fallback path if no atomic API is available
        mpImpl = new DrmLegacyPageFlipHandler( mDisplay );

        // Disable the plane allocator in legacy codepaths. This should result
        // in full screen composition to main plane always.
        Option* pOption = OptionManager::find("planealloc");
        if (pOption)
            pOption->set(0);
    }


    mbInit = true;
}

void DrmPageFlipHandler::uninit( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Uninitialising", DRMDISPLAY_ID_PARAMS );
    Mutex::Autolock _l( mLockPageFlip );

    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockPageFlip );
    if ( !mbInit )
        return;

    // Sync trailing flip.
    doSync( );

    // Everything should be complete.
    ALOG_ASSERT( !isOutstandingFlipWork( ) );

    // Uninit specialisation.
    delete mpImpl;
    mpImpl = NULL;

    mbInit = false;
}

Timeline::NativeFence DrmPageFlipHandler::registerNextFutureFrame( uint32_t* pTimelineIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );
    // NOTE:
    // Lock is not taken here so that onSet can retrieve fence with no stalls.
    Timeline::NativeFence fence = mTimeline.createFence( pTimelineIndex );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG,
        DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Registered next future frame %d/%u",
        DRMDISPLAY_ID_PARAMS, fence, *pTimelineIndex );
    return fence;
}

Timeline::NativeFence DrmPageFlipHandler::registerRepeatFutureFrame( uint32_t* pTimelineIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );
    // NOTE:
    // Lock is not taken here so that onSet can retrieve fence with no stalls.
    Timeline::NativeFence fence = mTimeline.repeatFence( pTimelineIndex );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG,
        DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Registered repeat future frame %d/%u",
        DRMDISPLAY_ID_PARAMS, fence, *pTimelineIndex );
    return fence;
}

void DrmPageFlipHandler::releaseTo( uint32_t timelineIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );
    Mutex::Autolock _l( mLockPageFlip );
    Log::alogd( DRM_PAGEFLIP_DEBUG, "drm releaseTo " DRMDISPLAY_ID_STR " [timeline:%u]", DRMDISPLAY_ID_PARAMS, timelineIndex );
    mTimeline.advanceTo( timelineIndex );
}

bool DrmPageFlipHandler::readyForFlip( void )
{
    Mutex::Autolock _l( mLockPageFlip );
    if ( isOutstandingFlipWork( ) )
    {
        nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
        uint32_t ela = (uint32_t)int64_t( now - mLastPageFlipTime );
        if ( ela > mTimeoutFlip )
        {
            Log::aloge( true, "Drm " DRMDISPLAY_ID_STR " flip completion timeout", DRMDISPLAY_ID_PARAMS );
            completeFlip( );
        }
    }
    return !isOutstandingFlipWork( );
}

bool DrmPageFlipHandler::flip( DisplayQueue::Frame* pNewFrame )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );
    Mutex::Autolock _l( mLockPageFlip );

    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Flip : Entry %s", DRMDISPLAY_ID_PARAMS, getStatusString( ).string( ) );
    ALOG_ASSERT( pNewFrame );

    bool bFlipped = false;

    // Skip frames when not initialised (=> unplugged/suspended).
    if ( mbInit )
    {
        const DisplayQueue::FrameId& newFrameId = pNewFrame->getFrameId( );
        ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( "Flip Frame %s", newFrameId.dump().string() ) );

        ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Flipping to frame %s",
                  DRMDISPLAY_ID_PARAMS, newFrameId.dump().string() );

        // First entry is main plane and is assumed always set.
        DisplayQueue::FrameLayer* pMain = NULL;
        bool bMainBlanked = false;

        if ( mMainPlaneIndex != -1 )
        {
            pMain = pNewFrame->editLayer( mMainPlaneIndex );
            bMainBlanked = pMain && pMain->isDisabled( );
            if ( bMainBlanked )
            {
                // The blanking layer must be primed for this frame, being careful to adjust for global scaling if it is enabled.
                const PhysicalDisplay::SGlobalScalingConfig& globalScalingNew = pNewFrame->getConfig().getGlobalScaling();
                uint32_t w, h;
                if ( globalScalingNew.mbEnabled )
                {
                    w = globalScalingNew.mSrcW;
                    h = globalScalingNew.mSrcH;
                    Log::alogd( DRM_PAGEFLIP_DEBUG,
                                DRM_PFH_NAME " Drm " DRMDISPLAY_ID_STR " blanking layer from global scaling source size %ux%u",
                                DRMDISPLAY_ID_PARAMS, w, h );
                }
                else
                {
                    w = mDisplay.getAppliedWidth();
                    h = mDisplay.getAppliedHeight();
                    Log::alogd( DRM_PAGEFLIP_DEBUG,
                                DRM_PFH_NAME " Drm " DRMDISPLAY_ID_STR " blanking layer from display mode size %ux%u",
                                DRMDISPLAY_ID_PARAMS, w, h );
                }
                mDisplay.allocateBlankingLayer( w, h );

                Log::alogd(DRMDISPLAY_MODE_DEBUG, "Using Blanking Layer: %s", mDisplay.getBlankingLayer().dump().string());

                // Replace the existing main layer with the blanking layer.
                // This ensures the blanking buffer will exist until it has been
                // removed from the display with a subsequent flip.
                pMain->reset( true );
                pMain->set( mDisplay.getBlankingLayer() );
            }
        }

        // Sync with previous flip.
        doSync();

        // Following synchronisation we should have no outstanding flip work.
        ALOG_ASSERT( !isOutstandingFlipWork( ) );

        Log::add( DRM_PFH_NAME " Drm " DRMDISPLAY_ID_STR " issuing drm updates for %s",
            DRMDISPLAY_ID_PARAMS, newFrameId.dump().string() );

        // Flip specialisation.
        if ( mpImpl )
        {
            // Validate just prior to flip.
            pNewFrame->validate();
            // Pending page flip depends on implementation flip success.
            uint32_t eventData = DrmEventThread::encodeIndex( mDisplay.getDrmDisplayID() );
            bFlipped = mpImpl->doFlip( pNewFrame, bMainBlanked, eventData );
            if ( bFlipped )
            {
                mLastPageFlipTime = systemTime(SYSTEM_TIME_MONOTONIC);
                mpLastFlippedFrame = pNewFrame;
            }
        }

        if ( pMain )
        {
            mDisplay.legacySeamlessAdaptMode( &pMain->getLayer() );
        }
    }
    else
    {
        Log::alogd( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Drm " DRMDISPLAY_ID_STR " display not available", DRMDISPLAY_ID_PARAMS );
    }

    // If the flip is not issued or fails for some reason then at least retire it.
    // This is to ensure we continue to cycle frames through the system.
    if ( !bFlipped )
    {
        Log::alogd( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " Drm " DRMDISPLAY_ID_STR " flip to display failed or skipped", DRMDISPLAY_ID_PARAMS );
        doRetire( pNewFrame );
    }

    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Flip : Exit %s", DRMDISPLAY_ID_PARAMS, getStatusString( ).string( ) );
    return bFlipped;
}

void DrmPageFlipHandler::retire( DisplayQueue::Frame* pNewFrame )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );
    Mutex::Autolock _l( mLockPageFlip );
    doRetire( pNewFrame );
}

void DrmPageFlipHandler::doRetire( DisplayQueue::Frame* pNewFrame )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockPageFlip );
    ALOG_ASSERT( pNewFrame );
    const uint32_t releaseTo = pNewFrame->getFrameId().getTimelineIndex();
    Log::alogd( DRM_PAGEFLIP_DEBUG, " Drm " DRMDISPLAY_ID_STR " advancing immediately for skipped frame [timeline:%u]", DRMDISPLAY_ID_PARAMS, releaseTo );
    mTimeline.advanceTo( releaseTo );
}

void DrmPageFlipHandler::pageFlipEvent( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );

    Mutex::Autolock _l( mLockPageFlip );

    if ( !mbInit )
    {
        Log::aloge( true, "Drm " DRMDISPLAY_ID_STR " Unexpected flip event - not initialised", DRMDISPLAY_ID_PARAMS );
        return;
    }

    if ( !isOutstandingFlipWork( ) )
    {
        Log::aloge( true, "Drm " DRMDISPLAY_ID_STR " Unexpected flip event - no outstanding flip", DRMDISPLAY_ID_PARAMS );
        return;
    }

    completeFlip( );
}

void DrmPageFlipHandler::sync( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockPageFlip );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Sync", DRMDISPLAY_ID_PARAMS );
    Mutex::Autolock _l( mLockPageFlip );
    if ( !mbInit )
        return;
    doSync( );
}

void DrmPageFlipHandler::doSync( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockPageFlip );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Sync", DRMDISPLAY_ID_PARAMS );

    if ( isOutstandingFlipWork( ) )
    {
        ALOG_ASSERT( mpLastFlippedFrame );
        ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Waiting for flip event for frame %s",
            DRMDISPLAY_ID_PARAMS, mpLastFlippedFrame->getFrameId().dump().string() );
        // If the most recent frame failed to issue a flip event request
        // or if we fail the wait for flip completion, then complete it now.
        if ( !waitForFlipCompletion( ) )
        {
            if ( isOutstandingFlipWork( ) )
            {
                Log::aloge( true, "Drm " DRMDISPLAY_ID_STR " Forcing flip completion for frame %s",
                    DRMDISPLAY_ID_PARAMS, mpLastFlippedFrame->getFrameId().dump().string() );
                completeFlip( );
            }
        }
    }
}

bool DrmPageFlipHandler::waitForFlipCompletion( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockPageFlip );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG, DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Wait for previous flip", DRMDISPLAY_ID_PARAMS );

    // Keep spinning until flip event has been received and processed.
    while ( isOutstandingFlipWork( ) )
    {
        status_t err = mConditionPageFlipComplete.waitRelative( mLockPageFlip, ms2ns( mTimeoutSyncMsec ) );
        if ( err == TIMED_OUT )
        {
            Log::aloge( true, "Drm " DRMDISPLAY_ID_STR " wait flip completion timed out [%ums].", DRMDISPLAY_ID_PARAMS, mTimeoutSyncMsec );
            return false;
        }
    }
    {
        // Mark completion in systrace.
        // Should be able to correlate this with DrmEventThread page flip event.
        ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( DRMDISPLAY_ID_STR " Flip Sync", DRMDISPLAY_ID_PARAMS ).string( ) );
    }

    return true;
}

void DrmPageFlipHandler::retirePreviousFrames( DisplayQueue::Frame* pNewFrame )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockPageFlip );
    ALOG_ASSERT( pNewFrame );
    // A frame with a valid frameId will be a regular frame.
    // A frame without a valid frameId will be an inserted frame (eg. blanking frame).
    // If we just completed the flip to a frame with a valid frameId then advance
    // the timeline to release ALL frames up to but not including this new frame.
    // In all other cases just check we have at least released up to the most recent frame.
    if ( pNewFrame->getFrameId().isValid() )
    {
        const uint32_t releaseTo = pNewFrame->getFrameId().getTimelineIndex() - 1;


        Log::alogd( DRM_PAGEFLIP_DEBUG, "drm Flip " DRMDISPLAY_ID_STR " completed flip to %s. Releasing all previous [timeline:%u]",
            DRMDISPLAY_ID_PARAMS, pNewFrame->getFrameId().dump().string(), releaseTo );
        mTimeline.advanceTo( releaseTo );
    }
    else if ( mpCurrentFrame && mpCurrentFrame->getFrameId().isValid() )
    {
        const uint32_t currentFrameTime = mpCurrentFrame->getFrameId().getTimelineIndex();
        const uint32_t currentTimeline = mTimeline.getCurrentTime();
        int32_t advance = int32_t( currentFrameTime - currentTimeline );
        if ( advance > 0 )
        {
            Log::alogd( DRM_PAGEFLIP_DEBUG, "drm Flip " DRMDISPLAY_ID_STR " completed flip to %s. Releasing current [timeline:%u]",
                DRMDISPLAY_ID_PARAMS, pNewFrame->getFrameId().dump().string(), currentFrameTime );
            mTimeline.advance( advance );
        }
    }
}

void DrmPageFlipHandler::completeFlip( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockPageFlip );
    ALOGD_IF( DRM_PAGEFLIP_DEBUG,
              DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Complete flip : Entry %s",
              DRMDISPLAY_ID_PARAMS, getStatusString( ).string( ) );

    ALOG_ASSERT( mpLastFlippedFrame );

    if ( DISPLAY_TRACE )
    {
        // Systrace frame flip complete.
        // NOTE:
        //  Frame latency is the time taken from when we first received the frame content (back in onPrepare)
        //  to when we get acknowledgement that the frame has completed flip (so is in scan-out).
        const nsecs_t rxTime = mpLastFlippedFrame->getFrameId().getHwcReceivedTime();
        const nsecs_t nowTime = systemTime( CLOCK_MONOTONIC );
        const uint64_t latencyUs = ( (uint64_t)int64_t( nowTime - rxTime ) ) / 1000;
        ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( DRMDISPLAY_ID_STR " Flip Complete %s (latency:%" PRIi64 "us)",
            DRMDISPLAY_ID_PARAMS, mpLastFlippedFrame->getFrameId().dump().string(), latencyUs ) );
        ATRACE_INT_IF( DISPLAY_TRACE, String8::format( DRMDISPLAY_ID_STR " Latency",
            DRMDISPLAY_ID_PARAMS ), latencyUs );
    }

    // Validate flipped frame.
    mpLastFlippedFrame->validate();

    if ( mpCurrentFrame )
    {
        // Validate previous frame on retire.
        mpCurrentFrame->validate();
        // Release the frame back to the queue.
        mDisplay.releaseFlippedFrame( mpCurrentFrame );
    }
    // Retire previous frame(s) now we have completed flip for this new frame.
    retirePreviousFrames( mpLastFlippedFrame );

    mpCurrentFrame = mpLastFlippedFrame;
    mpLastFlippedFrame = NULL;

    // Signal local synchronisation.
    mConditionPageFlipComplete.broadcast( );

    // Notify the display queue that new work can now be issued.
    mDisplay.notifyReady( );

    ALOGD_IF( DRM_PAGEFLIP_DEBUG,
              DRM_PFH_NAME " " DRMDISPLAY_ID_STR " Complete flip : Exit %s",
              DRMDISPLAY_ID_PARAMS, getStatusString( ).string( ) );
}

String8 DrmPageFlipHandler::getStatusString( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockPageFlip );
    String8 str = String8::format(
        "Timeline:%u/%u Current:%s LastFlip:%s",
        mTimeline.getCurrentTime( ), mTimeline.getFutureTime( ),
        mpCurrentFrame ? mpCurrentFrame->getFrameId().dump().string() : "N/A",
        mpLastFlippedFrame ? mpLastFlippedFrame->getFrameId().dump().string() : "N/A" );
    return str;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

