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

#ifndef INTEL_UFO_HWC_TIMELINE_H
#define INTEL_UFO_HWC_TIMELINE_H

#include <sync/sync.h>
#include "Log.h"
#include "cutils/atomic.h"

namespace intel {
namespace ufo {
namespace hwc {

// ********************************************************************
// Sync Timeline/Fence Class.
// This class provides a driver for buffer synchronisation via fence acquire/release.
// ********************************************************************

class Timeline : NonCopyable
{
public:

    // Native fence.
    // NOTE:
    //   This must be a file descriptor handle type to support exchange between Android subsystems.
    //   Valid values are 0:N, -1 indicates NULL (NullNativeFence).
    typedef int NativeFence;

    static const NativeFence NullNativeFence = -1;
    static NativeFence* const NullNativeFenceReference;

    // Default timeout for waitAndClose( ) methods.
    static const uint32_t DefaultTimeoutMs = 60000;


    // Hwc fence.
    // This extends NativeFence with extra features such
    // as optional early cancellation of sync points.
    class Fence : NonCopyable
    {
    public:

        // C'tor.
        Fence( ) : mFence( NullNativeFence ), mBoundFences( 0 ), mbSignalled( false ) { };

        // Returns true if the fence is currently null.
        bool isNull( void )
        {
            return Timeline::isNull( mFence );
        }

        // Returns true if the fence is a valid fence.
        bool isValid( void )
        {
            return Timeline::isValid( mFence );
        }

        // Set (or reset) the fence fd.
        // This is only valid if the fence has zero references.
        // If fence is valid then sync point references will be set to 1.
        void set( NativeFence fence )
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences == 0, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            mFence = fence;
            mbSignalled = false;
            if ( fence >= 0 )
            {
                incBoundFences();
            }
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: set %s", dump().string() );
        }

        // Combines another fence into this existing fence, creating a fence that represents completion of both.
        // This fence will be updated and pOtherFence will be closed and reset to NullNativeFence.
        // This will automatically increment the sync point reference count.
        void merge( NativeFence* pOtherFence )
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 0, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            ALOG_ASSERT( pOtherFence );
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: merging %s + %d", dump().string(), *pOtherFence );
            mbSignalled = false;
            if ( *pOtherFence >= 0 )
            {
                incBoundFences();
            }
            Timeline::mergeFence( &mFence, pOtherFence );
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: merged %s", dump().string() );
        }

        // Get the fence fd.
        NativeFence get( void ) const
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 0, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            return mFence;
        }

        // Cancel a previous set or merged fence.
        // Hwc fence maintains a count of fences set or merged into the Hwc fence object.
        // Normally, this fence will be considered blocking until *ALL* the fences' timelines have advanced
        // up to or beyond the fence sync point. However, sometimes it is useful to cancel a fence
        // after it has been merged - i.e. without having to wait for the timeline to advance.
        // Calling cancel() will decrement the count of bound fences.
        // If the count is reduced to zero then the next call that checks or waits on the fence will treat
        // the fence as non-blocking and close it.
        // NOTES:
        //   If this mechanism is used then the contributor MUST issue the cancel() *BEFORE* advancing its timeline.
        //   i.e. Hwc fence does not expect to receive a cancel() after it has been signalled.
        void cancel( void )
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 1, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            INTEL_HWC_DEV_ASSERT( !mbSignalled, "%s mbSignalled %d", __FUNCTION__, mbSignalled );
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: cancel %s", dump().string() );
            decBoundFences();
        }

        // Duplicate the fence.
        NativeFence dup( void ) const
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 0, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: duping %s", dump().string() );
            return Timeline::dupFence( &mFence );
        }

        // Wait for the fence to be non-blocking.
        // This will wait up to timeoutMs milliseconds.
        // timeoutMs must be >0. Use checkAndClose to poll.
        // Returns true and closes the fence if the fence is no longer blocking.
        bool waitAndClose( uint32_t timeoutMs = DefaultTimeoutMs )
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 0, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            ALOG_ASSERT( timeoutMs > 0 );
            // Blocking wait.
            bool bReleased = checkOrWait( timeoutMs );
            if ( bReleased )
            {
                // Blocking checkOrWait() will close the fence for us.
                ALOG_ASSERT( mFence == NullNativeFence );
            }
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: waitAndClose %s", dump().string() );
            return bReleased;
        }

        // Check if the fence is non-blocking.
        // Returns true and closes the fence if the fence is no longer blocking.
        bool checkAndClose( void )
        {
            // Polling.
            bool bReleased = checkOrWait( 0 );
            if ( bReleased )
            {
                // Close the fence.
                close();
            }
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: checkAndClose %s", dump().string() );
            return bReleased;
        }

        // Check if the fence is non-blocking.
        // Returns true if the fence is no longer blocking.
        bool check( void )
        {
            // Polling.
            bool bReleased = checkOrWait( 0 );
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: check %s", dump().string() );
            return bReleased;
        }

        // Close the fence.
        void close( void )
        {
            Log::alogd( SYNC_FENCE_DEBUG, "Fence: closing %s", dump().string() );
            Timeline::closeFence( &mFence );
            mBoundFences = 0;
        }

        // Dump fence info to logcat (with optional prefix).
        void logFence( const char* pchPrefix = NULL ) const
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 0, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            String8 prefix;
            prefix = String8::format( "%s - Refs:%d",  pchPrefix, mBoundFences );
            Timeline::logFence( &mFence, prefix.string() );
        }

        // Get fence info as a string.
        String8 dump( void ) const
        {
            // Wrap with "H[....]" to indicate Hwc fence.
            // Includes the ref count + fence signal status ('S' if signalled or 'B' if blocked.)
            // Postfixes with BLOCKED/NON-BLOCKED (is only blocked if >0 refs and not signalled).
            String8 str = String8::format( "H[ Refs:%d/%c ", mBoundFences, mbSignalled? 'S' : 'B' )
                        + Timeline::dumpFence( &mFence )
                        + String8::format( " %s ]", ( mBoundFences && !mbSignalled ) ? "BLOCKED" : "NON-BLOCKED" );
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 0, "%s %s mBoundFences %d", __FUNCTION__, str.string(), mBoundFences );
            return str;
        }

    protected:
        // Increment count of bound references.
        void incBoundFences( void )
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 0, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            android_atomic_inc( &mBoundFences );
        }

        // Decrement count of bound references.
        void decBoundFences( void )
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 1, "%s BoundFences %u", __FUNCTION__, mBoundFences );
            android_atomic_dec( &mBoundFences );
        }

        // Check or wait on fence.
        // Returns true if the fence is not blocking (because all bound fences are cancelled or because fence is signalled).
        // If timeoutMs is 0 then this will wait for a signal for up to timeoutMs msecs.
        // Returns false if the fence is still blocking.
        bool checkOrWait( uint32_t timeoutMs )
        {
            INTEL_HWC_DEV_ASSERT( mBoundFences >= 0, "%s mBoundFences %d", __FUNCTION__, mBoundFences );
            if ( mbSignalled || ( mBoundFences == 0 ) )
            {
                return true;
            }
            else if ( (timeoutMs > 0) && Timeline::waitAndClose( &mFence, timeoutMs ) )
            {
                mbSignalled = true;
                Log::alogd( SYNC_FENCE_DEBUG, "Fence: waitAndClose has signalled %s", dump().string() );
                return true;
            }
            else if ( (timeoutMs == 0) && Timeline::check( &mFence ) )
            {
                mbSignalled = true;
                Log::alogd( SYNC_FENCE_DEBUG, "Fence: check has signalled %s", dump().string() );
                return true;
            }
            return false;
        }


        NativeFence mFence;                 //< The underlying native fence object.
        volatile int32_t mBoundFences;      //< A count of sync points linked to this fence.
        bool mbSignalled;                   //< Has the native fence been signalled.
    };

    // A reference to a fence which may be of either native or Hwc type.
    class FenceReference
    {
    public:

        // Enum of fence supported fence types.
        enum EType
        {
            eTypeUnspecified = 0,           //< Fence is not specified or has been cleared.
            eTypeNative,                    //< Fence is native fd type.
            eTypeHwc                        //< Fence is extended Hwc type.
        };

        // C'tor.
        // Reference starts as 'unspecified'.
        FenceReference() : meType( eTypeUnspecified )
        {
            mpNativeFence = NULL;
            mpHwcFence = NULL;
        }

        // Clear reference back to 'unspecified'.
        void clear( void )
        {
            meType = eTypeUnspecified;
            mpNativeFence = NULL;
            mpHwcFence = 0;
        }

        // Get reference type.
        EType getType( void ) const
        {
            return meType;
        }

        // Set reference location as a native type.
        void setLocation( int* pFence )
        {
            if ( pFence )
            {
                mpNativeFence = pFence;
                meType = eTypeNative;
            }
            else
            {
                clear();
            }
        }

        // Set reference location as a Hwc type.
        void setLocation( Fence* pFence )
        {
            if ( pFence )
            {
                mpHwcFence = pFence;
                meType = eTypeHwc;
            }
            else
            {
                clear();
            }
        }

        // Set reference location from another FenceReference.
        void setLocation( const FenceReference& fenceRef )
        {
            switch ( fenceRef.getType() )
            {
                case eTypeNative:
                    setLocation( fenceRef.getLocationAsNativeFence() );
                    return;
                case eTypeHwc:
                    setLocation( fenceRef.getLocationAsHwcFence() );
                    return;
                default:
                    clear();
                    return;
            }
        }

        // Get reference location as a native type.
        int* getLocationAsNativeFence( void ) const
        {
            ALOG_ASSERT( ( meType == eTypeNative ) || ( meType == eTypeUnspecified ) );
            return mpNativeFence;
        }

        // Get reference location as a Hwc type.
        Fence* getLocationAsHwcFence( void ) const
        {
            ALOG_ASSERT( ( meType == eTypeHwc ) || ( meType == eTypeUnspecified ) );
            return mpHwcFence;
        }

        // Set the referenced fence to a specific native fence fd or to NullNativeFence.
        void set( NativeFence fence ) const
        {
            ALOG_ASSERT( ( fence == NullNativeFence ) || ( meType != eTypeUnspecified ) );
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    Timeline::closeFence( mpNativeFence );
                    *mpNativeFence = fence;
                    return;
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    mpHwcFence->close( );
                    mpHwcFence->set( fence );
                    return;
                default:
                    break;
            }
        }

        // Merge a native fence into the referenced fence.
        void merge( NativeFence* pOtherFence ) const
        {
            if ( meType == eTypeUnspecified )
            {
                Timeline::closeFence( pOtherFence );
                return;
            }
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    Timeline::mergeFence( mpNativeFence, pOtherFence );
                    return;
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    mpHwcFence->merge( pOtherFence );
                    return;
                default:
                    ALOG_ASSERT(0);
                    break;
            }
        }

        // Get the referenced fence's native fence.
        NativeFence get( void ) const
        {
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    return *mpNativeFence;
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    return mpHwcFence->get( );
                default:
                    return NullNativeFence;
            }
        }

        // Cancel the reference.
        // This is equivalent to clearing the reference except for Hwc types
        // the referenced fence will be cancelled too.
        void cancel( void )
        {
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    break;
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    mpHwcFence->cancel( );
                    break;
                default:
                    break;
            }
            clear();
        }

        // Dup the referenced fence.
        // Returns a new native fence.
        NativeFence dup( void ) const
        {
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    return Timeline::dupFence( mpNativeFence );
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    return mpHwcFence->dup( );
                default:
                    return NullNativeFence;
            }
        }

        // Wait for the referenced fence to be non-blocking.
        // This will wait up to timeoutMs milliseconds.
        // timeoutMs must be >0. Use checkAndClose to poll.
        // Returns true and closes the fence if the fence is no longer blocking.
        bool waitAndClose( uint32_t timeoutMs = DefaultTimeoutMs ) const
        {
            ALOG_ASSERT( timeoutMs > 0 );
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    return Timeline::waitAndClose( mpNativeFence, timeoutMs );
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    return mpHwcFence->waitAndClose( timeoutMs );
                default:
                    return true;
            }
        }

        // Check if the referenced fence is non-blocking.
        // Returns true and closes the fence if the fence is no longer blocking.
        bool checkAndClose( void ) const
        {
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    return Timeline::checkAndClose( mpNativeFence );
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    return mpHwcFence->checkAndClose( );
                default:
                    return true;
            }
        }

        // Close the referenced fence.
        void close( void ) const
        {
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    Timeline::closeFence( mpNativeFence );
                    return;
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    mpHwcFence->close( );
                    return;
                default:
                    return;
            }
        }

        // Get fence reference info as a string.
        String8 dump( void ) const
        {
            switch ( meType )
            {
                case eTypeNative:
                    ALOG_ASSERT( mpNativeFence );
                    return String8::format( "FenceReference %p [ ", this )
                         + Timeline::dumpFence( mpNativeFence )
                         + String8( " ]" );
                case eTypeHwc:
                    ALOG_ASSERT( mpHwcFence );
                    return String8::format( "FenceReference %p [ ", this )
                         + mpHwcFence->dump( )
                         + String8( " ]" );
                default:
                    return String8::format( "FenceReference %p [ -?- ]", this );
            }
        }

    protected:
        EType meType;                       //< Type.
        union
        {
            NativeFence* mpNativeFence;     //< Pointer to a native fence.
            Fence*       mpHwcFence;        //< Pointer to a Hwc extended fence.
        };
    };


    Timeline( );
    ~Timeline( );

    // Initialisation
    // Set name prefix for all future fences.
    // Pass explicit firstFutureTime to increase the initial delta.
    // If initialisation is succesful then this will return true.
    bool init( String8 name, uint32_t firstFutureTime = 1 );

    // Uninitialise the timeline releasing all fences.
    // The timeline can be re-initialised later.
    void uninit( void );

    // Get this Timeline's name.
    const char* getName( void ) const { return mName.string( ); }

    // Returns the next 'future time'.
    uint32_t getFutureTime( void ) const
    {
        return mNextFutureTime;
    }
    // Returns the 'current time'.
    uint32_t getCurrentTime( void ) const
    {
        return mCurrentTime;
    }

    // Returns true if the fence is currently null.
    static bool isNull( NativeFence fence ) { return ( fence == NullNativeFence ); }

    // Returns true if the fence is a valid fence.
    static bool isValid( NativeFence fence ) { return ( fence >= 0 ); }

    // Create a NativeFence that can be passed to another subsystem to block progress until the future time is reached.
    // The timeline is automatically advanced each time createFence() is called.
    // pTimelineIndex must be provided.
    // Returns the new fence if successful, in which case pTimelineIndex will be updated with the fence timeline index.
    // Returns NullNativeFence if not successful, in which case pTimelineIndex will be reset to 0.
    // The returned fence must be released using close( ).
    NativeFence createFence( uint32_t* pTimelineIndex );

    // Create a NativeFence that can be passed to another subsystem to block progress until the future time is reached.
    // This returns a fence that will be signalled at the same time as the previous created fence.
    // The timeline is NOT automatically advanced.
    // pTimelineIndex must be provided.
    // Returns the new fence if successful, in which case pTimelineIndex will be updated with the fence timeline index.
    // Returns NullNativeFence if not successful, in which case pTimelineIndex will be reset to 0.
    // The returned fence must be released using close( ).
    NativeFence repeatFence( uint32_t* pTimelineIndex );

    // Combines another fence into this existing fence, returning a fence that represents completion of both.
    // Returns true if succesful - in which case pFence will be updated and pOtherFence will be closed and reset to NullNativeFence.
    // The returned fence must be released using close( ).
    static bool mergeFence( NativeFence* pFence, NativeFence* pOtherFence );

    // Duplicate an existing fence.
    // Returns the duplicated fence if successful.
    // Returns NullNativeFence if not successful.
    // The returned fence must be released using close( ).
    static NativeFence dupFence( const NativeFence* pOtherFence );

    // Advance the 'current time' by N ticks.
    // This will release all fences up to and including the new current time.
    // By default, this will increase time by 1 tick.
    void advance( uint32_t ticks = 1 );

    // Advance to a specific time.
    void advanceTo( uint32_t absSync );

    // Wait for a fence to be signalled and close it
    // This will wait up to timeoutMs milliseconds.
    // timeoutMs must be >0. Use checkAndClose to poll.
    // Returns true and closes the fence if it is no longer blocking.
    // Returns false if the fence is still blocking.
    static bool waitAndClose( NativeFence *pFence, uint32_t timeoutMs = DefaultTimeoutMs )
    {
        int err = 0;
        ALOG_ASSERT( pFence );
        ALOG_ASSERT( timeoutMs > 0 );
        ALOGD_IF( SYNC_FENCE_DEBUG, "NativeFence: wait fence %p/%d", pFence, *pFence );
        // A negative fence is considered signalled
        if (*pFence >= 0)
        {
            // Any error should be considered as not signalled
            if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
            {
                Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: wait %s", dumpFence(pFence).string() );
            }
            err = sync_wait(*pFence, timeoutMs);
            if ( err < 0 )
            {
                Log::aloge( true, "NativeFence: wait Failed waiting for fence %p/%d err:%d/%s", pFence, *pFence, err, strerror(errno) );
            }
            else
            {
                // Multiple waiters can race to close the fence.
                if ( *pFence >= 0 )
                {
                    if ( Log::wantLog( SYNC_FENCE_DEBUG ) )
                    {
                        Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: close %s", dumpFence(pFence).string() );
                    }
                    err = close(*pFence);
                    const bool bErr = ( errno != EBADF ) && ( err < 0 );
                    Log::aloge( bErr, "NativeFence: wait Failed close fence %p/%d err:%d/%d/%s !ERROR!", pFence, *pFence, err, errno, strerror(errno) );
                    *pFence = NullNativeFence;
                }
                else
                {
                    Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: already closed %s", dumpFence(pFence).string() );
                }
            }
        }
        return ( err >= 0 );
    }

    // Check to see if a fence is signalled and close it if it is.
    // Returns true and closes the fence if it is no longer blocking.
    // Returns false if the fence is still blocking.
    static bool checkAndClose( NativeFence *pFence )
    {
        int err = 0;
        ALOG_ASSERT( pFence );
        ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline:checkAndClose fence %p/%d", pFence, *pFence );
        // A negative fence is considered signalled
        if (*pFence >= 0)
        {
            // Any error should be considered as not signalled
            err = sync_wait(*pFence, 0);
            if ( err >= 0 )
            {
                Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: check complete %s", dumpFence(pFence).string() );
                // Multiple waiters can race to close the fence.
                if ( *pFence >= 0 )
                {
                    if ( Log::wantLog( ) )
                    {
                        Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: close %s", dumpFence(pFence).string() );
                    }
                    int err = close(*pFence);
                    const bool bErr = ( errno != EBADF ) && ( err < 0 );
                    Log::aloge( bErr, "NativeFence: check failed close fence %p/%d err:%d/%d/%s !ERROR!", pFence, *pFence, err, errno, strerror(errno) );
                    *pFence = NullNativeFence;
                }
                else
                {
                    Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: already closed %s", dumpFence(pFence).string() );
                }
            }
        }
        return ( err >= 0 );
    }

    // Check to see if a fence is signalled.
    // Returns true and if it is no longer blocking.
    // Returns false if the fence is still blocking.
    static bool check( NativeFence *pFence )
    {
        int err = 0;
        ALOG_ASSERT( pFence );
        ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline:check fence %p/%d", pFence, *pFence );
        // A negative fence is considered signalled
        if (*pFence >= 0)
        {
            // Any error should be considered as not signalled
            err = sync_wait(*pFence, 0);
        }
        return ( err >= 0 );
    }

    // Close a fence.
    static void closeFence( NativeFence *pFence )
    {
        ALOG_ASSERT( pFence );
        ALOGD_IF( SYNC_FENCE_DEBUG, "Timeline:close fence %p/%d", pFence, *pFence );
        if (*pFence >= 0)
        {
            if ( Log::wantLog( ) )
            {
                Log::alogd( SYNC_FENCE_DEBUG, "NativeFence: close %s", dumpFence(pFence).string() );
            }
            int err = close( *pFence );
            const bool bErr = ( errno != EBADF ) && ( err < 0 );
            Log::aloge( bErr, "NativeFence: close failed %p/%d err:%d/%d/%s !ERROR!", pFence, *pFence, err, errno, strerror(errno) );
        }
        *pFence = NullNativeFence;
    }

    // Dump trace for timeline status (with optional prefix).
    void dumpTimeline( const char* pchPrefix = NULL ) const ;

    // Dump fence info to logcat (with optional prefix).
    static void logFence( const NativeFence* pFence, const char* pchPrefix = NULL );

    // Get fence info as string.
    static String8 dumpFence( const NativeFence* pFence );

protected:

    // Allocate a fence with the specified time.
    // Returns the new fence if successful.
    // Returns NullNativeFence if not successful.
    NativeFence allocFence( uint32_t time );

    // Human-readable name for this sync timeline.
    String8     mName;

    // Timeline handle.
    int         mSyncTimeline;

    // Timeline 'current time' counter.
    uint32_t    mCurrentTime;

    // This is the timeline 'future time'.
    // It is the fence counter used for any subsequent acquire( ) call.
    uint32_t    mNextFutureTime;

}; // class Timeline

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_TIMELINE_H
