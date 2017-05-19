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


#ifndef INTEL_UFO_HWC_DRMPAGEFLIPHANDLER_H
#define INTEL_UFO_HWC_DRMPAGEFLIPHANDLER_H

#include "Common.h"
#include "Drm.h"
#include "DisplayQueue.h"
#include "Timeline.h"
#include "Option.h"

namespace intel {
namespace ufo {
namespace hwc {

class Drm;
class DrmDisplay;

// Drm base display flip handler class.
class DrmPageFlipHandler : NonCopyable
{
public:

    class AbstractImpl : NonCopyable
    {
    public:
        AbstractImpl() { };
        virtual ~AbstractImpl() { };
        // Flip the next frame to the display.
        // Returns true if the flip event request is successfully issued.
        virtual bool doFlip( DisplayQueue::Frame* pNewFrame, bool bMainBlanked, uint32_t pFlipEvData ) = 0;
    };


public:

    // Timeout in nsecs for flip completion.
    static const nsecs_t mTimeoutFlip = 1000000000;

    DrmPageFlipHandler( DrmDisplay& display );
    virtual ~DrmPageFlipHandler( );

    // Startup display.
    // This is a one-time startup used to initialise
    // state that is persistent while the display is started (connected).
    // e.g. It will initialise the timeline.
    void startupDisplay( void );

    // Initialise the page flip handler.
    // init/uninit are used across mode changes.
    void init( void );

    // Uninitialise the page flip handler.
    // init/uninit are used across mode changes.
    void uninit( void );

    // Creates a retire fence for the next future frame.
    // pTimelineIndex must be provided.
    // Returns the new fence if successful, in which case pTimelineIndex will be updated with the fence timeline index.
    // Returns -1 if not successful, in which case pTimelineIndex will be reset to 0.
    Timeline::NativeFence registerNextFutureFrame( uint32_t* pTimelineIndex );

    // Creates a retire fence for a repeated future frame (same timeline index as the previous frame).
    // pTimelineIndex must be provided.
    // Returns the new fence if successful, in which case pTimelineIndex will be updated with the fence timeline index.
    // Returns -1 if not successful, in which case pTimelineIndex will be reset to 0.
    Timeline::NativeFence registerRepeatFutureFrame( uint32_t* pTimelineIndex );

    // Advance the timeline up to and including the specified timeline index.
    // All frames created with indices up to and including timelineIndex will be released.
    void releaseTo( uint32_t timelineIndex );

    // The next frame can only be flipped once the previous flip work has been completed.
    bool readyForFlip( void );

    // Flip the next frame to the display.
    // Returns true if the frame is flipped.
    // If the frame is not flipped then the caller must manage its release.
    bool flip( DisplayQueue::Frame* pNewFrame );

    // Retire the next frame (instead of flipping it).
    // This will advance timeline to release all work up to and including this frame.
    void retire( DisplayQueue::Frame* pNewFrame );

    // Waits for most recent flip to complete.
    void sync( void );

    // Callback for DrmEventHandler to complete the previous page flip.
    void pageFlipEvent( void );

protected:

    // Check if there is outstanding flip work.
    bool isOutstandingFlipWork( void ) { return ( mpLastFlippedFrame != NULL ); }

    // Retire the next frame (instead of flipping it).
    // This will advance timeline to release all work up to and including this frame.
    void doRetire( DisplayQueue::Frame* pNewFrame );

    // Waits for last flip to complete - force completion if necessary.
    void doSync( void );

    // Wait for last flip to complete.
    bool waitForFlipCompletion( void );

    // Retire previous frames for a new frame on the display.
    void retirePreviousFrames( DisplayQueue::Frame* pNewFrame );

    // Complete last flip.
    void completeFlip( void );

    // Get status string.
    String8 getStatusString( void );

protected:

    // Mutex used to synchronise Drm/Flip/Timeline state updates with PageFlip events.
    Mutex                   mLockPageFlip;

    // Drm instance.
    Drm&                    mDrm;

    // Owner DrmDisplay.
    DrmDisplay&             mDisplay;

    // Implementation.
    AbstractImpl*           mpImpl;

    // Is the page flip handler initialised?
    bool                    mbInit;

    // Plane count.
    uint32_t                mNumPlanes;

    // Index for the main plane.
    // -1 if not found.
    int32_t                 mMainPlaneIndex;

    // Time of last succesfully issued flip (is reset once flip has completed).
    nsecs_t                 mLastPageFlipTime;

    // Condition used to signal that a page flip has completed.
    Condition               mConditionPageFlipComplete;

    // Timeline for this display.
    Timeline                mTimeline;

    // Most recently flipped frame (may not have reached display yet).
    DisplayQueue::Frame*    mpLastFlippedFrame;

    // Frame currently on display.
    DisplayQueue::Frame*    mpCurrentFrame;

    // Timeout used for flip synchronisation.
    static const uint32_t   mTimeoutSyncMsec = 3000;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DRMPAGEFLIPHANDLER_H
