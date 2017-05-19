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

#include "Hwc.h"
#include "HwcService.h"
#include "PhysicalDisplay.h"
#include "Log.h"
#include "Utils.h"
#include "VirtualDisplay.h"
#include "AbstractPlatform.h"
#include "AbstractBufferManager.h"
#include "OptionManager.h"

namespace intel {
namespace ufo {
namespace hwc {

Hwc::Hwc(const hw_module_t* module) :
      mFilterManager(FilterManager::getInstance()),
      mCompositionManager(CompositionManager::getInstance()),
      mPhysicalDisplayManager(*this, mCompositionManager),
      mLogicalDisplayManager(*this, mPhysicalDisplayManager),
      mAbstractBufferManager(AbstractBufferManager::get()),
      mpVirtualDisplay(NULL),
      mbOpen(false),
      mRedrawFrames(0),
      mDisplayPlugChangeSequence(0),
      mGlobalScalingFilter(mPhysicalDisplayManager),
      mPrimaryDisplaySync(HWC_DISPLAY_PRIMARY),
      mbVsyncIsEnabled(false),
      mSFBlankMask(0)
{
    ALOG_ASSERT(module);

    // Our device is both a hwc_composer_device_1_t and a Hwc (a static_cast can resolve this).
    hwc_composer_device_1_t* hwc_composer_device_ptr = static_cast<hwc_composer_device_1_t*>(this);
    memset(hwc_composer_device_ptr,0,sizeof(hwc_composer_device_1_t));

    // Set interface functions
    common.tag              = HARDWARE_DEVICE_TAG;
    common.module           = const_cast<hw_module_t*>(module);
    common.close            = hook_close;

    prepare                 = hook_prepare;
    set                     = hook_set;
    eventControl            = hook_eventControl;
    query                   = hook_query;
    registerProcs           = hook_registerProcs;
    dump                    = hook_dump;
    getDisplayConfigs       = hook_getDisplayConfigs;
    getDisplayAttributes    = hook_getDisplayAttributes;

#if defined(HWC_DEVICE_API_VERSION_1_4) || defined (HWC_DEVICE_API_VERSION_1_5)
    #if defined(HWC_DEVICE_API_VERSION_1_5)
        common.version          = HWC_DEVICE_API_VERSION_1_5;
    #else
        common.version          = HWC_DEVICE_API_VERSION_1_4;
    #endif
    setPowerMode            = hook_setPowerMode;
    getActiveConfig         = hook_getActiveConfig;
    setActiveConfig         = hook_setActiveConfig;
    setCursorPositionAsync  = hook_setCursorPositionAsync;
#else
    common.version          = HWC_DEVICE_API_VERSION_1_3;
    blank                   = hook_blank;
#endif

    // A LogicalDisplayManager sits between the PhysicalDisplayManager and the Hwc.
    // PhysicalDisplayManager must send its notifications to the LogicalDisplayManager
    //  so it can marshall display availability.
    mPhysicalDisplayManager.setNotificationReceiver( &mLogicalDisplayManager );

    // add GlobalScalingFilter to FilterManager
    mFilterManager.add(mGlobalScalingFilter, FilterPosition::GlobalScaling);

    // after all internal objects are created,
    // Start the hwc service
    HwcService& hwcService = HwcService::getInstance();
    hwcService.start(*this);

    // Make sure the option manager is initialised (for forceGeometryChange updates)
    OptionManager::getInstance().initialize(*this);

#if INTEL_HWC_LOGVIEWER_BUILD
    // Enable logview to logcat.
    Option optionLogviewer( "logviewer", 0, false );
    if ( optionLogviewer.get() & HWC_LOGVIEWER_TO_LOGCAT )
    {
        ALOGI( "Enabling hwclogviewer to logcat" );
        Log::enableLogviewToLogcat();
        Log::enable();
    }
#endif

    // Dump version at startup.
    Log::alogi( hwcService.getHwcVersion().string() );
}

void Hwc::createAndRegisterVirtualDisplay( void )
{
    if (!mpVirtualDisplay)
        mpVirtualDisplay = new VirtualDisplay(*this);

    // Register the virtual display.
    if ( mpVirtualDisplay )
    {
        if ( mPhysicalDisplayManager.registerDisplay( mpVirtualDisplay ) != INVALID_DISPLAY_ID )
        {
            // It is available immediately.
            mPhysicalDisplayManager.notifyPhysicalAvailable( mpVirtualDisplay );
        }
    }
}

void Hwc::open( void )
{
    // Add in the virtual display.
    createAndRegisterVirtualDisplay();

    // The display manager must complete plug of its initial displays at this point.
    mLogicalDisplayManager.open( );

    // Flush redundant start-of-day display plugs.
    flushInitialPlugs( );

    // Notify FilterManager once displays are ready but before first frame(s).
    // This provides each filter with the context (Hwc) if it is required and
    // also gives the filter opportunity to run one-time initialization.
    mFilterManager.onOpen( *this );

    // Dump initial bindings.
    Log::alogd( HWC_DEBUG, "Initial bindings:\n%s", mLogicalDisplayManager.dumpDetail().string() );
}

int Hwc::hook_open(const hw_module_t* module, const char* name, hw_device_t** device)
{
    LOG_ALWAYS_FATAL_IF (!module || !name || !device, "Bad module, name or pointer");

    if (strcmp(name, HWC_HARDWARE_COMPOSER) == 0)
    {
        // Create Hwc driver module.
        Hwc* pHwc = new Hwc(module);
        if (!pHwc) {
            ALOGE("Failed to create HWComposer object");
            return NO_MEMORY;

        }

        // Open platform.
        status_t err = AbstractPlatform::get().open(pHwc);

        if (err != OK)
        {
            ALOGE("Failed to initialize Platform, %s", strerror(err));
        }

        // Complete Hwc driver module open.
        pHwc->open();

        *device = &pHwc->common;
        return OK;
    }

    return -EINVAL;
}

Hwc::~Hwc()
{
}

int Hwc::hook_close(struct hw_device_t* device)
{
    delete getComposer(device);
    return device ? 0 : -ENOENT;
}



/*
 * (*prepare)() is called for each frame before composition and is used by
 * SurfaceFlinger to determine what composition steps the HWC can handle.
 *
 * (*prepare)() can be called more than once, the last call prevails.
 *
 * The HWC responds by setting the compositionType field in each layer to
 * either HWC_FRAMEBUFFER or HWC_OVERLAY. In the former case, the
 * composition for the layer is handled by SurfaceFlinger with OpenGL ES,
 * in the later case, the HWC will have to handle the layer's composition.
 * compositionType and hints are preserved between (*prepare)() calles
 * unless the HWC_GEOMETRY_CHANGED flag is set.
 *
 * (*prepare)() is called with HWC_GEOMETRY_CHANGED to indicate that the
 * list's geometry has changed, that is, when more than just the buffer's
 * handles have been updated. Typically this happens (but is not limited to)
 * when a window is added, removed, resized or moved. In this case
 * compositionType and hints are reset to their default value.
 *
 * For HWC 1.0, numDisplays will always be one, and displays[0] will be
 * non-NULL.
 *
 * For HWC 1.1, numDisplays will always be HWC_NUM_PHYSICAL_DISPLAY_TYPES.
 * Entries for unsupported or disabled/disconnected display types will be
 * NULL.
 *
 * In HWC 1.3, numDisplays may be up to HWC_NUM_DISPLAY_TYPES. The extra
 * entries correspond to enabled virtual displays, and will be non-NULL.
 *
 * returns: 0 on success. An negative error code on error. If an error is
 * returned, SurfaceFlinger will assume that none of the layer will be
 * handled by the HWC.
 */

status_t Hwc::onPrepare(
        size_t numDisplays,
        hwc_display_contents_1_t** displays)
{
    ATRACE_CALL_IF(HWC_TRACE);
    ALOG_ASSERT(numDisplays > 0 && displays != NULL);

    mbOpen = true;

    const uint32_t hwcFrameIndex = getRedrawFrames();

    if (PREPARE_INFO_DEBUG)
    {
        ALOGD("-----------------------------------------------------------------------");
        ALOGD("Prepare entry display state dump");
        dumpDisplaysContents( "onPrepare", numDisplays, displays, hwcFrameIndex );
        ALOGD("-----------------------------------------------------------------------");
    }

    Log::add(displays, numDisplays, hwcFrameIndex, "onPrepare Entry");

    nsecs_t timestamp = systemTime(CLOCK_MONOTONIC);

    // Update the base content structure and obtain our baseline Content
    mInputAnalyzer.onPrepare(numDisplays, displays, hwcFrameIndex, timestamp, mLogicalDisplayManager);

    // Allow the composition manager to perform any required setup at the start of a frame
    mCompositionManager.onPrepareBegin(numDisplays, displays, timestamp);

    // Apply any filters to the content
    mpFinalContent = &mFilterManager.onPrepare(mInputAnalyzer.getContent());

    // Make any necessary decisions about the use of the hardware resources
    mPhysicalDisplayManager.onPrepare(*mpFinalContent);

    // Allow the composition manager to perform any updates of the flags in the input surfaces
    mCompositionManager.onPrepareEnd();

    if (PREPARE_INFO_DEBUG)
    {
        dumpDisplaysContents( "onPrepare Exit", numDisplays, displays, hwcFrameIndex );
    }
    Log::add(displays, numDisplays, hwcFrameIndex, "onPrepare Exit");

    return OK;
}

int Hwc::hook_prepare(
        struct hwc_composer_device_1 *dev,
        size_t numDisplays,
        hwc_display_contents_1_t** displays)
{
    return getComposer(dev)->onPrepare(numDisplays, displays);
}

void Hwc::setPrimaryDisplaySyncs( uint32_t sfIndex )
{
    Mutex::Autolock _l(mVsyncLock);

    // Change vsync to external display on extended mode
    if (mbVsyncIsEnabled)
    {
        mLogicalDisplayManager.onVSyncEnable( mPrimaryDisplaySync, false );
        ALOGD_IF( VSYNC_DEBUG, "Disable vsync on SF:%d", mPrimaryDisplaySync);
        mLogicalDisplayManager.onVSyncEnable( sfIndex, true );
        ALOGD_IF( VSYNC_DEBUG, "Enable vsync on SF:%d", sfIndex);
    }
    mPrimaryDisplaySync = sfIndex;
}

/*
 * (*set)() is used in place of eglSwapBuffers(), and assumes the same
 * functionality, except it also commits the work list atomically with
 * the actual eglSwapBuffers().
 *
 * The layer lists are guaranteed to be the same as the ones returned from
 * the last call to (*prepare)().
 *
 * When this call returns the caller assumes that the displays will be
 * updated in the near future with the content of their work lists, without
 * artifacts during the transition from the previous frame.
 *
 * A display with zero layers indicates that the entire composition has
 * been handled by SurfaceFlinger with OpenGL ES. In this case, (*set)()
 * behaves just like eglSwapBuffers().
 *
 * For HWC 1.0, numDisplays will always be one, and displays[0] will be
 * non-NULL.
 *
 * For HWC 1.1, numDisplays will always be HWC_NUM_PHYSICAL_DISPLAY_TYPES.
 * Entries for unsupported or disabled/disconnected display types will be
 * NULL.
 *
 * In HWC 1.3, numDisplays may be up to HWC_NUM_DISPLAY_TYPES. The extra
 * entries correspond to enabled virtual displays, and will be non-NULL.
 *
 * IMPORTANT NOTE: There is an implicit layer containing opaque black
 * pixels behind all the layers in the list. It is the responsibility of
 * the hwcomposer module to make sure black pixels are output (or blended
 * from).
 *
 * IMPORTANT NOTE: In the event of an error this call *MUST* still cause
 * any fences returned in the previous call to set to eventually become
 * signaled.  The caller may have already issued wait commands on these
 * fences, and having set return without causing those fences to signal
 * will likely result in a deadlock.
 *
 * returns: 0 on success. A negative error code on error:
 *    HWC_EGL_ERROR: eglGetError() will provide the proper error code (only
 *        allowed prior to HWComposer 1.1)
 *    Another code for non EGL errors.
 */

status_t Hwc::onSet(
        size_t numDisplays,
        hwc_display_contents_1_t** displays)
{
    ATRACE_CALL_IF(HWC_TRACE);
    ALOG_ASSERT(numDisplays > 0 && displays != NULL);
    ALOG_ASSERT(mpFinalContent);

    const uint32_t hwcFrameIndex = getRedrawFrames();

    // Entry logging - must be kept at the start of the function
    Log::add(displays, numDisplays, hwcFrameIndex, "onSet Entry");
    if (SET_INFO_DEBUG)
    {
        dumpDisplaysContents( "onSet Entry", numDisplays, displays, hwcFrameIndex );
    }

    // Trigger the composition manager to initiate any compositions that it may need for this frame
    mCompositionManager.onSetBegin(numDisplays, displays);

    // Now apply the frame.
    mPhysicalDisplayManager.onSet(*mpFinalContent);

    // Close the virtual display retire fence if present.
    // NOTE: WidiDisplay generates retire fences.
    if ( HWC_DISPLAY_VIRTUAL < numDisplays )
    {
        hwc_display_contents_1_t* pDisp = displays[ HWC_DISPLAY_VIRTUAL ];
        if ( pDisp )
        {
            Timeline::closeFence(&(pDisp->retireFenceFd));
        }
    }

    // Close any trailing open fences.
    for (size_t d = 0; d < numDisplays; ++d)
    {
        hwc_display_contents_1_t* pDisp = displays[d];
        if (pDisp)
        {
            for (size_t ly = 0; ly < pDisp->numHwLayers; ++ly)
            {
                hwc_layer_1_t& layer = pDisp->hwLayers[ly];
                Timeline::closeFence(&(layer.acquireFenceFd));
            }
            // Close the outFd if present.
            Timeline::closeFence(&(pDisp->outbufAcquireFenceFd));
        }
    }

    // Frame is complete.
    onEndOfFrame();

    // NOTE:
    // Logs for the final display state must be written just prior to onSet exit.
    Log::add(displays, numDisplays, hwcFrameIndex, "onSet Exit");

    if (SET_INFO_DEBUG)
    {
        dumpDisplaysContents( "onSet Exit", numDisplays, displays, hwcFrameIndex );
    }
    return OK;
}

void Hwc::onEndOfFrame( void )
{
    // Buffer manager end-of-frame processing.
    mAbstractBufferManager.onEndOfFrame( );

    // Composition manager end-of-frame processing.
    mCompositionManager.onEndOfFrame( mRedrawFrames );

    // Display manager end-of-frame processing.
    mLogicalDisplayManager.endOfFrame( );

    // Hwc plug changes.
    if ( flushPlugChanges( ) )
    {
        // Ensure we get a subsequent redraw and geometry change on any display plug change.
        forceGeometryChangeAndRedraw( );
    }

    // Finally, synchronize/signal end of frame.
    Mutex::Autolock _l( mEndOfFrameMutex );
    ALOGD_IF( HWC_DEBUG || HWC_SYNC_DEBUG, "End of frame %u", mRedrawFrames );
    // Advance the redraw count and  then signal end of frame.
    ++mRedrawFrames;
    mConditionEndOfFrame.broadcast( );
}

void Hwc::flushInitialPlugs( void )
{
    for (;;)
    {
        DisplayPlugChange plugChange;
        {
            Mutex::Autolock _l( mDisplayPlugChangesLock );
            if ( mDisplayPlugChanges.isEmpty() )
            {
                return;
            }
            plugChange = mDisplayPlugChanges[ 0 ];
            mDisplayPlugChanges.removeItemsAt( 0 );
        }
        LogicalDisplay* pDisplay = plugChange.mpDisplay;
        ALOG_ASSERT( plugChange.mFlags & DisplayPlugChange::DPC_FLAG_PLUG );
        if ( ( plugChange.mSfIndex != INVALID_DISPLAY_ID  )
          && ( mLogicalDisplayManager.plugSurfaceFlingerDisplay( pDisplay, plugChange.mSfIndex ) == OK ) )
        {
            Log::alogd( DRMDISPLAY_MODE_DEBUG, "Display %s initial plug to SF%u",
                pDisplay->dump().string(), plugChange.mSfIndex );
        }
    }
}

bool Hwc::flushPlugChanges( void )
{
    // Process the next display plug/unplug (if any).
    // Only one plug/unplug is processed per-frame - to satisfy SurfaceFlinger constraint.

    DisplayPlugChange plugChange;
    {
        Mutex::Autolock _l( mDisplayPlugChangesLock );
        if ( mDisplayPlugChanges.isEmpty() )
        {
            return false;
        }
        Log::alogd( DRMDISPLAY_MODE_DEBUG, "Display Plug Changes %zd", mDisplayPlugChanges.size() );
        plugChange = mDisplayPlugChanges[ 0 ];
        mDisplayPlugChanges.removeItemsAt( 0 );
        ++mDisplayPlugChangeSequence;
    }
    LogicalDisplay* pDisplay = plugChange.mpDisplay;

    const bool bTransitory = ( plugChange.mFlags & DisplayPlugChange::DPC_FLAG_TRANSITORY ) != 0;

    if ( plugChange.mFlags & DisplayPlugChange::DPC_FLAG_PLUG )
    {
        ALOG_ASSERT( !(plugChange.mFlags & DisplayPlugChange::DPC_FLAG_UNPLUG) );
        const uint32_t sfIndex = plugChange.mSfIndex;

        if ( ( sfIndex != INVALID_DISPLAY_ID  )
          && ( mLogicalDisplayManager.plugSurfaceFlingerDisplay( pDisplay, sfIndex, bTransitory ) == OK ) )
        {
            Log::alogd( DRMDISPLAY_MODE_DEBUG, "Display %s plug to SF%u%s",
                pDisplay->dump().string(), sfIndex, bTransitory ? " (Transition)" : "" );

            ALOG_ASSERT( sfIndex == pDisplay->getSurfaceFlingerIndex( ) );
            postHotPlug( sfIndex );
        }
        else
        {
            // May be out of slots.
            Log::alogd( DRMDISPLAY_MODE_DEBUG, "Display %s plug to SF%u%s - could not plug",
                pDisplay->dump().string(), sfIndex, bTransitory ? " (Transition)" : "" );
        }
    }
    else if ( plugChange.mFlags & DisplayPlugChange::DPC_FLAG_UNPLUG )
    {
        const uint32_t sfIndex = pDisplay->getSurfaceFlingerIndex();

        if ( sfIndex == INVALID_DISPLAY_ID )
        {
            Log::alogd( DRMDISPLAY_MODE_DEBUG, "Display %s unplug from SF%u%s- already unplugged",
                pDisplay->dump().string(), sfIndex, bTransitory ? " (Transition)" : "" );
        }
        else if ( mLogicalDisplayManager.unplugSurfaceFlingerDisplay( pDisplay, bTransitory ) == OK )
        {
            Log::alogd( DRMDISPLAY_MODE_DEBUG, "Display %s unplug from SF%u%s",
                pDisplay->dump().string(), sfIndex, bTransitory ? " (Transition)" : "" );
            postHotUnplug( sfIndex );
        }
        else
        {
            Log::aloge( true, "Display %s unplug from SF%u%s - failed to unplug",
                pDisplay->dump().string(), sfIndex, bTransitory ? " (Transition)" : "" );
        }
    }

    return true;
}

void Hwc::synchronize( nsecs_t timeoutNs )
{
    if ( !isOpen() )
    {
        ALOGD_IF(sbInternalBuild, "Skipped early synchronize" );
        return;
    }

    // Temporary filter to insert to sync with frame updates on a Hwc.
    class SyncFilter : public AbstractFilter
    {
    public:
        SyncFilter( Hwc& hwc, pid_t tid, nsecs_t timeoutNs ) :
            mHwc( hwc ), mTid( tid ), mFrameIndex( 0 ), mbPresented( false ), mTimeoutNs( timeoutNs ) { }
        const char* getName( void ) const { return "SyncFilter"; }
        bool outputsPhysicalDisplays() const { return false; }
        String8 dump( void ) { return String8( getName() ); }

        // Stores the first frame's index, sets the presented flag
        // and raises the presented signal.
        const Content& onApply( const Content& ref )
        {
            Mutex::Autolock _l( mLock );
            if ( !mbPresented )
            {
                mFrameIndex = mHwc.getRedrawFrames( );
                mbPresented = true;
            }
            Log::alogd( HWC_SYNC_DEBUG, "Synchronizing thread %u with frame:%u", mTid, mFrameIndex );
            mPresented.signal();
            return ref;
        }

        // If the presentation has not yet occured,
        // forces an update and waits for the presented signal.
        bool waitForPresentation( uint32_t* pFrameIndex )
        {
            ALOG_ASSERT( pFrameIndex );
            Mutex::Autolock _l( mLock );
            if ( !mbPresented )
            {
                // Force a redraw and wait for it to be issued.
                ALOGD_IF( HWC_SYNC_DEBUG, "SyncFilter thread %u force frame presentation", mTid );
                mHwc.forceRedraw( );
                ALOGD_IF( HWC_SYNC_DEBUG, "SyncFilter thread %u waiting for a frame to be presented", mTid );
                if ( mTimeoutNs > 0 )
                {
                    if ( mPresented.waitRelative( mLock, mTimeoutNs ) == OK )
                    {
                        ALOGD_IF( HWC_SYNC_DEBUG, "SyncFilter thread %u captured frame:%u", mTid, mFrameIndex );
                    }
                    else
                    {
                        ALOGE( "SyncFilter thread %u non-blocking wait for present *FAILED/TIMEOUT*", mTid );
                    }
                }
                else
                {
                    if ( mPresented.wait( mLock ) != OK )
                    {
                        ALOGE( "SyncFilter thread %u blocking wait for present *FAILED*", mTid );
                    }
                }
            }
            *pFrameIndex = mFrameIndex;
            return mbPresented;
        }

        // Reset the sync filter.
        // This makes the filter usable for a subsequent frame,
        void reset( void )
        {
            Mutex::Autolock _l( mLock );
            mFrameIndex = 0;
            mbPresented = false;
        }

    private:
        Hwc&        mHwc;
        pid_t       mTid;
        uint32_t    mFrameIndex;
        bool        mbPresented:1;
        Mutex       mLock;
        Condition   mPresented;
        nsecs_t     mTimeoutNs;
    } f( *this, gettid(), timeoutNs );

    Log::alogd( HWC_DEBUG || HWC_SYNC_DEBUG, "Synchronizing..." );

    // Capture sequence number of plug change when we start.
    uint32_t plugSequenceBegin = getPlugChangeSequence();

    // Insert the filter.
    // This will capture the index of the next presented frame.
    FilterManager::getInstance().add( f, FilterPosition::SyncFilter );

    bool bPresentFrame = true;
    const uint32_t passLimit = 100;
    uint32_t pass = 0;

    while ( bPresentFrame )
    {
        ALOGD_IF( HWC_SYNC_DEBUG, "Synchronizing: Wait for presentation pass %u...", pass );

        // This will wait until the next frame has been presented (not necessarily displayed)
        uint32_t frameIndex;
        if ( f.waitForPresentation( &frameIndex ) )
        {
            // Ensure the frame has been completed.
            synchronizeFrameEnd( frameIndex, timeoutNs );
            // Wait until the presented frame has reached the display (including flip completion).
            ALOGD_IF( HWC_SYNC_DEBUG, "Synchronizing: Flushing displays [frameIndex %u]", frameIndex );
            mPhysicalDisplayManager.flush( frameIndex );
            ALOGD_IF( HWC_SYNC_DEBUG, "Synchronizing: Flushed displays [frameIndex %u]", frameIndex );
        }

        // This will wait for any trailing plug changes to be processed.
        // Because a setDisplayMode can queue up changes in addition to plug events and because we process
        // at most one change per frame (due to SF constraint) then we shouldn't assume all plug changes that
        // were set up prior to our frame have been issued just because our frame is now done.
        synchronizePlugChanges( timeoutNs );

        // If any plug changes were processed then we respin another frame here.
        // This will wait for the display to flush again which will ensure a subsequent mode change can be completed.
        // This is required to fully synchronize startup and mode changes since a display only applies the mode change
        // once it has received a frame at the correct size.
        uint32_t plugSequenceEnd = getPlugChangeSequence();
        ALOGD_IF( HWC_SYNC_DEBUG, "Synchronizing: Plug sequence %u -> %u", plugSequenceBegin, plugSequenceEnd );
        bPresentFrame =  ( (uint32_t)int32_t(  plugSequenceEnd - plugSequenceBegin ) > 0 );
        plugSequenceBegin = plugSequenceEnd;

        // Reset frame present filter.
        f.reset();

        if ( ++pass > passLimit )
        {
            // Sanity check.
            ALOGE( "Excessive passes during synchronize %u", pass );
            break;
        }
    }

    // Remove the sync filter.
    FilterManager::getInstance().remove(f);

    Log::alogd( HWC_DEBUG || HWC_SYNC_DEBUG, "Synchronized" );
}

void Hwc::synchronizeFrameEnd( uint32_t frameIndex, nsecs_t timeoutNs )
{
    Mutex::Autolock _l( mEndOfFrameMutex );

    ALOGD_IF( HWC_DEBUG || HWC_SYNC_DEBUG,
              "Synchronizing FrameEnd: Sync with end of frame %u (timeout %uus)",
              frameIndex, (uint32_t)(timeoutNs/1000) );

    while ( int32_t( mRedrawFrames - frameIndex ) <= 0 )
    {
        ALOGD_IF( HWC_SYNC_DEBUG, "Synchronizing FrameEnd:   Waiting for end of frame %u [Redraws:%u]", frameIndex, mRedrawFrames );
        if ( timeoutNs )
        {
            if ( mConditionEndOfFrame.waitRelative( mEndOfFrameMutex, timeoutNs ) != OK )
            {
                ALOGE( "Synchronize FrameEnd:   Non-blocking wait for end of frame *FAILED/TIMEOUT*" );
                break;
            }
        }
        else
        {
            if ( mConditionEndOfFrame.wait( mEndOfFrameMutex ) != OK )
            {
                ALOGE( "Synchronize FrameEnd:   Blocking wait for end of frame *FAILED*" );
                break;
            }
        }
    }
}

uint32_t Hwc::synchronizePlugChanges( nsecs_t timeoutNs )
{
    Mutex::Autolock _l( mEndOfFrameMutex );

    uint32_t changes = getPendingPlugChanges( );

    ALOGD_IF( HWC_DEBUG || HWC_SYNC_DEBUG,
              "Synchronizing PlugChanges: Sync with %u plug changes [timeout %uus]",
              changes, (uint32_t)(timeoutNs/1000) );

    uint32_t changesRemaining = changes;
    while ( changesRemaining && getPendingPlugChanges() )
    {
        ALOGD_IF( HWC_SYNC_DEBUG, "Synchronizing PlugChanges:   Waiting for frame [Redraws:%u, Pending:%u, Remaining:%u]",
            mRedrawFrames, getPendingPlugChanges(), changesRemaining  );
        --changesRemaining;
        if ( timeoutNs )
        {
            if ( mConditionEndOfFrame.waitRelative( mEndOfFrameMutex, timeoutNs ) != OK )
            {
                ALOGE( "Synchronize PlugChanges:   Non-blocking wait for end of frame *FAILED/TIMEOUT*" );
                break;
            }
        }
        else
        {
            if ( mConditionEndOfFrame.wait( mEndOfFrameMutex ) != OK )
            {
                ALOGE( "Synchronize PlugChanges:   Blocking wait for end of frame *FAILED*" );
                break;
            }
        }
    }
    return changes;
}

void Hwc::notifyPlugChangeCompleted( )
{
    // Notify DisplayProxy of plug change has completed,
    // so that it can post any pending work, such as reconnect.
    mPhysicalDisplayManager.notifyPlugChangeCompleted( );
}

void Hwc::waitForSurfaceFlingerReady( void )
{
    // Polling wait for fully running.
    const uint32_t delay10ms = 10000;
    uint32_t openIterations = 0;

    while ( !isOpen() )
    {
        usleep( delay10ms );
        ++openIterations;
        if ( ( openIterations&127 ) == 0 )
        {
            ALOGE( "Waiting for SurfaceFlinger open (iterations %u)", openIterations );
        }
    }

    // Synchronized wait if any display blanked by SurfaceFlinger.
    uint32_t blankWaits = 0;
    Mutex::Autolock _l( mBlankLock );
    while ( mSFBlankMask != 0 )
    {
        Log::alogd( HWC_DEBUG, "Waiting for SurfaceFlinger to unblank (blankWaits %u, SFBlankMask == %x)", blankWaits, mSFBlankMask );
        mSFBlankMaskCondition.wait( mBlankLock );
        ++blankWaits;
    }

    if ( blankWaits | openIterations )
    {
        Log::add( "SurfaceFlinger ready (after %u open iterations and %u blank waits)", openIterations, blankWaits );
    }
}

void Hwc::postVSyncEvent( uint32_t sfIndex, nsecs_t timeStampNs )
{
    ATRACE_EVENT_IF( VSYNC_DEBUG, "HWC:VSYNC->SF" );
    ALOGD_IF( VSYNC_DEBUG, "Display SF:%u VSync to SurfaceFlinger, time %" PRIu64 "ms", sfIndex, timeStampNs / 1000000 );
    // We always sent vsync event as primary display since currently SF will only use primary display's vsync
    // to calibrate SW vsync.
    mSF.vsync( HWC_DISPLAY_PRIMARY, timeStampNs );
}

void Hwc::postHotPlug( uint32_t sfIndex )
{
    ATRACE_EVENT_IF( HPLUG_DEBUG, "HWC:PLUG->SF" );
    if ( isOpen() )
    {
        // Some display types can not be plugged/unplugged.
        if ( ( sfIndex != HWC_DISPLAY_PRIMARY )
          && ( sfIndex != HWC_DISPLAY_VIRTUAL ) )
        {
            Log::alogd( HPLUG_DEBUG || HWC_DEBUG, "Display SF%u Hot plug to SurfaceFlinger", sfIndex );
            mSF.hotplug( sfIndex, true );
        }
    }
    else
    {
        ALOGD_IF( HPLUG_DEBUG, "Display SF%u Hot plug to SurfaceFlinger", sfIndex );
    }
}

void Hwc::postHotUnplug( uint32_t sfIndex )
{
    ATRACE_EVENT_IF( HPLUG_DEBUG, "HWC:UNPLUG->SF" );
    if ( isOpen() )
    {
        // Some displays types can not be plugged/unplugged.
        if ( ( sfIndex != HWC_DISPLAY_PRIMARY )
          && ( sfIndex != HWC_DISPLAY_VIRTUAL ) )
        {
            // Reset blank mask.
            {
                Mutex::Autolock _l( mBlankLock );
                mSFBlankMask &= ~(1<<sfIndex);
                mSFBlankMaskCondition.broadcast( );
            }
            Log::alogd( HPLUG_DEBUG || HWC_DEBUG, "Display SF%u Hot unplug from SurfaceFlinger", sfIndex );
            mSF.hotplug( sfIndex, false );
        }
    }
    else
    {
        ALOGD_IF( HPLUG_DEBUG, "Display SF%u Hot unplug from SurfaceFlinger", sfIndex );
    }
}

void Hwc::notifyDisplayAvailable( LogicalDisplay* pDisplay, uint32_t sfIndex )
{
    ALOG_ASSERT( pDisplay );
    Mutex::Autolock _l( mDisplayPlugChangesLock );
    mDisplayPlugChanges.push( DisplayPlugChange( pDisplay, sfIndex, DisplayPlugChange::DPC_FLAG_PLUG ) );
    Log::alogd( DRMDISPLAY_MODE_DEBUG,
        "Display %s\n   now available to SF (requesting slot %u) => plug [changes issued %zd]",
        pDisplay->dump().string(), sfIndex, mDisplayPlugChanges.size() );
    forceGeometryChangeAndRedraw( );
}

void Hwc::notifyDisplayUnavailable( LogicalDisplay* pDisplay )
{
    ALOG_ASSERT( pDisplay );
    Mutex::Autolock _l( mDisplayPlugChangesLock );
    const uint32_t sfIndex = pDisplay->getSurfaceFlingerIndex( );
    mDisplayPlugChanges.push( DisplayPlugChange( pDisplay, sfIndex, DisplayPlugChange::DPC_FLAG_UNPLUG ) );
    Log::alogd( DRMDISPLAY_MODE_DEBUG,
        "Display %s\n   no longer available on slot %u => unplug [changes issued %zd]",
        pDisplay->dump().string(), pDisplay->getSurfaceFlingerIndex(), mDisplayPlugChanges.size() );
    forceGeometryChangeAndRedraw( );
}

void Hwc::notifyDisplayChangeSize( LogicalDisplay* pDisplay )
{
    ALOG_ASSERT( pDisplay );
    Mutex::Autolock _l( mDisplayPlugChangesLock );
    const uint32_t sfIndex = pDisplay->getSurfaceFlingerIndex( );
    // Display size change for SF needs an unplug/plug pair.
    // The unplug is transitory.
    mDisplayPlugChanges.push( DisplayPlugChange( pDisplay, sfIndex, DisplayPlugChange::DPC_FLAG_UNPLUG | DisplayPlugChange::DPC_FLAG_TRANSITORY ) );
    mDisplayPlugChanges.push( DisplayPlugChange( pDisplay, sfIndex, DisplayPlugChange::DPC_FLAG_PLUG ) );
    Log::alogd( DRMDISPLAY_MODE_DEBUG,
        "Display %s\n   size change => unplug/plug SF only on slot %d [changes issued %zd]",
        pDisplay->dump().string(), pDisplay->getSurfaceFlingerIndex(), mDisplayPlugChanges.size() );
    forceGeometryChangeAndRedraw( );
}

void Hwc::notifyDisplayVSync( LogicalDisplay* pDisplay, nsecs_t timeStampNs )
{
    ALOG_ASSERT( pDisplay );
    const uint32_t sfIndex = pDisplay->getSurfaceFlingerIndex( );
    if ( sfIndex < cMaxSupportedSFDisplays )
    {
        postVSyncEvent( sfIndex, timeStampNs );
    }
}

int Hwc::hook_set(
        struct hwc_composer_device_1 *dev,
        size_t numDisplays,
        hwc_display_contents_1_t** displays)
{
    return getComposer(dev)->onSet(numDisplays, displays);
}

/*
 * eventControl(..., event, enabled)
 * Enables or disables h/w composer events for a display.
 *
 * eventControl can be called from any thread and takes effect
 * immediately.
 *
 *  Supported events are:
 *      HWC_EVENT_VSYNC
 *
 * returns -EINVAL if the "event" parameter is not one of the value above
 * or if the "enabled" parameter is not 0 or 1.
 */
status_t Hwc::onEventControl(
        int d,
        int event,
        int enabled)
{
    switch (event)
    {
    case HWC_EVENT_VSYNC:
    {
        ATRACE_INT_IF( VSYNC_DEBUG, "HWC:HWC_EVENT_VSYNC", enabled );
        ALOG_ASSERT( d == HWC_DISPLAY_PRIMARY );
        HWC_UNUSED(d);

        Mutex::Autolock _l(mVsyncLock);

        mLogicalDisplayManager.onVSyncEnable( mPrimaryDisplaySync, enabled );
        mbVsyncIsEnabled = enabled;
        ALOGD_IF( VSYNC_DEBUG, "Display %d vsync %s", mPrimaryDisplaySync, enabled ? "enabled" : "disabled");
        return 0;
    }
    default:
        break;
    }

    ALOGE("eventControl failed: Unknown event %d", event);
    return -EINVAL;
}

int Hwc::hook_eventControl(
        struct hwc_composer_device_1* dev,
        int d,
        int event,
        int enabled)
{
    return getComposer(dev)->onEventControl(d, event, enabled);
}


/*
 * For HWC 1.3 and earlier, the blank() interface is used.
 *
 * blank(..., blank)
 * Blanks or unblanks a display's screen.
 *
 * Turns the screen off when blank is nonzero, on when blank is zero.
 * Multiple sequential calls with the same blank value must be
 * supported.
 * The screen state transition must be be complete when the function
 * returns.
 *
 * returns 0 on success, negative on error.
 */

status_t Hwc::onBlank(
        int d,
        int blank,
        AbstractDisplayManager::BlankSource source)
{
    Mutex::Autolock _l( mBlankLock );
    status_t result = mLogicalDisplayManager.onBlank(d, blank != 0, source );
    if ( result == 0 )
    {
        if ( blank != 0 )
        {
            mSFBlankMask |= (1<<d);
        }
        else
        {
            mSFBlankMask &= ~(1<<d);
        }
        mSFBlankMaskCondition.broadcast( );
    }
    return result;
}

int Hwc::hook_blank(
        struct hwc_composer_device_1* dev,
        int d,
        int blank)
{
    return getComposer(dev)->onBlank(d, blank, AbstractDisplayManager::BLANK_SURFACEFLINGER);
}

/*
 * For HWC 1.4 and above, setPowerMode() will be used in place of
 * blank().
 *
 * setPowerMode(..., mode)
 * Sets the display screen's power state.
 *
 * Refer to the documentation of the HWC_POWER_MODE_* constants
 * for information about each power mode.
 *
 * The functionality is similar to the blank() command in previous
 * versions of HWC, but with support for more power states.
 *
 * The display driver is expected to retain and restore the low power
 * state of the display while entering and exiting from suspend.
 *
 * Multiple sequential calls with the same mode value must be supported.
 *
 * The screen state transition must be be complete when the function
 * returns.
 *
 * returns 0 on success, negative on error.
 */

#if defined(HWC_DEVICE_API_VERSION_1_4)
int Hwc::hook_setPowerMode(
        struct hwc_composer_device_1* dev,
        int d,
        int mode)
{
    // TODO: Support the other HWC_POWER_MODE modes
    switch (mode)
    {
    case HWC_POWER_MODE_OFF:
        ALOGI("HWC_POWER_MODE_OFF");
        break;
    case HWC_POWER_MODE_NORMAL:
        ALOGI("HWC_POWER_MODE_NORMAL");
        break;
    case HWC_POWER_MODE_DOZE:
        ALOGI("HWC_POWER_MODE_DOZE");
        break;
    case HWC_POWER_MODE_DOZE_SUSPEND:
        ALOGI("HWC_POWER_MODE_DOZE_SUSPEND");
        break;
    }

    return getComposer(dev)->onBlank(d, mode == HWC_POWER_MODE_OFF, AbstractDisplayManager::BLANK_SURFACEFLINGER);
}
#endif
/*
 * Used to retrieve information about the h/w composer
 *
 * Returns 0 on success or -errno on error.
 */

inline
status_t Hwc::onQuery(int what, int* value)
{
    switch (what)
    {
        case HWC_BACKGROUND_LAYER_SUPPORTED:
            /*
             * Must return 1 if the background layer is supported, 0 otherwise.
             */
            *value = 0;
            break;
        case HWC_DISPLAY_TYPES_SUPPORTED:
            /*
             * Availability: HWC_DEVICE_API_VERSION_1_1
             * Returns a mask of supported display types.
             */
             *value = HWC_DISPLAY_PRIMARY |
                      HWC_DISPLAY_EXTERNAL |
                      HWC_DISPLAY_VIRTUAL;
             break;
        default:
            ALOGE("Unhandle query from SurfaceFlinger %d", what);
            return -ENOSYS; // EINVAL;
    }
    return 0;
}

int Hwc::hook_query(
        struct hwc_composer_device_1* dev,
        int what,
        int* value)
{
    return getComposer(dev)->onQuery(what, value);
}


/*
 * (*registerProcs)() registers callbacks that the h/w composer HAL can
 * later use. It will be called immediately after the composer device is
 * opened with non-NULL procs. It is FORBIDDEN to call any of the callbacks
 * from within registerProcs(). registerProcs() must save the hwc_procs_t
 * pointer which is needed when calling a registered callback.
 */

void Hwc::onRegisterProcs(hwc_procs_t const* procs)
{
    LOG_ALWAYS_FATAL_IF(!procs ||
                        !procs->invalidate ||
                        !procs->vsync ||
                        !procs->hotplug, "Bad callbacks");

    mSF.init(procs);
}

void Hwc::hook_registerProcs(
        struct hwc_composer_device_1* dev,
        hwc_procs_t const* procs)
{
    getComposer(dev)->onRegisterProcs(procs);
}


/*
 * This field is OPTIONAL and can be NULL.
 *
 * If non NULL it will be called by SurfaceFlinger on dumpsys
 */

void Hwc::onDump(char *pBuffer, uint32_t* pBufferLength)
{
    if (!sbInternalBuild)
        return;

    // See if we have a dump from the last call (ie, we were asked to size the dump)
    if (mPendingDump.length() == 0)
    {

        // Flags for dump sys option.
        enum EDumpSysOptions
        {
            DUMPSYS_WANT_BUFFERMANAGER                   = (1<<0),
            DUMPSYS_WANT_INPUTANALYZER                   = (1<<1),
            DUMPSYS_WANT_FILTERMANAGER                   = (1<<2),
            DUMPSYS_WANT_DISPLAYMANAGER                  = (1<<3),
            DUMPSYS_WANT_COMPOSITIONMANAGER              = (1<<4)
        };

        // Note, this option is queried on every dumpsys, so must be set via a setprop
        Option dumpSys("dumpsys", DUMPSYS_WANT_INPUTANALYZER | DUMPSYS_WANT_FILTERMANAGER | DUMPSYS_WANT_DISPLAYMANAGER);

        String8 tmp;
        const bool bWantLog = Log::wantLog();
        bool bWantDumpSys;

        HwcService& hwcService = HwcService::getInstance();
        String8 version = hwcService.getHwcVersion();
        mPendingDump += version + "\n\n";

        if ( bWantLog )
        {
            Log::alogd( false, "\n" );
            Log::alogd( false, "-----BEGIN---------------------------------------------------------------------------------" );
            Log::alogd( false, "%s\n", version.string() );
        }

        bWantDumpSys = ( dumpSys & DUMPSYS_WANT_INPUTANALYZER );
        if ( bWantLog || bWantDumpSys )
        {
            tmp = mInputAnalyzer.dump();
            if ( tmp.length() > 0 )
            {
                tmp = String8( "INPUTS:\n" ) + tmp;
                Log::alogd( false, tmp.string() );
                if ( bWantDumpSys )
                {
                    mPendingDump += tmp + "\n";
                }
            }
        }

        bWantDumpSys = ( dumpSys & DUMPSYS_WANT_FILTERMANAGER );
        if ( bWantLog || bWantDumpSys )
        {
            tmp = mFilterManager.dump();
            if ( tmp.length() > 0 )
            {
                tmp = String8( "FILTERS:\n" ) + tmp;
                Log::alogd( false, tmp.string() );
                if ( bWantDumpSys )
                {
                    mPendingDump += tmp + "\n";
                }
            }
        }

        bWantDumpSys = ( dumpSys & DUMPSYS_WANT_DISPLAYMANAGER );
        if ( bWantLog || bWantDumpSys )
        {
            tmp = mPhysicalDisplayManager.dump();
            if ( tmp.length() > 0 )
            {
                tmp = String8( "DISPLAYS:\n" ) + tmp;
                Log::alogd( false, tmp.string() );
                if ( bWantDumpSys )
                {
                    mPendingDump += tmp + "\n";
                }
            }
        }

        bWantDumpSys = ( dumpSys & DUMPSYS_WANT_COMPOSITIONMANAGER );
        if ( bWantLog || bWantDumpSys )
        {
            tmp = mCompositionManager.dump();
            if ( tmp.length() > 0 )
            {
                tmp = String8( "COMPOSITIONS:\n" ) + tmp;
                Log::alogd( false, tmp.string() );
                if ( bWantDumpSys )
                {
                    mPendingDump += tmp + "\n";
                }
            }
        }

        bWantDumpSys = ( dumpSys & DUMPSYS_WANT_BUFFERMANAGER );
        if ( bWantLog || bWantDumpSys )
        {
            tmp = AbstractBufferManager::get().dump();
            if ( tmp.length() > 0 )
            {
                tmp = String8( "BUFFERS:\n" ) + tmp;
                Log::alogd( false, tmp.string() );
                if ( bWantDumpSys )
                {
                    mPendingDump += tmp + "\n";
                }
            }
        }

        if ( bWantLog )
        {
            Log::alogd( false, "-----END-----------------------------------------------------------------------------------" );
            Log::alogd( false, "\n" );
        }
    }

    if (pBuffer == NULL)
    {
        // If we have no buffer pointer, they are after the size of the dump, not the content.
        // Save the content for the next call
        *pBufferLength = mPendingDump.length()+1;
    }
    else
    {
        strncpy(pBuffer, mPendingDump.string(), *pBufferLength);
        // Terminate the buffer, just in case the string got truncated
        pBuffer[*pBufferLength-1] = 0;
        mPendingDump = "";
    }
}

void Hwc::hook_dump(struct hwc_composer_device_1* dev, char *buff, int buff_len)
{
    uint32_t len = buff_len;
    getComposer(dev)->onDump(buff, &len);
}

/*
 * (*getDisplayConfigs)() returns handles for the configurations available
 * on the connected display. These handles must remain valid as long as the
 * display is connected.
 *
 * Configuration handles are written to configs. The number of entries
 * allocated by the caller is passed in *numConfigs; getDisplayConfigs must
 * not try to write more than this number of config handles. On return, the
 * total number of configurations available for the display is returned in
 * *numConfigs. If *numConfigs is zero on entry, then configs may be NULL.
 *
 * Hardware composers implementing HWC_DEVICE_API_VERSION_1_3 or prior
 * shall choose one configuration to activate and report it as the first
 * entry in the returned list. Reporting the inactive configurations is not
 * required.
 *
 * HWC_DEVICE_API_VERSION_1_4 and later provide configuration management
 * through SurfaceFlinger, and hardware composers implementing these APIs
 * must also provide getActiveConfig and setActiveConfig. Hardware composers
 * implementing these API versions may choose not to activate any
 * configuration, leaving configuration selection to higher levels of the
 * framework.
 *
 * Returns 0 on success or a negative error code on error. If disp is a
 * hotpluggable display type and no display is connected, an error shall be
 * returned.
 *
 * This field is REQUIRED for HWC_DEVICE_API_VERSION_1_1 and later.
 * It shall be NULL for previous versions.
 */

status_t Hwc::onGetDisplayConfigs(
        int d,
        uint32_t* paOutConfigHandles,
        uint32_t* pOutNumConfigs)
{
    LOG_ALWAYS_FATAL_IF(!pOutNumConfigs, "Bad pointer");

    AbstractDisplay* pHwcDisp = getSurfaceFlingerDisplay(d);
    if (pHwcDisp == NULL) {
        ALOGE_IF( HWC_DEBUG, "Get Display Config: Display SF%d does not exist", d );
        return -ENOENT;
    }

    // Pass on to the appropriate display
    int ret = pHwcDisp->onGetDisplayConfigs(paOutConfigHandles, pOutNumConfigs);
    return ret;
}

int Hwc::hook_getDisplayConfigs(struct hwc_composer_device_1* dev, int d, uint32_t* paOutConfigHandles, size_t* pOutNumConfigs)
{
    ALOG_ASSERT( pOutNumConfigs );
    ALOG_ASSERT( paOutConfigHandles || (*pOutNumConfigs == 0) );

    uint32_t numConfigs = *pOutNumConfigs;
    uint32_t* paConfigHandles = paOutConfigHandles;
    if ( numConfigs )
    {
        // An additional config is inserted at slot 0 and is used to represent the "current active config".
        --numConfigs;
        ++paConfigHandles;
    }

    // Get configs
    auto ret = getComposer(dev)->onGetDisplayConfigs( d, paConfigHandles, &numConfigs );

    // Add in the extra config handle for the "current active config" (at slot 0).
    if ( paOutConfigHandles && ( *pOutNumConfigs > 0 ) )
    {
        *paOutConfigHandles = PhysicalDisplay::CONFIG_HANDLE_RSVD_ACTIVE_CONFIG;
    }

    // Return full config count (including the "current active config").
    *pOutNumConfigs = numConfigs+1;
    Log::alogd( HWC_DEBUG, "SF:%d Get display configs : %u", d, *pOutNumConfigs );

    return ret;
}

/*
 * (*getDisplayAttributes)() returns attributes for a specific config of a
 * connected display. The config parameter is one of the config handles
 * returned by getDisplayConfigs.
 *
 * The list of attributes to return is provided in the attributes
 * parameter, terminated by HWC_DISPLAY_NO_ATTRIBUTE. The value for each
 * requested attribute is written in order to the values array. The
 * HWC_DISPLAY_NO_ATTRIBUTE attribute does not have a value, so the values
 * array will have one less value than the attributes array.
 *
 * This field is REQUIRED for HWC_DEVICE_API_VERSION_1_1 and later.
 * It shall be NULL for previous versions.
 *
 * If disp is a hotpluggable display type and no display is connected,
 * or if config is not a valid configuration for the display, a negative
 * error code shall be returned.
 */

int Hwc::onGetDisplayAttribute(int d, uint32_t configHandle, AbstractDisplay::EAttribute attribute, int32_t* pOutValue)
{
    LOG_ALWAYS_FATAL_IF(!pOutValue, "Bad pointers");

    AbstractDisplay* pHwcDisp = getSurfaceFlingerDisplay(d);
    if (pHwcDisp == NULL) {
        ALOGE("getDisplayAttributes failed: display SF%d not found", d);
        return -ENOENT;
    }

    // Pass on to the appropriate display
    int v = pHwcDisp->onGetDisplayAttribute(configHandle, attribute, pOutValue);
    Log::alogd( HWC_DEBUG, "SF:%d Get display attribute %u : %d", d, attribute, *pOutValue );
    return v;
}

status_t Hwc::hook_getDisplayAttributes(struct hwc_composer_device_1* dev, int d, uint32_t configHandle, const uint32_t* paAttributes, int32_t* paOutValues)
{
    if (configHandle == PhysicalDisplay::CONFIG_HANDLE_RSVD_ACTIVE_CONFIG)
    {
        const uint32_t configIndex = getComposer(dev)->onGetActiveConfig(d);
        configHandle = PhysicalDisplay::CONFIG_HANDLE_BASE + configIndex;
    }

    while (*paAttributes != HWC_DISPLAY_NO_ATTRIBUTE)
    {
        AbstractDisplay::EAttribute attribute = AbstractDisplay::ATTRIB_UNKNOWN;
        switch(*paAttributes)
        {
            case HWC_DISPLAY_VSYNC_PERIOD : attribute = AbstractDisplay::ATTRIB_VSYNC;  break;
            case HWC_DISPLAY_WIDTH        : attribute = AbstractDisplay::ATTRIB_WIDTH;  break;
            case HWC_DISPLAY_HEIGHT       : attribute = AbstractDisplay::ATTRIB_HEIGHT; break;
            case HWC_DISPLAY_DPI_X        : attribute = AbstractDisplay::ATTRIB_XDPI;   break;
            case HWC_DISPLAY_DPI_Y        : attribute = AbstractDisplay::ATTRIB_YDPI;   break;
        }

        if (attribute != AbstractDisplay::ATTRIB_UNKNOWN)
        {
            int ret = getComposer(dev)->onGetDisplayAttribute(d, configHandle, attribute, paOutValues);
            if (ret)
                return ret;
        }
        else
        {
            ALOGD_IF(sbInternalBuild, "getDisplayAttributes: Unknown attribute %d", *paAttributes);
        }

        // Advance the lists
        paAttributes++;
        paOutValues++;
    }
    return 0;
}

/*
 * (*getActiveConfig)() returns the index of the configuration that is
 * currently active on the connected display. The index is relative to
 * the list of configuration handles returned by getDisplayConfigs. If there
 * is no active configuration, -1 shall be returned.
 *
 * Returns the configuration index on success or -1 on error.
 *
 * This field is REQUIRED for HWC_DEVICE_API_VERSION_1_4 and later.
 * It shall be NULL for previous versions.
 */
int Hwc::onGetActiveConfig(int d)
{
    AbstractDisplay* pHwcDisp = getSurfaceFlingerDisplay(d);
    if (pHwcDisp == NULL) {
        ALOGE("getActiveConfig failed: display SF%d not found", d);
        return -ENOENT;
    }

    // Pass on to the appropriate display
    return pHwcDisp->onGetActiveConfig( );
}

int Hwc::hook_getActiveConfig(struct hwc_composer_device_1* dev, int d)
{
    // Adjust index to account for the "current active config" at index 0.
    int c = getComposer(dev)->onGetActiveConfig(d)+1;
    Log::alogd( HWC_DEBUG, "SF:%d Get active config : %d", d, c );
    return c;
}

/*
 * (*setActiveConfig)() instructs the hardware composer to switch to the
 * display configuration at the given index in the list of configuration
 * handles returned by getDisplayConfigs.
 *
 * If this function returns without error, any subsequent calls to
 * getActiveConfig shall return the index set by this function until one
 * of the following occurs:
 *   1) Another successful call of this function
 *   2) The display is disconnected
 *
 * Returns 0 on success or a negative error code on error. If disp is a
 * hotpluggable display type and no display is connected, or if index is
 * outside of the range of hardware configurations returned by
 * getDisplayConfigs, an error shall be returned.
 *
 * This field is REQUIRED for HWC_DEVICE_API_VERSION_1_4 and later.
 * It shall be NULL for previous versions.
 */
int Hwc::onSetActiveConfig(int d, int configIndex)
{
    AbstractDisplay* pHwcDisp = getSurfaceFlingerDisplay(d);
    if (pHwcDisp == NULL) {
        ALOGE("setActiveConfig failed: display SF%d not found", d);
        return -ENOENT;
    }

    // Pass on to the appropriate display
    return pHwcDisp->onSetActiveConfig( configIndex );
}

int Hwc::hook_setActiveConfig(struct hwc_composer_device_1* dev, int d, int configIndex)
{
    // Index 0 is special, its the current mode. Do nothing.
    if (configIndex == 0)
        return 0;
    // Adjust index to account for the "current active config" at index 0.
    return getComposer(dev)->onSetActiveConfig(d, configIndex-1);
}

/*
 * Asynchronously update the location of the cursor layer.
 *
 * Within the standard prepare()/set() composition loop, the client
 * (surfaceflinger) can request that a given layer uses dedicated cursor
 * composition hardware by specifiying the HWC_IS_CURSOR_LAYER flag. Only
 * one layer per display can have this flag set. If the layer is suitable
 * for the platform's cursor hardware, hwcomposer will return from prepare()
 * a composition type of HWC_CURSOR_OVERLAY for that layer. This indicates
 * not only that the client is not responsible for compositing that layer,
 * but also that the client can continue to update the position of that layer
 * after a call to set(). This can reduce the visible latency of mouse
 * movement to visible, on-screen cursor updates. Calls to
 * setCursorPositionAsync() may be made from a different thread doing the
 * prepare()/set() composition loop, but care must be taken to not interleave
 * calls of setCursorPositionAsync() between calls of set()/prepare().
 *
 * Notes:
 * - Only one layer per display can be specified as a cursor layer with
 *   HWC_IS_CURSOR_LAYER.
 * - hwcomposer will only return one layer per display as HWC_CURSOR_OVERLAY
 * - This returns 0 on success or -errno on error.
 * - This field is optional for HWC_DEVICE_API_VERSION_1_4 and later. It
 *   should be null for previous versions.
 */

int Hwc::onSetCursorPositionAsync(int d, int /*x_pos*/, int /*y_pos*/)
{
    AbstractDisplay* pHwcDisp = getSurfaceFlingerDisplay(d);
    if (pHwcDisp == NULL) {
        ALOGE("setCursorPositionAsync failed: display SF%d not found", d);
        return -ENOENT;
    }

    // Pass on to the appropriate display
    return -ENOENT;
}

int Hwc::hook_setCursorPositionAsync(struct hwc_composer_device_1 *dev, int d, int x, int y)
{
    return getComposer(dev)->onSetCursorPositionAsync(d, x, y);
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

/*
 * Every hardware module must have a data structure named HAL_MODULE_INFO_SYM
 * and the fields of this data structure must begin with hw_module_t
 * followed by module specific information.
 */

static struct hw_module_methods_t methods =
{
    .open = intel::ufo::hwc::Hwc::hook_open
};

#pragma GCC visibility push(default)
hwc_module_t HAL_MODULE_INFO_SYM =
{
    .common =
    {
        .tag                = HARDWARE_MODULE_TAG,
        .module_api_version = HWC_MODULE_API_VERSION_0_1,
        .hal_api_version    = HARDWARE_HAL_API_VERSION,
        .id                 = HWC_HARDWARE_MODULE_ID,
        .name               = "VPG HWComposer",
        .author             = "Intel Corporation",
        .methods            = &methods,
        .dso                = NULL,
        .reserved           = { 0 }
    }
};

