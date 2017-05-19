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

#include "PassthroughDisplay.h"
#include "Hwc.h"
#include "Log.h"

namespace intel {
namespace ufo {
namespace hwc {

//Factory Instance
PassthroughDisplayFactory gPassthroughDisplayFactory;

PassthroughDisplayFactory::PassthroughDisplayFactory()
{
    LogicalDisplay::add(this);
}

PassthroughDisplayFactory::~PassthroughDisplayFactory()
{
    LogicalDisplay::remove(this);
}

LogicalDisplay* PassthroughDisplayFactory::create( const char* pchConfig,
                                        Hwc& hwc,
                                        LogicalDisplayManager& ldm,
                                        PhysicalDisplayManager& pdm,
                                        const int32_t sfIndex,
                                        LogicalDisplay::EIndexType eIndexType,
                                        int32_t phyIndex,
                                        EDisplayType eType )
{
    HWC_UNUSED( eType );
    PassthroughDisplay* pLDPT = NULL;
    if(strlen(pchConfig) >11 && strstr(pchConfig, "PASSTHROUGH") != NULL)
    {
        PassthroughDisplay::LogicalConfig* config = NULL;
        config = getConfig(sfIndex, pchConfig);
        if(config != NULL)
        {
            pLDPT = new PassthroughDisplay( hwc, ldm, pdm, *config );
            pLDPT->setTag( "REAL" );
        }
    }
    else if(strcmp(pchConfig, "PASSTHROUGH") == 0)
    {
        PassthroughDisplay::LogicalConfig config( sfIndex, eIndexType, phyIndex, 0, 0, 0 );
        pLDPT = new PassthroughDisplay( hwc, ldm, pdm, config );
        pLDPT->setTag( "REAL" );
    }
    return pLDPT;
}

PassthroughDisplay::LogicalConfig* PassthroughDisplayFactory::getConfig(const int32_t sfIndex, const char* pchConfig )
{
    // Format for DISPLAY is:
    //   {IT}P:{P} {PW}x{{PH}
    // Where
    //   IT  : Index type (SF=>Notional SurfaceFlinger, P=>Physical)
    //    P  : Display index (-1 => find first unused match).
    //   PW  : Physical display timing pixel width
    //   PH  : Physical display timing pixel height
    // e.g.
    //  Match P0 any timing
    //  [P:0 0x0@0]
    //  Match P0 but must be 1280x720
    //  [P:0 1280x720@0]
    //  Match notional SF0 any timing
    //  [SF:0 0x0@0]
    //
    const char* tp = pchConfig;
    PassthroughDisplay::LogicalConfig* pconfig = NULL;
    LogicalDisplay::EIndexType eIT;
    int32_t p;
    uint32_t pw, ph, pr;
    bool bOK = true;
    uint32_t readParams;

    // Parse mapping.
    // Read {IT}:{P} {PW}x{PH}
    if ( strstr( tp, "SF:" ) == tp )
    {
        eIT = LogicalDisplay::eITNotionalSurfaceFlinger;
        tp += 3;
    }
    else if ( strstr( tp, "P:" ) == tp )
    {
        eIT = LogicalDisplay::eITPhysical;
        tp += 2;
    }
    else
    {
        ALOGE( "PassthroughDisplay::create : Config malformed index type\"%s\"", tp );
        return pconfig;
    }

    readParams = sscanf( tp, "%d %ux%u@%u", &p, &pw, &ph, &pr );

    bOK = ( readParams == 4 );

    if ( bOK )
    {
        PassthroughDisplay::LogicalConfig config( sfIndex, eIT, p, pw, ph, pr );
        pconfig = &config;
    }
    else
    {
        ALOGE( "PassthroughDisplay::create : Config malformed \"%s\" READ:%u", tp, readParams );
    }
    return pconfig;
}

PassthroughDisplay::PassthroughDisplay( Hwc& hwc,
                                        LogicalDisplayManager& ldm,
                                        PhysicalDisplayManager& pdm,
                                        const LogicalConfig& config) :
    LogicalDisplay( hwc, ldm, pdm, LOGICAL_TYPE_PASSTHROUGH ),
    mConfig( config ),
    mPhysicalIndex( INVALID_DISPLAY_ID ),
    mpPhysical( NULL )
{
}

PassthroughDisplay::~PassthroughDisplay( )
{
}

void PassthroughDisplay::setPhysical( AbstractPhysicalDisplay* pPhysical )
{
    if ( pPhysical )
    {
        mPhysicalIndex = pPhysical->getDisplayManagerIndex();
        mpPhysical = pPhysical;
    }
}

AbstractPhysicalDisplay* PassthroughDisplay::getPhysical( void ) const
{
    return mpPhysical;
}

bool PassthroughDisplay::updateAvailability( LogicalDisplayManager& ldm, uint32_t sfIndex, uint32_t enforceWidth , uint32_t enforceHeight )
{
    // Check availability/suitability of physical displays.
    // If we can satisfy this display, then set it up.

    if ( ( mConfig.mSfIndex != -1 ) && ( sfIndex != (uint32_t)mConfig.mSfIndex ) )
    {
        ALOGD_IF( LOGDISP_DEBUG, "PassthroughDisplay::updateAvailability : Unavailable sfIndex %d v %d",
            sfIndex, mConfig.mSfIndex );
        return false;
    }

    AbstractPhysicalDisplay* pPhysical = NULL;

    // Note;
    // Currently PassthroughDisplay copies over layer stack and can't be shared.
    const bool bExclusive = true;

    uint32_t width = enforceWidth ? enforceWidth : mConfig.mWidth;
    uint32_t height = enforceHeight ? enforceHeight : mConfig.mHeight;
    uint32_t refresh = mConfig.mRefresh;
    int32_t matchedTimingIndex = -1;

    // Check that we can satisfy the mapping for this display.
    pPhysical = ldm.findAvailable( mConfig.meIndexType,
                                   mConfig.mPhyIndex,
                                   bExclusive,
                                   width,
                                   height,
                                   refresh,
                                   matchedTimingIndex );

    setPhysical( pPhysical );

    if ( pPhysical )
    {
        if ( matchedTimingIndex != -1 )
        {
            pPhysical->setSpecificDisplayTiming( matchedTimingIndex, false );
            Log::alogd( LOGDISP_DEBUG, "PassthroughDisplay : requested timing index %u %ux%u@%u on physical display %u %s",
                matchedTimingIndex, width, height, refresh, pPhysical->getDisplayManagerIndex(), pPhysical->dump().string() );
        }

        // Keep manager informed that this physical is being used.
        ldm.acquirePhysical( pPhysical, bExclusive, width, height, refresh, matchedTimingIndex );

        // Logical display size from requested physical timing.
        setSize( width, height );

        return true;
    }

    return false;
}

void PassthroughDisplay::filter( const LogicalDisplayManager& ldm,
                                 const Content::Display& sfDisplay, Content& out,
                                 Vector<FilterDisplayState> &displayState,
                                 bool bUpdateGeometry )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s", getName(), __FUNCTION__ );
    HWC_UNUSED( displayState );
    HWC_UNUSED( bUpdateGeometry );
    AbstractPhysicalDisplay* pPhysical = getPhysical( );
    ALOG_ASSERT( pPhysical );

    // From this logical index.
    const uint32_t ld = sfDisplay.getDisplayManagerIndex();

    // Output to this physical index.
    const uint32_t pd = pPhysical->getDisplayManagerIndex();
    ALOG_ASSERT( pd < cMaxSupportedPhysicalDisplays );

    // Using this output content display slot.
    const uint32_t outSlotIndex = ldm.getPhysicalState( pd ).mOutSlot;

    // Copy display/layerstack from sfDisplay to our new slot.
    const Content::LayerStack& sfLayerStack = sfDisplay.getLayerStack( );
    Content::Display& phDisplay = out.editDisplay( outSlotIndex );
    Content::LayerStack& phLayerStack = phDisplay.editLayerStack( );
    phDisplay = sfDisplay;
    phLayerStack = sfLayerStack;

    // Update display manager index to be the physical display index.
    ALOGD_IF( LOGDISP_DEBUG, "   Out ->L%u->D%u->P%u", ld, outSlotIndex, pd );
    phDisplay.setDisplayManagerIndex( pd );
}

void PassthroughDisplay::notifyDisplayVSync( uint32_t phyIndex, nsecs_t timeStampNs )
{
    if ( ( phyIndex == mPhysicalIndex ) && isPluggedToSurfaceFlinger( ) )
    {
        ALOGD_IF( LOGDISP_DEBUG, "%s %s (SF:%u)", getName(), __FUNCTION__, getSurfaceFlingerIndex( ) );
        mHwc.notifyDisplayVSync( this, timeStampNs );
    }
}

void PassthroughDisplay::onVSyncEnableDm( uint32_t sfIndex, bool bEnableVSync )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u %d", getTagStr(), __FUNCTION__, sfIndex, mPhysicalIndex, bEnableVSync );
    mPhysicalDisplayManager.vSyncEnable( mPhysicalIndex, bEnableVSync );
}

int PassthroughDisplay::onBlankDm( uint32_t sfIndex, bool bEnableBlank, AbstractDisplayManager::BlankSource source )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u %d,%d", getTagStr(), __FUNCTION__, sfIndex, mPhysicalIndex, bEnableBlank, source );
    return mPhysicalDisplayManager.blank( mPhysicalIndex, bEnableBlank, source );
}

String8 PassthroughDisplay::dump( void ) const
{
    return String8::format( "%s PASSTHROUGH SF%u->P%u %s %d %ux%u@%u",
                            getTagStr(), getSurfaceFlingerIndex( ), mPhysicalIndex,
                            LogicalDisplayManager::indexTypeToString( mConfig.meIndexType ), mConfig.mPhyIndex,
                            mConfig.mWidth, mConfig.mHeight, mConfig.mRefresh );
}

const char* PassthroughDisplay::getName( void ) const
{
    return "PassthroughDisplay";
}

int PassthroughDisplay::onGetDisplayConfigs( uint32_t* paConfigHandles, uint32_t* pNumConfigs ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->onGetDisplayConfigs( paConfigHandles, pNumConfigs );
}

int PassthroughDisplay::onGetDisplayAttribute( uint32_t configHandle, EAttribute attribute, int32_t* pValue ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->onGetDisplayAttribute( configHandle, attribute, pValue );
}

int PassthroughDisplay::onGetActiveConfig( void ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->onGetActiveConfig( );
}

int PassthroughDisplay::onSetActiveConfig( uint32_t configIndex )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->onSetActiveConfig( configIndex );
}

int PassthroughDisplay::onVSyncEnable( bool bEnable )
{
    // Routed via AbstractDisplayManager mux.
    ALOGW( "%s %s SF%u P%u %d NOT IMPLEMENTED", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex, bEnable );
    return -1;
}

int PassthroughDisplay::onBlank( bool bEnable, bool bIsSurfaceFlinger )
{
    // Routed via AbstractDisplayManager mux.
    ALOGW( "%s %s SF%u P%u %d,%d NOT IMPLEMENTED", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex, bEnable, bIsSurfaceFlinger );
    return -1;
}

void PassthroughDisplay::dropAllFrames( void )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    mpPhysical->dropAllFrames( );
}

void PassthroughDisplay::flush( uint32_t frameIndex, nsecs_t timeoutNs )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u %u,%" PRIi64, getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex, frameIndex, timeoutNs );
    ALOG_ASSERT( mpPhysical );
    mpPhysical->flush( frameIndex, timeoutNs );
}

const DisplayCaps& PassthroughDisplay::getDisplayCaps( ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getDisplayCaps( );
}

int32_t PassthroughDisplay::getDefaultOutputFormat( void ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT;
}

bool PassthroughDisplay::getTiming( Timing& timing ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getTiming( timing );
}

uint32_t PassthroughDisplay::getRefresh( void ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getRefresh( );
}

EDisplayType PassthroughDisplay::getDisplayType( void ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getDisplayType( );
}

uint32_t PassthroughDisplay::getWidth() const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getWidth( );
}

uint32_t PassthroughDisplay::getHeight() const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getHeight( );
}

int32_t PassthroughDisplay::getXdpi() const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getXdpi( );
}

int32_t PassthroughDisplay::getYdpi() const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getYdpi( );
}

void PassthroughDisplay::copyDisplayTimings( Vector<Timing>& timings ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->copyDisplayTimings( timings );
}

void PassthroughDisplay::copyDefaultDisplayTiming( Timing& timing ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->copyDefaultDisplayTiming( timing );
}

bool PassthroughDisplay::setDisplayTiming( const Timing& timing, bool bSynchronize, Timing* pResultantTiming )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ) );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->setDisplayTiming( timing, bSynchronize, pResultantTiming );
}

void PassthroughDisplay::setUserOverscan( int32_t xoverscan, int32_t yoverscan )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->setUserOverscan( xoverscan, yoverscan );
}

void PassthroughDisplay::getUserOverscan( int32_t& xoverscan, int32_t& yoverscan ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getUserOverscan( xoverscan, yoverscan );
}

void PassthroughDisplay::setUserScalingMode( EScalingMode eScaling )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    mpPhysical->setUserScalingMode( eScaling );
}

void PassthroughDisplay::getUserScalingMode( EScalingMode& eScaling ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    mpPhysical->getUserScalingMode( eScaling );
}

bool PassthroughDisplay::setUserDisplayTiming( const Timing& timing, bool bSynchronize )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->setUserDisplayTiming( timing, bSynchronize );
}

bool PassthroughDisplay::getUserDisplayTiming( Timing& timing ) const
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    return mpPhysical->getUserDisplayTiming( timing );
}

void PassthroughDisplay::resetUserDisplayTiming( void )
{
    ALOGD_IF( LOGDISP_DEBUG, "%s %s SF%u P%u", getTagStr(), __FUNCTION__, getSurfaceFlingerIndex( ), mPhysicalIndex );
    ALOG_ASSERT( mpPhysical );
    mpPhysical->resetUserDisplayTiming( );
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

