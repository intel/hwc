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

#ifndef INTEL_UFO_HWC_DISPLAYQUEUE_H
#define INTEL_UFO_HWC_DISPLAYQUEUE_H

#include "Layer.h"
#include "AbstractBufferManager.h"
#include <utils/Thread.h>
#include <utils/Mutex.h>
#include "PhysicalDisplay.h"

namespace intel {
namespace ufo {
namespace hwc {

// Queue of display work.
class DisplayQueue
{
public:

    enum EBehaviourFlags
    {
        eBF_SYNC_BEFORE_FLIP = (1<<0)   //< Explicit synchronisation prior to flipping the frame.
    };

    // Timeout in nsecs for retrying ready.
    static const nsecs_t mTimeoutForReady = 10000000;
    // Max time to wait for queued frame count to reduce to its limit in nsecs.
    static const nsecs_t mTimeoutForLimit = 2000000000;

    // FrameId describes indices for the frame.
    class FrameId
    {
    public:
        FrameId( ) : mTimelineIndex( 0 ), mHwcIndex( 0 ), mHwcReceivedTime( 0 ), mbValid( false ) { }
        FrameId( uint32_t timelineIndex ) :
            mTimelineIndex( timelineIndex ), mHwcIndex( 0 ), mHwcReceivedTime( 0 ), mbValid( false )
        {
        }
        FrameId( uint32_t timelineIndex, uint32_t hwcIndex, nsecs_t rxTime ) :
            mTimelineIndex( timelineIndex ), mHwcIndex( hwcIndex ), mHwcReceivedTime( rxTime ), mbValid( true )
        {
        }
        uint32_t getTimelineIndex( void ) const { return mTimelineIndex; }
        uint32_t getHwcIndex( void ) const { return mHwcIndex; }
        nsecs_t getHwcReceivedTime( void ) const { return mHwcReceivedTime; }
        bool isValid( void ) const { return mbValid; }
        String8 dump( void ) const
        {
            return mbValid ? String8::format( "frame:%u %" PRIi64 "s %03" PRIi64 "ms [timeline:%u]",
                             mHwcIndex,
                             mHwcReceivedTime/1000000000, (mHwcReceivedTime%1000000000)/1000000,
                             mTimelineIndex )
                           : String8::format( "<no valid frameId>" );
        }
        void validateFutureFrame( const FrameId& futureFrame )
        {
            HWC_UNUSED( futureFrame );
            LOG_FATAL_IF( ( int32_t( futureFrame.getHwcIndex() - getHwcIndex() ) < 0 )
                       || ( int32_t( futureFrame.getTimelineIndex() - getTimelineIndex() ) < 0 ),
                       "Future %s must not precede current %s", futureFrame.dump().string(), dump().string() );
        }

    protected:
        uint32_t mTimelineIndex;
        uint32_t mHwcIndex;
        nsecs_t  mHwcReceivedTime;
        bool     mbValid:1;
    };

    class WorkItem
    {
    public:
        enum EType
        {
            WORK_ITEM_EVENT     = 0,    //< Event.
            WORK_ITEM_FRAME     = 1     //< Frame.
        };

        // C'tor.
        WorkItem( EType eType );

        // D'tor.
        virtual ~WorkItem( );

        // Get work item type.
        EType       getWorkItemType( void ) const;

        // Get next workitem (if any).
        WorkItem*   getNext( void ) const;

        // Get last workitem (if any).
        WorkItem*   getLast( void ) const;

        // Is this workitem queued? (is it in use).
        bool        isQueued( void ) const;

        // Set effective frame Id for this workitem.
        // This is the frame that will be guaranteed released once this workitem is consumed.
        void        setEffectiveFrame( const FrameId& id );

        // Get effective frame Id for this workitem.
        // This is the frame that will be guaranteed released once this workitem is consumed.
        FrameId     getEffectiveFrame( void ) const;

        // Called just prior to dequeue.
        virtual void onDequeue( void ) { };

        // Get description of item as human-readable string.
        // This can be overridden but derived implementations should concatentate with parent string.
        virtual String8 dump( void ) const;

        // Queue a new workitem at the end of the queue.
        static void queue( WorkItem** pQueue, WorkItem* pNewWork );

        // Remove an old workitem from the queue.
        static void dequeue( WorkItem** pQueue, WorkItem* pOldWork );

    protected:
        EType       meType;             //< Type of work item.
        FrameId     mEffectiveFrame;    //< The frame reached when this workitem is consumed.
        WorkItem*   mpPrev;             //< Previous item in queue or NULL if not queued.
        WorkItem*   mpNext;             //< Next item in queue or NULL if not queued.

        WorkItem( );
    };

    // An event is a work item that will trigger a callback on the DisplayQueue::event( ) method.
    class Event : public WorkItem
    {
    public:
        Event( uint32_t id );

        // Returns ID for event.
        uint32_t getId( void ) const;

        // Get description of item as human-readable string.
        // This can be overridden but derived implementations should concatentate with parent string.
        virtual String8 dump( void ) const;

    private:
        uint32_t mId;
    };

    // Layer encapsulates a Layer plus acquire fence.
    class FrameLayer
    {
    public:
        // C'tor
        FrameLayer( );

        // D'tor
        // The layer will be reset to ensure fences and acquired buffers are released.
        ~FrameLayer( );

        // Set the layer state and acquire fence and acquire buffer.
        // Returns OK if successful.
        void set( const Layer& layer );

        // Assert layer is valid.
        void validate( void );

        // Ensure acquire fence is closed and buffer is released.
        // If the buffer isn't going to be signalled then pass bCancel true
        // and the release fence will be cancelled instead.
        void reset( bool bCancel );

        // Wait for layer (wait for buffer rendering to complete).
        void waitRendering( void );

        // Is layer ready (is buffer rendering already completed).
        bool isRenderingComplete( void );

        // Close acquire fence (if the frame is dropped).
        void closeAcquireFence( void );

        // Is the layer disabled? (no buffer).
        bool isDisabled( void ) const;

        // Get layer.
        const Layer& getLayer( ) const { return mLayer; }

        // Has this layer been set?
        bool isSet( void ) { return mbSet; }

    private:
        Layer mLayer;
        int mAcquireFence;
        sp<AbstractBufferManager::Buffer> mpAcquiredBuffer;
        bool mbSet:1;
    };

    // A frame is work item that enapsulates all state for a queueFrame( ) call.
    class Frame : NonCopyable, public WorkItem
    {
    public:
        // Constants.
        enum EFrameType
        {
            eFT_DISPLAY_QUEUE = 0,      //< Frame is a DisplayQueue frame.
            eFT_CUSTOM                  //< Frame is custom.
            // Users can define their own types >= eFT_CUSTOM.
        };

        // Encapsulate frame size, refresh, globalscaling.
        class Config
        {
        public:
            Config() :
                mWidth(0), mHeight(0), mRefresh(0)
                    { memset( &mGlobalScaling, 0, sizeof(mGlobalScaling) ); }
            Config( uint32_t w, uint32_t h, uint32_t r, const PhysicalDisplay::SGlobalScalingConfig& scaleCfg ) :
                mWidth(w), mHeight(h), mRefresh(r), mGlobalScaling(scaleCfg)
                    { }
            Config( const Content::Display& display, const PhysicalDisplay::SGlobalScalingConfig& scaleCfg ) :
                mWidth( display.getWidth() ),
                mHeight( display.getHeight() ),
                mRefresh( display.getRefresh() ),
                mGlobalScaling( scaleCfg )
            {}
            uint32_t getWidth( void ) const { return mWidth; }
            uint32_t getHeight( void ) const { return mHeight; }
            uint32_t getRefresh( void ) const { return mRefresh; }
            const PhysicalDisplay::SGlobalScalingConfig& getGlobalScaling( void) const { return mGlobalScaling; }
        protected:
            uint32_t mWidth;
            uint32_t mHeight;
            uint32_t mRefresh;
            PhysicalDisplay::SGlobalScalingConfig mGlobalScaling;
        };

        // C'tor.
        Frame( );

        // D'tor.
        // The layers will be deleted and reset to ensure fences and acquired buffers are released.
        virtual ~Frame( );

        // Set type.
        // Type is eFT_DISPLAY_CUSTOM by default.
        void setType( uint32_t type );

        // Set the frame from the set( ) parameters.
        // This will acquire buffers.
        // Returns true if successful.
        bool set( const Content::LayerStack& stack, uint32_t zorder, const FrameId& id, const Config& config );

        // Assert frame is valid.
        void validate( void );

        // Get type.
        uint32_t getType( void ) const;

        // Get layer count.
        uint32_t getLayerCount( void ) const;

        // Get specific layer.
        const FrameLayer* getLayer( uint32_t ly ) const;

        // Edit layer.
        FrameLayer* editLayer( uint32_t ly ) const;

        // Get ZOrder.
        uint32_t getZOrder( void ) const;

        // Get Hwc frame index.
        const FrameId& getFrameId( void ) const;

        // Get config.
        const Config& getConfig( void ) const { return mConfig; }

        // Wait for all layer buffers to be ready.
        void waitRendering( void ) const;

        // Returns true if all layer buffers are ready.
        bool isRenderingComplete( void ) const;

        // Lock the frame for display.
        // Once the frame is locked for display then it can not be dropped or reused.
        void lockForDisplay( void ) { mbLockedForDisplay = true; }

        // Unlock the frame for display.
        void unlockForDisplay( void ) { mbLockedForDisplay = false; }

        // Mark the frame as invalid.
        void invalidate( void ) { mbValid = false; }

        // Is the frame locked for display?
        bool isLockedForDisplay( void ) const { return mbLockedForDisplay; }

        // Is the frame still valid?
        bool isValid( void ) const { return mbValid; }

        // Reset the frame object ready for re-use.
        // This will reset all layers to close any acquire fences and release buffers.
        // If the frame isn't going to be signalled then pass bCancel true
        // and the release fences will be cancelled instead.
        void reset( bool bCancel );

        // Get description of item as human-readable string.
        // This can be overridden but derived implementations should concatentate with parent string.
        virtual String8 dump( void ) const;

    private:
        uint32_t    mType;                  //< Type (EFrameType).
        uint32_t    mLayerAllocCount;       //< Count of allocated space for layers.
        uint32_t    mLayerCount;            //< Count of layers.
        FrameLayer* maLayers;               //< Array of layers.
        uint32_t    mZOrder;                //< ZOrder.
        FrameId     mFrameId;               //< Timeline frame index.
        bool        mbLockedForDisplay:1;   //< The frame has been locked for display.
        bool        mbValid:1;              //< The frame has bee set and is still valid.
        Config      mConfig;                //< Config.
    };

    // C'tor.
    DisplayQueue( uint32_t behaviourFlags );

    // D'tor.
    virtual ~DisplayQueue( );

    // Initialise the DisplayQueue with the specified thread name.
    void init( const String8& threadName );

    // Get DisplayQueue thread name.
    String8 getName( void ) { return mName; }

    // Queue an event for execution.
    // Returns OK if successful.
    int queueEvent( Event* pEvent );

    // Queue a frame for display.
    // The display must call either queueFrame() or queueDrop() for each frame.
    // Returns OK if successful.
    int queueFrame( const Content::LayerStack& stack, uint32_t zorder, const FrameId& id, const Frame::Config& displayCfg );

    // Sometimes a display may want to drop frames. In which case, the display must guarantee that the last queued work
    // item will release both the last issued frame *and* all subsequent dropped frames in one go. For example,
    // consider frames queued to a display after a suspend event has been queued. The display must call queueDrop()
    // instead of queueFrame() for these frames so that the DisplayQueue can correctly advance its internal frame index.
    void queueDrop( const FrameId& id );

    // Drop all queued frames.
    void dropAllFrames( void );

    // Drop frames where there is at least one newer frame for which rendering is done.
    void dropRedundantFrames( void );

    // This will block until the specified frame has reached the display.
    // If frameIndex is zero, then it will block until all applied state has reached the display.
    // It will only flush work that queued before flush is called.
    // If timeoutNs is zero then this is blocking.
    // If the consumer is blocked or becomes blocked then this will return false.
    void flush( uint32_t frameIndex, nsecs_t timeoutNs );

    // This must be called when this display's consumer thread will be blocked.
    // Attempts to flush() a blocked display will invalidate queued frames instead.
    void consumerBlocked( void );

    // This must be called when this display's consumer thread is no longer blocked.
    void consumerUnblocked( void );

    // If the display is constrained in how/when work can be issued then it must
    // implement readyForNextWork( ) and only return true if the next work item can be issued.
    // The display must also call notifyReady( ) whenever ready status changes.
    void notifyReady( void );

    // Is the display available.
    // Returns true only if the display is available (consuming frames).
    // Classes deriving from FrameQueue must implement this.
    virtual bool available( void ) = 0;

    // If the display is constrained in how/when work can be issued then it must
    // implement readyForNextWork( ) and only return true if the next work item can be issued.
    // The display must also call notifyReady( ) whenever ready status changes.
    virtual bool readyForNextWork( void ) { return true; }

    // Release a frame that was previously on the display.
    // This will reset the frame to ensure acquired fences/buffers are released.
    // This should only be called for frames with type eFT_DISPLAY_QUEUE.
    // This may be overridden to handle custom frame types, but the base class method must
    // still be called for regular eFT_DISPLAY_QUEUE type frames.
    virtual void releaseFrame( Frame* pOldFrame );

    // Sync with last flip (wait for it to be displaying and all previous frames to be released).
    // Classes deriving from FrameQueue must implement this.
    virtual void syncFlip( void ) = 0;

    // Get HWC context.
    // Classes deriving from FrameQueue must implement this.
    virtual Hwc& getHwc( void ) = 0;

    // Consume work (frame or event).
    virtual void consumeWork( WorkItem* pWork ) = 0;

    // Get description of queue as human-readable string.
    String8 dump( void );

protected:

    // Worker thread.
    class Worker : public Thread
    {
    public:
        Worker( DisplayQueue& queue, const String8& threadName );
        virtual ~Worker( );

        void signalWork( void );

    protected:
        DisplayQueue& mQueue;
        bool mbRunning:1;

        Mutex mLock;
        Condition mWork;
        int32_t mSignalled;

        void start( const String8& threadName );
        void stop( void );

        virtual bool threadLoop( );

    private:
        virtual void requestExit( );
        void requestExitAndWait( );
        status_t join();
    };

    // Timeout used for wait for rendering synchronisation.
    static const uint32_t   mTimeoutWaitRenderingMsec = 3000;

    // Timeout used for queue synchronisation.
    static const uint32_t   mTimeoutSyncMsec = 3000;

    // Pool of N frames absolute maximum.
    // Older frames will be dropped if more frames are queued.
    static const int32_t    mFramePoolCount = 10;

    // If more than this number of frames are queued then a delay
    // is introduced to give the queue a chance to to drain.
    static const int32_t    mFramePoolLimit = 5;

    // Mutex for queue/consume.
    Mutex                   mLockQueue;

    // Name for this queue (and thread).
    String8                 mName;

    // Queue behaviour for this display (see EBehaviourFlags).
    uint32_t                mBehaviourFlags;

    // Worker thread for display updates.
    sp<Worker>              mpWorker;

    // Pool of display frames.
    Frame                   maFrames[ mFramePoolCount ];

    // Display work queue.
    // This is a pointer to work items to process in sequence.
    // mpWorkQueue is the current work item or NULL at start of day.
    // mpWorkQueue->mpNext is the next work item to consume.
    // mpWorkQueue->mpPrev is end of queue (most recently queued work item).
    WorkItem*               mpWorkQueue;

    // Count of work items queued in set( ) but yet to be consumed.
    int32_t                 mQueuedWork;

    // Count of frames queued in set( ) but yet to be consumed.
    int32_t                 mQueuedFrames;

    // Count of frames currently locked for display.
    int32_t                 mFramesLockedForDisplay;

    // Count of frames in use from the pool.
    // This includes both queued frames and frames that are consumed (flipped) but not yet released.
    int32_t                 mFramePoolUsed;

    // Peak count of frames used.
    int32_t                 mFramePoolPeak;

    // Condition used to signal that queued work has been consumed.
    Condition               mConditionWorkConsumed;

    // Condition used to signal that a presented frame has been released.
    Condition               mConditionFrameReleased;

    // Frame index for most recently queued frame.
    FrameId                 mLastQueuedFrame;

    // Frame index for most recently issued frame.
    FrameId                 mLastIssuedFrame;

    // Frame index for most recently dropped frame.
    FrameId                 mLastDroppedFrame;

    // Count of consumed work.
    uint32_t                mConsumedWork;

    // Count of consumed frames since the last init().
    uint32_t                mConsumedFramesSinceInit;

    // The consumer can be locked (see consumerBlocked).
    bool                    mbConsumerBlocked:1;

    // Queue work item.
    void doQueueWork( WorkItem* pWork );

    // This will block until the specified frame has reached the display.
    // If frameIndex is zero, then it will block until all applied state has reached the display.
    // It will only flush work that queued before flush is called.
    // If timeoutNs is zero then this is blocking.
    // If the consumer is blocked or becomes blocked then this will return false.
    bool doFlush( uint32_t frameIndex, nsecs_t timeoutNs );

    // Tag all queued frames as 'invalid'.
    void doInvalidateFrames( void );

    // Release a frame that was previously on the display.
    void doReleaseFrame( Frame* pFrame );

    // If used frame count exceeds mFramePoolLimit then wait
    // for up to mTimeoutForLimit nsecs for this to reduce.
    // Queue mutex must be held on entry.
    void limitUsedFrames( void );

    // Find unqueued frame or oldest queued frame that has not been consumed yet.
    // Queue mutex must be held on entry.
    Frame* findFree( void );

    // Drop frame from queue.
    // Queue mutex must be held on entry.
    void dropFrame( Frame* pFrame );

    // Drop frames where there is at least one newer frame for which rendering is done.
    void doDropRedundantFrames( void );

    // Returns number of queued work items.
    uint32_t getQueuedWork( void ) { return mQueuedWork; }

    // Consume the next work item.
    // Returns true if a work item is consumed.
    bool consumeWork( void );

    // Advance the last issued frame to this frame.
    void doAdvanceIssuedFrame( const FrameId& id );

    // Consume the next work item.
    // Queue mutex must be held on entry.
    // Returns true if a work item is consumed.
    bool doConsumeWork( void );

    // Consume a user item.
    // Queue mutex must be held on entry.
    void doConsumeEvent( void );

    // Consume a frame.
    // Queue mutex must be held on entry.
    void doConsumeFrame( void );

    // Lock a frame for display.
    void lockFrameForDisplay( Frame* pFrame ) { ++mFramesLockedForDisplay; pFrame->lockForDisplay( ); }

    // Unlock a frame for display.
    void unlockFrameForDisplay( Frame* pFrame ) { --mFramesLockedForDisplay; pFrame->unlockForDisplay( ); }

    // Assert queue sanity.
#if INTEL_HWC_INTERNAL_BUILD
    void doValidateQueue( void );
#else
    inline void doValidateQueue( void ) { };
#endif

    // Start worker.
    void startWorker( void );

    // Stop worker.
    void stopWorker( void );

    // Get worker Tid.
    // Returns 0 if not running.
    pid_t getWorkerTid( void );
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DISPLAYQUEUE_H
