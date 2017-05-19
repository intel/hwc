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

#ifndef INTEL_UFO_HWC_DISPLAY_MANAGER_H
#define INTEL_UFO_HWC_DISPLAY_MANAGER_H

#include "LogicalDisplay.h"
#include "AbstractFilter.h"
#include "AbstractDisplayManager.h"
#include "PhysicalDisplayManager.h"
#include "Option.h"

namespace intel {
namespace ufo {
namespace hwc {

class Hwc;
class LogicalDisplay;

// This class is a logical display manager.
// Only logical displays can be plugged to SF.
// Physical displays are marshalled by the display manager.
// The regular case is simple passthrough, but remapping to 0,1 or more physical displays is supported.
// When remapping is used, the display manager will add itself as a filter to remap from SF display
// space to physical display space (filtering is deferred to the indiviual logical display instances).
// Logical displays are constructed at run time from the "dmconfig" option using a factory pattern.
// Logical displays must implement their own create/filter/updateAvailability methods.
class LogicalDisplayManager : public AbstractDisplayManager,
                              public AbstractFilter,
                              public PhysicalDisplayNotificationReceiver
{
public:

    // LogicalDisplayManager config option ID.
    const char*                 CONFIG_OPTION_ID        = "dmconfig";
    // LogicalDisplayManager config - reserved strings.
    const char*                 TERMINATE_CONFIG_STRING = "TERM";       //< Do not process any configs beyond this point.

    // Some physical display state is maintained on eacn updateAvailability().
    class PhysicalState
    {
        public:
            PhysicalState() { reset(); }
            enum EFlags
            {
                FLAG_ACQUIRED  = (1<<0),        //< Physical has been acquired.
                FLAG_EXCLUSIVE = (1<<1)         //< Physical has been acquired exclusively.
            };
            void reset( void )
            {
                mFlags      = 0;
                mWidth      = 0;
                mHeight     = 0;
                mRefresh    = 0;
                mTimingIndex = -1;
                mOutSlot    = 0;
            }
            uint32_t    mFlags;                 //< Flags from EFlags.
            uint32_t    mWidth;                 //< If acquired, then this is the width.
            uint32_t    mHeight;                //< If acquired, then this is the height.
            uint32_t    mRefresh;               //< If acquired, then this is the refresh.
            int32_t     mTimingIndex;           //< If acquired, then this is the timing index.
            uint32_t    mOutSlot;               //< If acquired, which content slot to use.
    };

    // C'tor.
                                LogicalDisplayManager( Hwc& hwc, PhysicalDisplayManager& hardwareManager );

    // D'tor.
    virtual                    ~LogicalDisplayManager();

    LogicalDisplayManager(LogicalDisplayManager const&) = delete;
    LogicalDisplayManager& operator=(LogicalDisplayManager const&) = delete;

    // *************************************************************************
    // This class implements the AbstractFilter API.
    // *************************************************************************
    const char*                 getName() const { return "LogicalDisplayManagerFilter"; }
    bool                        outputsPhysicalDisplays() const { return true; }
    const Content&              onApply( const Content& ref );
    /* String8                  dump( ); */

    // *************************************************************************
    // This class implements the LogicalDisplayManager API.
    // This class implements **LOGICAL** displays.
    // *************************************************************************
    void                         open( void );
    status_t                     plugSurfaceFlingerDisplay( LogicalDisplay* pDisplay, uint32_t sfIndex, bool bTransitory = false );
    status_t                     unplugSurfaceFlingerDisplay( LogicalDisplay* pDisplay, bool bTransitory = false );
    LogicalDisplay*              getSurfaceFlingerDisplay( uint32_t sfIndex ) const;
    uint32_t                     getNumSurfaceFlingerDisplays( void ) const { return mSfPlugged; }
    void                         onVSyncEnable( uint32_t sfIndex, bool bEnableVSync );
    int                          onBlank( uint32_t sfIndex, bool bEnableBlank, BlankSource source );
    void                         flush( uint32_t frameIndex = 0, nsecs_t timeoutNs = AbstractDisplay::mTimeoutForFlush );
    void                         endOfFrame( void );
    String8                      dump( void );
    String8                      dumpDetail( void );


    // *************************************************************************
    // This class implements the PhysicalDisplayNotificationReceiver API.
    // *************************************************************************
    void                         notifyDisplayAvailable( AbstractPhysicalDisplay* pDisplay);
    void                         notifyDisplayUnavailable( AbstractPhysicalDisplay* pDisplay );
    void                         notifyDisplayChangeSize( AbstractPhysicalDisplay* pDisplay );
    void                         notifyDisplayVSync( AbstractPhysicalDisplay* pDisplay, nsecs_t timeStampNs );


    // Returns true if display is virtual/widi type.
    static bool                  isVirtualType( AbstractPhysicalDisplay* pPhysical );

    // Get some attributes from a display+timing.
    static void                  getAttributes( AbstractPhysicalDisplay* pPhysical, int32_t timingIndex,
                                                uint32_t& width, uint32_t& height, uint32_t& refresh,
                                                uint32_t& xdpi, uint32_t& ydpi );

    // Add a logical display.
    // On success, returns logical display index.
    // On failure (no space), return -1.
    int32_t                      addLogicalDisplay( LogicalDisplay* pLD );

    // Used by logical displays during updateAvailability( ) to find a suitable and available physical display device.
    // If index is -1 then will match first suitable - else will match the specified display.
    // If eIT is eITNotionalSurfaceFlinger, then index must be [0:cMaxSupportedSFDisplays-1] and will reference the
    //  display that *WOULD* be available to SurfaceFlinger if this logical display manager had *NOT* been present!
    // Else, if eIT is eITPhysical, then index must be [0:cMaxSupportedPhysicalDisplays-1] and will reference
    //  that by physical display space.
    // Match is made on available timings - if any/all of requiredWidth/Height/Refresh are 0 then this will match any.
    // If bRequiredExclusive is set then the display must be available exclusively (else can share - e.g. mux N:1).
    // If succesful, returns the physical display and
    //    1/ updates requiredWidth/Height/Refresh with the actual timing attributes.
    //    2/ updates matchedTimingIndex with the matched timing index or -1 if timing is already OK.
    AbstractPhysicalDisplay*    findAvailable( LogicalDisplay::EIndexType eIT,
                                               int32_t index,
                                               bool bRequiredExclusive,
                                               uint32_t& requiredWidth,
                                               uint32_t& requiredHeight,
                                               uint32_t& requiredRefresh,
                                               int32_t& matchedTimingIndex );

    // Used by logical displays during updateAvailability( ) to acquire a physical display device at a given size/refresh.
    void                        acquirePhysical( AbstractPhysicalDisplay* pPhysical,
                                                 bool bExclusive,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint32_t refresh = INTEL_HWC_DEFAULT_REFRESH_RATE,
                                                 int32_t timingIndex = 0 );


    // Once a logical display has acquired a physical display then use getPhysicalState to  access the established mappings/state.
    const PhysicalState&        getPhysicalState( uint32_t phyIndex ) const;

    // String from EIndexType.
    static const char*          indexTypeToString( LogicalDisplay::EIndexType eIT ) { return ( eIT == LogicalDisplay::eITPhysical ) ? "PhysIndex" : "~SFIndex"; }

private:

    // Constants.
    enum
    {
        REFLECT_CONFIG_CHANGE = ~0U,
    };

    // This class describes the current state of a logical display.
    // mpLogicalDisplay[] keeps persistent array of logical displays.
    // Logical displays may be plugged/unplugged dynamically using plugSurfaceFlingerDisplay/unplugSurfaceFlingerDisplay.
    // mDisplayState tracks plug status.
    class DisplayState
    {
    public:
                            DisplayState() : mpDisplay( NULL )      { }
                            ~DisplayState()                         { }
        bool                isAttached( void )                      { return ( mpDisplay != NULL ); }
        void                setDisplay( LogicalDisplay* pDisplay )  { mpDisplay = pDisplay; }
        LogicalDisplay*     getDisplay( void ) const                { return mpDisplay; }
    private:
        // Pointer to the logical display that this state is going to be applied to.
        // This may be null if no display is currently attached.
        LogicalDisplay*     mpDisplay;
    };

    Hwc&                        mHwc;                                                       //< Hwc context (ref).
    PhysicalDisplayManager&     mPhysicalDisplayManager;                                    //< Hardware manager (ref).

    Option                      mOptionConfig;                                              //< Option based configuration.

    DisplayState                mDisplayState[ cMaxSupportedSFDisplays ];                   //< Plugged displays (SF space).
    uint32_t                    mSfPlugged;                                                 //< Number of plugged displays.

    LogicalDisplay*             mpLogicalDisplay[ cMaxSupportedLogicalDisplays ];           //< Pool of logical displays.
    uint32_t                    mLogicalDisplays;                                           //< Count of logical displays.
    uint32_t                    mConfiguredDisplays;                                        //< Count of configured displays (excludes virtual and fallback).

    AbstractPhysicalDisplay*    mpFakePhysical;                                             //< Fake physical (if required).
    int32_t                     mFakeDisplay;                                               //< Logcal index of fake display or -1 if none.

    AbstractPhysicalDisplay*    mpVirtualDisplay;                                           //< Virtual physical.
    int32_t                     mVirtualDisplay;                                            //< Logical index of virtual display or -1 if none.

    uint32_t                    mAvailableLogical;                                          //< Set of available logical displays (BIT0=>Logical display 0).
    uint32_t                    mNumAvailableLogical;                                       //< Count of available logical displays.
    int32_t                     mLogicalToSurfaceFlinger[ cMaxSupportedLogicalDisplays ];   //< Mapping of logical display to specific SF slot.
    int32_t                     mSurfaceFlingerToLogical[ cMaxSupportedSFDisplays ];        //< Reverse mapping of logical display to specific SF slot.

    Content                     mFilterOut;                                                 //< Filter contents (if not passthrough+1:1).
    Vector<LogicalDisplay::FilterDisplayState>  maFilterDisplayState;                       //< Filter contents for each display.

    PhysicalState               mPhysicalState[ cMaxSupportedPhysicalDisplays ];            //< Physical state locked down during updateAvailability.
    uint32_t                    mNumAcquiredPhysical;                                       //< Total physical displays acquired during updateAvailability.

    uint32_t                    mPrimaryWidth;                                              //< Primary size width pixels (immutable).
    uint32_t                    mPrimaryHeight;                                             //< Primary size height pixels (immutable).

    Mutex                       mPhysicalNotificationLock;                                  //< Lock for physical display notification state.

    bool                        mbPassthrough:1;                                            //< Pass-through mode (no complex mappings).
    bool                        mbOneToOne:1;                                               //< SF order matches physical order.
    bool                        mbFilterActive:1;                                           //< Is the LogicalDisplayManager filter active?
    bool                        mbGeometryChange:1;                                         //< One-shot force geometry change.

    // State relating to physical display changes.
    uint32_t                    mAvailablePhysical;                                         //< Set of available physical displays.
    AbstractPhysicalDisplay*    mpSurfaceFlingerToPhysical[ cMaxSupportedSFDisplays ];      //< Map of notional SurfaceFlinger slot (from physical display manager notifications).

    bool                        mbDirtyConfig:1;                                            //< Config is modifed.
    bool                        mbInConfigChange:1;                                         //< Processing config change (unplug/plug).
    bool                        mbDirtyPhys:1;                                              //< Physical display change.

    // Create fake passthrough display.
    void        createFakeDisplay( void );

    // One-time creation of logical displays.
    void        createLogical( void );

    // Destroy logical displays.
    void        destroyLogical( void );

    // Removes all available,
    void        resetAvailableLogical( void );

    // Set availability of a logical display - plus set maps
    // (mLogicalToSurfaceFlinger/mSurfaceFlingertoLogical).
    void        setAvailableLogical( uint32_t sfIndex, uint32_t logical );

    // Get the set of availabile logical displays (BIT0=>L0).
    uint32_t    getAvailableLogical( void ) { return mAvailableLogical; }

    // Get number of available logical displays.
    uint32_t    getNumAvailableLogical( void ) { return mNumAvailableLogical; }

    // Get availability of a logical display (to SF).
    bool        isLogicalAvailable( uint32_t index ) const { return mAvailableLogical & (1<<index); }

    // Get availability of a physical display (to LogicalDisplayManager).
    bool        isPhysicalAvailable( uint32_t index ) const { return mAvailablePhysical & (1<<index); }

    // Helper for findAvailable() to check a specific physical display availability (with specified timing attributes).
    bool        checkPhysicalAvailable( AbstractPhysicalDisplay* pPhysicalDisplay,
                                        bool bRequiredExclusive,
                                        uint32_t& requiredWidth,
                                        uint32_t& requiredHeight,
                                        uint32_t& requiredRefresh,
                                        int32_t& matchedTimingIndex );

    // Searches for required timing.
    // Match is made on available timings - if any/all of requiredWidth/Height/Refresh are 0 then this will match any.
    // If succesful, returns the timing index (>=0) and updates requiredWidth/Height/Refresh with the actual attributes.
    int32_t     checkTimingAvailable( AbstractPhysicalDisplay* pPhysical,
                                      uint32_t& requiredWidth,
                                      uint32_t& requiredHeight,
                                      uint32_t& requiredRefresh );

    // Which logical displays are available (dependent on which physical displays are currently plugged).
    // On return, mAvailable will indicate which of the logical displays should be added to SF
    // and the maps logicalToSurfaceFlinger/surfaceFlingertoLogical will be updated.
    void        updateAvailability( void );

    // Process any changes (plug/unplug/size) received in this frame.
    void        flushPhysicalDisplayChanges( void );

    // Reflect physical display plug/unplug changes and config changes.
    // This method calls updateAvailability() to update LogicalDisplays and work out availability and mapping.
    // It then calls Hwc::notifyHotPlug/UnplugLogical() to plug/unplug logical display with SF.
    // (this will trigger the callback to add/remove the logical display).
    void        reflectChanges( void );
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DISPLAY_MANAGER_H
