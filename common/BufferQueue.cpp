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

#include "AbstractBufferManager.h"
#include "BufferQueue.h"

namespace intel {
namespace ufo {
namespace hwc {

// This class should be private to the buffer queue.
// It's a helper class to wrap a graphic buffer and an acquire fence.
class Buffer
{
public:

    // Use bit flags.
    enum EUsageFlags
    {
        EUsedThisFrame = (1<<0),    //< Used this frame.
        EUsedRecently  = (1<<1)     //< Used recently.
    };

    // Construct a Buffer with the specified size, format and usage flags.
    Buffer( uint32_t w, uint32_t h, int32_t format, uint32_t usage );

    // Construct a Buffer from an existing GraphicBuffer.
    Buffer( sp<GraphicBuffer> pBuffer );

    // Check underlying allocation was succesful.
    bool allocationOK( void )  { return  ( ( mpGraphicBuffer != NULL )
                                        && ( mpGraphicBuffer->handle != NULL ) ); }

    // Allocate the actual graphics buffer.
    void allocate( uint32_t w, uint32_t h, int32_t format, uint32_t  usage );

    // Re-allocate the actual graphics buffer.
    void reallocate( uint32_t w, uint32_t h, int32_t format, uint32_t  usage );

    // Reconfigue this Buffer with a new size, format and usage flags.
    void reconfigure( uint32_t w, uint32_t h, int32_t format, uint32_t  usage );

    // Compare buffer with required configuration.
    bool matchesConfiguration( uint32_t w, uint32_t h, int32_t format, uint32_t  usage );

    // Get human-readable description of Buffer state.
    String8 dump( void );

    AbstractBufferManager&          mBM;                    // Buffer manager.
    sp<GraphicBuffer>               mpGraphicBuffer;        // Pointer to the buffer itself
    uint32_t                        mSizeBytes;             // Size of the buffer in bytes (0 if shared).
    Timeline::Fence                 mAcquireFence;          // Fence that needs to be waited on before access.
                                                            // NOTES: BufferQueue is using a Hwc fence so fences can be
                                                            // cancelled out-of-order (to support early release of buffers back
                                                            // to the queue when frames are consumed/dropped out-of-order).
    BufferQueue::BufferReference*   mpRef;                  // External reference
    uint32_t                        mUse;                   // Buffer usage flags
    nsecs_t                         mLastFrameUsedTime;     // Buffer last frame used time (updated at onEndOfFrame).
    bool                            mbShared:1;             // Graphic buffer is shared.
};


Buffer::Buffer( uint32_t w, uint32_t h, int32_t format, uint32_t usage ) :
     mBM( AbstractBufferManager::get() ),
     mSizeBytes( 0 ),
     mpRef(NULL),
     mUse(0),
     mbShared( false )
{
    allocate( w, h, format, usage );
}

Buffer::Buffer( sp<GraphicBuffer> pBuffer ) :
    mBM( AbstractBufferManager::get() ),
    mpGraphicBuffer( pBuffer ),
    mSizeBytes( 0 ),
    mpRef(NULL),
    mUse(0),
    mLastFrameUsedTime(0),
    mbShared( true )
{
}

void Buffer::allocate( uint32_t w, uint32_t h, int32_t format, uint32_t  usage )
{
    ALOG_ASSERT( w );
    ALOG_ASSERT( h );
    ALOG_ASSERT( format );
    ALOG_ASSERT( usage & GRALLOC_USAGE_HW_COMPOSER );
    mpGraphicBuffer = mBM.createGraphicBuffer( "BUFFERQUEUE", w, h, format, usage );
    mbShared = false;
    if ( allocationOK() )
    {
        mSizeBytes = mBM.getBufferSizeBytes( mpGraphicBuffer->handle );
    }
    else
    {
        mpGraphicBuffer = NULL;
        mSizeBytes = 0;
    }
}

void Buffer::reallocate( uint32_t w, uint32_t h, int32_t format, uint32_t  usage )
{
    ALOG_ASSERT( w );
    ALOG_ASSERT( h );
    ALOG_ASSERT( format );
    ALOG_ASSERT( usage & GRALLOC_USAGE_HW_COMPOSER );
    mBM.reallocateGraphicBuffer( mpGraphicBuffer, "BUFFERQUEUE", w, h, format, usage );
    mbShared = false;
    if ( allocationOK() )
    {
        mSizeBytes = mBM.getBufferSizeBytes( mpGraphicBuffer->handle );
    }
    else
    {
        mpGraphicBuffer = NULL;
        mSizeBytes = 0;
    }
}

void Buffer::reconfigure( uint32_t w, uint32_t h, int32_t format, uint32_t  usage )
{
    ALOG_ASSERT( w );
    ALOG_ASSERT( h );
    ALOG_ASSERT( format );
    ALOG_ASSERT( usage & GRALLOC_USAGE_HW_COMPOSER );
    if ( !allocationOK() )
    {
        // Attempt to allocate a buffer that was not yet successfully allocated.
        allocate( w, h, format, usage );
    }
    else if ( ( mpGraphicBuffer->getWidth( ) != w )
           || ( mpGraphicBuffer->getHeight( ) != h )
           || ( mpGraphicBuffer->getPixelFormat( ) != format )
           || ( mpGraphicBuffer->getUsage( ) != usage ) )
    {
        // Re-allocate an existing buffer.
        mAcquireFence.waitAndClose();
        reallocate( w, h, format, usage );
    }
    ALOGE_IF( !allocationOK(), "BufferQueue failed to reconfigure [%ux%u fmt:%u/%s usage:0x%x]",
            w, h, format, getHALFormatShortString(format), usage );
}

bool Buffer::matchesConfiguration(uint32_t w, uint32_t h, int32_t format, uint32_t  usage)
{
    ALOG_ASSERT( w );
    ALOG_ASSERT( h );
    ALOG_ASSERT( format );
    ALOG_ASSERT( usage & GRALLOC_USAGE_HW_COMPOSER );
    if ( !allocationOK() )
        return false;
    GraphicBuffer& gb = *mpGraphicBuffer;
    bool bMatch = ((gb.getWidth( ) == w )
                  && ( gb.getHeight( ) == h )
                  && (gb.getPixelFormat( ) == format)
                  && (gb.getUsage( ) == usage));
    ALOGD_IF( BUFFERQUEUE_DEBUG, "%s %s %s", __FUNCTION__, dump().string(), bMatch ? "MATCH" : "MISMATCH" );
    return bMatch;
}

String8 Buffer::dump( void )
{
    if ( !allocationOK() )
        return String8("Invalid Allocation");

    GraphicBuffer& gb = *mpGraphicBuffer;
    return String8::format("Record:%p GraphicBuffer:%p %8u bytes%s %4dx%4d %s 0x%08x use:%c|%c %" PRIi64 "s %03" PRIi64 "ms ref:%-18p %s",
                    this, gb.handle, mSizeBytes, mbShared ? " (shared)" : "",
                     gb.getWidth(), gb.getHeight(),
                    getHALFormatShortString(gb.getPixelFormat()),
                    gb.getUsage(),
                    mUse & EUsedThisFrame ? 'U' : '-',
                    mUse & EUsedRecently ? 'R' : '-',
                    mLastFrameUsedTime / 1000000000,
                    ( mLastFrameUsedTime / 1000000 ) % 1000,
                    mpRef,
                    mAcquireFence.dump().string() );
}

BufferQueue::BufferQueue() :
#if INTEL_HWC_INTERNAL_BUILD
    mStatsEnabled( "compbufferstats", 0 ),
#endif
    mOptionGCTimeout( "compbuffergc", 8000 ),
    mMaxBufferCount(0),
    mMaxBufferAlloc(0),
    mBufferAllocBytes(0),
    mLatestAvailableBuffer(0),
    mDequeuedBuffer(~0U),
    mIdleTimer(*this)
{
}

BufferQueue::~BufferQueue()
{
    clear();
}

void BufferQueue::setConstraints( uint32_t maxBufferCount, uint32_t maxBufferAlloc )
{
    mMaxBufferCount   = maxBufferCount;
    mMaxBufferAlloc   = maxBufferAlloc;
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue x%u %uMB", mMaxBufferCount, mMaxBufferAlloc );
}

String8 BufferQueue::dump( void ) const
{
    String8 output;
    for (uint32_t i = 0; i < mBuffers.size(); i++)
    {
        if ( mBuffers[i]->allocationOK() )
        {
            output.appendFormat( "BufferQueue: i%u %s\n", i, mBuffers[i]->dump().string() );
        }
        else
        {
            output.appendFormat( "BufferQueue: i%u !ALLOCATION FAILED!\n", i );
        }
    }
    return output;
}

void BufferQueue::dumpBlockedBuffers( void )
{
    for (uint32_t i = 0; i < mBuffers.size(); i++)
    {
        Log::alogd( BUFFERQUEUE_DEBUG, "InternalBuffer: Checking %02u/%02zu %s", i, mBuffers.size(), mBuffers[i]->mAcquireFence.dump().string() );

        if ( mBuffers[i]->mAcquireFence.isValid() )
        {
            if ( !mBuffers[i]->mAcquireFence.checkAndClose() )
            {
                Log::alogd( BUFFERQUEUE_DEBUG, "InternalBuffer: Blocked i%02u %s", i, mBuffers[i]->dump().string() );
            }
            else
            {
                Log::alogd( BUFFERQUEUE_DEBUG, "InternalBuffer: checkAndClose i%02u %s", i, mBuffers[i]->dump().string() );
            }
        }
    }
}

#if INTEL_HWC_INTERNAL_BUILD
void BufferQueue::updateBufferStats( void )
{
    const uint32_t sz = mBuffers.size();

    // Reset statistics sampling if the elapsed time between updates is too great (e.g. >10s).
    // Or, if all buffers are deleted.
    nsecs_t nowTime = systemTime(CLOCK_MONOTONIC);
    uint64_t elaTime = (uint64_t)int64_t( nowTime - mStats.mLastSampleTime );
    if ( ( elaTime > 10000000000ULL ) || !sz )
    {
        Log::alogd( true, "BufferQueue STAT RESET %" PRIi64 " v %" PRIi64 " ela %" PRIi64, nowTime, mStats.mLastSampleTime, elaTime );
        mStats.reset( );
    }
    mStats.mLastSampleTime = nowTime;

    char* stateStrE = new char [ sz+1 ];
    char* stateStrA = new char [ sz+1 ];
    char* stateStrB = new char [ sz+1 ];
    char* stateStrU = new char [ sz+1 ];
    char* stateStrR = new char [ sz+1 ];

    if ( stateStrE && stateStrA && stateStrB && stateStrU && stateStrR )
    {
        memset( stateStrE, '-', sz );
        memset( stateStrA, '-', sz );
        memset( stateStrB, '-', sz );
        memset( stateStrU, '-', sz );
        memset( stateStrR, '-', sz );

        stateStrE[ sz ] = '\0';
        stateStrA[ sz ] = '\0';
        stateStrB[ sz ] = '\0';
        stateStrU[ sz ] = '\0';
        stateStrR[ sz ] = '\0';

        uint32_t errors = 0;
        uint32_t allocated = 0;
        uint32_t allocatedBytes = 0;
        uint32_t blocked = 0;
        uint32_t usedThisFrame = 0;
        uint32_t usedRecently = 0;

        for (uint32_t i = 0; i < sz; i++)
        {
            Buffer& b = *mBuffers[i];

            // Perform some validation that the release fence isnt left trailing at -3
            // (add other error checks here).
            INTEL_HWC_DEV_ASSERT( b.mAcquireFence.get() != AWAITING_RELEASE_FENCE );
            if ( b.mAcquireFence.get() == AWAITING_RELEASE_FENCE )
            {
                ++errors;
                stateStrE[ i ] = 'E';
            }
            // Allocated.
            if ( ( b.mpGraphicBuffer != NULL ) && b.mpGraphicBuffer->handle && !b.mbShared )
            {
                ++allocated;
                allocatedBytes += b.mSizeBytes;
                stateStrA[ i ] = 'A';
            }
            // Blocked.
            if ( b.mAcquireFence.isValid() )
            {
                ++blocked;
                stateStrB[ i ] = 'B';
            }
            // Used (most recent frame).
            if ( b.mUse & Buffer::EUsedThisFrame )
            {
                ++usedThisFrame;
                stateStrU[ i ] = 'U';
            }
            // Used (recently).
            if ( b.mUse & Buffer::EUsedRecently )
            {
                ++usedRecently;
                stateStrR[ i ] = 'R';
            }
        }

        mStats.sample( allocated,
                       allocatedBytes,
                       blocked,
                       usedThisFrame,
                       usedRecently );

        Log::alogd( true,
            "BufferQueue STAT COUNTS        : "
            "Allocated %u [%u-%u] peak %u, "
            "KB %u [%u-%u] peak %u, "
            "Blocked %u [%u-%u] peak %u,"
            "UsedThisFrame %u [%u-%u] peak %u,"
            "UsedRecently %u [%u-%u] peak %u",
            // Allocated
            allocated,
            mStats.mMetricRecent[ Stats::METRIC_ALLOCATED ].mMin, mStats.mMetricRecent[ Stats::METRIC_ALLOCATED ].mMax,
            mStats.mMetric[ Stats::METRIC_ALLOCATED ].mMax,
            // Allocated bytes
            allocatedBytes/1024,
            mStats.mMetricRecent[ Stats::METRIC_ALLOCATED_BYTES ].mMin/1024, mStats.mMetricRecent[ Stats::METRIC_ALLOCATED_BYTES ].mMax/1024,
            mStats.mMetric[ Stats::METRIC_ALLOCATED_BYTES ].mMax/1024,
            // Blocked
            blocked,
            mStats.mMetricRecent[ Stats::METRIC_BLOCKED ].mMin, mStats.mMetricRecent[ Stats::METRIC_BLOCKED ].mMax,
            mStats.mMetric[ Stats::METRIC_BLOCKED ].mMax,
            // Used this frame
            usedThisFrame,
            mStats.mMetricRecent[ Stats::METRIC_USED_THIS_FRAME ].mMin, mStats.mMetricRecent[ Stats::METRIC_USED_THIS_FRAME ].mMax,
            mStats.mMetric[ Stats::METRIC_USED_THIS_FRAME ].mMax,
            // Used recently
            usedRecently,
            mStats.mMetricRecent[ Stats::METRIC_USED_RECENTLY ].mMin, mStats.mMetricRecent[ Stats::METRIC_USED_RECENTLY ].mMax,
            mStats.mMetric[ Stats::METRIC_USED_RECENTLY ].mMax );

        if ( errors )
        {
            Log::alogd( true, "BufferQueue STAT ERROR         : %s", stateStrE );
        }
        if ( allocated )
        {
            Log::alogd( true, "BufferQueue STAT ALLOC         : %s", stateStrA );
        }
        if ( blocked )
        {
            Log::alogd( true, "BufferQueue STAT BLOCKED       : %s", stateStrB );
        }
        if ( usedThisFrame )
        {
            Log::alogd( true, "BufferQueue STAT USED          : %s", stateStrU );
        }
        if ( usedRecently )
        {
            Log::alogd( true, "BufferQueue STAT USED RECENTLY : %s", stateStrR );
        }

        mStats.mHistogram[ Stats::HISTOGRAM_ALLOCATED ].sample( allocated );
        mStats.mHistogram[ Stats::HISTOGRAM_USED_THIS_FRAME ].sample( usedThisFrame );

        for ( uint32_t h = 0; h < Stats::HISTOGRAM_MAX; ++h )
        {
            const uint32_t framesWith = mStats.mSamples - mStats.mHistogram[ h ].mSlot[ 0 ];
            for ( uint32_t s = 0; s <= mStats.mHistogram[ h ].mStat.mMax; ++s )
            {
                Log::alogd( true, "BufferQueue STAT HIST %8s : S%02u %5u %6.2f%% (%6.2f%%)",
                    h == Stats::HISTOGRAM_ALLOCATED ? "ALLOC" :
                    h == Stats::HISTOGRAM_USED_THIS_FRAME ? "USED" :
                    "<?>",
                    s, mStats.mHistogram[ h ].mSlot[ s ],
                    (float)mStats.mHistogram[ h ].mSlot[ s ] * 100.0f/mStats.mSamples,
                    (float)mStats.mHistogram[ h ].mSlot[ s ] * 100.0f/framesWith );
            }
        }

        delete [] stateStrE;
        delete [] stateStrA;
        delete [] stateStrB;
        delete [] stateStrU;
        delete [] stateStrR;
    }
}
#endif

void BufferQueue::logBufferState( void )
{
    const uint32_t sz = mBuffers.size();
    bool bInvalidFence = false;

    for (uint32_t i = 0; i < sz; i++)
    {
        // Perform some validation that the release fence isnt left trailing at -3.
        INTEL_HWC_DEV_ASSERT(mBuffers[i]->mAcquireFence.get() != AWAITING_RELEASE_FENCE);
        if (mBuffers[i]->mAcquireFence.get() == AWAITING_RELEASE_FENCE)
            bInvalidFence = true;
    }

    if ( !Log::wantLog( BUFFERQUEUE_DEBUG ) && !bInvalidFence )
    {
        return;
    }

    uint32_t errors = 0;
    uint32_t allocated = 0;
    uint32_t allocatedBytes = 0;
    uint32_t blocked = 0;
    uint32_t usedThisFrame = 0;
    uint32_t usedRecently = 0;

    for (uint32_t i = 0; i < sz; i++)
    {
        Buffer& b = *mBuffers[i];

        Log::alogd( BUFFERQUEUE_DEBUG, "InternalBuffer:%02d %s", i, b.dump().string() );

        // Error.
        if ( b.mAcquireFence.get() == AWAITING_RELEASE_FENCE )
        {
            ++errors;
        }
        // Allocated.
        if ( ( b.mpGraphicBuffer != NULL ) && b.mpGraphicBuffer->handle && !b.mbShared )
        {
            ++allocated;
            allocatedBytes += b.mSizeBytes;
        }
        // Blocked.
        if ( b.mAcquireFence.isValid() )
        {
            ++blocked;
        }
        // Used (most recent frame).
        if ( b.mUse & Buffer::EUsedThisFrame )
        {
            ++usedThisFrame;
        }
        // Used (recently).
        if ( b.mUse & Buffer::EUsedRecently )
        {
            ++usedRecently;
        }
    }

    Log::alogd( BUFFERQUEUE_DEBUG,
        "InternalBuffer: Allocated %u, KB %u, Blocked %u, UsedThisFrame %u, UsedRecently %u",
        allocated, allocatedBytes/1024, blocked, usedThisFrame, usedRecently );

    Log::aloge( errors, "Invalid fence on InternalBuffer");
}

uint32_t BufferQueue::getBlockedBuffers( uint32_t* pBitmask )
{
    uint32_t blocked = 0;
    uint32_t bitmask = 0;
    for (uint32_t i = 0; i < mBuffers.size(); i++)
    {
        if ( mBuffers[i]->mAcquireFence.isValid() )
        {
            if (!mBuffers[i]->mAcquireFence.checkAndClose())
            {
                ++blocked;
                bitmask |= 1<<i;
            }
        }
    }
    if( pBitmask )
        *pBitmask = bitmask;
    return blocked;
}

BufferQueue::BufferHandle BufferQueue::dequeue(uint32_t width, uint32_t height, int32_t bufferFormat, uint32_t usage, Timeline::Fence** ppReleaseFence)
{
    ALOGD_IF(BUFFERQUEUE_DEBUG, "BufferQueue::dequeue %dx%d %x %x", width, height, bufferFormat, usage);
    ALOG_ASSERT( mDequeuedBuffer == ~0U );
    Buffer* pBuffer;

    // To maximize buffer re-use, we use equivalent buffer formats with alpha (e.g.RGBX=>RGBA).
    // This relies on the composition itself to enable/disable blending as necessary based on
    // its original requested format.
    int altFormat = equivalentFormatWithAlpha( bufferFormat );
    if ( altFormat != bufferFormat )
    {
        ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::dequeue Using alpha equivalent format %d/%s for %d/%s",
            altFormat, getHALFormatShortString( altFormat ), bufferFormat, getHALFormatShortString( bufferFormat ) );
        bufferFormat = altFormat;
    }

    // First check to see if any current buffers have been released
    pBuffer = checkForMatchingAvailableBuffer(width, height, bufferFormat, usage);
    if (pBuffer == NULL)
    {
        // Keep adding buffers if we haven't exceeded limits yet.
        // This is just a crude worst-case estimate assuming 4byte-pixels and 4K aligned scanlines.
        const uint32_t estimateWorstCaseSize = (( width*4 + 4095 ) & ~4095) * height;
        ALOGD_IF( BUFFERQUEUE_DEBUG, " Need new/reuse buffers %zu/%u alloc %u/%u est +%u bytes",
            mBuffers.size(), mMaxBufferCount, mBufferAllocBytes, mMaxBufferAlloc,  estimateWorstCaseSize );

        if ( ( !mMaxBufferCount || ( mBuffers.size() < mMaxBufferCount) )
          && ( !mMaxBufferAlloc || ( ( mBufferAllocBytes + estimateWorstCaseSize ) < mMaxBufferAlloc ) ) )
        {
            ALOGD_IF(BUFFERQUEUE_DEBUG, "BufferQueue::dequeue New buffer allocated");
            // Add new Buffers on demand.
            pBuffer = new Buffer(width, height, bufferFormat, usage);
            if ( ( pBuffer == NULL ) || ( !pBuffer->allocationOK() ) )
            {
                ALOGE( "BufferQueue::Buffer allocation failure" );
                delete pBuffer;
                return NULL;
            }
            mBufferAllocBytes += pBuffer->mSizeBytes;
            mLatestAvailableBuffer = mBuffers.size();
            mBuffers.push_back(pBuffer);
            ALOGD_IF(BUFFERQUEUE_DEBUG, "BufferQueue::dequeue pool grown - new size %u", mLatestAvailableBuffer+1);
        }
        else
        {
            // Access existing Buffer.
            ALOGD_IF(BUFFERQUEUE_DEBUG, "BufferQueue::dequeue Wait for existing %dx%d %x %x", width, height, bufferFormat, usage);
            pBuffer = waitForFirstAvailableBuffer(width, height, bufferFormat, usage);
            if ( pBuffer == NULL )
            {
                ALOGE( "BufferQueue::Wait for first available buffer failure" );
                return NULL;
            }
            if ( !pBuffer->allocationOK() )
            {
                ALOGE( "BufferQueue::Wait for first available buffer alloc failure" );
                return NULL;
            }
            // Ensure it matches the current configuration.
            mBufferAllocBytes -= pBuffer->mSizeBytes;
            pBuffer->reconfigure(width, height, bufferFormat, usage);
            if ( !pBuffer->allocationOK() )
            {
                ALOGE( "BufferQueue::Buffer reconfigure alloc failure" );
                return NULL;
            }
            mBufferAllocBytes += pBuffer->mSizeBytes;
        }
    }

#if BUFFERQUEUE_DEBUG
    // Debug: Show which buffers are currently blocked.
    uint32_t blocked, bitmask;
    blocked = getBlockedBuffers( &bitmask );
    if ( blocked )
    {
        uint32_t sz = mBuffers.size();
        char chBits[ sz+1 ];
        uint32_t c = 0;
        for ( ; c < sz; ++c )
            chBits[c] = bitmask&(1<<c) ? 'B' : '-';
        chBits[c] = '\0';
        ALOGD( "BufferQueue : Blocked: x%u [0x%x : %s]", blocked, bitmask, chBits );
    }
#endif

    ALOG_ASSERT( pBuffer == mBuffers[ mLatestAvailableBuffer ] );
    mDequeuedBuffer = mLatestAvailableBuffer;
    pBuffer->mAcquireFence.set( DEQUEUED_BUFFER ); // Indicate that this buffer is now dequeued
    *ppReleaseFence = &(pBuffer->mAcquireFence);
    if ( pBuffer->mpRef )
    {
        // Inform an existing external reference that this buffer is no longer valid.
        pBuffer->mpRef->referenceInvalidate( pBuffer );
    }
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::dequeue index:%d, handle:%p, pReleaseFence:%p",
        mLatestAvailableBuffer, pBuffer->mpGraphicBuffer->handle, *ppReleaseFence );
    return pBuffer;
}

void BufferQueue::queue( int releaseFenceFd )
{
    ALOG_ASSERT( mDequeuedBuffer == mLatestAvailableBuffer );
    mBuffers[ mLatestAvailableBuffer ]->mAcquireFence.set( releaseFenceFd );
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::queue index:%d %s", mLatestAvailableBuffer, mBuffers[ mLatestAvailableBuffer ]->dump().string() );
    mDequeuedBuffer = ~0U;
}

sp<GraphicBuffer> BufferQueue::getGraphicBuffer( BufferHandle handle )
{
    if ( handle == NULL )
        return NULL;
    else
        return handle->mpGraphicBuffer;
}

void BufferQueue::registerReference( BufferHandle handle, BufferReference* pExternalObject )
{
    ALOG_ASSERT( handle );
    if ( handle->mpRef )
    {
        // An existing reference MUST be removed before a new reference is registered.
        ALOG_ASSERT( ( handle->mpRef == NULL )
                  || ( pExternalObject == NULL )
                  || ( handle->mpRef == pExternalObject ) );
    }
    handle->mpRef = pExternalObject;
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::registerReference buffer %s", handle->dump().string() );
}

void BufferQueue::markUsed( BufferHandle handle )
{
    if ( handle == NULL )
        return;
    handle->mUse |= Buffer::EUsedThisFrame;
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::markUsed buffer %s", handle->dump().string() );
}

void BufferQueue::clear( void )
{
    for (uint32_t i = 0; i < mBuffers.size(); i++)
    {
        if (mBuffers[i]->mAcquireFence.isValid())
        {
            ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::clear closing fence" );
            mBuffers[i]->mAcquireFence.close();
        }
        delete mBuffers[i];
    }
    mBuffers.clear();
    mBufferAllocBytes = 0;
    mLatestAvailableBuffer = 0;
    mDequeuedBuffer = ~0U;
}

Buffer* BufferQueue::checkForMatchingAvailableBuffer( uint32_t w, uint32_t h, int32_t format, uint32_t usage )
{
    ALOGD_IF( BUFFERQUEUE_DEBUG, "checkForMatchingAvailableBuffer" );

    for ( uint32_t i = 0; i < mBuffers.size(); i++ )
    {
        Buffer& nb = *mBuffers[ i ];
        ALOGD_IF( BUFFERQUEUE_DEBUG, " Buffer %d %s", i, nb.dump().string() );

        if ( nb.mbShared )
        {
            // Don't match 'temporary' shared records.
            ALOGD_IF( BUFFERQUEUE_DEBUG, "  skipping temporary record" );
        }
        else if ( nb.mUse & Buffer::EUsedThisFrame )
        {
            // Don't match records that are already used in this frame.
            ALOGD_IF( BUFFERQUEUE_DEBUG, "  skipping used buffer" );
        }
        else if (nb.matchesConfiguration(w, h, format, usage))
        {
            // The format matches the requirements
            if ( nb.mAcquireFence.isNull() )
            {
                ALOGD_IF( BUFFERQUEUE_DEBUG, "  is matched and unused and fence is null, returning" );
                mLatestAvailableBuffer = i;
                return &nb;
            }
            else if ( nb.mAcquireFence.isValid()
                   && nb.mAcquireFence.checkAndClose() )
            {
                ALOGD_IF( BUFFERQUEUE_DEBUG, "  is matched and unused and signalled, returning" );
                mLatestAvailableBuffer = i;
                return &nb;
            }
            ALOGD_IF( BUFFERQUEUE_DEBUG, "  is matched and unused but not ready, looking for another" );
        }
    }
    ALOGD_IF(BUFFERQUEUE_DEBUG, "checkForMatchingAvailableBuffer No match" );
    return NULL;
}

Buffer* BufferQueue::waitForFirstAvailableBuffer( uint32_t w, uint32_t h, int32_t format, uint32_t usage )
{
    ALOGD_IF( BUFFERQUEUE_DEBUG, "waitForFirstAvailableBuffer" );

    const uint32_t retryAttempts = 50; // 0.5 second retries,
    const uint32_t retryDelayMS  = 10;
    for ( uint32_t retries = retryAttempts; retries; --retries )
    {
        for ( uint32_t i = 0; i < mBuffers.size(); i++ )
        {
            Buffer& nb = *mBuffers[ i ];
            ALOGD_IF( BUFFERQUEUE_DEBUG, " Buffer %d %s", i, nb.dump().string() );
            if ( nb.mbShared )
            {
                // Don't match 'temporary' shared records.
                ALOGD_IF( BUFFERQUEUE_DEBUG, "  is a temporary record, look for another" );
            }
            else if ( nb.mUse & Buffer::EUsedThisFrame )
            {
                // Don't match records that are already used in this frame.
                ALOGD_IF( BUFFERQUEUE_DEBUG, "  is used, look for another" );
            }
            else
            {
                if (nb.mAcquireFence.isNull())
                {
                    ALOGD_IF( BUFFERQUEUE_DEBUG, "  is unused and fence is null, returning" );
                    mLatestAvailableBuffer = i;
                    return &nb;
                }
                else if ( nb.mAcquireFence.isValid()
                       && nb.mAcquireFence.checkAndClose() )
                {
                    ALOGD_IF( BUFFERQUEUE_DEBUG, "  is unused and signalled, returning" );
                    mLatestAvailableBuffer = i;
                    return &nb;
                }
            }
        }
        ALOGD_IF(BUFFERQUEUE_DEBUG, " waiting for %dms for a free buffer", retryDelayMS);
        usleep( retryDelayMS * 1000 );
    }

    // Fallback path.
    // This is to cover the situation where all composition buffers (count/allocation)
    // are exhausted and blocking.
    Log::aloge(true, "%s: Timeout waiting for client to release buffers.", __FUNCTION__ );
    dumpBlockedBuffers( );

    // Find an existing buffer that can be used as a fallback.
    // This fallback buffer will be shared or evicted+replaced.
    // We know the fallback buffer is blocking (=> is in use) so this may cause a visual artefact.
    // But this is better than growing composition buffer allocations unbound.
    bool bMatch = false;
    const int32_t fallback = findFallbackBuffer( w, h, format, usage, bMatch );

    // Add new Buffer record if we must.
    Buffer* pBuffer = NULL;

    if ( fallback < 0 )
    {
        Log::aloge(true, "%s: Fallback buffer %u no suitable buffer to share/kick",
            __FUNCTION__, mBuffers.size() );
        pBuffer = new Buffer( w, h, format, usage );
        mBufferAllocBytes += pBuffer->mSizeBytes;
    }
    else
    {
        // We have a suitable fallback.
        Buffer* pFallback = mBuffers[ fallback ];

        // Inform a composition referencing the fallback that it is now invalid
        // (either deleted or contents may be overwritten).
        if ( pFallback->mpRef )
        {
            pFallback->mpRef->referenceInvalidate( pFallback );
        }

        if ( bMatch )
        {
            // Share the fallback buffer record's GraphicBuffer if it is good to use.
            Log::aloge( true,
                        "%s: New buffer %u sharing existing GraphicBuffer [GRALLOC %p] from fallback buffer %u %s",
                        __FUNCTION__,
                        mBuffers.size(),
                        pFallback->mpGraphicBuffer == NULL ? 0 : pFallback->mpGraphicBuffer->handle,
                        fallback, pFallback->dump().string() );

            pBuffer = new Buffer( pFallback->mpGraphicBuffer );
        }
        else
        {
            // Create a new allocation at the required size/format.
            pBuffer = new Buffer( w, h, format, usage );
            if ( pBuffer && pBuffer->allocationOK() )
            {
                mBufferAllocBytes += pBuffer->mSizeBytes;
                // Drop the fallback record's existing allocation and replace it
                // with a share to our pBuffer record's new GraphicBuffer.
                Log::aloge( true,
                            "%s: New buffer %u sharing new GraphicBuffer [GRALLOC %p] to fallback buffer %u %s [kicking GRALLOC %p]",
                            __FUNCTION__,
                            mBuffers.size(),
                            pBuffer->mpGraphicBuffer->handle,
                            fallback, pFallback->dump().string(),
                            pFallback->mpGraphicBuffer == NULL ? 0 : pFallback->mpGraphicBuffer->handle );

                mBufferAllocBytes -= pFallback->mSizeBytes;
                pFallback->mSizeBytes = 0;
                pFallback->mpGraphicBuffer = pBuffer->mpGraphicBuffer;
                pFallback->mbShared = true;
            }
        }
    }

    if ( ( pBuffer == NULL ) || ( !pBuffer->allocationOK() ) )
    {
        ALOGE( "BufferQueue::Buffer allocation failure" );
        delete pBuffer;
        return NULL;
    }

    mLatestAvailableBuffer = mBuffers.size();
    mBuffers.push_back(pBuffer);
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::dequeue pool grown - new size %u", mLatestAvailableBuffer+1 );
    return mBuffers[ mLatestAvailableBuffer ];
}

int32_t BufferQueue::findFallbackBuffer( uint32_t w, uint32_t h, int32_t format, uint32_t usage, bool& bMatch )
{
    ALOGD_IF( BUFFERQUEUE_DEBUG, "findFallbackBuffer for %u x %u fmt %d", w, h, format );

    uint32_t fallback = 0;
    int8_t   fallbackMatches = 0;
    int32_t  fallbackScore = -999;

    for ( uint32_t i = 0; i < mBuffers.size(); i++ )
    {
        Buffer& nb = *mBuffers[ i ];

        ALOGD_IF( BUFFERQUEUE_DEBUG, " Buffer %d %s", i, nb.dump().string() );

        int8_t isShared = nb.mbShared ? 1 : 0;
        int8_t useThis = ( nb.mUse & Buffer::EUsedThisFrame ) ? 1 : 0;
        int8_t useRecent = ( nb.mUse & Buffer::EUsedRecently ) ? 1 : 0;
        int8_t matchesConfig = nb.matchesConfiguration( w, h, format, usage ) ? 1 : 0;
        int32_t score =
                      + ( -3 * isShared )
                      + ( -2 * useThis )
                      + ( -1 * useRecent )
                      + ( +1 * matchesConfig );
        bool bBetter = ( score > fallbackScore );

        ALOGD_IF( BUFFERQUEUE_DEBUG, " Score candidate %u %d (%d/%d/%d) %s",
            i, score, useThis, useRecent, matchesConfig, bBetter ? " BETTER" : "" );

        if ( bBetter )
        {
            fallback        = i;
            fallbackScore   = score;
            fallbackMatches = matchesConfig;
        }
    }

    // We can NOT use a shared buffer or a buffer that is already in use this frame.
    // And probably shouldn't use a buffer used on previous frame (e.g. other display).
    // If this occurs then max buffer count/allocation is set too low.
    if ( mBuffers[ fallback ]->mbShared || ( mBuffers[ fallback ]->mUse & Buffer::EUsedThisFrame ) )
        return -1;

    bMatch = ( fallbackMatches == 1 );
    return fallback;
}

void BufferQueue::idleTimeoutHandler( void )
{
    Log::alogd( BUFFERQUEUE_DEBUG, "BufferQueue: idle timeout" );
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    Mutex::Autolock _l( mLock );
    processBuffers( );
}

void BufferQueue::onPrepareBegin( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    mLock.lock();
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::onPrepareBegin (Buffers %zu/%u %u/%u bytes)",
        mBuffers.size(), mMaxBufferCount,
        mBufferAllocBytes, mMaxBufferAlloc );
}

void BufferQueue::onPrepareEnd( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLock );
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::onPrepareEnd (Buffers %zu/%u %u/%u bytes)",
        mBuffers.size(), mMaxBufferCount,
        mBufferAllocBytes, mMaxBufferAlloc );
    mLock.unlock();
}

void BufferQueue::onSetBegin( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    mLock.lock();
    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::onSetBegin (Buffers %zu/%u %u/%u bytes)",
        mBuffers.size(), mMaxBufferCount,
        mBufferAllocBytes, mMaxBufferAlloc );
}

void BufferQueue::onSetEnd( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLock );

    ALOGD_IF( BUFFERQUEUE_DEBUG, "BufferQueue::onSetEnd (Buffers %zu/%u %u/%u bytes)",
        mBuffers.size(), mMaxBufferCount,
        mBufferAllocBytes, mMaxBufferAlloc );

    processBuffers( );

    // Set timeout.
    Log::alogd( BUFFERQUEUE_DEBUG, "BufferQueue: set idle timer %ums", mOptionGCTimeout.get() );
    mIdleTimer.set( mOptionGCTimeout );

    // TODO
    // Consider adding a call up to the CompositionManager to release stale/unused composition records.
    //  It may be better to then move this end-frame/idle processing into CompositionManager and call
    //   down to BuffeQueue.

    mLock.unlock();
}

void BufferQueue::processBuffers( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLock );

#if INTEL_HWC_INTERNAL_BUILD
    // Check our buffer byte count is aligned.
    uint32_t totalBytes = 0;
    for (Buffer* b : mBuffers)
    {
        totalBytes += b->mSizeBytes;
    }
    LOG_ALWAYS_FATAL_IF( totalBytes != mBufferAllocBytes, "Expected alloc bytes %u (got %u)", mBufferAllocBytes, totalBytes );
#endif

    // Update fences first.
    for (Buffer* b : mBuffers)
    {
        if ( b->mAcquireFence.isValid() )
        {
            b->mAcquireFence.checkAndClose();
        }
    }

    uint32_t trimmed = 0;

    // Tag used buffers with system time.
    nsecs_t nowTime = systemTime(CLOCK_MONOTONIC);

    // Iterate buffers in reverse order in case we garbage collect them.
    for ( int32_t i = mBuffers.size()-1; i >= 0; i-- )
    {
        Buffer& b = *mBuffers[i];

        // Dont even consider this buffer if it has been used recently.
        // Just propagate the EUsedThisFrame into EUsedLastFrame and EUsedRecently and clear it.
        // Record the frame time.
        if ( b.mUse & Buffer::EUsedThisFrame )
        {
            ALOGD_IF( BUFFERQUEUE_DEBUG, "  Buffer %d used this frame", i );
            b.mUse |= Buffer::EUsedRecently;
            b.mLastFrameUsedTime = nowTime;
            continue;
        }

        // Clear "used recently" flag if this buffer wasn't used for a while.
        if ( b.mUse & Buffer::EUsedRecently )
        {
            nsecs_t ela = (nsecs_t)int64_t( nowTime - b.mLastFrameUsedTime );
            if ( ela/1000000 >= mOptionGCTimeout )
            {
                // Clear used recently flag.
                b.mUse &= ~Buffer::EUsedRecently;
            }
        }

        // Is this buffer removable right now?
        if ( !b.mAcquireFence.isNull() )
        {
            continue;
        }

        bool bRemoveBuffer = false;

        if ( ( mMaxBufferAlloc > 0 ) && ( mBufferAllocBytes > mMaxBufferAlloc ) )
        {
            // Remove because we are exceeding the buffer allocation limit.
            Log::alogd( BUFFERQUEUE_DEBUG, "InternalBuffer:%02d GC overallocated bytes (%8u v %8u) %s",
                i, mBufferAllocBytes, mMaxBufferAlloc, b.dump().string() );
            bRemoveBuffer = true;
        }
        else if ( ( mMaxBufferCount > 0 ) && ( mBuffers.size() > mMaxBufferCount ) )
        {
            // Remove because we are exceeding the buffer count limit.
            Log::alogd( BUFFERQUEUE_DEBUG, "InternalBuffer:%02d GC overallocated count (%02zd v %02u) %s",
                i, mBuffers.size(), mMaxBufferCount, b.dump().string() );
            bRemoveBuffer = true;
        }
        else if ( !( b.mUse & Buffer::EUsedRecently ) )
        {
            // Remove because it has not be used for a long time.
            Log::alogd( BUFFERQUEUE_DEBUG, "InternalBuffer:%02d GC unused %s",
                i, b.dump().string() );
            bRemoveBuffer = true;
        }

        if ( bRemoveBuffer )
        {
            if ( b.mpRef )
            {
                // Inform an existing external reference that this buffer is no longer valid.
                ALOGD_IF( BUFFERQUEUE_DEBUG, "Invalidating external reference %p", b.mpRef );
                b.mpRef->referenceInvalidate( &b );
            }
            ALOGD_IF( BUFFERQUEUE_DEBUG, "Deleting buffer record %p", &b );

            mBufferAllocBytes -= mBuffers[i]->mSizeBytes;
            delete mBuffers[i];
            mBuffers.removeAt(i);
            ++trimmed;
        }
    }

    if ( trimmed )
    {
        mLatestAvailableBuffer = 0;
        mDequeuedBuffer = ~0U;
    }

    // Log end of process state.
    logBufferState( );

#if INTEL_HWC_INTERNAL_BUILD
    // Per-frame stats.
    if ( mStatsEnabled )
    {
        updateBufferStats( );
    }
#endif

    // Reset all "used this frame" flags (after logging/stats).
    for ( int32_t i = mBuffers.size()-1; i >= 0; i-- )
    {
        Buffer& b = *mBuffers[i];
        b.mUse &= ~Buffer::EUsedThisFrame;
    }
}

} // namespace hwc
} // namespace ufo
} // namespace intel
