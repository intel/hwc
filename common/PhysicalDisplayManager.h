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

#ifndef INTEL_UFO_HWC_PHYSICAL_DISPLAY_MANAGER_H
#define INTEL_UFO_HWC_PHYSICAL_DISPLAY_MANAGER_H

#include "AbstractDisplayManager.h"
#include "AbstractPhysicalDisplay.h"
#include "PlaneComposition.h"
#include "Timer.h"
#include "Option.h"
#include <utils/BitSet.h>

namespace intel {
namespace ufo {
namespace hwc {

class Hwc;
class Content;
class DisplayCaps;
class CompositionManager;
class InputAnalyzer;

class PhysicalDisplayManager : public AbstractDisplayManager
{
public:
    PhysicalDisplayManager( Hwc& hwc, CompositionManager& compositionManager );
    virtual ~PhysicalDisplayManager();

    // Register a display.
    // Returns an index or INVALID_DISPLAY_ID if no space.
    uint32_t            registerDisplay(AbstractPhysicalDisplay* pDisplay);

    // Unregister a display.
    void                unregisterDisplay( AbstractPhysicalDisplay* pDisplay );

    // Get count of physical displays.
    uint32_t            getNumPhysicalDisplays( void ) { return mPhysicalDisplays; }

    // Get physical display.
    AbstractPhysicalDisplay* getPhysicalDisplay( uint32_t phyIndex );

    // Called each frame onPrepare().
    int                 onPrepare(const Content& ref);

    // Called each frame onSet().
    int                 onSet(const Content& ref);

    // Set a remapping.
    // An upstream logical display manager can use this to remap its indices into physical
    //  indices for display content that is just passthrough.
    void                setRemap( uint32_t displayIndex, uint32_t physicalIndex );

    // Reset remapping (no remapping).
    void                resetRemap( void );

    // Get remapped display index
    uint32_t            remap( uint32_t displayIndex );

    // Set display contents in SF display order.
    void                setSFDisplayOrder( bool bSFOrder ) { mbSFDisplayOrder = bSFOrder; }

    // Are display contents in SF display order.
    bool                getSFDisplayOrder( void ) const { return mbSFDisplayOrder; }

    // Enable or disable vsyncs for a physical display.
    void                vSyncEnable( uint32_t phyIndex, bool bEnableVSync );

    // Modify the banking state for a physical display.
    // Returns OK (0) if the requested blanking state is applied on return, negative on error.
    // This will block for change to complete before returning for BLANK_SURFACEFLINGER source.
    int                 blank( uint32_t phyIndex, bool bEnableBlank, BlankSource source );

    // *************************************************************************
    // This class implements the AbstractDisplayManager API.
    // *************************************************************************
    void                open( void );
    void                onVSyncEnable( uint32_t sfIndex, bool bEnableVSync );
    int                 onBlank( uint32_t sfIndex, bool bEnableBlank, BlankSource source );
    void                flush( uint32_t frameIndex = 0, nsecs_t timeoutNs = AbstractDisplay::mTimeoutForFlush );
    void                endOfFrame( void );
    String8             dump( void );
    String8             dumpDetail( void );

    // Physical displays should call notifyPhysical*** to notify physical display changes.
    // Notifications will be forwarded to the set receiver (Hwc or logical display manager).
    void                setNotificationReceiver( PhysicalDisplayNotificationReceiver* pNotificationReceiver );
    void                notifyPhysicalAvailable( AbstractPhysicalDisplay* pPhysical);
    void                notifyPhysicalUnavailable( AbstractPhysicalDisplay* pPhysical );
    void                notifyPhysicalChangeSize( AbstractPhysicalDisplay* pPhysical );
    void                notifyPhysicalVSync( AbstractPhysicalDisplay* pPhysical, nsecs_t timeStampNs );

    // Notify plug change has completed, so that make sure plug event can be
    // fully serialized and synchronized.
    void                notifyPlugChangeCompleted( void );


private:
    Hwc&                                    mHwc;
    CompositionManager&                     mCompositionManager;
    PhysicalDisplayNotificationReceiver*    mpDisplayNotificationReceiver;
    bool                                    mbSFDisplayOrder:1;

    // This class describes the current state of a physical display
    class DisplayState
    {
    public:

        DisplayState();
        ~DisplayState();

        void setIndex( uint32_t index )
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            mIndex= index;
        }

        bool isAttached(void) const
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            return mpHwDisplay != NULL;
        }

        uint32_t getBlank( void ) const
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            return mBlankMask;
        }

        bool isBlanked( void ) const
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            return ( mBlankMask != 0 );
        }

        Content::Display& getContent( void )
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            return mContent;
        }

        AbstractPhysicalDisplay* getHwDisplay( void ) const
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            return mpHwDisplay;
        }

        PlaneComposition& getPlaneComposition( void )
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            return mPlaneComposition;
        }

        void setHwDisplay( AbstractPhysicalDisplay* pDisp );

        void onVSyncEnable( bool bEnableVSync );

        int onBlank(bool bEnableBlank, BlankSource source, bool& bChange);

        void setFrameIndex( uint32_t frameIndex )
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            mFrameIndex = frameIndex;
            mbValid = true;
        }

        void setFrameReceivedTime( nsecs_t rxTime )
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            mFrameReceivedTime = rxTime;
        }

        bool validateFrame( uint32_t frameIndex )
        {
            INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
            Mutex::Autolock _l( mLock );
            return mbValid && ( mFrameIndex = frameIndex);
        }

    private:

        // Lock on DisplayState.
        mutable Mutex mLock;

        // Index for this state.
        uint32_t mIndex;

        // Pointer to the physical display that this state is going to be applied to
        AbstractPhysicalDisplay* mpHwDisplay;

        // This structure holds the layer state for this hardware display
        Content::Display mContent;

        // Composition thats currently in use
        PlaneComposition mPlaneComposition;

        // Indication of which external components have blanked the display
        uint32_t mBlankMask;

        // Frame index (set through setFrameIndex in prepare).
        uint32_t mFrameIndex;

        // Frame received time (set through setFrameReceivedTime in prepare).
        nsecs_t mFrameReceivedTime;

        // Has prepare (and frame index) been set yet?
        bool mbValid:1;

        // Are VSyncs currently enabled for this display hardware.
        bool mbVSyncEnabled:1;
    };


    DisplayState                mDisplayState[ cMaxSupportedPhysicalDisplays ];         //< Display state.

    AbstractPhysicalDisplay*    mpPhysicalDisplay[ cMaxSupportedPhysicalDisplays ];     //< Pool of physical displays.
    uint32_t                    mPhysicalDisplays;                                      //< Count of physical displays.

    uint32_t                    mPhyIndexRemap[ cMaxSupportedLogicalDisplays ];         //< Used to remap indices into physical index.
    bool                        mbRemapIndices:1;                                       //< Use remap?

    class IdleTimeout {
    public:
        IdleTimeout(Hwc& hwc);

        bool shouldReAnalyse();
        void setCanOptimize(unsigned display, bool can);
        bool frameIsIdle() const { return  mOptionIdleTimeout && ( mFramesToExitIdle > 1 ); }
        void nextFrame() { resetIdleTimer(); }
    private:
        void idleTimeoutHandler();
        void resetIdleTimer();
        // The value '1' denotes that the current frame is transitioning to active.
        bool frameComingOutOfIdle() const { return mFramesToExitIdle == 1; }

        Hwc&        mHwc;            // Needed to request a re-draw.

        // Milliseconds before switching to idle mode. 0 disables idle entirely.
        Option mOptionIdleTimeout;
        // Milliseconds for the display to remain idle in order to maintain idle mode.
        Option mOptionIdleTimein;

        TimerMFn<IdleTimeout, &IdleTimeout::idleTimeoutHandler>  mIdleTimer;
        uint32_t    mFramesToExitIdle;
        BitSet32    mDisplaysCanOptimise;
        bool        mbForceReAnalyse;
    };

    IdleTimeout mIdleTimeout;

    Option      mEnablePlaneAllocator;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_PHYSICAL_DISPLAY_MANAGER_H
