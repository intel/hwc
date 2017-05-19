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

/**
 * \remark This file is Android specific
 * \warning Do not include HW dependent code
 * \warning Do not include DDI layer dependencies
 */

#ifndef INTEL_UFO_HWC_HWC_H
#define INTEL_UFO_HWC_HWC_H

#include "Common.h"
#include "InputAnalyzer.h"
#include "FilterManager.h"
#include "CompositionManager.h"
#include "PhysicalDisplayManager.h"
#include "LogicalDisplayManager.h"
#include "GlobalScalingFilter.h"
#include "LogicalDisplay.h"

#include <utils/Vector.h>
#include <binder/IInterface.h>
#include <utils/Mutex.h>
#include <utils/Condition.h>
#include "SurfaceFlingerProcs.h"
#include "Timeline.h"

namespace intel {
namespace ufo {
namespace hwc {

class HwcService;
class AbstractBufferManager;
class Hwc;

class Hwc : NonCopyable, public hwc_composer_device_1_t, public LogicalDisplayNotificationReceiver
{
public:
    // Enums for the hotplug callback
    enum {
        HWC_HOTPLUG_DISCONNECTED = 0,
        HWC_HOTPLUG_CONNECTED = 1,
    };

    // Enums (flags) for logviewer option.
    enum {
        HWC_LOGVIEWER_TO_LOGCAT = (1<<0),
        // Other options to follow if required.
    };

private:
    Hwc(const hw_module_t* module);
    /*virtual*/ ~Hwc();

    // Create the virtual display.
    void createAndRegisterVirtualDisplay( void );

    void open( void );

private:
    int onPrepare(size_t numDisplays, hwc_display_contents_1_t** displays);
    int onSet(size_t numDisplays, hwc_display_contents_1_t** displays);
    int onEventControl(int disp, int event, int enabled);
    int onQuery(int what, int* value);
    void onRegisterProcs(hwc_procs_t const* procs);
    void onDump(char *pBuffer, uint32_t* pBufferLength);
    int onGetDisplayAttribute(int disp, uint32_t config, AbstractDisplay::EAttribute attribute, int32_t* paValue);
    int onGetDisplayConfigs(int disp, uint32_t* paOutConfigHandles, uint32_t* pOutNumConfigs);
    int onGetActiveConfig(int disp);
    int onSetActiveConfig(int disp, int configIndex);
    int onSetCursorPositionAsync(int disp, int x_pos, int y_pos);

public:
    // Returns OK (0) if successful, negative on error.
    int     onBlank(int disp, int blank, AbstractDisplayManager::BlankSource source);

    // Function to get a SurfaceFlinger plugged display object.
    LogicalDisplay* getSurfaceFlingerDisplay( uint32_t sfIndex ) { return mLogicalDisplayManager.getSurfaceFlingerDisplay( sfIndex ); }

    // Set the virtual display.
    void setVirtualDisplay( AbstractPhysicalDisplay* pDisplay ) { mpVirtualDisplay = pDisplay; }

    // Get the virtual display.
    AbstractPhysicalDisplay* getVirtualDisplay( void ) { return mpVirtualDisplay; }

    // Get physical display manager.
    PhysicalDisplayManager& getPhysicalDisplayManager( void ) { return mPhysicalDisplayManager; }

    // Get count of physical displays.
    uint32_t    getPhysicalDisplays( void ) { return mPhysicalDisplayManager.getNumPhysicalDisplays(); }

    // Function to get a physical display object
    AbstractPhysicalDisplay* getPhysicalDisplay( uint32_t phyIndex ) { return mPhysicalDisplayManager.getPhysicalDisplay( phyIndex ); }

    // function to get GlobalScaling Filter
    GlobalScalingFilter& getGlobalScalingFilter() { return mGlobalScalingFilter; }

    // Force a geometry change on the next onPrepare.
    void forceGeometryChange() { mInputAnalyzer.forceGeometryChange(); }

    // Force a geometry change on the next onPrepare and redraw.
    void forceGeometryChangeAndRedraw() { mInputAnalyzer.forceGeometryChange(); forceRedraw(); }

    // Force a Surfaceflinger update (also see forceGeometryChange).
    void forceRedraw( void ) { if ( !isOpen() ) { ALOGD_IF( sbInternalBuild, "Skipped early invalidate" ); return; } mSF.refresh(); }

    // Returns the number of frames drawn.
    uint32_t getRedrawFrames( void ) const { Mutex::Autolock _l( mEndOfFrameMutex ); return mRedrawFrames; }

    // Returns true only once SF/HWC is fully open.
    bool isOpen( void ) const { return mbOpen; }

    // Flush all post probe plugs.
    void flushInitialPlugs( void );

    // Flush plug changes to SurfaceFlinger.
    // Returns true if any significant change occurs.
    bool flushPlugChanges( void );

    // This will force a fresh redraw and wait for it to complete.
    // It flushes up to the issued frame on all displays before returning to caller.
    // Any trailing plug changes will also have been fully processed.
    // If timeoutNs is 0 this is blocking.
    void synchronize( nsecs_t timeoutNs = 5000000000 );

    // This will wait until HWC fully completes end-of-frame processing.
    // NOTE: The frame may still not have been delivered to the display.
    // If timeoutNs is 0 this is blocking.
    void synchronizeFrameEnd( uint32_t frameIndex, nsecs_t timeoutNs );

    // This will wait until plug changes have been forwarded to SurfaceFlinger.
    // Returns number of changes processed.
    // If timeoutNs is 0 this is blocking (no timeout).
    uint32_t synchronizePlugChanges( nsecs_t timeoutNs );

    // Notify plug change has completed, so that make sure plug event can be
    // fully serialized and synchronized.
    void notifyPlugChangeCompleted( );

    // Block until SF is up and running (is fully open and is not blanked/powered-off).
    void waitForSurfaceFlingerReady( void );

    // Post VSync event to SF.
    void postVSyncEvent( uint32_t sfIndex, nsecs_t time );

    // Post hot plug event to SF.
    void postHotPlug( uint32_t sfIndex );

    // Post hot unplug event to SF.
    void postHotUnplug( uint32_t sfIndex );

    // Implements LogicalDisplayNotificationReceiver API.
    // NOTE: Hwc processes plug/unplug/size changes at the end of the currrent/next frame.
    void notifyDisplayAvailable( LogicalDisplay* pDisplay, uint32_t sfIndex );
    void notifyDisplayUnavailable( LogicalDisplay* pDisplay );
    void notifyDisplayChangeSize( LogicalDisplay* pDisplay );
    void notifyDisplayVSync( LogicalDisplay* pDisplay, nsecs_t timeStampNs );

    // Returns the plug change sequence index.
    uint32_t getPlugChangeSequence( void ) { Mutex::Autolock _l( mDisplayPlugChangesLock ); return mDisplayPlugChangeSequence; }

    // Returns the number of pending plug changes (waiting to be flushed at end of next frame).
    uint32_t getPendingPlugChanges( void ) { Mutex::Autolock _l( mDisplayPlugChangesLock ); return mDisplayPlugChanges.size(); }

    // Verify display proxy state, if no primary display, need create display proxy.
    void verifyDisplayProxy();

    // Normally the primary provides its own vsync and retire fences.
    // However, it is possible to override this so that these events can be issued from
    // another display (e.g. for extended mode)
    void setPrimaryDisplaySyncs( uint32_t sfIndex );

public:
    // hook for hw_module_t
    static int hook_open(const hw_module_t* module, const char* name, hw_device_t** hw_device);

private:
    // hooks required by hw_device_t and hwc_composer_device_1
    static int hook_close(struct hw_device_t* device);
    static int hook_prepare(struct hwc_composer_device_1 *dev, size_t numDisplays, hwc_display_contents_1_t** displays);
    static int hook_set(struct hwc_composer_device_1 *dev, size_t numDisplays, hwc_display_contents_1_t** displays);
    static int hook_eventControl(struct hwc_composer_device_1* dev, int disp, int event, int enabled);
    static int hook_setPowerMode(struct hwc_composer_device_1* dev, int disp, int blank);
    static int hook_blank(struct hwc_composer_device_1* dev, int disp, int blank);
    static int hook_query(struct hwc_composer_device_1* dev, int what, int* value);
    static void hook_registerProcs(struct hwc_composer_device_1* dev, hwc_procs_t const* procs);
    static void hook_dump(struct hwc_composer_device_1* dev, char *buff, int buff_len);
    static int hook_getDisplayConfigs(struct hwc_composer_device_1* dev, int disp, uint32_t* paOutConfigHandles, size_t* pOutNumConfigs);
    static int hook_getDisplayAttributes(struct hwc_composer_device_1* dev, int disp, uint32_t configHandle, const uint32_t* paAttributes, int32_t* paOutValues);
    static int hook_getActiveConfig(struct hwc_composer_device_1* dev, int disp);
    static int hook_setActiveConfig(struct hwc_composer_device_1* dev, int disp, int configIndex);
    static int hook_setCursorPositionAsync(struct hwc_composer_device_1 *dev, int disp, int x_pos, int y_pos);
    static Hwc* getComposer(hwc_composer_device_1 *dev)
    {
        ALOG_ASSERT(dev);
        // Our device is both a hwc_composer_device_1_t and a Hwc (a static_cast can resolve this).
        return static_cast<Hwc*>(dev);
    }
    static Hwc* getComposer(hw_device_t *dev)
    {
        return getComposer( (hwc_composer_device_1*)dev );
    }

private:

    // DisplayPlugChange is used to sequence plug/unplug processing:
    // See mDisplayPlugChanges.
    class DisplayPlugChange
    {
        public:
            enum
            {
                DPC_FLAG_PLUG           = (1<<0),   //< Unplug
                DPC_FLAG_UNPLUG         = (1<<1),   //< Plug
                DPC_FLAG_TRANSITORY     = (1<<2)    //< Transitory; will be reverting shortly (e.g. for mode change).
            };
            DisplayPlugChange( ) : mpDisplay( NULL ), mSfIndex( 0 ), mFlags( 0 ) { }
            DisplayPlugChange( LogicalDisplay* pDisplay, uint32_t sfIndex, uint32_t flags ) :
                mpDisplay( pDisplay ), mSfIndex( sfIndex ), mFlags( flags ) { }
            LogicalDisplay* mpDisplay;
            uint32_t mSfIndex;
            uint32_t mFlags;
    };

    InputAnalyzer               mInputAnalyzer;             // Class that analyzes HWC input content and produces an initial Content class
    FilterManager&              mFilterManager;             // Manager for the filter subsystem that adjusts the content
    CompositionManager&         mCompositionManager;        // Manager for all the GPU based composition engines
    PhysicalDisplayManager      mPhysicalDisplayManager;    // Manager for physical displays
    LogicalDisplayManager       mLogicalDisplayManager;     // Manager for logical displays (if used).
    AbstractBufferManager&      mAbstractBufferManager;     // Manager for the allocation of buffers
    AbstractPhysicalDisplay*    mpVirtualDisplay;           // The Hwc created and owned virtual/widi display.

    const Content*              mpFinalContent;             // Content of the final content to be composed

    SurfaceFlingerProcs         mSF;
    bool                        mbOpen;                     // Hwc is now open.
    uint32_t                    mRedrawFrames;              // Incrementing count of redraws (prepare/set).

    Vector<DisplayPlugChange>   mDisplayPlugChanges;        // List of SF display plug changes.
    Mutex                       mDisplayPlugChangesLock;    // Lock for SF display plug changes list.
    uint32_t                    mDisplayPlugChangeSequence; // Sequence (count) of issued SF display plugs.

    GlobalScalingFilter         mGlobalScalingFilter;       // Global scaling filter.

    // The index of the display currently providing sync/timeline to SF for the primary
    uint32_t mPrimaryDisplaySync;
    bool mbVsyncIsEnabled;
    Mutex mVsyncLock;

    // Used by endOfFrame() and synchronizeFrame() to ensure a frame has been fully processed.
    // NOTE: mRedrawFrames will incremented first - before the signal is broadcast.
    mutable Mutex           mEndOfFrameMutex;
    Condition               mConditionEndOfFrame;

    // End of frame (increment count of redraws and broadcast it).
    void onEndOfFrame( void );

    Mutex                   mBlankLock;             // Lock for onBlank entrypoint (to ensure no reentry) and blank mask state.
    uint32_t                mSFBlankMask;           // Set of displays blanked by SF (BIT0=>SF0).
    Condition               mSFBlankMaskCondition;  // Signalled for any potential change to mSFBlankMask.


    bool                    mbDisplayManagerEnabled:1;  // Is LogicalDisplayManager used?

private:
    String8 mPendingDump;                               // A dump string to be returned on the next dump call

};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_HWC_H
