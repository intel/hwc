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
#include "Timeline.h"

#ifdef SW_SYNC_H_PATH
// This header became private. As we still need it, we now have to find it in the makefile.
#include SW_SYNC_H_PATH
#else
#include <sync/sw_sync.h>
#endif


static const int MaxFenceNameLength = 32;

namespace intel {
namespace ufo {
namespace hwc {

#define TIMELINE_ENTRY_EXIT ( SYNC_FENCE_DEBUG && 0 )

Timeline::NativeFence* const Timeline::NullNativeFenceReference = NULL;

Timeline::Timeline( ) :
    mName( "N/A" ),
    mSyncTimeline( -1 ),
    mCurrentTime( 0 ),
    mNextFutureTime( 0 )
{
    mSyncTimeline = sw_sync_timeline_create( );
    ALOGE_IF( mSyncTimeline == -1, "Failed to create sync timeline." );
    ALOGD_IF( SYNC_FENCE_DEBUG, "SyncTimeline %d(%s) [mCurrentTime %u/mNextFutureTime %u] created",
        mSyncTimeline, mName.string( ), mCurrentTime, mNextFutureTime );
}

Timeline::~Timeline( )
{
    ALOGD_IF( SYNC_FENCE_DEBUG, "SyncTimeline %d(%s) [mCurrentTime %u/mNextFutureTime %u] destroy",
        mSyncTimeline, mName.string( ), mCurrentTime, mNextFutureTime );
    uninit( );
    if ( mSyncTimeline != -1 )
    {
        close( mSyncTimeline );
        mSyncTimeline = -1;
    }
}

bool Timeline::init( String8 name, uint32_t firstFutureTime )
{
    if ( mSyncTimeline == -1 )
    {
        ALOGE( "Missing sync timeline." );
        return false;
    }
    mName = name;
    ALOGD_IF( SYNC_FENCE_DEBUG, "SyncTimeline %d(%s) [mCurrentTime %u/mNextFutureTime %u] init firstFutureTime %u",
        mSyncTimeline, mName.string( ), mCurrentTime, mNextFutureTime, firstFutureTime );
    ALOGE_IF( !firstFutureTime, "Expected non-zero firstFutureTime" );
    if ( mCurrentTime != mNextFutureTime )
    {
        advanceTo( mNextFutureTime );
    }
    mNextFutureTime = mCurrentTime + firstFutureTime;
    ALOGD_IF( SYNC_FENCE_DEBUG, " == mCurrentTime %u/mNextFutureTime %u", mCurrentTime, mNextFutureTime );
    return true;
}

void Timeline::uninit( void )
{
    ALOGD_IF( SYNC_FENCE_DEBUG, "SyncTimeline %d(%s) [mCurrentTime %u/mNextFutureTime %u] uninit",
        mSyncTimeline, mName.string( ), mCurrentTime, mNextFutureTime );
    if ( mCurrentTime != mNextFutureTime )
    {
        advanceTo( mNextFutureTime );
    }
    ALOGD_IF( SYNC_FENCE_DEBUG, " == mCurrentTime %u/mNextFutureTime %u", mCurrentTime, mNextFutureTime );
}

Timeline::NativeFence Timeline::allocFence( uint32_t time )
{
    ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline:alloc fence" );

    if ( mSyncTimeline == -1 )
    {
        ALOGW_IF( SYNC_FENCE_DEBUG, "SyncTimeline is not initialised" );
        return NullNativeFence;
    }

    char fenceName[ MaxFenceNameLength + 32 ];

    // Build a NativeFence name from SyncTimeline name + SyncCounter.
    snprintf( fenceName, sizeof( fenceName ), "%s:%u", mName.string( ), time );
    NativeFence newFence = sw_sync_fence_create( mSyncTimeline, fenceName, time );
    if ( newFence < 0 )
    {
        ALOGE( "Timeline %d : Failed to alloc new fence [%s] : %s", mSyncTimeline, fenceName, strerror(errno) );
        Log::alogd( true, "NativeFence: create !ERROR!" );
        return NullNativeFence;
    }

    ALOGD_IF( SYNC_FENCE_DEBUG, "SyncTimeline %d(%s) : Allocated new fence %d(%s)",
        mSyncTimeline, mName.string( ), newFence, fenceName );

    return newFence;
}

Timeline::NativeFence Timeline::createFence( uint32_t* pTimelineIndex )
{
    ALOG_ASSERT( pTimelineIndex != NULL );

    ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline:create fence" );

    *pTimelineIndex = 0;
    uint32_t time = mNextFutureTime;
    Timeline::NativeFence newFence = allocFence( time );
    if ( newFence != NullNativeFence )
    {
        if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
        {
            Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: Timeline %s created %s [timeline:%u]",
                        mName.string( ), dumpFence(&newFence).string(), time );
        }
        *pTimelineIndex = time;
        ++mNextFutureTime;
    }
    return newFence;
}

Timeline::NativeFence Timeline::repeatFence( uint32_t* pTimelineIndex )
{
    ALOG_ASSERT( pTimelineIndex != NULL );

    ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline:repeat fence" );

    *pTimelineIndex = 0;
    uint32_t time = mNextFutureTime-1;
    Timeline::NativeFence newFence = allocFence( time );
    if ( newFence != NullNativeFence )
    {
        if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
        {
            Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: Timeline %s repeated %s [timeline:%u]",
                        mName.string( ), dumpFence(&newFence).string(), time );
        }
        *pTimelineIndex = time;
    }
    return newFence;
}

bool Timeline::mergeFence( NativeFence* pFence, NativeFence* pOtherFence )
{
    ALOG_ASSERT( pFence && pOtherFence );
    ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline:merge fence %p/%d other %p/%d", pFence, *pFence, pOtherFence, *pOtherFence );

    if ( !isValid( *pFence ) )
    {
        if ( isValid( *pOtherFence ) )
        {
            if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
            {
                Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: merge (transfer) %s", dumpFence(pOtherFence).string() );
            }
            *pFence = *pOtherFence;
            *pOtherFence = NullNativeFence;
        }
        else
        {
            // This is to handle the case both the two fences < 0
            // e.g. pFence = -3 since it's queued to buffer queue but have been assigned a valid release fence
            // pOtherFence = -1 since no valid fence is provided by HW display
            if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
            {
                Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: merge reset source from %s to -1", dumpFence(pFence).string());
            }
            *pFence = NullNativeFence;
        }
        return true;
    }
    else if ( !isValid( *pOtherFence ) )
    {
        if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
        {
            Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: merge (no-op) %s", dumpFence(pFence).string() );
        }
        *pOtherFence = NullNativeFence;
        return true;
    }

    char fenceName[ MaxFenceNameLength + 32 ];

    if (SYNC_FENCE_DEBUG)
    {
        // Create a syncpoint that merges the Fences.
        struct sync_fence_info_data* pInfo1 = sync_fence_info( *pFence );
        struct sync_fence_info_data* pInfo2 = sync_fence_info( *pOtherFence );

        if ( pInfo1 )
        {
            ALOGD_IF( SYNC_FENCE_DEBUG && pInfo1, "NativeFence Info1: %s status %d", pInfo1->name, pInfo1->status );
            struct sync_pt_info* pSyncPointInfo = NULL;
            while ( ( pSyncPointInfo = sync_pt_info( pInfo1, pSyncPointInfo ) ) != NULL )
            {
                ALOGD_IF( SYNC_FENCE_DEBUG, "  SyncPoint Driver %s Status %d Timestamp %.03f",
                    pSyncPointInfo->driver_name,
                    pSyncPointInfo->status,
                    (float)pSyncPointInfo->timestamp_ns * (1/1000000000.0f) );
            }
        }
        if ( pInfo2 )
        {
            ALOGD_IF( SYNC_FENCE_DEBUG && pInfo2, "NativeFence Info2: %s status %d", pInfo2->name, pInfo2->status );
            struct sync_pt_info* pSyncPointInfo = NULL;
            while ( ( pSyncPointInfo = sync_pt_info( pInfo2, pSyncPointInfo ) ) != NULL )
            {
                ALOGD_IF( SYNC_FENCE_DEBUG, "  SyncPoint Driver %s Status %d Timestamp %.03f",
                    pSyncPointInfo->driver_name,
                    pSyncPointInfo->status,
                    (float)pSyncPointInfo->timestamp_ns * (1/1000000000.0f) );
            }
        }
        if ( pInfo1 && pInfo2 )
        {
            // Create a combined NativeFence name from info names.
            snprintf( fenceName, sizeof( fenceName ), "[%s && %s]", pInfo1->name, pInfo2->name );
        }
        else
        {
            // Create a combined NativeFence name from handles.
            snprintf( fenceName, sizeof( fenceName ), "[F%d && F%d]", *pFence, *pOtherFence );
        }
        if ( pInfo1 )
            sync_fence_info_free( pInfo1 );
        if ( pInfo2 )
            sync_fence_info_free( pInfo2 );
    }
    else
    {
        // Create a combined NativeFence name from handles.
        snprintf( fenceName, sizeof( fenceName ), "[F%d && F%d]", *pFence, *pOtherFence );
    }

    // Merge the two component fences.
    NativeFence mergedFence = sync_merge( fenceName, *pFence, *pOtherFence );
    if ( mergedFence < 0 )
    {
        Log::aloge( true, "NativeFence: merge %s + %s !ERROR!", dumpFence(pFence).string(), dumpFence(pOtherFence).string() );
        return false;
    }
    else
    {
        if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
        {
            Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: merge %s + %s -> %s",
                dumpFence(pFence).string(), dumpFence(pOtherFence).string(),
                dumpFence(&mergedFence).string() );
        }
        // Close the two component fences for the merge.
        ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline : Merged fence %d(%s)", mergedFence, fenceName );
        close( *pFence );
        close( *pOtherFence );
        *pFence = mergedFence;
        *pOtherFence = NullNativeFence;
        return true;
    }
}

Timeline::NativeFence Timeline::dupFence( const NativeFence* pOtherFence )
{
    ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline:dup fence %d", *pOtherFence );
    if ( *pOtherFence < 0 )
    {
        if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
        {
            Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: dup %s -> %d", dumpFence(pOtherFence).string(), NullNativeFence );
        }
        return NullNativeFence;
    }

    NativeFence newFence = dup( *pOtherFence );
    if ( newFence < 0 )
    {
        ALOGE( "Failed to dup fence : %s", strerror(errno) );
        Log::alogd( true, "NativeFence: dup !ERROR!" );
        return NullNativeFence;
    }

    if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
    {
        Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: dup %s -> %s", dumpFence(pOtherFence).string(), dumpFence(&newFence).string() );
    }
    return newFence;
}

void Timeline::advance( uint32_t ticks )
{
    if ( mSyncTimeline == -1 )
    {
        ALOGW_IF( SYNC_FENCE_DEBUG, "SyncTimeline is not initialised" );
        return;
    }

    if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
    {
        Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: Timeline %s release next %u [timeline:%u]", mName.string( ), ticks, mCurrentTime + ticks );
    }

    int err = sw_sync_timeline_inc( mSyncTimeline, ticks );
    mCurrentTime += ticks;

    if ( err < 0 )
    {
        ALOGE( "**************** CRITICAL ****************" );
        ALOGE( "Failed to advance sync timeline %d(%s)", mSyncTimeline, mName.string( ) );
        ALOGE( "**************** CRITICAL ****************" );
    }
}

void Timeline::advanceTo( uint32_t absSync )
{
    if ( mSyncTimeline == -1 )
    {
        ALOGW_IF( SYNC_FENCE_DEBUG, "SyncTimeline is not initialised" );
        return;
    }

    int32_t delta = int32_t( absSync - mCurrentTime );
    if ( delta > 0 )
    {
        ALOGD_IF( SYNC_FENCE_DEBUG, "advanceTo( %u ) mCurrentTime %u => delta %u", absSync, mCurrentTime, delta );
        advance( (uint32_t)delta );
    }
    else if ( delta < 0 )
    {
        Log::aloge( true, "Advance timeline delta is %d (expected >= 0)", delta );
    }
}

void Timeline::dumpTimeline( const char* pchPrefix ) const
{
    if (SYNC_FENCE_DEBUG)
    {
        ALOGD( "%s%sSyncTimeline %d(%s) mNextFutureTime %u",
            pchPrefix ? pchPrefix : "",
            pchPrefix ? " - " : "",
            mSyncTimeline,
            mName.string( ),
            mNextFutureTime );
    }
}

void Timeline::logFence( const NativeFence* pFence, const char* pchPrefix )
{
    if (SYNC_FENCE_DEBUG)
    {
        ALOGD( "%s - %s", pchPrefix, dumpFence( pFence ).string() );
    }
}

String8 Timeline::dumpFence( const NativeFence* pFence )
{
    if ( ( SYNC_FENCE_DEBUG ) && ( isValid( *pFence ) ) )
    {
        struct sync_fence_info_data* pInfo = NULL;
        pInfo = sync_fence_info( *pFence );
        if ( pInfo )
        {
            // Wrap with "N[....]" to indicate NativeFence.
            String8 info = String8::format( "N[ %p Fd:%d %s %d {", pFence, *pFence, pInfo->name, pInfo->status );
            struct sync_pt_info* pSyncPointInfo = NULL;
            while ( ( pSyncPointInfo = sync_pt_info( pInfo, pSyncPointInfo ) ) != NULL )
            {
                info += String8::format( " SP %s %d %.03f",
                    pSyncPointInfo->driver_name, pSyncPointInfo->status,
                    (float)pSyncPointInfo->timestamp_ns * (1/1000000000.0f) );
            }
            sync_fence_info_free( pInfo );
            info += String8( " } ]" );
            return info;
        }
        else
        {
            return String8::format( "N[ %p Fd:%d ]", pFence, *pFence );
        }
    }
    return String8::format( "N[ %p Fd:%d ]", pFence, *pFence );
}


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

