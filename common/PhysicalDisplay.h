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

#ifndef INTEL_UFO_HWC_PHYSICALDISPLAY_H
#define INTEL_UFO_HWC_PHYSICALDISPLAY_H

#include "Hwc.h"
#include "AbstractPhysicalDisplay.h"
#include <utils/StrongPointer.h>
#include <utils/Mutex.h>
#include <utils/Condition.h>
#include "HwcServiceApi.h"
#include "Transform.h"
#include "Option.h"

namespace intel {
namespace ufo {
namespace hwc {

class AbstractComposer;
class SoftwareVsyncThread;
class DisplayCaps;

#define AUTOLOCK_DISPLAY Display::AutoLock _autolock_display( this )

class PhysicalDisplay : public AbstractPhysicalDisplay, NonCopyable
{
public:

    typedef EHwcsScalingMode EScalingMode;

    static const uint32_t UnknownDisplayTiming = 0xffffffff;

    enum
    {
        CONFIG_HANDLE_RSVD_ACTIVE_CONFIG  = 0x0,                // Handle zero reserved to represent the 'current active config'.
        CONFIG_HANDLE_BASE                = 0x12340000          // Initial config handle of the first returned config for a device.
    };

    PhysicalDisplay(Hwc& hwc);
    virtual ~PhysicalDisplay();

    // *************************************************************************
    // This class implements these AbstractDisplay APIs.
    // *************************************************************************
    virtual const char*         getName( void ) const;
    virtual void                setProxyOnly( bool bProxyOnly );
    virtual bool                getProxyOnly( void ) const;
    virtual int                 onGetDisplayConfigs( uint32_t* paConfigHandles, uint32_t* pNumConfigs ) const;
    virtual int                 onGetDisplayAttribute( uint32_t configHandle, EAttribute attribute, int32_t* pValue ) const;
    virtual int                 onGetActiveConfig( void ) const;
    virtual int                 onSetActiveConfig( uint32_t configIndex );
    virtual int                 onVSyncEnable( bool bEnable );
    virtual int                 onBlank( bool /* bEnable */, bool /* bIsSurfaceFlinger */ ) { return OK; }
    virtual void                dropAllFrames( void ) { };
    virtual void                flush( uint32_t /* frameIndex */, nsecs_t /* timeoutNs */ ) { };
    virtual const DisplayCaps&  getDisplayCaps( void ) const { ALOG_ASSERT(mpDisplayCaps); return *mpDisplayCaps; }
    virtual int32_t             getDefaultOutputFormat( void ) const;
    virtual bool                getTiming( Timing& timing ) const           { return copyDisplayTiming( mNotifiedTimingIndex, timing ); }
    virtual uint32_t            getRefresh( void ) const                    { return getNotifiedRefresh( ); }
    uint32_t                    getWidth( void ) const                      { return getNotifiedWidth(); }
    uint32_t                    getHeight( void ) const                     { return getNotifiedHeight(); }
    int32_t                     getXdpi( void ) const                       { return getNotifiedXdpi(); }
    int32_t                     getYdpi( void ) const                       { return getNotifiedYdpi(); }
    uint32_t                    getVSyncPeriod( void ) const                { return getNotifiedVSyncPeriod(); }
    Timing::EAspectRatio        getRatio( void ) const                      { return getNotifiedRatio(); }
    virtual EDisplayType        getDisplayType( void ) const                { return meDisplayType; }
    void                        setDisplayManagerIndex( uint32_t dmIndex )  { mDmIndex = dmIndex; }
    uint32_t                    getDisplayManagerIndex( void ) const        { return mDmIndex; }
    virtual void                copyDisplayTimings( Vector<Timing>& timings ) const;
    virtual void                copyDefaultDisplayTiming( Timing& timing ) const;
    virtual bool                setDisplayTiming( const Timing& timing, bool bSynchronize = true, Timing* pResultantTiming = NULL );
    virtual void                setUserOverscan( int32_t xoverscan, int32_t yoverscan );
    virtual void                getUserOverscan( int32_t& xoverscan, int32_t& yoverscan ) const;
    virtual void                setUserScalingMode( EScalingMode eScaling );
    virtual void                getUserScalingMode( EScalingMode& eScaling ) const;
    virtual bool                setUserDisplayTiming( const Timing& timing, bool bSynchronize = true );
    virtual bool                getUserDisplayTiming( Timing& timing ) const;
    virtual void                resetUserDisplayTiming( void );
    virtual String8             dump( void ) const;

    // *************************************************************************
    // This class implements these AbstractPhysicalDisplay APIs.
    // *************************************************************************
    // Extended display timings APIs (lock MUST NOT be held on entry).
    virtual void                setDisplayTimings( Vector<Timing>& timings );
    virtual void                notifyTimingsModified( void );
    virtual uint32_t            getTimingIndex( void ) const { return mNotifiedTimingIndex; }
    virtual bool                copyDisplayTiming( uint32_t timingIndex, Timing& timing ) const;
    virtual int32_t             getXdpiForTiming( const Timing& t ) const;
    virtual int32_t             getYdpiForTiming( const Timing& t ) const;
    virtual int32_t             getDefaultDisplayTiming( void ) const;
    virtual int32_t             findDisplayTiming( const Timing& timing,
                                                   uint32_t findFlags = FIND_MODE_FLAG_FALLBACK_TO_DEFAULT ) const;
    virtual bool                setSpecificDisplayTiming( uint32_t timingIndex, bool bSynchronize = true );
    virtual bool                acquireGlobalScaling( uint32_t /*srcW*/, uint32_t /*srcH*/,
                                                      int32_t  /*dstX*/, int32_t  /*dstY*/,
                                                      uint32_t /*dstW*/, uint32_t /*dstH*/ ) { return false; }
    virtual bool                releaseGlobalScaling( void ) { return false; }
    virtual void                updateOutputFormat( int32_t /*format*/ )    { /*NOP*/ }
    virtual void                postSoftwareVSync( void ) { }
    virtual void                reconnect( void ) { }
    virtual bool                notifyNumActiveDisplays( uint32_t active );

    // *************************************************************************

    void setDisplayType( EDisplayType eDT ) { meDisplayType = eDT; }

    // Global scaling configuration.
    // This state describes global scaling for the display.
    // It is up to the derived display class to implement the support (if any).
    struct SGlobalScalingConfig
    {
        uint32_t mSrcW;                                         // Source co-ordinate system width.
        uint32_t mSrcH;                                         // Source co-ordinate system height.
        int32_t  mDstX;                                         // Destination frame X position.
        int32_t  mDstY;                                         // Destination frame Y position.
        uint32_t mDstW;                                         // Destination frame width.
        uint32_t mDstH;                                         // Destination frame height.
        bool     mbEnabled:1;                                   // Enabled?
    };

    // Get width of applied mode
    uint32_t            getAppliedWidth( void  ) const;

    // Get height of applied mode
    uint32_t            getAppliedHeight( void ) const;

protected:

    // Initialise the internal options variable. We need a valid index to be provided by the derived class
    void initializeOptions(const char* prefix, uint32_t optionIndex);


    // Get requested timing index.
    uint32_t            getRequestedTimingIndex( void ) const { return mRequestedTimingIndex; }

    // Get notified timing index.
    uint32_t            getNotifiedTimingIndex( void ) const { return mNotifiedTimingIndex; }

    // Get applied timing index.
    uint32_t            getAppliedTimingIndex( void ) const { return mAppliedTimingIndex; }

    // Get refresh of notified mode..
    uint32_t            getNotifiedRefresh( void ) const;

    // Get width of notified mode
    uint32_t            getNotifiedWidth( void  ) const;

    // Get height of notified mode
    uint32_t            getNotifiedHeight( void ) const;

    // Get Xdpi of notified mode
    int32_t             getNotifiedXdpi( void ) const;

    // Get Ydpi of notified mode
    int32_t             getNotifiedYdpi( void ) const;

    // Get VSync period of notified mode
    uint32_t            getNotifiedVSyncPeriod( void ) const;

    // Get ratio of notified mode
    Timing::EAspectRatio getNotifiedRatio( void ) const;

    // Get a default dpi given this display's type.
    int32_t             getDefaultDpi( void ) const;

    // Convert a refresh rate in Hz to period in nanoseconds.
    int32_t             convertRefreshRateToPeriodNs( uint32_t refreshRate ) const { return 1000000000 / ( refreshRate ? refreshRate : INTEL_HWC_DEFAULT_REFRESH_RATE ); }

    // Call this to modify vsync period.
    void                setVSyncPeriod( uint32_t vsyncPeriod );

    // Initialize state relating to setUser*** APIs (persistent display timing, overscan, scaling mode.)
    // On return, the global scaling filter will be configured and mUserTiming will be set.
    void                initUserConfig( void );

    // Derived display class must register display caps.
    void                registerDisplayCaps( const DisplayCaps* pCaps ) { ALOG_ASSERT( pCaps ); mpDisplayCaps = pCaps; }
    // Derived display class can retrieve registered display caps.
    // This will return NULL caps have not been registered yet.
    const DisplayCaps*  getRegisteredDisplayCaps( void ) const { return mpDisplayCaps; }

    // Enable virtual resolution.
    // Derived display classes should use virtualResTransform( ) to pre-transform all layer display frames.
    // The only use-case for virtual resolution so far is global scaling - a derived
    // display class must use this if it supports global scaling.
    void                enableVirtualResolution( uint32_t virtW, uint32_t virtH,
                                             float preOffX = 0.0f, float preOffY = 0.0f,
                                             float scaleX = 1.0f, float scaleY = 1.0f );
    // Disable virtual resolution.
    void                disableVirtualResolution( void );

    // Create SW vsync thread.
    // createSoftwareVSyncGeneration() must be used first before enable/disableSoftwareVSyncGeneration().
    void                createSoftwareVSyncGeneration( void );

    // Destroy SW vsync thread.
    void                destroySoftwareVSyncGeneration( void );

    // Enable generation of SW vsyncs from this display device.
    // createSoftwareVSyncGeneration() must be used first before enable/disableSoftwareVSyncGeneration().
    void                enableSoftwareVSyncGeneration( void );

    // Disable generation of SW vsyncs from this display device.
    // createSoftwareVSyncGeneration() must be used first before enable/disableSoftwareVSyncGeneration().
    void                disableSoftwareVSyncGeneration( void );

    // If the display timings are dynamic then this function can be called to update the timings appropriately.
    /// Display timings lock MUST NOT be held on entry.
    void                processDynamicDisplayTimings( void );

    // Displays must apply timings using setAppliedTiming.
    // This method may be overridden to update display-specific state
    // but the base class method must still be called.
    // Display timings lock MUST NOT be held on entry.
    virtual void        setAppliedTiming( uint32_t timingIndex );

    // Helper to "do" setAppliedTiming() with timing lock already held.
    void                doSetAppliedTiming( uint32_t timingIndex );

    // Called to set requested timing.
    // Display timings lock MUST NOT be held on entry.
    void                setRequestedTiming( uint32_t timingIndex );

    // Cancel pending requested mode.
    void                cancelRequestedTiming( void );

    // The display implementation should call this to forward a requested timing via a notification.
    // The display implementation is expected to call setAppliedTiming() once frames corresponding to the timing change are received.
    // Display timings lock MUST NOT be held on entry.
    void                notifyNewRequestedTiming( void );

    // Returns true and the timing index if notifyTiming() has been called but the timing has not yet been applied.
    // The display implementation is expected to call setAppliedTiming() once frames corresponding to the timing change are received.
    bool                haveNotifiedTimingChange( uint32_t& timingIndex );

    // Set initial timing at connect startup.
    // This aligns all requested, notified and applied.
    // Initial frames must be delivered at this size.
    // Display timings lock MUST NOT be held on entry.
    void                setInitialTiming( uint32_t timingIndex );

    // Called when a display timing has been changed.
    void                notifyDisplayTimingChange( const Timing& t );

    // Call when the display is now available for frames.
    void                notifyAvailable( void );

    // Call when the display is no longer available for frames.
    void                notifyUnavailable( void );

    // User config describing configuration adjustments made via the HwcService SetUser**** APIs.
    class UserConfig
    {
    public:
        // mode<X> defines the secondary display mode in one of these formats
        // 1280x720@60 or 1920x1080. 0 is considered to match anything
        Option  mMode;

        // scalingmode<X> defines the primary scaling mode in one of these formats
        // 0 : HWCS_SCALE_CENTRE
        // 1 : HWCS_SCALE_STRETCH
        // 2 : HWCS_SCALE_FIT
        // 3 : HWCS_SCALE_FILL
        Option  mScalingMode;

        /// Overscan in the range +/-IDisplayOverscanControl::MAX_OVERSCAN inclusive
        //  (which set under/overscan upto IDisplayOverscanControl::RANGE percent)
        // -ve : zoom/crop the image  (increase display overscan).
        // +ve : shrink the image (decrease display overscan).
        Option  mOverscan;
    };

    Hwc&                        mHwc;                           //< Hardware composer
    PhysicalDisplayManager&     mPhysicalDisplayManager;        //< Physical display manager
    uint32_t                    mSfIndex;                       //< Current display index (or INVALID_DISPLAY_ID if detached).
    uint32_t                    mDmIndex;                       //< Display manager index (Hardware Manager registration index).
    EDisplayType                meDisplayType;                  //< The display type.
    sp<SoftwareVsyncThread>     mpSoftwareVsyncThread;

    uint32_t                    mVsyncPeriod;                   // The vsync period in nanoseconds.
    uint32_t                    mAppliedTimingIndex;            // Index of most recent applied mode.
    uint32_t                    mRequestedTimingIndex;          // Index of most recent requested mode.
    uint32_t                    mNotifiedTimingIndex;           // Index of most recent mode forwarded as a notification to SF.
    bool                        mbRequestedTiming:1;            // A request has been made for timing change (requires notifying).
    bool                        mbNotifiedTiming:1;             // A requested timing change has been notified (requires applying).
    mutable Mutex               mTimingLock;                    // Thread protection on timing index transitions (requested, notified).

    // Attribute info - must be initialised by derived class
    uint32_t                    mWidthmm;                       // Width of display in mm
    uint32_t                    mHeightmm;                      // Height of display in mm

    Vector<Timing>              mDisplayTimings;                // List of timings the display can support
    mutable Mutex               mDisplayTimingsLock;            // Lock to access display timings/configs.

    UserConfig                  mUserConfig;                    // User config (HwcService SetUser**** APIs).

    bool                        mbSoftwareVSyncEnabled:1;       // Is software vsync event generation currently enabled for this display?
    bool                        mbRegisterWithHwc:1;            // Display should be plugged.
    bool                        mbNotifiedAvailable:1;          // Display has been made available to Hwc.
    bool                        mbProxyOnly:1;                  // Display is set as available for primary proxy only.

    struct SGlobalScalingConfig mGlobalScalingRequested;        // New global scaling state.
    struct SGlobalScalingConfig mGlobalScalingActive;           // Active global scaling state.

private:
    const DisplayCaps*          mpDisplayCaps;                  // Capabilities for this display. After initialisation, it will never be NULL.

    int32_t                     mUserTimingIndex;               // Index of user timing mode (which is also used as default).
    Timing                      mUserTiming;                    // Most recently succesfully requested user timing.
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_PHYSICALDISPLAY_H
