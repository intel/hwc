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

#ifndef INTEL_UFO_HWC_BUFFERQUEUE_H
#define INTEL_UFO_HWC_BUFFERQUEUE_H

#include <utils/Vector.h>
#include <ui/GraphicBuffer.h>
#include <sync/sync.h>
#include "Timeline.h"
#include "Utils.h"
#include "Timer.h"

namespace intel {
namespace ufo {
namespace hwc {

class Buffer;

//*****************************************************************************
//
// BufferQueue class.
// Manages a cyclic list of GraphicBuffers + associated fence.
// Buffers are allocated on demand (when they are first dequeued).
// The BufferQueue may be dynamically reconfigured using setConfigure.
//
//*****************************************************************************
class BufferQueue
{

public:
    class BufferReference;

private:

    // Special values used in the fence fields to indicate that a fence is not yet provided.
    static const int DEQUEUED_BUFFER = -2;              // Fence value set initially on a dequeue
    static const int AWAITING_RELEASE_FENCE = -3;       // Fence value set on a queue when we still dont yet know the actual fence is.

public:

    // Opaque handle to a BufferQueue buffer.
    typedef Buffer* BufferHandle;

    // If an external object holds a reference to a buffer in the buffer queue
    // then it MUST register its reference using registerReference. Currently, only one object
    // can register a reference at any time. The object registering a reference MUST inherit
    // BufferReference and implement referenceInvalidate to receive a notification when
    // the buffer is garbage collected or repurposed.
    class BufferReference
    {
    public:
        // This interface is called when the buffer contents are no longer valid.
        virtual void referenceInvalidate( BufferHandle handle ) = 0;
        virtual ~BufferReference() { }
    };

    // General constructor for a buffer queue of the requested number and type of formats
    BufferQueue();

    // Destructor.
    ~BufferQueue();

    // Set buffer queue max buffer count or max buffer allocation (bytes).
    // Either or both these may be zero which means unconstrained.
    void setConstraints( uint32_t maxBufferCount, uint32_t maxBufferAlloc );

    // Generate debug trace for all buffers.
    String8 dump( void ) const;

    // Get access to the next buffer on the queue.
    // The user must wait on the returned acquireFenceFd before using the Buffer.
    // Use queue( ) to insert the Buffers back into the queue.
    // Calls to dequeue and queue should be paired.
    // Only one buffer may be dequeued at a time.
    BufferHandle dequeue(uint32_t width, uint32_t height, int32_t bufferFormat, uint32_t usage, Timeline::Fence** ppReleaseFence);

    // Return a previously dequeued buffer.
    // Calls to dequeue and queue should be paired.
    // The releaseFenceFd becomes the acquireFenceFd for the next dequeue.
    // TODO: Remove this and merge with dequeue for new composition cases
    void queue( int releaseFenceFd = AWAITING_RELEASE_FENCE );

    // Get graphic buffer from handle.
    sp<GraphicBuffer> getGraphicBuffer( BufferHandle handle );

    // Register an external buffer reference.
    // Only one external reference can be registered at any time.
    // BufferReference::referenceInvalidate will be called when the buffer is garbage collected or repurposed.
    void registerReference( BufferHandle handle, BufferReference* pExternalObject );

    // Mark a buffer as used.
    void markUsed( BufferHandle handle );

    // Delete any allocated buffers
    void clear( void );

    // Synchronize main thread entry point prepare enter.
    void onPrepareBegin( void );
    // Synchronize main thread entry point prepare leave.
    void onPrepareEnd( void );
    // Synchronize main thread entry point set enter.
    void onSetBegin( void );
    // Synchronize main thread entry point set leave (runs end of frame processing).
    void onSetEnd( void );

private:
    // Look for the first buffer with the specified configuration and return it if its available.
    // This should be quite lightweight and is expected to get a hit for most allocation requests.
    Buffer* checkForMatchingAvailableBuffer(uint32_t w, uint32_t h, int32_t format, uint32_t usage);

    // Try to find the next available (unblocked) buffer.
    // Try a few times with a small delay between each retry.
    // If a free buffer can still not be found after several waits/retries,
    //  then fallback to sharing or evicting+replacing an existing buffer.
    // This is heavier than the above checkForMatchingAvailableBuffer as it may result in a buffer reallocation
    Buffer* waitForFirstAvailableBuffer( uint32_t w, uint32_t h, int32_t format, uint32_t usage );

    // Find a buffer to use as a fallback.
    // This prefers a buffer that is:
    //  a) Not used this frame b) Not used recently c) matches required geometry.
    // Returns -1 if no fallback found.
    int32_t findFallbackBuffer( uint32_t w, uint32_t h, int32_t format, uint32_t usage, bool& bMatch );

    // Called when no frames are seen for a long period.
    void idleTimeoutHandler( void );

    // Logviewer dump of buffers.
    void logBufferState( void );

    // Process buffers, including garbage collection.
    // This is called synchronously on the main SF thread at the end of each onSet()
    // and also asynchronously if no frames have been received for some time (see mOptionGCTimeout).
    void processBuffers( void );

    // Generate debug trace for buffers that are currently blocking.
    void dumpBlockedBuffers( void );

    // Get a count and (optionally) a mask of which buffers are currently blocking.
    uint32_t getBlockedBuffers( uint32_t* pBitmask = NULL );


#if INTEL_HWC_INTERNAL_BUILD
    class Stat
    {
    public:
        Stat( ) : mMin( ~0U ), mMax( 0 ) { }
        uint32_t mMin;
        uint32_t mMax;
        void reset( void )
        {
            mMin = ~0U;
            mMax = 0;
        }
        void sample( uint32_t sample )
        {
            if ( sample < mMin )
                mMin = sample;
            if ( sample > mMax )
                mMax = sample;
        }
    };
    class Histogram
    {
    public:
        enum { HISTOGRAM_SLOTS = 32 };
        uint32_t mSlot[ HISTOGRAM_SLOTS+1 ];
        Stat mStat;
        Histogram( )
        {
            reset( );
        }
        void reset( void )
        {
            memset( mSlot, 0, sizeof(uint32_t) * HISTOGRAM_SLOTS+1 );
            mStat.reset( );
        }
        void sample( uint32_t sample )
        {
            uint32_t histSlot = sample > HISTOGRAM_SLOTS ? HISTOGRAM_SLOTS : sample;
            ++mSlot[ histSlot ];
            mStat.sample( histSlot );
        }
    };
    class Stats
    {
    public:
        Stats( ) : mSamples( 0 ) { }
        void reset( void )
        {
            for ( uint32_t m = 0; m < METRIC_MAX; ++m )
            {
                mMetricRecent[ m ].reset( );
                mMetric[ m ].reset( );
            }
            for ( uint32_t h = 0; h < HISTOGRAM_MAX; ++h )
            {
                mHistogram[ h ].reset( );
            }
            mSamples = 0;
        }
        void sample( uint32_t allocated,
                     uint32_t alloctedBytes,
                     uint32_t blocked,
                     uint32_t usedThisFrame,
                     uint32_t usedRecently )
        {
            if ( (mSamples % Stats::RECENT_WINDOW) == 0 )
            {
                for ( uint32_t m = 0; m < METRIC_MAX; ++m )
                    mMetricRecent[ m ].reset( );
            }
            mMetric[ METRIC_ALLOCATED ].sample( allocated );
            mMetric[ METRIC_ALLOCATED_BYTES ].sample( alloctedBytes );
            mMetric[ METRIC_BLOCKED ].sample( blocked );
            mMetric[ METRIC_USED_THIS_FRAME ].sample( usedThisFrame );
            mMetric[ METRIC_USED_RECENTLY ].sample( usedRecently );
            mMetricRecent[ METRIC_ALLOCATED ].sample( allocated );
            mMetricRecent[ METRIC_ALLOCATED_BYTES ].sample( alloctedBytes );
            mMetricRecent[ METRIC_BLOCKED ].sample( blocked );
            mMetricRecent[ METRIC_USED_THIS_FRAME ].sample( usedThisFrame );
            mMetricRecent[ METRIC_USED_RECENTLY ].sample( usedRecently );
            ++mSamples;
        }
        uint32_t mSamples;
        nsecs_t mLastSampleTime;
        enum { RECENT_WINDOW = 500 };
        enum EMetric { METRIC_ALLOCATED, METRIC_ALLOCATED_BYTES, METRIC_BLOCKED, METRIC_USED_THIS_FRAME, METRIC_USED_RECENTLY, METRIC_MAX };
        enum EHistogram { HISTOGRAM_ALLOCATED, HISTOGRAM_USED_THIS_FRAME, HISTOGRAM_MAX };
        Stat mMetric[ METRIC_MAX ];
        Stat mMetricRecent[ METRIC_MAX ];
        Histogram mHistogram[ HISTOGRAM_MAX ];
    };
    Stats                       mStats;
    Option                      mStatsEnabled;                              //< Enable stats generation.
    void updateBufferStats( void );
#endif

    Option                      mOptionGCTimeout;                           //< Time in milliseconds after which unused buffers are released.
    uint32_t                    mMaxBufferCount;                            //< Max buffer count to grow pool by; if zero then unbound.
    uint32_t                    mMaxBufferAlloc;                            //< Max buffer allocation in MB to grow pool by; if zero then unbound.
    Vector< Buffer* >           mBuffers;                                   //< List of Buffer records.
    uint32_t                    mBufferAllocBytes;                          //< Total buffer allocations in bytes.
    uint32_t                    mLatestAvailableBuffer;                     //< Index of current/next buffer to use (if possible).
    uint32_t                    mDequeuedBuffer;                            //< Index of buffer most recently dequeued (or ~0U if none dequeued).
    TimerMFn<BufferQueue, &BufferQueue::idleTimeoutHandler>  mIdleTimer;    //< Timeout for garbage collection buffers.
    Mutex                       mLock;                                      //< Lock required to synchronize timeout GC with SF thread.
};

} // namespace hwc
} // namespace ufo
} // namespace intel

#endif // INTEL_UFO_HWC_BUFFERQUEUE_H
