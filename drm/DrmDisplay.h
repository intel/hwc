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

#ifndef INTEL_UFO_HWC_DRMDISPLAY_H
#define INTEL_UFO_HWC_DRMDISPLAY_H

#include "Drm.h"
#include "PhysicalDisplay.h"
#include "DrmPageFlipHandler.h"
#include "DrmDisplayCaps.h"
#include "DisplayQueue.h"
#include "Option.h"

#include <xf86drmMode.h>

#include <utils/Condition.h>
#include <ui/GraphicBuffer.h>

#include <memory>

namespace intel {
namespace ufo {
namespace hwc {

class Drm;
class DrmEventHandler;
class DrmDisplayCaps;
class DrmNuclearHelper;

class DrmDisplay : public PhysicalDisplay, public DisplayQueue
{
public:

    // For which events should HWC Drm self-teardown protected sessions?
    enum ESelfTeardownOptions
    {
        SELF_TEARDOWN_SET       = 1,            // Set display - used on startup/plug.
        SELF_TEARDOWN_RESET     = 2,            // Reset display - used on shutdown/unplug.
        SELF_TEARDOWN_SUSPEND   = 4,            // Suspend - used on blank.
        SELF_TEARDOWN_RESUME    = 8             // Resume - used on unblank.
    };

    DrmDisplay( Hwc& hwc, uint32_t drmConnectorIndex );
    virtual ~DrmDisplay();

    // Boot-time (one-time) open of a display.
    // This must set all immutable state and the initial connected status.
    // Given the display's initial connected status:
    // 1/ CONNECTED   : The display will be started once a subsequent call to start() is received.
    // 2/ UNCONNECTED : The display will be started once a plug event is received through onHotPlugEvent().
    status_t open( drmModeConnector *pConnector, bool bRegisterWithHwc );

    // Finalize opening of a display that is connected at boot.
    // Returns OK if succesful.
    // Returns INVALID_OPERATION if the connection can not be started.
    status_t start( uint32_t crctId, uint32_t pipeIndex );

    // Release miscellaneous Drm resources such as panel fitter.
    void releaseDrmResources( void );

    // Implements AbstractDisplay::onVSyncEnable( ).
    virtual void onSet(const Content::Display& display, uint32_t zorder, int* pRetireFenceFd);
    // Implements AbstractDisplay::onVSyncEnable( ).
    virtual int onVSyncEnable( bool bEnable );
    // Implements AbstractDisplay::onBlank( ).
    virtual int onBlank( bool bEnable, bool bIsSurfaceFlinger );

    // Implements Display::acquireGlobalScaling( ).
    virtual bool acquireGlobalScaling( uint32_t srcW, uint32_t srcH,
                                       int32_t dstX, int32_t dstY,
                                       uint32_t dstW, uint32_t dstH );
    // Implements Display::releaseGlobalScaling( ).
    virtual bool releaseGlobalScaling( void );
    // Apply global scaling to panel fitter.
    bool applyGlobalScalingConfig( const SGlobalScalingConfig& globalScalingConfigNew);

    uint32_t getPossibleCrtcs() { return mPossibleCrtcs; }

#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY
    // Two part global scaling (panel fitter) update.
    // issueGlobalScalingConfig( ) should be called to set up programming of the panel fitter via the atomic display block.
    // finalizeGlobalScalingConfig( ) should be called only once programming has completed successfully.
    void issueGlobalScalingConfig( drm_mode_set_display& display, const SGlobalScalingConfig& globalScalingNew );
    void finalizeGlobalScalingConfig( const SGlobalScalingConfig& globalScalingNew );
#endif

protected:
    void resetGlobalScaling( void );

    // Set panel fitter with specific parameters.
    // The panel fitter must be acquired first.
    // Returns true if successful.
    bool setPanelFitter( uint32_t pfitMode,
                         uint32_t srcW, uint32_t srcH,
                         uint32_t dstX, uint32_t dstY,
                         uint32_t dstW, uint32_t dstH );

    // Reset panel fitter (ensure it is not used).
    // Returns true if successful.
    bool resetPanelFitter( void );

public:
    // Allocate the blanking buffer that is used for initial mode set and whenever we require a black plane
    // The buffer is sized to the current display mode adjusted for current global display scaling.
    // It is possible to override the default size.
    void allocateBlankingLayer( const uint32_t width = 0, const uint32_t height = 0 );

    // Get the blanking layer.
    const Layer& getBlankingLayer( void ) { mBlankBufferFramesSinceLastUsed = 0; return mBlankLayer; }

    // Release unused buffers if they have not been used for a number of frames.
    void considerReleasingBuffers( void );

    // This will drop any set frames that have not yet reached the display (for displays that implement a queue).
    virtual void dropAllFrames( void );

    // This will block until the specified frame has reached the display.
    // If frameIndex is zero, then it will block until all applied state has reached the display.
    // It will only flush work that queue before flush is called.
    // If timeoutNs is zero then this is blocking.
    virtual void flush( uint32_t frameIndex, nsecs_t timeoutNs );

    // This must be called when a vsync event is received for this display.
    // The display will forward the event through Hwc to SurfaceFlinger.
    void vsyncEvent(unsigned int frame, unsigned int sec, unsigned int usec);

    // This must be called when a page flip event is received for this display.
    void pageFlipEvent( void ) { mPageFlipHandler.pageFlipEvent(); }

    // Returns true if the display is attached and available.
    bool isAvailable( void ) const { return ( meStatus == AVAILABLE ); }

    // Returns true if the display is suspended.
    bool isSuspended( void ) const { return ( meStatus == SUSPENDED ); }

    // Was this display connected last time we checked with drm?
    bool isDrmConnected( void ) const { return mCurrentConnection.isConnected(); }

    // Poll the display to establish any plug changes.
    // This must return UEVENT_UNRECOGNISED if there is no change.
    // Else it must return one of UEVENT_HOTPLUG_CONNECTED, UEVENT_HOTPLUG_DISCONNECTED or UEVENT_HOTPLUG_RECONNECT.
    // If a change is detected then subsequent calls to issueHotplugEvent() will be made to process the changes.
    Drm::UEvent onHotPlugEvent( void );

    // This attempts to apply a plug (UEVENT_HOTPLUG_CONNECTED).
    // This may still fail if a pipe is not available.
    void issueHotPlug( void );

    // This applies an unplug (UEVENT_HOTPLUG_DISCONNECTED).
    void issueHotUnplug( void );

    // Reconnect hotplugable device.
    // It is for dual HDMI/DP scenario: one HDMI/DP is attached to DisplayProxy and as D0's physical display, another is D1's.
    // When unplug D0's physical display, D1's physical display should be attched to D0. We can reach it by doing unplug/plug this display.
    virtual void reconnect( void );

    // Enter recovery mode - the display will be recovered before the next work is consumed.
    void enterRecovery( void ) { android_atomic_write( 1, &mRecovering ); }

    // Exit recovery (called before display recovery is attempted).
    void exitRecovery( void ) { android_atomic_write( 0, &mRecovering ); }

    // Is the display in recovery mode?
    bool isInRecovery( void ) { return mRecovering; }

    // Do recovery
    void recover( void );

    // Process ESD event - ESD recovery.
    void onESDEvent( Drm::UEvent eEvent );

    // Get the Gralloc buffer format for blank buffer and/or initial mode set.
    int32_t     getDefaultGrallocBufferFormat( void );

    // Accessor functions
    uint32_t    getDrmDisplayID()       const   { return mDrmDisplay; }
    uint32_t    getDrmConnectorID()     const   { return mDrmConnectorID; }
    uint32_t    getDrmConnectorType()           { return mDrmConnectorType; }
    // Wrapper accessors for *active* connection.
    drmModeConnector* getDrmConnector() const   { return mActiveConnection.getConnector(); }
    uint32_t    getDrmCrtcID()          const   { return mActiveConnection.getCrtcID(); }
    uint32_t    getDrmPipeIndex()       const   { return mActiveConnection.getPipeIndex(); }
    String8     getConnectionDesc()     const   { return mActiveConnection.dump(); }

    const DrmDisplayCaps& getDrmDisplayCaps() const { return mDrmCaps; }

    // This returns the name of the display.
    const char* getName() const { return mName.string(); }

    // Dump DrmDisplay info.
    virtual String8 dump( void ) const
    {
        String8 str;
        str = String8::format( "%s, %s DrmConnector:%u Active:%s",
                                PhysicalDisplay::dump().string(),
                                getName(),
                                getDrmConnectorID(),
                                mActiveConnection.dump().string() );
        return str;
    }


protected:
    friend class DrmPageFlipHandler;
    friend class DrmLegacyPageFlipHandler;
    friend class DrmNuclearPageFlipHandler;

private:
    // DisplayQueue Event IDs.
    enum
    {
        EVENT_STARTUP = 0,
        EVENT_SHUTDOWN,
        EVENT_SUSPEND,
        EVENT_RESUME
    };

private:
    // Drm ID is set by the DRM probe class
    friend int Drm::probe(Hwc& hwc);
    void setDrmDisplayID(uint32_t id)    { mDrmDisplay = id; }
    bool updateTiming( const DisplayQueue::Frame& frame );

    // Connection.
    // This encapsulates a Connector ptr plus CrtcID and PipeIndex if they are known.
    // The CrtcID and PipeIndex can be configured/reset separately to  support displays sharing pipes.
    // i.e. A display can be connected but still not available if there is/was no available pipe for it.
    class Connection
    {
    public:
        Connection( );
        String8 dump( void ) const;

        // Set up connection from other connection (connector and pipe).
        // (frees previous connector, establishes connected status).
        void set( Connection& other );

        // Set only the connector
        // (frees previous connector, establishes connected status).
        void setConnector( drmModeConnector* pConnector );

        // Set only the pipe.
        // The connector must have been specified first.
        void setPipe( uint32_t crtcId, uint32_t pipeIndex );

        // Clear connector details (drop connector reference).
        void clearConnector( void );

        // Clear pipe details.
        void clearPipe( void );

        // Reset (frees the connector, clear details).
        void reset( void );

        // Accessors.
        drmModeConnector* getConnector( void ) const { return mpConnector; }
        uint32_t getCrtcID( void ) const { return mCrtcId; }
        uint32_t getPipeIndex( void ) const { return mPipeIndex; }
        bool hasPipe( void ) const { return mbHasPipe; }
        bool isConnected( void ) const { return mbConnected; }
    protected:
        Drm& mDrm;                          // Drm manager.
        drmModeConnector* mpConnector;      // Connector.
        uint32_t mCrtcId;                   // ID for this display's Crtc.
        uint32_t mPipeIndex;                // Index of the pipe (0:N)
        bool mbConnected:1;                 // True when the connector is connected and there are modes.
        bool mbHasPipe:1;                   // True when crtc/pipe have been specified.
    };

    // Display status.
    enum EStatus
    {
        UNKNOWN = 0,                    //< Hardware display status is unknown at start of day.
        SUSPENDED,                      //< Hardware display has been suspended (turned off).
        AVAILABLE,                      //< Hardware display is ready for frames.
        AVAILABLE_PENDING_START,        //< Hardware display is ready for frames but start is still pending.
    };

    // DrmDisplay custom frame types.
    enum EFrameType
    {
        eFT_BlankingFrame = DisplayQueue::Frame::eFT_CUSTOM + 1
    };

    // Options for a default frame following a modeset.
    enum EDefaultFrame
    {
        eDF_off  = 0,                   //< Never flip a default frame.
        eDF_On   = 1,                   //< Always flip a default frame.
        eDF_Auto = 2                    //< Flip depending on display caps *NOT IMPLEMENTED*
    };

    // Initialise this class to 'start-of-day'.
    void initialize( void );

    // Common initialisation code to startup a display with the specified connection.
    // Used by probe and hotplug
    // Initialises DisplayQueue and queues startup display.
    // Set bNew to true if updating the connection and to send a notification of
    // the display change to SF once the display has started.
    void startupDisplay( Connection& newConnection, bool bNew );

    // Returns true if a default frame should be flipped first following set display.
    bool defaultFrameRequired( void );

    // Set Drm display with current mode.
    // If defaultFrameRequired() returns true then this will also set an initial blanking frame.
    // Optionally, override the applied mode (e.g. to change refresh rate).
    // On return meStatus will be set to AVAILABLE.
    void setDisplay( int32_t overrideMode = -1 );

    // Reset Drm display (leaves display showing blanking).
    // On return meStatus will be set to SUSPENDED.
    void resetDisplay( void );

    // Set a new connection.
    // This will update display timings and initialize default mode.
    // This is normally called by the worker from consumeStartup().
    // Returns true if the new connection is OK.
    // If the new connection can not be set then the
    bool setNewConnection( Connection& newConnection );

    // Wait for all work to be consumed and sync HWC.
    // Used to synchronize plug/unplugs with SF.
    // Must be called from an external thread.
    void synchronizeEvent( void );

    // Wait for all work on other displays to be consumed.
    // Must be called from the consumer thread.
    // Frames queued up on *this* display will be invalidated.
    void synchronizeFromConsumer( void );

    // Updates the plug thread local modes list on a connection change.
    void setCurrentConnectionModes( drmModeConnector* pNewConnector );

    // Checks current connection modes list for any changes.
    // Will return false if modes are not consistent.
    bool checkCurrentConnectionModes( drmModeConnector* pNewConnector );

    // Consume event to startup display.
    // Set bNew to true if updating the connection and to send a notification of
    // the display change to SF once the display has started.
    // On return the display status will be AVAILABLE_PENDING_START
    // (the first real frame will complete start and transition to AVAILABLE).
    void consumeStartup( Connection& mNewConnection, bool bNew );

    // Recover Drm Display: to do DPMS_OFF and DPMS_ON, it's better to set mode again.
    void processRecovery( void );

    // Consume event to shutdown the display.
    // All frames created with indices up to and including timelineIndex will be released.
    void consumeShutdown( uint32_t timelineIndex );

    // Consume event to suspend a display.
    // Disable it and prevent its use until resume is called.
    // All frames created with indices up to and including timelineIndex will be released.
    // If bUseDPMS is true then DPMS will be used to put display into low power.
    // If bDeactivateDisplay is true then all resources (such as dbuf allocation) will be released.
    // On return the display status will be SUSPENDED.
    void consumeSuspend( uint32_t timelineIndex, bool bUseDPMS = true, bool bDeactivateDisplay = false );

    // Consume event to resume a display that was suspended.
    // If the display status was SUSPENDED, this will move to AVAILABLE.
    void consumeResume( void );

    // Consume flip work.
    void consumeFrame( DisplayQueue::Frame* pNewDisplayFrame );

    // Called before any work is consumed to process any deferred/pending work/state.
    void processPending( void );

    // Set display to show blanking.
    // This will program the display synchronously.
    // It is used by set/resetDisplay( ).
    void setBlanking( void );

    // Implements DisplayQueue::available()
    // Returns true only if the display is available (consuming frames).
    virtual bool available( void ) { return isAvailable(); }

    // Overrides default DisplayQueue implementation.
    // Check ready for event or frame.
    // Drm APIs are also currently constrained so that HWC must wait for the previous flip
    // to complete before trying to flip the next frame.
    // Also, process recovery to bring device back up if necessary.
    virtual bool readyForNextWork( void )
    {
        processRecovery();
        return !isAvailable( ) || mPageFlipHandler.readyForFlip( );
    }

    // Called from page flip handler to release the old frame when a new frame has been flipped.
    void releaseFlippedFrame( Frame* pOldFrame );

    // Implements DisplayQueue::syncFlip( ).
    // This is called from the DisplayQueue worker to ensure the most recent Drm flip has completed.
    virtual void syncFlip( void );

    // Implements DisplayQueue::getHwc( ).
    virtual Hwc& getHwc( void ) { return mHwc; }

    // Function to update our list of timings to the current connector.
    // Display timings lock MUST NOT be held on entry.
    void updateDisplayTimings( void );

    // Set vsyncs on/off.
    // This must be thread safe since it services both SF event
    // control requests received via onVSyncEnable() and internal
    // updates via startup/shutdown/suspend/resume events.
    void setVSync( bool bEnable );
    // Set vsyncs on/off.
    // The vsync lock must be held on entry,
    void doSetVSync( bool bEnable );

    // Convert global scaling to panel fitter mode.
    uint32_t globalScalingToPanelFitterMode( const SGlobalScalingConfig& config );

    // Overrides Display::setAppliedTiming( ).
    virtual void setAppliedTiming( uint32_t timingIndex );

    // Internal setmode implementation
    void doSetDisplayMode(uint32_t mode);

    // Get and apply the seamless mode if as required.
    // Returns true if there is a seamless update required.
    bool getSeamlessMode( drmModeModeInfo &modeInfoOut );
    void applySeamlessMode( const drmModeModeInfo &modeInfo );
    // Adapt the display mode if required with the specified fb.
    // We need to know the fb because setCrtc requires it.
    // This is called from the end of flip.
    void legacySeamlessAdaptMode( const Layer* pLayer );

    // Set new status.
    // Notify ready (potentially) on a status change.
    void setStatus( EStatus eStatus ) { meStatus = eStatus; notifyReady(); }

    Drm&                mDrm;                               // Drm manager.
    DrmPageFlipHandler  mPageFlipHandler;                   // Page flip handler for this display.
    String8             mName;                              // Name returned from getName() and mainly used for debug.
    Option              mOptionSelfTeardown;                // Options for teardown (combinations of ESelfTeardownOptions).
    Option              mOptionPanelFitterMigration;        // Options for enable Panel fitter Migration, default:disabled.

    static Option       mOptionDefaultFrame;                // Is a 'default' frame required following modeset? (See EDefaultFrame).

    // Immutable state esablished during open().
    uint32_t            mPossibleCrtcs;                     // Mask of valid Crtc/pipe indices for this display/connector.
    uint32_t            mDrmConnectorIndex;                 // Drm connector index [0:N].
    uint32_t            mDrmConnectorID;                    // Drm ID for this display's connector.
    uint32_t            mDrmConnectorType;                  // Connector type of this display's connector.
    bool                mbSeamlessDRRSSupported;            // Display supports seamless refresh rate changes.
    bool                mbDynamicModeSupport;               // Display supports variable timings (refresh).
    uint32_t            mPropPanelFitterMode;               // Drm property ID for panel fitter mode (-1 if not available).
    uint32_t            mPropPanelFitterSource;             // Drm property ID for panel fitter source size (-1 if not available).
    uint32_t            mPropDPMS;                          // Drm property ID for DPMS control.

    // Connection state that can change each time a connection is established.
    Connection          mCurrentConnection;                 // Current connection (most recent - as established by start/hotplug).
    Vector<drmModeModeInfo> mCurrentConnectionModes;        // Current connection modes (most recent - as established by start/hotplug).

    Connection          mActiveConnection;                  // Active connection (received and applied by worker).
    Vector<uint32_t>    mTimingToConnectorMode;             // LUT to convert from display timing index to Drm connector mode index.

    // Generic state.
    uint32_t            mDrmDisplay;                        // ID for this DrmDisplay instance (set by Drm manager).
    DrmDisplayCaps      mDrmCaps;                           // Augmented capabilities (stores generic DisplayCaps).
    EStatus             meStatus;                           // Current status.
    sp<GraphicBuffer>   mpBlankBuffer;                      // Blanking buffer used when main plane should be disabled.
    bool                mbBlankBufferPurged;                // Blanking buffer is succesfully purged.
    Layer               mBlankLayer;                        // Blanking layer used when main plane should be disabled.
    uint32_t            mBlankBufferFramesSinceLastUsed;    // Count of frames without use of blanking buffer/layer.
    int32_t             mDrmPanelFitterMode;                // Applied DRM panel fitter mode (-1 if not active).

    // DRRS and dynamic mode state.
    uint32_t            mFilterAppliedRefresh;              // Refresh mode established from frame
                                                            // (usually via a filter) and applied via DRRS or dynamic.
    uint32_t            mSeamlessRequestedRefresh;          // DRRS refresh rate Hz (requested - to be applied).
    uint32_t            mSeamlessAppliedRefresh;            // DRRS refresh rate Hz (applied).
    uint32_t            mDynamicAppliedTimingIndex;         // Dynamic refresh timing index (applied).

    Mutex               mSetVSyncLock;                      // Lock for setVSync.

    // Queue state.
    enum EQueueState
    {
        QUEUE_STATE_SHUTDOWN = 0,
        QUEUE_STATE_STARTED,
        QUEUE_STATE_SUSPENDED
    };
    Mutex               mSyncQueue;                         // Sync work being queued.
    EQueueState         meQueueState;                       // Track queue state (startup/shutdown/suspend/resume).

    // Flags.
    bool                mbSuspendDPMS:1;                    // Was DPMS used to put display into suspend?
    bool                mbSuspendDeactivated:1;             // Was display deactivated during suspend (releases all resources such as dbuf)?
    bool                mbScreenCtlOn:1;                    // Is the screen control state 'on'? (if screen control API is available).
    bool                mbDrmVsyncEnabled:1;                // Is vsync enabled via Drm for this display?
    bool                mbVSyncGenEnabled:1;                // Is vsync generation (sw or hw) enabled for this display?


    volatile int32_t mRecovering;                           // Is in recovery mode?

    Option mOptionNuclearModeset;
    // Technically only a pointer so we don't really need to #if this out....
#if VPG_DRM_HAVE_ATOMIC_NUCLEAR
    std::shared_ptr<DrmNuclearHelper> mpNuclearHelper;
#endif

    // *****************************************************************************
    // Display Queue
    // *****************************************************************************

    // Forward declare DrmDisplay specific events.
    class EventStartup;
    class EventShutdown;
    class EventSuspend;
    class EventResume;

    // Some queued work will necessarily trigger a mode set/reset.
    // We need to disable encrypted sessions before this occurs.
    void disableAllEncryptedSessions( void );

    // Queue state a string.
    String8 queueStateDump( void );

    // Queue startup display with the specified connection.
    // Set bNew to true if updating the connection and to send a notification of
    // the display change to SF once the display has started.
    // Returns OK (0) if display is started on return, negative on error.
    int queueStartup( const Connection& newConnection, bool bNew );

    // Queue shutdown display.
    // Returns OK (0) if display is shutdown on return, negative on error.
    int queueShutdown( void );

    // Queue suspend.
    // Returns OK (0) if display is suspended on return, negative on error.
    int queueSuspend( bool bUseDPMS, bool bDeactivateDisplay );

    // Queue resume.
    // Returns OK (0) if display is resumed on return, negative on error.
    int queueResume( void );

    // Queue frame.
    // Returns OK (0) if successful, negative on error.
    int queueFrame( const Content::Display& display, uint32_t zorder, int* pRetireFenceFd );

    // Implements DisplayQueue::consumeWork( ).
    // This is called from the DisplayQueue worker to issue flips and events.
    virtual void consumeWork( DisplayQueue::WorkItem* pWork );
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DRMDISPLAY_H
