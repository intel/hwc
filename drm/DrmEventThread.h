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

#ifndef INTEL_UFO_HWC_DRMEVENTTHREAD_H
#define INTEL_UFO_HWC_DRMEVENTTHREAD_H

#include "Hwc.h"
#include <utils/Thread.h>

#include <xf86drm.h>

namespace intel {
namespace ufo {
namespace hwc {

class DrmDisplay;
class Drm;

//*****************************************************************************
//
// DrmEventThread class - responsible for handling page flip events
//
//*****************************************************************************

class DrmEventThread : public Thread
{
    drmEventContext mEvctx;
    int             mDrmFd;

    virtual void onFirstRef();
    virtual bool threadLoop();

public:
    DrmEventThread();
    virtual ~DrmEventThread() {}

    // Enable vsync generation for the specified display.
    bool enableVSync(DrmDisplay* pDisp);

    // Disable vsync generation for the specified display.
    // Pass bWait = true to ensure vsyncs are quiescent before returning.
    bool disableVSync(DrmDisplay* pDisp, bool bWait);

    // Create a handle from a zero based 16bit index.
    inline static int32_t encodeIndex( uint16_t idx ) { return (0xABCD0000|idx); }

    // Create a zero-based 16bit index from a handle previously created with encodeIndex.
    // Returns -1 if not a valid handle.
    inline static int32_t decodeIndex( uint32_t handle ) { return (handle>>16)==0xABCD ? handle&0xFFFF : -1; }
private:


    static void vblank_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data);
    static void page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data);

    // Handler for display.
    class VSyncHandler
    {
    public:
        VSyncHandler( );
        // Set the vsync index and display for which we are handling vsync events.
        void setDisplay( uint16_t index, DrmDisplay* pDisp );
        // Set the DRM vblank event flags for this handler.
        void setFlags( uint32_t flags );
        // Enable vsync events.
        bool enable( void );
        // Disable vsync events.
        // Pass bWait = true to ensure vsyncs are quiescent before returning.
        bool disable( bool bWait );
        // Handler callback.
        void event(unsigned int frame, unsigned int sec, unsigned int usec);
    protected:
        enum EMode
        {
            eModeStopped = 0,
            eModeRunning,
            eModeStopping
        };
        mutable Mutex           mLockData;
        Condition               mConditionStopped;
        int                     mDrmFd;
        DrmDisplay*             mpDisplay;
        int32_t                 mIndex;
        uint32_t                mFlags;
        uint32_t                mVblankEventInflight;
        uint32_t                meMode;
    };

    enum
    {
        PRIMARY_VSYNC_HANDLER   = 0,
        SECONDARY_VSYNC_HANDLER = 1,
        MAX_VSYNC_HANDLERS      = 2
    };

    static VSyncHandler mVSyncHandler[ MAX_VSYNC_HANDLERS ];
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DRMEVENTTHREAD_H
