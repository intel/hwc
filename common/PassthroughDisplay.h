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

#ifndef INTEL_UFO_HWC_PASSTHROUGH_DISPLAY_H
#define INTEL_UFO_HWC_PASSTHROUGH_DISPLAY_H

#include "LogicalDisplay.h"

namespace intel {
namespace ufo {
namespace hwc {

class AbstractPhysicalDisplay;

// This class implements a passthrough logical display.
// All calls are forwarded directly to the attached physical display/PhysicalDisplayManager.
// All content is mirrored to the physical display replacing any other content.
//  i.e. this display required exclusivity of the physical display.
class PassthroughDisplay : public LogicalDisplay
{
public:
    // Configuration used when creating this logical display.
    class LogicalConfig
    {
    public:
        LogicalConfig( int32_t sfIndex, EIndexType eIndexType,
                        int32_t phyIndex, uint32_t width, uint32_t height, uint32_t refresh ) :
            mSfIndex( sfIndex ),
            meIndexType( eIndexType ),
            mPhyIndex( phyIndex ),
            mWidth( width ),
            mHeight( height ),
            mRefresh( refresh )
        {
        }

        int32_t  mSfIndex;
        EIndexType meIndexType;
        int32_t  mPhyIndex;
        uint32_t mWidth;
        uint32_t mHeight;
        uint32_t mRefresh;
    };

                                PassthroughDisplay( Hwc& hwc,
                                                    LogicalDisplayManager& ldm,
                                                    PhysicalDisplayManager& pdm,
                                                    const LogicalConfig& config);
    virtual                    ~PassthroughDisplay( );
    void                        setPhysical( AbstractPhysicalDisplay* pPhysical );
    AbstractPhysicalDisplay*    getPhysical( void ) const;

    // *************************************************************************
    // This class implements the LogicalDisplay API
    // *************************************************************************
    virtual bool                updateAvailability( LogicalDisplayManager& ldm, uint32_t sfIndex,
                                                uint32_t enforceWidth = 0, uint32_t enforceHeight = 0 );
    virtual void                filter( const LogicalDisplayManager& ldm,
                                        const Content::Display& sfDisplay, Content& out,
                                        Vector<FilterDisplayState> &displayState,
                                        bool bUpdateGeometry );
    virtual void                notifyDisplayVSync( uint32_t phyIndex, nsecs_t timeStampNs );

    // *************************************************************************
    // Mux some AbstractDisplayManager APIs through the logical display itself.
    // *************************************************************************
    void                        onVSyncEnableDm( uint32_t display, bool bEnableVSync );
    int                         onBlankDm( uint32_t display, bool bEnableBlank, AbstractDisplayManager::BlankSource source );

    // *************************************************************************
    // This class implements these AbstractDisplay APIs.
    // *************************************************************************
    virtual const char*         getName( void ) const;
    virtual int                 onGetDisplayConfigs( uint32_t* paConfigHandles, uint32_t* pNumConfigs ) const;
    virtual int                 onGetDisplayAttribute( uint32_t configHandle, EAttribute attribute, int32_t* pValue ) const;
    virtual int                 onGetActiveConfig( void ) const;
    virtual int                 onSetActiveConfig( uint32_t configIndex );
    virtual int                 onVSyncEnable( bool bEnable );
    virtual int                 onBlank( bool bEnable, bool bIsSurfaceFlinger = false );
    virtual void                dropAllFrames( void );
    virtual void                flush( uint32_t frameIndex = 0, nsecs_t timeoutNs = mTimeoutForFlush );
    virtual const               DisplayCaps& getDisplayCaps( ) const;
    virtual int32_t             getDefaultOutputFormat( void ) const;
    virtual bool                getTiming( Timing& ) const;
    virtual uint32_t            getRefresh( void ) const;
    virtual EDisplayType        getDisplayType( void ) const;
    virtual uint32_t            getWidth() const;
    virtual uint32_t            getHeight() const;
    virtual int32_t             getXdpi() const;
    virtual int32_t             getYdpi() const;
    virtual void                copyDisplayTimings( Vector<Timing>& timings ) const;
    virtual void                copyDefaultDisplayTiming( Timing& timing ) const;
    virtual bool                setDisplayTiming( const Timing& timing, bool bSynchronize, Timing* pResultantTiming );
    virtual void                setUserOverscan( int32_t xoverscan, int32_t yoverscan );
    virtual void                getUserOverscan( int32_t& xoverscan, int32_t& yoverscan ) const;
    virtual void                setUserScalingMode( EScalingMode scaling );
    virtual void                getUserScalingMode( EScalingMode& eScaling ) const;
    virtual bool                setUserDisplayTiming( const Timing& timing, bool bSynchronize = true );
    virtual bool                getUserDisplayTiming( Timing& timing ) const;
    virtual void                resetUserDisplayTiming( void );
    virtual String8             dump( void ) const;
protected:
    LogicalConfig               mConfig;            //< Config for this logical display.
    uint32_t                    mPhysicalIndex;     //< Physical ID of currently set physical display.
    AbstractPhysicalDisplay*    mpPhysical;         //< Currently set physical display.
};

class PassthroughDisplayFactory : public LogicalDisplay::Factory
{
public:
    PassthroughDisplayFactory();
    ~PassthroughDisplayFactory();
    virtual LogicalDisplay* create( const char* pchConfig,
                                        Hwc& hwc,
                                        LogicalDisplayManager& ldm,
                                        PhysicalDisplayManager& pdm,
                                        const int32_t sfIndex,
                                        LogicalDisplay::EIndexType eIndexType,
                                        int32_t phyIndex,
                                        EDisplayType eType = eDTPanel);
    PassthroughDisplay::LogicalConfig* getConfig(const int32_t sfIndex, const char* pchConfig);
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_PASSTHROUGH_DISPLAY_H
