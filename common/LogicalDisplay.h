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

#ifndef INTEL_UFO_HWC_LOGICAL_DISPLAY_H
#define INTEL_UFO_HWC_LOGICAL_DISPLAY_H

#include "AbstractDisplayManager.h"
#include <vector>
namespace intel {
namespace ufo {
namespace hwc {

class Hwc;
class LogicalDisplayManager;
class PhysicalDisplayManager;
class AbstractPhysicalDisplay;

// Logical display base class.
// Displays that are created for LogicalDisplayManager must derive from this class.
class LogicalDisplay : public AbstractDisplay
{
public:

    // When logical displays reference a specific physical display, it can use
    // and index that is absolute -- or -- a notional surface flinger index
    // (i.e. the display that *WOULD* be available to SurfaceFlinger if this logical
    // display manager had *NOT* been present!).
    enum EIndexType
    {
        eITPhysical,                            //< Physical index.
        eITNotionalSurfaceFlinger               //< Physical display in requested SurfaceFlinger slot.
    };

    enum ELogicalType
    {
        LOGICAL_TYPE_PASSTHROUGH,
        LOGICAL_TYPE_MOSAIC
    };

    // Class used per-display for display manager filters.
    class FilterDisplayState
    {
    public:
        uint32_t                mNumLayers;     //< Count of (accumulated) layers.
        std::vector<Layer>      mLayers;        //< Filter contents for each display.
    };

    class Factory
    {
    public:
        Factory() { }

        virtual ~Factory() { }

        virtual LogicalDisplay* create( const char* pchConfig,
                                    Hwc& hwc,
                                    LogicalDisplayManager& ldm,
                                    PhysicalDisplayManager& pdm,
                                    const int32_t sfIndex,
                                    EIndexType eIndexType,
                                    int32_t phyIndex,
                                    EDisplayType eType = eDTPanel);

    };

    LogicalDisplay( Hwc& hwc, LogicalDisplayManager& ldm, PhysicalDisplayManager& pdm, ELogicalType eType );

    virtual ~LogicalDisplay();

    ELogicalType        getLogicalType( void ) const;
    void                setTag( const char* pchTag );
    const               String8& getTag( void ) const;
    const char*         getTagStr( void ) const;

    // Specify the size that will be reported to SurfaceFlinger.
    // This must be updated during updateAvailability.
    void                setSize( uint32_t w, uint32_t h );

    // Get the size that will be reported to SurfaceFlinger.
    uint32_t            getSizeWidth( void ) const;
    uint32_t            getSizeHeight( void ) const;

    // Check/update availability (e.g. can this logical displays requirements be satisfied?)
    // If this logical display is available then make it available and return true.
    // If enforceWidth/Height are specified then these override the logical displays size and must be satisfied.
    // If available, then physical displays must be acquired and this logical display
    //  must at least update setSize().
    virtual bool        updateAvailability( LogicalDisplayManager& ldm, uint32_t sfIndex, uint32_t enforceWidth = 0, uint32_t enforceHeight = 0 ) = 0;

    // Filter processing for this logical display.
    virtual void        filter( const LogicalDisplayManager& ldm,
                                const Content::Display& sfDisplay, Content& out,
                                Vector<FilterDisplayState> &displayState,
                                bool bUpdateGeometry ) = 0;

    // Called when a vsync event is generated for the specified physical display.
    // The logical display must forward to Hwc as necessary.
    virtual void        notifyDisplayVSync( uint32_t phyIndex, nsecs_t timeStampNs ) = 0;

    // *************************************************************************
    // Mux some LogicalDisplayManager APIs through the logical display itself.
    // *************************************************************************
    virtual void        onVSyncEnableDm( uint32_t sfIndex, bool bEnableVSync ) = 0;
    virtual int         onBlankDm( uint32_t sfIndex, bool bEnableBlank, AbstractDisplayManager::BlankSource source ) = 0;

    virtual void  setPhysical( AbstractPhysicalDisplay* pPhysical ) = 0 ;
    virtual AbstractPhysicalDisplay*    getPhysical( void ) const=0;

    // *************************************************************************
    // This class implements these LogicalDisplay APIs.
    // *************************************************************************
    virtual void        setSurfaceFlingerIndex( uint32_t sfIndex );
    virtual uint32_t    getSurfaceFlingerIndex( void ) const;
    virtual bool        isPluggedToSurfaceFlinger( void ) const;
    virtual void        setDisplayManagerIndex( uint32_t dmIndex );
    virtual uint32_t    getDisplayManagerIndex( void ) const;
    static void add(Factory* instance);
    static void remove(Factory* instance);
    static LogicalDisplay* instantiate( const char* pchConfig,
                                        Hwc& hwc,
                                        LogicalDisplayManager& ldm,
                                        PhysicalDisplayManager& pdm,
                                        const int32_t sfIndex,
                                        EIndexType eIndexType,
                                        int32_t phyIndex,
                                        EDisplayType eType = eDTPanel);
protected:
    Hwc&                        mHwc;
    LogicalDisplayManager&      mLogicalDisplayManager;
    PhysicalDisplayManager&     mPhysicalDisplayManager;
    uint32_t                    mSfIndex;
    uint32_t                    mDmIndex;
    ELogicalType                meLogicalType;
    uint32_t                    mSizeWidth;
    uint32_t                    mSizeHeight;
    String8                     mTag;
    bool                        mbProxyOnly:1;                  // Display is set as available for primary proxy only.
    static std::vector<Factory*> sLogicalDisplayVec;
};

// Callback class to receive notification of a change.
// The PhysicalDisplayManager forwards notifications of display changes to a receiver.
// The Hwc itself can be the receiver, in which case it will just complete plug to SurfaceFlinger.
// Or, a logical LogicalDisplayManager can be inserted between the PhysicalDisplayManager and Hwc to marshall displays.
class LogicalDisplayNotificationReceiver
{
public:
    virtual         ~LogicalDisplayNotificationReceiver( )  { }

    // This must be called when a display becomes available.
    // If all slots are already taken, or, if this display is available but should not be
    // plugged to SurfaceFlinger, then sfIndex can be INVALID_DISPLAY_ID.
    // The display mmay end up proxied (plugged as primary).
    // If bPrimaryProxyOnly is true then the display will be considered for primary proxy only.
    // The receiver must call display manager plugSurfaceFlingerDisplay( ) to finalize plug
    //  if it is plugged to SurfaceFlinger.
    virtual void    notifyDisplayAvailable( LogicalDisplay* pDisplay, uint32_t sfIndex ) = 0;

    // This must be called when a display is no longer available.
    // The receiver must call display manager unplugSurfaceFlingerDisplay( ) to finalize unplug
    //  if it is unplugged from SurfaceFlinger.
    virtual void    notifyDisplayUnavailable( LogicalDisplay* pDisplay ) = 0;

    // This must be called when a display wants to change its size.
    virtual void    notifyDisplayChangeSize( LogicalDisplay* pDisplay ) = 0;

    // This must be called when a display generates a VSync event.
    virtual void    notifyDisplayVSync( LogicalDisplay* pDisplay, nsecs_t timeStampNs ) = 0;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_LOGICAL_DISPLAY_H
