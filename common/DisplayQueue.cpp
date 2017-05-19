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
#include "DisplayQueue.h"
#include "Timeline.h"

namespace intel {
namespace ufo {
namespace hwc {

// Minimum number of allocated layers to allow for various
// display arrangements while minimising reallocation.
// Allocated layers can grow beyound this.
static const uint32_t cMinimumLayerAllocCount = 8;

// *****************************************************************************
// DisplayQueue::WorkItem
// *****************************************************************************

DisplayQueue::WorkItem::WorkItem( EType eType ) :
    meType( eType ),
    mpPrev( NULL ),
    mpNext( NULL )
{
}

DisplayQueue::WorkItem::~WorkItem( void )
{
}

DisplayQueue::WorkItem::EType DisplayQueue::WorkItem::getWorkItemType( void ) const
{
    return meType;
}

DisplayQueue::WorkItem* DisplayQueue::WorkItem::getNext( void ) const
{
    return mpNext;
}

DisplayQueue::WorkItem* DisplayQueue::WorkItem::getLast( void ) const
{
    return mpPrev;
}

bool DisplayQueue::WorkItem::isQueued( void ) const
{
    return ( mpPrev && mpNext );
}

void DisplayQueue::WorkItem::setEffectiveFrame( const FrameId& id )
{
    mEffectiveFrame = id;
}

DisplayQueue::FrameId DisplayQueue::WorkItem::getEffectiveFrame( void ) const
{
    return mEffectiveFrame;
}

String8 DisplayQueue::WorkItem::dump( void ) const
{
    return String8::format( "WorkItem:%p %s", this, mEffectiveFrame.dump().string() );
}

void DisplayQueue::WorkItem::queue( DisplayQueue::WorkItem** pQueue, DisplayQueue::WorkItem* pNewWork )
{
    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "DisplayQueue::WorkItem::queue" );
    ALOG_ASSERT( pQueue );
    ALOG_ASSERT( pNewWork );
    ALOG_ASSERT( !pNewWork->isQueued( ) );
    if ( (*pQueue) == NULL )
    {
        (*pQueue) = pNewWork;
        pNewWork->mpNext = pNewWork;
        pNewWork->mpPrev = pNewWork;
        return;
    }
    (*pQueue)->mpPrev->mpNext = pNewWork;
    pNewWork->mpPrev = (*pQueue)->mpPrev;
    pNewWork->mpNext = (*pQueue);
    (*pQueue)->mpPrev = pNewWork;
}

void DisplayQueue::WorkItem::dequeue( DisplayQueue::WorkItem** pQueue, DisplayQueue::WorkItem* pOldWork )
{
    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "DisplayQueue::WorkItem::remove" );
    ALOG_ASSERT( pQueue );
    ALOG_ASSERT( pOldWork );
    ALOG_ASSERT( pOldWork->isQueued( ) );
    ALOG_ASSERT( *pQueue );
    pOldWork->onDequeue( );
    DisplayQueue::WorkItem* pNext = pOldWork->mpNext;
    pOldWork->mpPrev->mpNext = pOldWork->mpNext;
    pOldWork->mpNext->mpPrev = pOldWork->mpPrev;
    pOldWork->mpPrev = NULL;
    pOldWork->mpNext = NULL;
    if ( pOldWork == *pQueue )
    {
        *pQueue = pNext == pOldWork ? NULL : pNext;
    }
}

// *****************************************************************************
// DisplayQueue::WorkItem::Event
// *****************************************************************************

DisplayQueue::Event::Event( uint32_t id ) :
    WorkItem( WORK_ITEM_EVENT ),
    mId( id )
{
}

uint32_t DisplayQueue::Event::getId( void ) const
{
    return mId;
}

String8 DisplayQueue::Event::dump( void ) const
{
    return WorkItem::dump() + String8::format( " Event" );
}

// *****************************************************************************
// DisplayQueue::WorkItem::Frame
// *****************************************************************************

DisplayQueue::FrameLayer::FrameLayer( ) :
    mAcquireFence( -1 ),
    mpAcquiredBuffer( NULL ),
    mbSet( false )
{
}

DisplayQueue::FrameLayer::~FrameLayer( )
{
    reset( false );
}

void DisplayQueue::FrameLayer::set( const Layer& layer )
{
    ALOG_ASSERT( !mbSet );
    ALOG_ASSERT( mpAcquiredBuffer == NULL );

    // Since we will be queuing the layer we must take a "snapshot" of the layer
    // to ensure that references through to composition have been removed before
    // it is queued.
    mLayer.snapshotOf( layer );

    const Timeline::FenceReference& acquireRef = layer.getAcquireFenceReturn( );
    Log::add( "Fence: Layer fb%" PRIi64 " Acq %s", layer.getBufferDeviceId(), acquireRef.dump( ).string( ) );

    ALOG_ASSERT( mAcquireFence < 0 );
    mAcquireFence = acquireRef.dup( );
    mLayer.setAcquireFenceReturn( &mAcquireFence );

    // Our frame layer copy should NOT reference native release fences after this point.
    // We have no guarantee these will remain valid; frame release is signalled by advancing the timeline.
    // Non-native release fence references *ARE* retained; this is to support out-of-order composition buffer release.
    if ( mLayer.getReleaseFenceReturn().getType() == Timeline::FenceReference::eTypeNative )
    {
        mLayer.setReleaseFenceReturn( (int*)NULL );
    }

    buffer_handle_t handle = mLayer.getHandle( );
    if ( handle )
    {
        AbstractBufferManager& bufferManager = AbstractBufferManager::get();
        mpAcquiredBuffer = bufferManager.acquireBuffer( handle );
        validate();
        bufferManager.setBufferUsage( handle, AbstractBufferManager::eBufferUsage_Display );
    }

    Log::add( "Fence: Set Layer gralloc buffer %p device fb%" PRIi64 " Acq %s Rel %s",
        mLayer.getHandle(),
        mLayer.getBufferDeviceId(),
        mLayer.getAcquireFenceReturn().dump().string(),
        mLayer.getReleaseFenceReturn().dump().string() );

    mbSet = true;
}

void DisplayQueue::FrameLayer::validate( void )
{
#if INTEL_HWC_INTERNAL_BUILD
    buffer_handle_t handle = mLayer.getHandle( );
    if ( handle )
    {
        ALOG_ASSERT( mpAcquiredBuffer != NULL );
        ALOG_ASSERT( mLayer.isBufferDeviceIdValid() );
        ALOG_ASSERT( mLayer.getBufferDeviceId() );
        AbstractBufferManager::get().validate( mpAcquiredBuffer, handle, mLayer.getBufferDeviceId() );
    }
#endif
}

void DisplayQueue::FrameLayer::reset( bool bCancel )
{
    Log::add( "Fence: Reset Layer gralloc buffer %p device fb%" PRIi64 " Acq %s Rel %s",
        mLayer.getHandle(),
        mLayer.getBufferDeviceId(),
        mLayer.getAcquireFenceReturn().dump().string(),
        mLayer.getReleaseFenceReturn().dump().string() );

    if ( mAcquireFence >= 0 )
    {
        Timeline::closeFence( &mAcquireFence );
    }

    // Cancel the release fence if we aren't signalling it.
    // This will drop this display queue's reference on the fence so
    // if this layer is a composition buffer it may be released back for reuse
    // as soon as possible.
    if ( bCancel )
    {
        mLayer.cancelReleaseFence( );
    }

    mpAcquiredBuffer = NULL;
    mbSet = false;
}

void DisplayQueue::FrameLayer::waitRendering( void )
{
    if ( !mLayer.isDisabled( ) )
    {
        mLayer.waitRendering( ms2ns(mTimeoutWaitRenderingMsec) );
    }
}

bool DisplayQueue::FrameLayer::isRenderingComplete( void )
{
    if ( mLayer.isDisabled( ) )
        return true;
    else
        return mLayer.waitRendering( 0 );
}

void DisplayQueue::FrameLayer::closeAcquireFence( void )
{
    Timeline::closeFence( &mAcquireFence );
}

bool DisplayQueue::FrameLayer::isDisabled( void ) const
{
    return ( mLayer.isDisabled( ) || mLayer.getBufferDeviceId( ) == 0 );
}

DisplayQueue::Frame::Frame( void ) :
    WorkItem( WORK_ITEM_FRAME ),
    mType( Frame::eFT_CUSTOM ),
    mLayerAllocCount( 0 ),
    mLayerCount( 0 ),
    maLayers( NULL ),
    mZOrder( 0 ),
    mbLockedForDisplay( false ),
    mbValid( false )
{
}

DisplayQueue::Frame::~Frame( )
{
    delete [] maLayers;
}

void DisplayQueue::Frame::setType( uint32_t type )
{
    ALOG_ASSERT( !isLockedForDisplay( ) );
    mType = type;
}

bool DisplayQueue::Frame::set( const Content::LayerStack& stack, uint32_t zorder, const FrameId& id, const Config& config )
{
    ALOG_ASSERT( !isQueued( ) );
    ALOG_ASSERT( !isLockedForDisplay( ) );

    mZOrder = zorder;
    mFrameId = id;
    mbValid = true;


    // Allocate space for layers.
    const uint32_t stackSize = stack.size( );
    const bool bReAllocLayers = ( mLayerAllocCount < stackSize );

    if ( bReAllocLayers )
    {
        delete [] maLayers;
        mLayerAllocCount = (stackSize > cMinimumLayerAllocCount) ?
                                    stackSize : cMinimumLayerAllocCount;

        maLayers = new FrameLayer[ mLayerAllocCount ];

        if ( maLayers == NULL )
        {
            ALOGE( "Failed to allocate x%u FrameLayers", mLayerAllocCount );
            mLayerAllocCount = 0;
            mLayerCount = 0;
            return false;
        }
    }

    if ( mLayerAllocCount < stackSize )
        return false;

    mLayerCount = stackSize;

    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "Display Frame Set x%u layers", mLayerCount );
    for (uint32_t ly = 0; ly < stackSize; ly++)
    {
        const Layer& layer = stack.getLayer(ly);
        maLayers[ ly ].set( layer );
    }

    mConfig = config;

    return true;
}

void DisplayQueue::Frame::validate( void )
{
#if INTEL_HWC_INTERNAL_BUILD
    for ( uint32_t ly = 0; ly < mLayerCount; ly++ )
    {
        maLayers[ ly ].validate( );
    }
#endif
}

uint32_t DisplayQueue::Frame::getType( void ) const
{
    return mType;
}

uint32_t DisplayQueue::Frame::getLayerCount( void ) const
{
    return mLayerCount;
}

const DisplayQueue::FrameLayer* DisplayQueue::Frame::getLayer( uint32_t ly ) const
{
    return editLayer( ly );
}

DisplayQueue::FrameLayer* DisplayQueue::Frame::editLayer( uint32_t ly ) const
{
    return ( mLayerCount && ( ly < mLayerCount ) ) ? &maLayers[ly] : NULL;
}

uint32_t DisplayQueue::Frame::getZOrder( void ) const
{
    return mZOrder;
}

const DisplayQueue::FrameId& DisplayQueue::Frame::getFrameId( void ) const
{
    return mFrameId;
}

void DisplayQueue::Frame::waitRendering( void ) const
{
    for ( uint32_t ly = 0; ly < mLayerCount; ly++ )
    {
        maLayers[ ly ].waitRendering( );
    }
}

bool DisplayQueue::Frame::isRenderingComplete( void ) const
{
    for ( uint32_t ly = 0; ly < mLayerCount; ly++ )
    {
        if ( !maLayers[ ly ].isRenderingComplete( ) )
            return false;
    }
    return true;
}

void DisplayQueue::Frame::reset( bool bCancel )
{
    mbLockedForDisplay = false;
    for (uint32_t ly = 0; ly < mLayerCount; ly++)
    {
        maLayers[ ly ].reset( bCancel );
    }
}

String8 DisplayQueue::Frame::dump( void ) const
{
    return WorkItem::dump();
}

// *****************************************************************************
// DisplayQueue
// *****************************************************************************

DisplayQueue::DisplayQueue( uint32_t behaviourFlags ) :
    mBehaviourFlags( behaviourFlags ),
    mpWorker( NULL ),
    mpWorkQueue( NULL ),
    mQueuedWork( 0 ),
    mQueuedFrames( 0 ),
    mFramesLockedForDisplay( 0 ),
    mFramePoolUsed( 0 ),
    mFramePoolPeak( 0 ),
    mConsumedWork( 0 ),
    mConsumedFramesSinceInit( 0 ),
    mbConsumerBlocked( false )
{
    for ( int32_t f = 0; f < DisplayQueue::mFramePoolCount; ++f )
    {
        maFrames[ f ].setType( Frame::eFT_DISPLAY_QUEUE );
    }
}

DisplayQueue::~DisplayQueue( )
{
    Mutex::Autolock _l( mLockQueue );
    ALOG_ASSERT( !mQueuedFrames );
    ALOG_ASSERT( !mQueuedWork );
    ALOG_ASSERT( !mFramesLockedForDisplay );
    stopWorker( );
}

void DisplayQueue::init( const String8& threadName )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    Mutex::Autolock _l( mLockQueue );
    mName = threadName;
    mConsumedFramesSinceInit = 0;;
}

int DisplayQueue::queueEvent( Event* pEvent )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    ATRACE_NAME_IF( DISPLAY_QUEUE_DEBUG, "DQ queueEvent" );

    ALOG_ASSERT( pEvent );
    ALOG_ASSERT( pEvent->getWorkItemType( ) == WorkItem::WORK_ITEM_EVENT );

    Mutex::Autolock _l( mLockQueue );

    // The effective frame for an event is just a repeat of the last queued frame.
    pEvent->setEffectiveFrame( mLastQueuedFrame );

    doQueueWork( pEvent );

    return OK;
}

int DisplayQueue::queueFrame( const Content::LayerStack& stack, uint32_t zorder, const FrameId& id, const Frame::Config& config )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    ATRACE_NAME_IF( DISPLAY_QUEUE_DEBUG, "DQ queueFrame" );

    Mutex::Autolock _l( mLockQueue );

    // Queued frame sequence can not go backwards.
    mLastQueuedFrame.validateFutureFrame( id );

    uint32_t delta = (uint32_t)int32_t( id.getHwcIndex() - mLastIssuedFrame.getHwcIndex() );
    const uint32_t errorThreshold = 16;
    ALOGE_IF( (mConsumedFramesSinceInit > 0) && mFramesLockedForDisplay && ( delta > errorThreshold),
        "%s display worker tid:%u - display last displayed frame %s [new frame %s]",
        mName.string(), getWorkerTid(), mLastIssuedFrame.dump().string(), id.dump().string() );

    limitUsedFrames();

    DisplayQueue::Frame* pNewFrame = findFree( );
    if ( !pNewFrame )
    {
        ALOGE( "Failed to find free frame" );
        return -ENOSYS;
    }

    // We only expect display queue frames in the worker queue.
    ALOG_ASSERT( pNewFrame->getType() == Frame::eFT_DISPLAY_QUEUE );

    ++mFramePoolUsed;
    if ( mFramePoolUsed > mFramePoolPeak )
    {
        mFramePoolPeak = mFramePoolUsed;
        Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Peak used %u", mName.string(), mFramePoolPeak );
    }

    if ( !pNewFrame->set( stack, zorder, id, config ) )
    {
        ALOGE( "Failed to set display frame" );
        return -ENOSYS;
    }

    // The effective frame id for a frame is (obviously) the frame id itself.
    pNewFrame->setEffectiveFrame( id );

    // Update last queued frame.
    mLastQueuedFrame = id;

    doQueueWork( pNewFrame );

    return OK;
}

void DisplayQueue::queueDrop( const FrameId& id )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    ATRACE_NAME_IF( DISPLAY_QUEUE_DEBUG, "DQ queueDrop" );

    Mutex::Autolock _l( mLockQueue );

    // Queued frame sequence can not go backwards.
    mLastQueuedFrame.validateFutureFrame( id );

    WorkItem* pLastItem = mpWorkQueue ? mpWorkQueue->getLast() : NULL;
    if ( pLastItem == NULL )
    {
        // If we have no queued work then just update display queue state
        // immediately to include this dropped frame.
        // (This will also signal consumed work).
        Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Drop frame %s.", mName.string(), id.dump().string() );
        doAdvanceIssuedFrame( id );
    }
    else
    {
        // Else, advance the last work item's effective frame to also include this dropped frame.
        // The display queue's state will advance once this last item is consumed.
        pLastItem->setEffectiveFrame( id );
        Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Drop frame. Updated last item to %s", mName.string(), pLastItem->dump().string() );
    }

    // Update last queued frame.
    mLastQueuedFrame = id;

    doValidateQueue();
}

void DisplayQueue::dropAllFrames( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    Mutex::Autolock _l( mLockQueue );

    doValidateQueue();

    WorkItem* pWork = mpWorkQueue;
    bool bDone = ( pWork == NULL );
    while ( !bDone )
    {
        WorkItem* pNext = pWork->getNext();
        bDone = ( pNext == mpWorkQueue );
        if ( ( pWork->getWorkItemType() == WorkItem::WORK_ITEM_FRAME )
          && ( !static_cast<Frame*>(pWork)->isLockedForDisplay( ) )
          && ( static_cast<Frame*>(pWork)->getType() == Frame::eFT_DISPLAY_QUEUE ) )
        {
            dropFrame( static_cast<Frame*>(pWork) );
        }
        pWork = pNext;
    }

    doValidateQueue();
}

void DisplayQueue::dropRedundantFrames( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    Mutex::Autolock _l( mLockQueue );
    doDropRedundantFrames( );
}

bool DisplayQueue::consumeWork( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    ATRACE_NAME_IF( DISPLAY_QUEUE_DEBUG, "DQ consumeWork" );

    Mutex::Autolock _l( mLockQueue );
    return doConsumeWork( );
}

void DisplayQueue::flush( uint32_t frameIndex, nsecs_t timeoutNs )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    Mutex::Autolock _l( mLockQueue );

    // The worker thread cannot flush itself synchronously!
    const bool bFlushed  = ( getWorkerTid() != gettid() )
                        && !mbConsumerBlocked
                        && doFlush( frameIndex, timeoutNs );

    // We could not flush or the consumer became locked during the call to flush.
    // Instead, invalidate all currently queued frames so they can be skipped/retired later.
    if ( !bFlushed  )
    {
        doInvalidateFrames();
    }
}

void DisplayQueue::consumerBlocked( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );
    Mutex::Autolock _l( mLockQueue );
    mbConsumerBlocked  = true;
    mConditionWorkConsumed.broadcast( );
}

void DisplayQueue::consumerUnblocked( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );
    Mutex::Autolock _l( mLockQueue );
    INTEL_HWC_DEV_ASSERT( mbConsumerBlocked );
    mbConsumerBlocked  = false;
    mConditionWorkConsumed.broadcast( );
}

void DisplayQueue::notifyReady( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s Notified ready", mName.string() );

    Mutex::Autolock _l( mLockQueue );
    if ( mpWorker != NULL )
    {
        mpWorker->signalWork( );
    }
}

void DisplayQueue::releaseFrame( Frame* pOldFrame )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLockQueue );

    Mutex::Autolock _l( mLockQueue );
    doReleaseFrame( pOldFrame );
}

String8 DisplayQueue::dump( void )
{
    String8 str;
#if DISPLAY_QUEUE_DEBUG
    int32_t queuedWork = 0;
    int32_t queuedFrames = 0;
    int32_t framesLockedForDisplay = 0;

    str += String8::format( "%s : QueuedWork %u QueuedFrames %u PoolUsed %u LastQueued %s LastIssued %s FramesLockedForDisplay %u ConsumedWork %u mConsumedFramesSinceInit %u",
        mName.string(), mQueuedWork, mQueuedFrames, mFramePoolUsed,
        mLastQueuedFrame.dump().string(), mLastIssuedFrame.dump().string(),
        mFramesLockedForDisplay,
        mConsumedWork,
        mConsumedFramesSinceInit );

    // Dump queue.
    str += String8::format( " QueuedWork={" );
    if ( mpWorkQueue )
    {
        DisplayQueue::WorkItem* pWork = mpWorkQueue;
        do
        {
            str += String8::format( " %s", pWork->dump().string() );
            pWork = pWork->getNext( );
            ++queuedWork;
        } while ( pWork != mpWorkQueue );
    }
    str += String8::format( " } QueuedFrames={" );
    for ( int32_t f = 0; f < DisplayQueue::mFramePoolCount; ++f )
    {
        DisplayQueue::Frame* pFrame = &maFrames[ f ];
        if ( pFrame->isQueued( ) )
        {
            str += String8::format( " %s", pFrame->dump().string() );
            ++queuedFrames;
        }
    }
    str += String8::format( " } FramesLockedForDisplay={" );
    for ( int32_t f = 0; f < DisplayQueue::mFramePoolCount; ++f )
    {
        DisplayQueue::Frame* pFrame = &maFrames[ f ];
        if ( pFrame->isLockedForDisplay( ) )
        {
            str += String8::format( " %s", pFrame->dump().string() );
            ++framesLockedForDisplay;
        }
    }
    str += String8::format( " }" );

    ALOG_ASSERT( queuedWork == mQueuedWork );
    ALOG_ASSERT( queuedFrames == mQueuedFrames );
    ALOG_ASSERT( framesLockedForDisplay == mFramesLockedForDisplay );
#endif
    return str;
}

void DisplayQueue::doQueueWork( WorkItem* pWork )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    ALOG_ASSERT( pWork );

    bool bIsAFrame = ( pWork->getWorkItemType() == WorkItem::WORK_ITEM_FRAME );

    // Tracing for production of this work item (including counter values once queued).
    ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( "%s Queue %s", mName.string(), pWork->dump().string() ) );
    Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Queue %s [Work:%u Frames:%u PoolUsed:%u]",
        mName.string(), pWork->dump().string(), mQueuedWork+1, bIsAFrame ? mQueuedFrames+1 : mQueuedFrames, mFramePoolUsed );

    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s doQueueWork Before: %s", mName.string(), dump().string() );

    ALOG_ASSERT( ( ( mQueuedWork == 0 ) && ( mpWorkQueue == NULL ) )
              || ( ( mQueuedWork > 0 ) && ( mpWorkQueue != NULL ) ) );

    // Issued frame indices must always trail queued frame indices.
    mLastIssuedFrame.validateFutureFrame( pWork->getEffectiveFrame() );

    DisplayQueue::WorkItem::queue( &mpWorkQueue, pWork );
    ++mQueuedWork;
    if ( bIsAFrame )
    {
        ++mQueuedFrames;
    }

    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s doQueueWork After: %s", mName.string(), dump().string() );

    if ( mpWorker == NULL )
    {
        startWorker( );
    }

    if ( mpWorker != NULL )
    {
        mpWorker->signalWork( );
    }

    doValidateQueue();
}

bool DisplayQueue::doFlush( uint32_t frameIndex, nsecs_t timeoutNs )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    ALOGD_IF( DISPLAY_QUEUE_DEBUG || HWC_SYNC_DEBUG, "Flush %s [flush to frame %u, timeout %" PRIi64 "]", dump().string(), frameIndex, timeoutNs );

    // Wait for worker to reach or pass the specified frame.
    if ( mpWorker != NULL )
    {
        const int32_t maxConsume = mQueuedWork;
        const uint32_t startConsumeCount = mConsumedWork;

        ALOGD_IF( DISPLAY_QUEUE_DEBUG || HWC_SYNC_DEBUG, " maxConsume %u, startConsumeCount %u", maxConsume, startConsumeCount );

        while ( !mbConsumerBlocked                                                  // This display consumer is not blocked.
           &&   mQueuedWork                                                         // Keep going while there is still queued work
           && ( int32_t( mConsumedWork - startConsumeCount ) < maxConsume )         // and we didn't yet consume all the work we started with
           && ( (frameIndex == 0)                                                   // and we wanted to consume all work
             || ( int32_t( frameIndex - mLastIssuedFrame.getHwcIndex() ) > 0 ) ) )  // or up to and including a specific frame.
        {
            ALOGD_IF( DISPLAY_QUEUE_DEBUG || HWC_SYNC_DEBUG, "QueuedWork x%u, LastQueued %s, LastIssued %s",
                mQueuedWork,  mLastQueuedFrame.dump().string(),  mLastIssuedFrame.dump().string() );
            mpWorker->signalWork( );
            status_t err;
            if ( timeoutNs )
                err = mConditionWorkConsumed.waitRelative( mLockQueue, timeoutNs );
            else
                err = mConditionWorkConsumed.wait( mLockQueue );
            if ( err != OK )
            {
                Log::aloge( true, "%s flush work wait return %d/%s", mName.string(),
                            err, ( err == TIMED_OUT ) ? "TIMEOUT" : "-?-" );
                break;
            }
        }
    }

    if ( mbConsumerBlocked )
    {
        return false;
    }

    Log::alogd( DISPLAY_QUEUE_DEBUG || HWC_SYNC_DEBUG, "Queue: %s flushed Frame:%u", mName.string(), mLastIssuedFrame.getHwcIndex() );
    mLockQueue.unlock();

    // Synchronize the flip completion.
    syncFlip( );

    mLockQueue.lock();
    Log::alogd( DISPLAY_QUEUE_DEBUG || HWC_SYNC_DEBUG, "Queue: %s completed flip to Frame:%u", mName.string(), mLastIssuedFrame.getHwcIndex() );
    return true;
}

void DisplayQueue::doInvalidateFrames( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    ALOGD_IF( DISPLAY_QUEUE_DEBUG || HWC_SYNC_DEBUG, "Invalidate %s", dump().string() );

    doValidateQueue();

    WorkItem* pWork = mpWorkQueue;
    bool bDone = ( pWork == NULL );
    while ( !bDone )
    {
        WorkItem* pNext = pWork->getNext();
        bDone = ( pNext == mpWorkQueue );
        if ( ( pWork->getWorkItemType() == WorkItem::WORK_ITEM_FRAME )
          && ( !static_cast<Frame*>(pWork)->isLockedForDisplay( ) )
          && ( static_cast<Frame*>(pWork)->getType() == Frame::eFT_DISPLAY_QUEUE ) )
        {
            Frame* pFrame = static_cast<Frame*>(pWork);
            pFrame->invalidate();
        }
        pWork = pNext;
    }

    doValidateQueue();
}


void DisplayQueue::doReleaseFrame( Frame* pOldFrame )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    ALOG_ASSERT( pOldFrame );
    ALOG_ASSERT( pOldFrame->getWorkItemType( ) == WorkItem::WORK_ITEM_FRAME );
    ALOG_ASSERT( pOldFrame->getType() == Frame::eFT_DISPLAY_QUEUE );
    ALOG_ASSERT( pOldFrame->isLockedForDisplay() );

    doValidateQueue();

    // Tracing for release of this work item (including counter values once released).
    Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Release %s [Work:%u Frames:%u PoolUsed:%u]",
        mName.string(), pOldFrame->dump().string(), mQueuedWork, mQueuedFrames, mFramePoolUsed-1 );

    pOldFrame->reset( false );

    ALOG_ASSERT( mFramesLockedForDisplay > 0 );
    ALOG_ASSERT( mFramePoolUsed > 0 );
    --mFramesLockedForDisplay;
    --mFramePoolUsed;

    doValidateQueue();

    mConditionFrameReleased.broadcast( );
}

void DisplayQueue::limitUsedFrames( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    // Generally, we want to queue all frames and leave any dropping to the worker.
    // However, we have some circumstances where this is not sufficient.
    // e.g.
    //   - If we are being delivered frames faster than the display can consume them.
    //   - If the display worker is stalled issueing an operation that takes some time
    //      to complete (e.g. mode change)
    // Strategy:
    //   - Drop any redundant frames first (as early as possible).
    //   - Else, if queued frames exceeds some limit then we can try:
    //     - Stall for some time to give display a chance to drain.
    //     - Else give up (in which case, if all frames end up used, findFree() will just drop the oldest).

    doDropRedundantFrames( );

    if ( mFramePoolUsed < mFramePoolLimit )
        return;

    nsecs_t beginTimeNs = systemTime(SYSTEM_TIME_MONOTONIC);
    nsecs_t elaNs = 0;
    for (;;)
    {
        nsecs_t waitNs = mTimeoutForLimit - elaNs;
        Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Limit [used %u/%u]",
            mName.string(), mFramePoolUsed, mFramePoolLimit );
        status_t err = mConditionWorkConsumed.waitRelative( mLockQueue, waitNs );
        if (( err != OK ) && ( err != TIMED_OUT ))
        {
            Log::aloge( true, "Queue: %s Limit wait work !ERROR! %d", mName.string(), err );
        }
        if ( mFramePoolUsed < mFramePoolLimit )
        {
            break;
        }
        nsecs_t endTimeNs = systemTime(SYSTEM_TIME_MONOTONIC);
        elaNs = (nsecs_t)int64_t( endTimeNs - beginTimeNs );
        if ( elaNs >= mTimeoutForLimit )
        {
            Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Limit TIMEOUT", mName.string() );
            break;
        }
    }
}

DisplayQueue::Frame* DisplayQueue::findFree( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    // Find first unused or oldest queued.
    DisplayQueue::Frame* pOldest = NULL;
    for ( int32_t f = 0; f < DisplayQueue::mFramePoolCount; ++f )
    {
        DisplayQueue::Frame* pFrame = &maFrames[ f ];
        if ( pFrame->isLockedForDisplay( ) )
        {
            continue;
        }
        if ( !pFrame->isQueued( ) )
        {
            return pFrame;
        }
        if ( ( pOldest == NULL )
          || ( int32_t( pOldest->getFrameId().getTimelineIndex()
                      - pFrame->getFrameId().getTimelineIndex() ) > 0 ) )
        {
            pOldest = pFrame;
        }
    }
    if ( pOldest == NULL )
    {
        Log::aloge( true, "Queue: All frames on display - check releaseFrame( ) is being called [Queued %u, OnDisplay %u, Pool %u]",
            mQueuedFrames, mFramesLockedForDisplay, DisplayQueue::mFramePoolCount );
        ALOGE( "%s", dump().string() );
        return NULL;
    }
    dropFrame( pOldest );
    return pOldest;
}

void DisplayQueue::dropFrame( DisplayQueue::Frame* pFrame )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    ALOG_ASSERT( pFrame );
    ALOG_ASSERT( pFrame->getWorkItemType( ) == WorkItem::WORK_ITEM_FRAME );
    ALOG_ASSERT( pFrame->getType() == Frame::eFT_DISPLAY_QUEUE );
    ALOG_ASSERT( pFrame->isQueued() );
    ALOG_ASSERT( !pFrame->isLockedForDisplay() );

    mLastDroppedFrame = pFrame->getFrameId();

    // Tracing for consumption of this work item (including counter values once consumed).
    ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( "%s Drop %s", mName.string(), pFrame->dump().string() ) );
    Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Drop %s [Work:%u Frames:%u PoolUsed:%u]",
        mName.string(), pFrame->dump().string(), mQueuedWork-1, mQueuedFrames-1, mFramePoolUsed-1 );

    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s dropFrame Before: %s", mName.string(), dump().string() );

    // Dequeue frame.
    DisplayQueue::WorkItem::dequeue( &mpWorkQueue, pFrame );
    ALOG_ASSERT( mQueuedFrames > 0 );
    ALOG_ASSERT( mQueuedWork > 0 );
    ALOG_ASSERT( mFramePoolUsed > 0 );
    --mQueuedFrames;
    --mQueuedWork;
    --mFramePoolUsed;

    // Reset with cancel.
    pFrame->reset( true );


    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s dropFrame After: %s", mName.string(), dump().string() );

    // Signal consume.
    mConditionWorkConsumed.broadcast( );
}

void DisplayQueue::doDropRedundantFrames( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    // Check we have some work.
    if ( mpWorkQueue == NULL )
        return;

    // Get most recent queued work.
    WorkItem* pNewer = mpWorkQueue->getLast();

    // Check we actually have multiple items of work.
    if ( pNewer == mpWorkQueue )
        return;

    // Is the newer item a completed frame?
    bool bNewerComplete =
            ( pNewer->getWorkItemType() == WorkItem::WORK_ITEM_FRAME )
         && ( static_cast<Frame*>(pNewer)->isRenderingComplete( ) );

    // Get preceding work.
    WorkItem* pCurrent = pNewer->getLast();

    // Now step through from newer to older frames.
    // Drop frames where there is at least one newer frame for which rendering is done.
    for (;;)
    {
        bool bReachedHead = ( pCurrent == mpWorkQueue );
        WorkItem* pNext = pCurrent->getLast();

        if ( pCurrent->getWorkItemType() == WorkItem::WORK_ITEM_FRAME )
        {
            Frame* pFrame = static_cast<Frame*>(pCurrent);
            if ( bNewerComplete )
            {
                if ( !pFrame->isLockedForDisplay() )
                {
                    dropFrame( pFrame );
                }
            }
            else
            {
                bNewerComplete = pFrame->isRenderingComplete( );
            }
        }
        if ( bReachedHead )
            break;
        pCurrent = pNext;
    }
}

void DisplayQueue::doAdvanceIssuedFrame( const FrameId& id )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    // We expect hwc index and timeline index to NOT move backwards.
    mLastIssuedFrame.validateFutureFrame( id );
    mLastIssuedFrame = id;
    // Signal consumed.
    mConditionWorkConsumed.broadcast( );
}

bool DisplayQueue::doConsumeWork( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    doValidateQueue();

    if ( mpWorkQueue == NULL )
    {
        ALOG_ASSERT( mQueuedWork == 0 );
        return false;
    }
    ALOG_ASSERT( mQueuedWork > 0 );

    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s doConsumeWork Before: %s", mName.string(), dump().string() );

    // Consume the next work item.
    switch ( mpWorkQueue->getWorkItemType( ) )
    {
        case WorkItem::WORK_ITEM_FRAME:
            doConsumeFrame( );
            break;
        case WorkItem::WORK_ITEM_EVENT:
            doConsumeEvent( );
            break;
        default:
            ALOG_ASSERT( 0 ) ;
            break;
    }

    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s doConsumeWork After: %s", mName.string(), dump().string() );

    return true;
}

void DisplayQueue::doConsumeEvent( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    ALOG_ASSERT( mpWorkQueue );
    ALOG_ASSERT( mQueuedWork > 0 );
    ALOG_ASSERT( mpWorkQueue->getWorkItemType( ) == WorkItem::WORK_ITEM_EVENT );

    // Consume event.
    Event* pEvent = static_cast<Event*>(mpWorkQueue);

    // Issued frame sequence can not go backwards.
    mLastIssuedFrame.validateFutureFrame( pEvent->getEffectiveFrame() );

    // Tracing for consumption of this work item (including counter values once consumed).
    ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( "%s Consume event %s", mName.string(), pEvent->dump().string() ) );
    Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Consume event %s [Work:%u Frames:%u PoolUsed:%u]",
        mName.string(), pEvent->dump().string(), mQueuedWork-1, mQueuedFrames, mFramePoolUsed );

    // Issue event without lock so future work can continue to be queued.
    ATRACE_INT_IF( DISPLAY_QUEUE_DEBUG, "DQ event (unlocked)", 1 );
    mLockQueue.unlock( );

    // Issue event.
    consumeWork( pEvent );

    ATRACE_INT_IF( DISPLAY_QUEUE_DEBUG, "DQ event (unlocked)", 0 );
    mLockQueue.lock( );

    // Re-validate.
    doValidateQueue();

    // Dequeue consumed work.
    ALOG_ASSERT( mQueuedWork > 0 );
    DisplayQueue::WorkItem::dequeue( &mpWorkQueue, pEvent );
    --mQueuedWork;
    ++mConsumedWork;

    // Advance issued frame from this work item's effective frame.
    // (This will also signal consumed work).
    doAdvanceIssuedFrame( pEvent->getEffectiveFrame() );

    // Delete the event.
    delete pEvent;
}

void DisplayQueue::doConsumeFrame( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );

    ALOG_ASSERT( mpWorkQueue );
    ALOG_ASSERT( mQueuedWork > 0 );
    ALOG_ASSERT( mQueuedFrames > 0 );
    ALOG_ASSERT( mpWorkQueue->getWorkItemType( ) == WorkItem::WORK_ITEM_FRAME );

    // We only expect one frame to be locked at the point we consume the next frame
    // since the current design anticipates the display will not return "readyForNextWork"
    // until the previous frame flip is complete.
    ALOG_ASSERT( mFramesLockedForDisplay <= 1 );

    // Consume Frame.
    Frame* pFrame = static_cast<Frame*>(mpWorkQueue);

    // We only expect display queue frames in the worker queue.
    ALOG_ASSERT( pFrame->getType() == Frame::eFT_DISPLAY_QUEUE );

    // Issued frame sequence can not go backwards.
    mLastIssuedFrame.validateFutureFrame( pFrame->getEffectiveFrame() );

    // Issued frame sequence can not go backwards.
    mLastIssuedFrame.validateFutureFrame( pFrame->getFrameId() );

    // Lock the frame for display immediately so it can't be reused or removed during consume.
    lockFrameForDisplay( pFrame );

    // Synchronise source buffers if necessary.
    if ( mBehaviourFlags & eBF_SYNC_BEFORE_FLIP )
    {
        // Sync all buffers before flipping them.
        ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s Waiting for frame %s rendering to complete", mName.string(), pFrame->dump().string() );

        // Wait for buffers without lock so future work can continue to be queued.
        ATRACE_INT_IF( DISPLAY_QUEUE_DEBUG, "DQ wait rendering (unlocked)", 1 );
        mLockQueue.unlock( );

        // Wait for source buffer rendering to complete.
        pFrame->waitRendering( );
        ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s Frame %s rendering completed", mName.string(), pFrame->dump().string() );

        ATRACE_INT_IF( DISPLAY_QUEUE_DEBUG, "DQ wait rendering (unlocked)", 0 );
        mLockQueue.lock( );

        // Re-validate.
        doValidateQueue();

        // The head work item must not have changed.
        ALOG_ASSERT( mpWorkQueue == pFrame );
        ALOG_ASSERT( pFrame->isLockedForDisplay() );
        unlockFrameForDisplay( pFrame );

        // It is possible that newer frames may be queued that have already completed rendering.
        // Always flip the newest (most recently queued) ready frame.
        // Drop all older frames.
        doDropRedundantFrames( );

        // We must still have at least one workitem queued.
        ALOG_ASSERT( mpWorkQueue );

        // First work item may no longer be a frame!
        // NOTE: mpWorkQueue cannot be NULL here, but we still explicitly check as a W/A for code analysis tools.
        if ( ( mpWorkQueue == NULL ) || ( mpWorkQueue->getWorkItemType( ) != WorkItem::WORK_ITEM_FRAME ) )
            return;

        pFrame = static_cast<Frame*>(mpWorkQueue);
        lockFrameForDisplay( pFrame );

        // We only expect display queue frames in the worker queue.
        ALOG_ASSERT( pFrame->getType() == Frame::eFT_DISPLAY_QUEUE );
    }

    // Tracing for consumption of this work item.
    ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( "%s Consume frame %s", mName.string(), pFrame->dump().string() ) );

    // Tracing for consumption of this work item (including counter values once consumed).
    Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Consume frame %s [Work:%u Frames:%u PoolUsed:%u]",
        mName.string(), pFrame->dump().string(), mQueuedWork-1, mQueuedFrames-1, mFramePoolUsed );

    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "%s Flipping to frame %s", mName.string(), pFrame->dump().string() );

    // Dequeue frame before trying to flip it.
    // We MUST do this first because a failed flip will return the frame object
    // to the pool for immediate reuse.
    ALOG_ASSERT( mQueuedFrames > 0 );
    ALOG_ASSERT( mQueuedWork > 0 );
    DisplayQueue::WorkItem::dequeue( &mpWorkQueue, pFrame );
    --mQueuedFrames;
    --mQueuedWork;
    ++mConsumedFramesSinceInit;
    ++mConsumedWork;

    FrameId effectiveIssuedFrame = pFrame->getEffectiveFrame();
    // Advance issued frame from this work item's effective frame.
    // (This will also signal consumed work).
    // NOTE:
    //   Because we can coalesce later dropped frame info into the last work
    //   item the effective frame may advance beyond frame index.
    ALOG_ASSERT( int32_t( effectiveIssuedFrame.getHwcIndex() - pFrame->getFrameId().getHwcIndex() ) >= 0 );

    // Issue flip without lock so future work can continue to be queued.
    ATRACE_INT_IF( DISPLAY_QUEUE_DEBUG, "DQ flip (unlocked)", 1 );
    mLockQueue.unlock( );

    // Issue frame.
    // NOTE:
    //  When a flip fails then we expect the Display to synchronously release
    //  the frame for us - for this reason we MUST NOT reference the frame state
    //   after this point.
    consumeWork( pFrame );

    ATRACE_INT_IF( DISPLAY_QUEUE_DEBUG, "DQ flip (unlocked)", 0 );
    mLockQueue.lock( );

    // Re-validate.
    doValidateQueue();

    // Advance issued frame from this work item's effective frame.
    // (This will also signal consumed work).
    doAdvanceIssuedFrame( effectiveIssuedFrame );
}

#if INTEL_HWC_INTERNAL_BUILD
void DisplayQueue::doValidateQueue( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLockQueue );
    // Queued frame indices can not go backwards.
    // Also, check counter consistency.
    int32_t frame = 0;
    int32_t work = 0;
    int32_t pool = 0;
    WorkItem* pWork = mpWorkQueue;
    if ( pWork )
    {
        for ( ;; )
        {
            ++work;
            if ( pWork->getWorkItemType() == WorkItem::WORK_ITEM_FRAME )
            {
                ++frame;
                Frame* pFrame = static_cast<Frame*>(pWork);
                if ( pFrame->getType() == Frame::eFT_DISPLAY_QUEUE )
                {
                    ++pool;
                }
            }

            WorkItem* pNext = pWork->getNext();
            ALOG_ASSERT( pNext );
            if ( pNext == mpWorkQueue )
                break;

            FrameId frameId = pWork->getEffectiveFrame();
            frameId.validateFutureFrame( pNext->getEffectiveFrame() );
            pWork = pNext;
        }
    }
    // Assert counter consistency,
    // NOTE:
    //   A flipped frame will no longer be in the queue, but will
    //   still be counted against frame pool used until it is released.
    LOG_FATAL_IF( work  != mQueuedWork,    "DisplayQueue state work %d v mQueuedWork %d",    work,  mQueuedWork    );
    LOG_FATAL_IF( frame != mQueuedFrames,  "DisplayQueue state frame %d v mQueuedFrames %d", frame, mQueuedFrames  );
    LOG_FATAL_IF( pool  >  mFramePoolUsed, "DisplayQueue state pool %d v mFramePoolUsed %d", pool,  mFramePoolUsed );
    // Issued frame indices must always trail queued frame indices.
    mLastIssuedFrame.validateFutureFrame( mLastQueuedFrame );
}
#endif

void DisplayQueue::startWorker( void )
{

    if ( mpWorker == NULL )
    {
        ALOGD_IF( DISPLAY_QUEUE_DEBUG, "Starting worker %s", mName.string( ) );
        mpWorker = new Worker( *this, mName );
        ALOGE_IF( mpWorker.get() == NULL, "Failed to start worker for %s", mName.string( ) );
    }
}

void DisplayQueue::stopWorker( void )
{

    if ( mpWorker != NULL )
    {
        ALOGD_IF( DISPLAY_QUEUE_DEBUG, "Stopping worker %s", mName.string( ) );
        mpWorker = NULL;
    }
}


pid_t DisplayQueue::getWorkerTid( void )
{
    return ( mpWorker == NULL ) ? 0 : mpWorker->getTid();
}

// *****************************************************************************
// DisplayQueue::Worker
// *****************************************************************************

DisplayQueue::Worker::Worker( DisplayQueue& queue, const String8& threadName ) :
    mQueue( queue ),
    mbRunning( false ),
    mSignalled( 0 )
{
    start( threadName );
    ALOG_ASSERT( mbRunning );
    ALOG_ASSERT( !exitPending( ) );
}

DisplayQueue::Worker::~Worker( )
{
    stop( );
    ALOGE_IF( mbRunning, "Display queue worker thread was not terminated" );
}

void DisplayQueue::Worker::signalWork( void )
{
    Mutex::Autolock _l( mLock );
    ALOGD_IF( DISPLAY_QUEUE_DEBUG, "Display queue worker signal work" );
    ALOG_ASSERT( !exitPending( ) );
    ALOG_ASSERT( mSignalled >= 0 );
    ++mSignalled;
    mWork.broadcast( );
}

void DisplayQueue::Worker::stop( void )
{
    if ( mbRunning )
    {
        mbRunning = false;
        Thread::requestExit( );
        mWork.broadcast( );
        Thread::join( );
    }
}

void DisplayQueue::Worker::start( const String8& threadName )
{
    if ( !mbRunning )
    {
        ALOGD_IF( DISPLAY_QUEUE_DEBUG, "Display queue thread starting [%s]", threadName.string( ) );
        if ( run( threadName.string( ), PRIORITY_URGENT_DISPLAY ) == android::OK )
        {
            mbRunning = true;
        }
    }
}

bool DisplayQueue::Worker::threadLoop( )
{

    // Spin until work is available and device is ready.
    for (;;)
    {
        bool bWaitForWork = false;
        bool bWaitForReady = false;

        // Drop redundant frames as early as possible.
        mQueue.dropRedundantFrames();

        // Poll queue/device status.
        if ( !mQueue.readyForNextWork( ) )
        {
            bWaitForReady = true;
        }
        else if ( !mQueue.getQueuedWork( ) )
        {
            bWaitForWork = true;
        }

        // Apply waits if necessary.
        if ( bWaitForWork || bWaitForReady )
        {
            Mutex::Autolock _l( mLock );

            // Re-check we didn't already get signalled.
            ALOG_ASSERT( mSignalled >= 0 );
            if ( mSignalled > 0 )
            {
                --mSignalled;
            }
            else
            {
                // Apply wait.
                if ( bWaitForReady )
                {
                    // Display is not ready.
                    // Block until signalled ready or timeout (to cover flip failure).
                    ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( "%s Not ready", mQueue.getName().string() ) );
                    Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Not ready", mQueue.getName().string() );
                    status_t err = mWork.waitRelative( mLock, mTimeoutForReady );
                    if ( err == OK )
                    {
                        ALOG_ASSERT( mSignalled > 0 );
                        --mSignalled;
                    }
                    else if ( err == TIMED_OUT )
                    {
                        ALOGD_IF( DISPLAY_QUEUE_DEBUG, "Display queue timeout waiting for display to signal ready" );
                    }
                    else
                    {
                        ALOGE( "Display queue error waiting for display to signal ready" );
                    }
                }
                else
                {
                    // Display is ready but we don't have any more work yet.
                    // Block for new work.
                    ATRACE_NAME_IF( DISPLAY_TRACE, String8::format( "%s Out of work", mQueue.getName().string() ) );
                    Log::alogd( DISPLAY_QUEUE_DEBUG, "Queue: %s Out of work", mQueue.getName().string() );
                    status_t err = mWork.wait( mLock );
                    if ( err == OK )
                    {
                        ALOG_ASSERT( mSignalled > 0 );
                        --mSignalled;
                    }
                    else
                    {
                        ALOGE( "Display queue error waiting for work" );
                    }
                }
            }
        }
        else
        {
            break;
        }
    }

    // Consume work.
    mQueue.consumeWork( );

    return true;
}

void DisplayQueue::Worker::requestExit()
{
    Thread::requestExit( );
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
