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

#include "DrmEventThread.h"
#include "DrmDisplay.h"
#include "Drm.h"

#include <utils/Atomic.h>
#include <utils/Thread.h>

namespace intel {
namespace ufo {
namespace hwc {

DrmEventThread::VSyncHandler DrmEventThread::mVSyncHandler[ MAX_VSYNC_HANDLERS ];

//*****************************************************************************
//
// DrmEventThread::VSyncHandler class - responsible for
//    handling vblank events for a specific display.
//
//*****************************************************************************

DrmEventThread::VSyncHandler::VSyncHandler( ) :
    mDrmFd( -1 ),
    mpDisplay( NULL ),
    mIndex( -1 ),
    mFlags( 0 ),
    mVblankEventInflight( 0 ),
    meMode( eModeStopped )
{
}

void DrmEventThread::VSyncHandler::setDisplay( uint16_t index, DrmDisplay* pDisp )
{
    ALOG_ASSERT( pDisp );
    Mutex::Autolock _l( mLockData );

    // Init handler.
    mpDisplay = pDisp;
    mDrmFd = Drm::get().getDrmHandle( );
    mIndex = index;

    // Update flags according to pipeId
    uint32_t pipe = mpDisplay->getDrmPipeIndex();
    uint32_t flags = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
    if(pipe == 1)
        flags |= DRM_VBLANK_SECONDARY;
    else if(pipe > 1)
        flags |= ((pipe << DRM_VBLANK_HIGH_CRTC_SHIFT) & DRM_VBLANK_HIGH_CRTC_MASK);

    ALOGD_IF( VSYNC_DEBUG,"DrmEventThread::VSyncHandler::setDisplay - pipeId=%d, flags=0x%x", pipe, flags);
    mFlags = flags;
}

void DrmEventThread::VSyncHandler::setFlags( uint32_t flags )
{
    Mutex::Autolock _l( mLockData );
    mFlags = flags;
}

bool DrmEventThread::VSyncHandler::enable( void )
{
    // Ensure vsyncs are being generated and mode is eModeRunning.
    ALOGD_IF( VSYNC_DEBUG,
        "DrmEventThread::VSyncHandler::enable Handler:%p/Display:%p/flags 0x%x",
        this, mpDisplay, mFlags );
    Mutex::Autolock _l( mLockData );
    ALOG_ASSERT( mpDisplay != NULL );
    ALOG_ASSERT( mDrmFd != -1 );
    ALOG_ASSERT( mIndex != -1 );
    if ( meMode != eModeRunning )
    {
        if ( meMode == eModeStopped )
        {
            // Request first event.
            drmVBlank vbl;
            vbl.request.type = (drmVBlankSeqType)mFlags;
            vbl.request.sequence = 1;
            vbl.request.signal   = DrmEventThread::encodeIndex( mIndex );

            ALOGD_IF( VSYNC_DEBUG,
                "DrmEventThread::VSyncHandler::enable Request first VBlank event Handler:%p/Display:%p/flags 0x%x",
                this, mpDisplay, mFlags );
            if ( drmWaitVBlank(mDrmFd, &vbl) != Drm::SUCCESS )
            {
                ALOGE( "DrmEventThread::VSyncHandler::enable drmWaitVBlank FAILED" );
                return false;
            }
        }
        // Set running.
        ALOGD_IF( VSYNC_DEBUG,
            "DrmEventThread::VSyncHandler::enable -->eModeRunning Handler:%p/Display:%p/flags 0x%x",
            this, mpDisplay, mFlags );
        meMode = eModeRunning;
    }
    return true;
}

bool DrmEventThread::VSyncHandler::disable( bool bWait )
{
    // Flag stop of vsync generation, set mode to eModeStopping.
    ALOGD_IF( VSYNC_DEBUG,
        "DrmEventThread::VSyncHandler::disable Handler:%p/Display:%p/flags 0x%x",
        this, mpDisplay, mFlags );
    Mutex::Autolock _l( mLockData );
    if ( meMode == eModeRunning )
    {
        // Set stopping.
        ALOGD_IF( VSYNC_DEBUG,
            "DrmEventThread::VSyncHandler::disable -->eModeStopping Handler:%p/Display:%p/flags 0x%x",
            this, mpDisplay, mFlags );
        meMode = eModeStopping;
    }
    if ( bWait && ( meMode != eModeStopped ) )
    {
        // Wait for stop before exit.
        const uint32_t timeoutNS = 250000000; // 250ms.
        ALOGD_IF( VSYNC_DEBUG,
            "DrmEventThread::VSyncHandler::disable waiting for eModeStopped Handler:%p/Display:%p/flags 0x%x",
            this, mpDisplay, mFlags );
        mConditionStopped.waitRelative( mLockData, timeoutNS );
    }
    return true;
}

void DrmEventThread::VSyncHandler::event(unsigned int frame, unsigned int sec, unsigned int usec)
{
    // Handle event.
    ALOGD_IF( VSYNC_DEBUG,
        "DrmEventThread::VSyncHandler::event Handler:%p/Display:%p/flags 0x%x",
        this, mpDisplay, mFlags );

    // Should we issue this event?
    bool bIssueEvent = false;
    {
        Mutex::Autolock _l( mLockData );
        bIssueEvent = ( ( meMode == eModeRunning )
                     && ( mpDisplay != NULL ) );
    }

    if ( bIssueEvent )
    {
        mpDisplay->vsyncEvent(frame, sec, usec);
    }

    // Now process state updates and request next event.
    {
        Mutex::Autolock _l( mLockData );
        bool bStop = false;
        if ( meMode != eModeRunning )
        {
            bStop = true;
        }
        else if ( mpDisplay )
        {
            // Request next event.
            drmVBlank vbl;
            vbl.request.type = (drmVBlankSeqType)mFlags;
            vbl.request.sequence = 1;
            vbl.request.signal = DrmEventThread::encodeIndex( mIndex );

            ALOGD_IF( VSYNC_DEBUG,
                "DrmEventThread::VSyncHandler::event Request next VBlank event Handler:%p/Display:%p/flags 0x%x",
                this, mpDisplay, mFlags );

            if ( drmWaitVBlank(mDrmFd, &vbl) != Drm::SUCCESS )
            {
                ALOGE( "DrmEventThread::VSyncHandler::event drmWaitVBlank FAILED" );
                bStop = true;
            }
        }
        else
        {
            ALOGE( "DrmEventThread::VSyncHandler::event Missing mpDisplay" );
            bStop = true;
        }

        if ( bStop )
        {
            // Stop and signal.
            ALOGD_IF( VSYNC_DEBUG,
                "DrmEventThread::VSyncHandler::event -->eModeStopped Handler:%p/Display:%p/flags 0x%x",
                this, mpDisplay, mFlags );
            meMode = eModeStopped;
            mConditionStopped.signal( );
        }
    }
}

//*****************************************************************************
//
// DrmEventThread class - responsible for handling vblank and page flip events
//
//*****************************************************************************

void DrmEventThread::vblank_handler(int, unsigned int frame, unsigned int sec, unsigned int usec, void *data)
{
    ATRACE_CALL_IF(DISPLAY_TRACE);
    int32_t syncIndex = decodeIndex( (uint32_t)((uintptr_t)data&0xFFFFFFFF) );
    if ( ( syncIndex >= 0 ) && ( syncIndex < MAX_VSYNC_HANDLERS ) )
    {
        mVSyncHandler[ syncIndex ].event(frame, sec, usec);
        return;
    }
    Log::aloge( true, "Vblank for unknown handler %d [data %p]", syncIndex, data );
    ALOG_ASSERT( false );
}

void DrmEventThread::page_flip_handler(int, unsigned int, unsigned int, unsigned int, void *data)
{
    ATRACE_CALL_IF(DISPLAY_TRACE);
    static Drm& drm = Drm::get();
    int32_t displayIndex = decodeIndex( (uint32_t)((uintptr_t)data&0xFFFFFFFF) );
    if ( displayIndex >= 0 )
    {
        DrmDisplay* pDisplay = drm.getDrmDisplay( displayIndex );
        if ( pDisplay )
        {
            pDisplay->pageFlipEvent();
            return;
        }
    }
    Log::aloge( true, "Page flip for unknown display %d [data %p]", displayIndex, data );
    ALOG_ASSERT( false );
}

DrmEventThread::DrmEventThread()
{
    // Set default sync handlers flags- will be updated in enableVsync to match display crtcId.
    mVSyncHandler[PRIMARY_VSYNC_HANDLER].setFlags( DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT );
    mVSyncHandler[SECONDARY_VSYNC_HANDLER].setFlags( DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT | DRM_VBLANK_SECONDARY );
    memset(&mEvctx, 0, sizeof mEvctx);
    mEvctx.version = DRM_EVENT_CONTEXT_VERSION;
    mEvctx.vblank_handler = vblank_handler;
    mEvctx.page_flip_handler = page_flip_handler;

    mDrmFd = Drm::get().getDrmHandle();
}

void DrmEventThread::onFirstRef()
{
    run("DrmEventThread", PRIORITY_URGENT_DISPLAY + PRIORITY_MORE_FAVORABLE);
}

bool DrmEventThread::enableVSync(DrmDisplay* pDisp)
{
    ALOG_ASSERT( pDisp );
    ATRACE_CALL_IF(DISPLAY_TRACE);

    uint32_t handler;
    switch ( pDisp->getDisplayType( ) )
    {
        case eDTPanel:
            handler = PRIMARY_VSYNC_HANDLER;
            break;
        case eDTExternal:
            handler = SECONDARY_VSYNC_HANDLER;
            break;
        default:
            ALOGE( "Expected eDTExternal or eDTPanel" );
            return false;
    }

    ALOGD_IF( VSYNC_DEBUG,
        "DrmEventThread::enableVSync P:%u, DrmDisplay %u/%p, handler %u",
        pDisp->getDisplayManagerIndex( ), pDisp->getDrmDisplayID( ), pDisp, handler );

    mVSyncHandler[handler].setDisplay( handler, pDisp );

    return mVSyncHandler[handler].enable( );
}

bool DrmEventThread::disableVSync(DrmDisplay* pDisp, bool bWait)
{
    ALOG_ASSERT( pDisp );
    ATRACE_CALL_IF(DISPLAY_TRACE);

    uint32_t handler;
    switch ( pDisp->getDisplayType( ) )
    {
        case eDTPanel:
            handler = PRIMARY_VSYNC_HANDLER;
            break;
        case eDTExternal:
            handler = SECONDARY_VSYNC_HANDLER;
            break;
        default:
            ALOGE( "Expected eDTHDMI or eDTPanel" );
            return false;
    }

    ALOGD_IF( VSYNC_DEBUG,
        "DrmEventThread::disableVSync P:%u, DrmDisplay %u/%p, bWait %d, handler %u",
        pDisp->getDisplayManagerIndex( ), pDisp->getDrmDisplayID( ), pDisp, bWait, handler );

    return mVSyncHandler[handler].disable( bWait );
}

bool DrmEventThread::threadLoop()
{
    // Handle all events
    drmHandleEvent(mDrmFd, &mEvctx);
    return true;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
