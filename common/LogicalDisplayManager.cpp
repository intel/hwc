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
#include "Log.h"
#include "LogicalDisplayManager.h"
#include "LogicalDisplay.h"
#include "FakeDisplay.h"
#include "FilterManager.h"

namespace intel {
namespace ufo {
namespace hwc {

// NOTES:
//
// Multiple logical displays can be configured but SF supports only two "real" displays.
// Configuration can be modified using the property intel.hwc.dmconfig or at run-time using option "dmconfig".
//
// If there is a config or plug event then the availability of displays is re-established.
// Available logical displays are matched and allocated on a 0:N logical display priority basis.
// Think of the logical display configs as pattern-matched rules.
// A config line containing "TERM" indicates that following logical display configs should be ignored.
// Fallback rules for passthrough of SF0/SF1 are appended automatically (*except* where "TERM" is used).
//
// **WARNING** the SurfaceFlinger's primary resolution can NOT be changed once it is has been applied.
// LogicalDisplayManager will try to allocate displays such that the initial primary size is still satisfied.
// This means that behaviour can/will change depending on which displays were plugged at start of day.
//
// In all cases, a FakeDisplay will be used in the absence of any suitable primary.
//
// Example configuration options:
//
// Example 1.
//   Empty config - this will fallthrough to the fallback passthrough rules.
//      ""
//
// Example 2.
//   Empty config but "TERM" is specified. The "TERM" prevents fallthrough to the fallback passthrough rules.
//   Fake will be used instead.
//      "TERM"
//
// Example 3.
//   Primary in 720p any device if possible, external any other (remaining) device.
//      "[SF:0 PASSTHROUGH SF:-1 1280x720@0]"
//      "[SF:1 PASSTHROUGH SF:-1 0x0@0]"
//
// Example 4.
//   Virtual 480p to HDMI 480p
//      "[SF:0 MOSAIC 640x480 (P:1 3 0,0 640x480 0,0 640x480@60)]"
//
// Example 5.
//   Primary in 720p any device if possible, external any other (remaining) device.
//      "[SF:0 PASSTHROUGH SF:-1 1280x720@0]"
//      "[SF:1 PASSTHROUGH SF:-1 0x0@0]"
//
// Example 6.
//   Dual display 32x12 to 19x12 PANEL + HDMI 720p crop (lower right).
//   Physical flags are both displays mandatory and exclusive.
//      "[SF:0 MOSAIC 3200x1200 PANEL (SF:0 3 0,0 1920x1200 0,0 1920x1200@60) (SF:1 3 1920,480 1280x720 0,0 1280x720@60)]"
//
// Example 7.
//   Double wide 1080p to 19x10 on first two physical displays.
//      Physical flags are both displays mandatory and exclusive.
//    Fallback to regular passthrough if not available (=>Fake for 38x10 + clone to primary)
//      "[SF:0 MOSAIC 3840x1080 PANEL (P:0 3 0,0 1920x1080 0,60 1920x1200@60) (P:1 3 1920,0 1920x1080 0,0 1920x1080@60)]"
//
// Example 8.
//   Two logical displays @960x1080 both outputing to both PANEL and HDMI (LFP:A+B HDMI:B+A)
//      Physical flags are manadtory but not exclusive (both logicals write to both physicals).
//    Fallback to regular passthrough if not available (=>Fake for 960x1080 + clone to primary)
//      "[SF:0 MOSAIC 960x1080 PANEL (P:0 1 0,0 960x1080 0,0 1920x1200@60) (P:1 1 0,0 960x1080 960,0 1920x1080@60)]"
//      "[SF:1 MOSAIC 960x1080 PANEL (P:1 1 0,0 960x1080 0,0 1920x1080@60) (P:0 1 0,0 960x1080 960,0 1920x1200@60)]"
//
#define INTEL_UFO_HWC_DEFAULT_FAKE_DISPLAY_WIDTH    1280
#define INTEL_UFO_HWC_DEFAULT_FAKE_DISPLAY_HEIGHT   720


LogicalDisplayManager::LogicalDisplayManager( Hwc& hwc, PhysicalDisplayManager& physicalDisplayManager ) :
    mHwc( hwc ),
    mPhysicalDisplayManager( physicalDisplayManager ),
    mOptionConfig( CONFIG_OPTION_ID,
#if 0
        // Example1:
        // Split 3840x1080 to 2x 1920 wide displays - both physical displays must be available.
        //  Do NOT fallback to passthrough.
        "[SF:0 MOSAIC 3840x1080 PANEL (P:0 3 0,0 1920x1080 R,C 1920x0@0) (P:1 3 1920,0 1920x1080 L,C 1920x0@0)]"
        "TERM"
#elif 0
        // Example2:
        // Complex arrangement of N:M for testing - physical displays are not strictly required.
        //  Else fall back to passthrough
        "[SF:0 MOSAIC 960x1080 PANEL (SF:0 1 0,0 960x1080 0,0 1920x1200@60) (SF:1 1 0,0 960x1080 960,0 1920x1080@60)]"
        "[SF:1 MOSAIC 960x1080 PANEL (SF:1 1 0,0 960x1080 0,0 1920x1080@60) (SF:0 1 0,0 960x1080 960,0 1920x1200@60)]"
#elif 0
        // Example3:
        // Swap displays.
        "[SF:0 PASSTHROUGH SF:1 0x0@0]"
        "[SF:1 PASSTHROUGH SF:0 0x0@0]"
#else
        // Unspecified - fallthrough to the default passthrough.
        ""
#endif
    ),
    mSfPlugged( 0 ),
    mLogicalDisplays( 0 ),
    mConfiguredDisplays( 0 ),
    mpFakePhysical( NULL ),
    mFakeDisplay( -1 ),
    mpVirtualDisplay( NULL ),
    mVirtualDisplay( -1 ),
    mAvailableLogical( 0 ),
    mNumAcquiredPhysical( 0 ),
    mPrimaryWidth( 0 ),
    mPrimaryHeight( 0 ),
    mbPassthrough( false ),
    mbOneToOne( false ),
    mbFilterActive( false ),
    mbGeometryChange( true ),
    mAvailablePhysical( 0 ),
    mbDirtyConfig( false ),
    mbInConfigChange( false ),
    mbDirtyPhys( false )
{
    for ( uint32_t d = 0; d < cMaxSupportedLogicalDisplays; ++d )
    {
        mpLogicalDisplay[ d ] = NULL;
    }
    for ( uint32_t sf = 0; sf < cMaxSupportedSFDisplays; ++sf )
    {
        mpSurfaceFlingerToPhysical[ sf ] = NULL;
    }
    resetAvailableLogical();
}

LogicalDisplayManager::~LogicalDisplayManager()
{
    destroyLogical();
}

const Content& LogicalDisplayManager::onApply( const Content& ref )
{
    LOG_FATAL_IF( !mbFilterActive, "LogicalDisplayManager L Filter should not be active" );

    if ( mbPassthrough )
    {
        // Fastpath that does not copy any content.
        // This can be used if all displays are passthrough.
        // If display manager indices map 1:1 then this is trivial.
        // If display manager indices do not map 1:1 then a remap will have been configured
        //  at the end of the previous updateAvailability.
        // Release old output (it is not used).
        mFilterOut.resize( 0 );
        maFilterDisplayState.clear( );
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Filter no change (passthrough %s)", mbOneToOne ? "1:1" : "remapped" );
        return ref;
    }

    const uint32_t outDisplays = mNumAcquiredPhysical;
    const uint32_t inDisplays = ref.size() < cMaxSupportedSFDisplays ? ref.size() : cMaxSupportedSFDisplays;

    ALOG_ASSERT( cMaxSupportedPhysicalDisplays <= 32 );

    // A change in any SF display propagates a geometry change.
    for ( uint32_t sf = 0; sf < inDisplays; ++sf )
    {
        const Content::Display& sfDisplay = ref.getDisplay( sf );
        if ( sfDisplay.isGeometryChanged() )
        {
            ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Filter geometry change on SF display %u (=> geometry change)", sf );
            mbGeometryChange = true;
        }
    }

    // Make sure our output is sized correctly.
    if ( mFilterOut.size( ) != outDisplays )
    {
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Filter resize reference for %u physical displays (=> geometry change)", outDisplays );
        mFilterOut.resize( outDisplays );
        maFilterDisplayState.resize( outDisplays );
        mbGeometryChange = true;
    }

    // Clear all physical displays to start with.
    // Logical displays filter() will fill in display state.
    for ( uint32_t pd = 0; pd < outDisplays; ++pd )
    {
        Content::Display& phDisplay = mFilterOut.editDisplay( pd );
        if ( mbGeometryChange )
        {
            phDisplay.setDisplayType( eDTUnspecified );
            phDisplay.setOutputLayer( NULL );
            // Blank until some layers are present.
            phDisplay.setEnabled( false );
            phDisplay.setBlanked( true );
            maFilterDisplayState.editItemAt( pd ).mLayers.clear( );
            maFilterDisplayState.editItemAt( pd ).mNumLayers = 0;
        }
        phDisplay.setGeometryChanged( false );
    }

    // Apply logical display filters.
    for ( uint32_t sf = 0; sf < inDisplays; ++sf )
    {
        const Content::Display& sfDisplay = ref.getDisplay( sf );
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Filter SF%u%s %s",
            sf, sfDisplay.isGeometryChanged() ? " (Geom)" : "", mbGeometryChange ? "+Geom" : "" );
        if ( !mDisplayState[ sf ].isAttached() )
            continue;
        LogicalDisplay* pLD = mDisplayState[ sf ].getDisplay();
        ALOG_ASSERT( pLD );
        pLD->filter( *this, sfDisplay, mFilterOut, maFilterDisplayState,
                    ( mbGeometryChange || sfDisplay.isGeometryChanged( ) ) );
    }

    // Finish updates.
    for ( uint32_t pd = 0; pd < outDisplays; ++pd )
    {
        Content::Display& phDisplay = mFilterOut.editDisplay( pd );
        if ( phDisplay.isGeometryChanged() )
        {
            ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Finish update out P%u/%u", pd, outDisplays );
            Content::LayerStack& phLayerStack = phDisplay.editLayerStack( );

            uint32_t usedLayers = 0;
            const Layer* const* ppLayers = phLayerStack.getLayerArray();
            for ( uint32_t ly = 0; ly < phLayerStack.size(); ++ly )
            {
                if ( ppLayers[ ly ] == NULL )
                    break;
                ++usedLayers;
            }

            ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Filter out P%u Layer stack size %u used %u",
                pd, phLayerStack.size(), usedLayers );

            if ( usedLayers )
            {
                // Enable.
                phDisplay.setEnabled( true );
                phDisplay.setBlanked( false );
            }

            // Trim layer stack to final accumulated layer count.
            if ( phLayerStack.size() > usedLayers )
            {
                ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Filter   Trim layer stack (%u v %u)", phLayerStack.size(), usedLayers );
                phLayerStack.resize( usedLayers );
            }
            // Update layer stack flags.
            ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Filter   Update layer stack flags" );
            phLayerStack.updateLayerFlags( );
        }
    }

    // Apply enforced geometry change.
    if ( mbGeometryChange )
    {
        mFilterOut.setGeometryChanged( mbGeometryChange );
        mbGeometryChange = false;
    }

    if ( LOGDISP_DEBUG )
    {
        ALOGD( "LogicalDisplayManager : Filter Results" );
        ALOGD( "-- IN -----------------------------------------------------------------------" );
        for ( uint32_t d = 0; d < ref.size(); ++d )
        {
            String8 identifier = String8::format( "Filter in display %u", d );
            ALOGD( "%s", ref.getDisplay( d ).dump( identifier.string() ).string() );
        }
        ALOGD( "-- OUT ----------------------------------------------------------------------" );
        for ( uint32_t d = 0; d < mFilterOut.size(); ++d )
        {
            String8 identifier = String8::format( "Filter out display %u", d );
            ALOGD( "%s", mFilterOut.getDisplay( d ).dump( identifier.string() ).string() );
        }
        ALOGD( "-----------------------------------------------------------------------------" );
    }

    return mFilterOut;
}

void LogicalDisplayManager::open( void )
{
    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : open" );

    // Give physical manager a chance to do some work (e.g. add a proxy physical).
    mPhysicalDisplayManager.open( );

    // Create start-of-day logical displays from our notified physical displays.
    // And reflect those displays to SurfaceFlinger.
    createLogical();
    reflectChanges();
}

status_t LogicalDisplayManager::plugSurfaceFlingerDisplay( LogicalDisplay* pDisplay, uint32_t sfIndex, bool bTransitory )
{
    ALOG_ASSERT( pDisplay );
    HWC_UNUSED( bTransitory );

    ALOGD_IF( LOGDISP_DEBUG, "Plugging logical display %p (sfIndex %d added %u)\n%s",
        pDisplay, sfIndex, mSfPlugged, pDisplay->dump().string() );


    ALOG_ASSERT( sfIndex < (int32_t)cMaxSupportedSFDisplays );
    ALOG_ASSERT( pDisplay->getSurfaceFlingerIndex( ) == INVALID_DISPLAY_ID );
    ALOG_ASSERT( mDisplayState[ sfIndex ].getDisplay() == NULL );

    mDisplayState[ sfIndex ].setDisplay( reinterpret_cast< LogicalDisplay* >( pDisplay ) );
    pDisplay->setSurfaceFlingerIndex( sfIndex );
    ++mSfPlugged;

    ALOGD_IF( LOGDISP_DEBUG, "Plugged logical display %p (sfIndex %d added %u)\n%s",
        pDisplay, sfIndex, mSfPlugged, pDisplay->dump().string() );

    return OK;
}

status_t LogicalDisplayManager::unplugSurfaceFlingerDisplay( LogicalDisplay* pDisplay, bool bTransitory )
{
    ALOG_ASSERT( pDisplay );
    HWC_UNUSED( bTransitory );

    const uint32_t sfIndex = pDisplay->getSurfaceFlingerIndex();

    ALOGD_IF( LOGDISP_DEBUG, "Unplugging logical display %p (sfIndex %u, added %u)\n%s",
        pDisplay, sfIndex, mSfPlugged, pDisplay->dump().string() );

    ALOG_ASSERT( sfIndex != INVALID_DISPLAY_ID );
    ALOG_ASSERT( mDisplayState[ sfIndex ].getDisplay() == pDisplay );
    ALOG_ASSERT( mSfPlugged > 0 );

    mDisplayState[ sfIndex ].setDisplay( NULL );
    pDisplay->setSurfaceFlingerIndex( INVALID_DISPLAY_ID );
    --mSfPlugged;

    ALOGD_IF( LOGDISP_DEBUG, "Unplugged logical display %p (sfIndex %u, added %u)\n%s",
        pDisplay, sfIndex, mSfPlugged, pDisplay->dump().string() );

    return OK;
}

LogicalDisplay* LogicalDisplayManager::getSurfaceFlingerDisplay( uint32_t sfIndex ) const
{
    ALOG_ASSERT( sfIndex < cMaxSupportedSFDisplays );
    return mDisplayState[ sfIndex ].getDisplay();
}

void LogicalDisplayManager::onVSyncEnable( uint32_t sfIndex, bool bEnableVSync )
{
    Log::alogd( LOGDISP_DEBUG || VSYNC_DEBUG, "LogicalDisplayManager SF%u VSYNC %s",
                sfIndex, bEnableVSync ? "Enabled" : "Disabled" );
    ALOG_ASSERT( sfIndex < cMaxSupportedSFDisplays );
    LogicalDisplay* pDisplay = reinterpret_cast< LogicalDisplay* >(getSurfaceFlingerDisplay( sfIndex ));
    if ( pDisplay )
    {
        pDisplay->onVSyncEnableDm( sfIndex, bEnableVSync );
    }
}

int LogicalDisplayManager::onBlank( uint32_t sfIndex, bool bEnableBlank, BlankSource source )
{
    Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager SF%u %s %s",
                sfIndex, bEnableBlank ? "Blank" : "Unblank",
                source == BLANK_CONTENT        ? "NO CONTENT" :
                source == BLANK_SURFACEFLINGER ? "SF" :
                source == BLANK_HWCSERVICE     ? "SERVICE" :
                "UNKNOWN" );
    ALOG_ASSERT( sfIndex < cMaxSupportedSFDisplays );
    LogicalDisplay* pDisplay = reinterpret_cast< LogicalDisplay* >(getSurfaceFlingerDisplay( sfIndex ));
    if ( pDisplay )
    {
        return pDisplay->onBlankDm( sfIndex, bEnableBlank, source );
    }
    return BAD_VALUE;
}

void LogicalDisplayManager::flush( uint32_t frameIndex, nsecs_t timeoutNs )
{
    Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager flush frame:%u,%u", frameIndex, timeoutNs );
    mPhysicalDisplayManager.flush( frameIndex, timeoutNs );
}

void LogicalDisplayManager::endOfFrame( void )
{
    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : endOfFrame" );

    if ( mOptionConfig.isChanged( ) )
    {
        // Connfig change.
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : endOfFrame - new config" );
        mOptionConfig.setChanged( false );
        mbDirtyConfig = true;
    }

    // Reflect changes (if any).
    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : endOfFrame - reflect changes" );
    reflectChanges( );
}

String8 LogicalDisplayManager::dump( void )
{
    String8 str;
    // Filter info.
    str = String8::format( "Primary %ux%u Passthrough:%d OneToOne:%d Active:%d Available:0x%x (%u)",
        mPrimaryWidth, mPrimaryHeight, mbPassthrough, mbOneToOne, mbFilterActive, mAvailableLogical, mNumAvailableLogical );
    // Current SF displays info.
    for ( uint32_t d = 0; d < HWC_NUM_DISPLAY_TYPES; ++d )
    {
        if ( !mDisplayState[d].isAttached() )
            continue;
        if ( str.length() )
            str += "\n";
        str += String8::format( " SF%u %s (map:L%d)", d,  mDisplayState[d].getDisplay()->dump().string(), mSurfaceFlingerToLogical[d] );
    }
    return str;
}

String8 LogicalDisplayManager::dumpDetail( void )
{
    String8 str;

    // Filter + Current SF displays info.
    str = dump( );
    str += "\n";

    // Nore detail: Add in logical displays manager info.
    for ( uint32_t d = 0; d < cMaxSupportedLogicalDisplays; ++d )
    {
        if ( !mpLogicalDisplay[d] )
            continue;
        str += String8::format( " L%u %s (map:SF%d)\n", d, mpLogicalDisplay[d]->dump().string(), mLogicalToSurfaceFlinger[d] );
    }
    // Nore detail: Add in physical displays manager info.
    str += mPhysicalDisplayManager.dump();
    return str;
}

void LogicalDisplayManager::notifyDisplayAvailable(AbstractPhysicalDisplay* pDisplay)
{
    ALOG_ASSERT( pDisplay );
    ALOG_ASSERT( cMaxSupportedPhysicalDisplays <= 32 );
    const uint32_t phyIndex = pDisplay->getDisplayManagerIndex();
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays );

    Mutex::Autolock _l( mPhysicalNotificationLock );

    mAvailablePhysical |= (1<<phyIndex);

    // We should really assign these via some defined algorithm during initialization.
    // However, for now, just plug these into the first SurfaceFlinger display slot available.
    for (uint32_t sfIndex = 0; sfIndex < cMaxSupportedSFDisplays; sfIndex++)
    {
        if (mpSurfaceFlingerToPhysical[ sfIndex ] == NULL)
        {
            mpSurfaceFlingerToPhysical[ sfIndex ] = pDisplay;
            break;
        }
    }
    ALOGD("LogicalDisplayManager::notifyDisplayAvailable: %s", pDisplay->dump().string());

    mbDirtyPhys = true;
    mHwc.forceRedraw( );
}

void LogicalDisplayManager::notifyDisplayUnavailable( AbstractPhysicalDisplay* pDisplay )
{
    ALOG_ASSERT( pDisplay );
    ALOG_ASSERT( cMaxSupportedPhysicalDisplays <= 32 );
    const uint32_t phyIndex = pDisplay->getDisplayManagerIndex();
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays );

    Mutex::Autolock _l( mPhysicalNotificationLock );

    mAvailablePhysical &= ~(1<<phyIndex);
    for ( uint32_t sf = 0; sf < cMaxSupportedSFDisplays; ++sf )
    {
        if ( mpSurfaceFlingerToPhysical[ sf ] == pDisplay )
            mpSurfaceFlingerToPhysical[ sf ] = NULL;
    }

    Log::alogd( LOGDISP_DEBUG, "Physical display P%u unavailable [-> 0x%x]\n%s",
        phyIndex, mAvailablePhysical, pDisplay->dump( ).string( ) );

    mbDirtyPhys = true;
    mHwc.forceRedraw( );
}

void LogicalDisplayManager::notifyDisplayChangeSize( AbstractPhysicalDisplay* pDisplay )
{
    ALOG_ASSERT( pDisplay );
    const uint32_t phyIndex = pDisplay->getDisplayManagerIndex();
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays );

    Mutex::Autolock _l( mPhysicalNotificationLock );

    Log::alogd( LOGDISP_DEBUG, "Physical display P%u size change", phyIndex );

    mbDirtyPhys = true;
    mHwc.forceRedraw( );
}

void LogicalDisplayManager::notifyDisplayVSync( AbstractPhysicalDisplay* pDisplay, nsecs_t timeStampNs )
{
    ALOG_ASSERT( pDisplay );
    const uint32_t phyIndex = pDisplay->getDisplayManagerIndex();

    ALOGD_IF( LOGDISP_DEBUG, "Physical display P%u vsync", phyIndex );
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays );

    for ( uint32_t sf = 0; sf < cMaxSupportedSFDisplays; ++sf )
    {
        if ( !mDisplayState[ sf ].isAttached( ) )
            continue;
        LogicalDisplay* pLogical = mDisplayState[ sf ].getDisplay( );
        ALOG_ASSERT( pLogical );
        pLogical->notifyDisplayVSync( phyIndex, timeStampNs );
    }
}

bool LogicalDisplayManager::isVirtualType( AbstractPhysicalDisplay* pPhysical )
{
    ALOG_ASSERT( pPhysical );
    return ( ( pPhysical->getDisplayType() == eDTVirtual )
          || ( pPhysical->getDisplayType() == eDTWidi    ) );
}

void LogicalDisplayManager::getAttributes( AbstractPhysicalDisplay* pPhysical, int32_t timingIndex,
                                           uint32_t& width, uint32_t& height, uint32_t& refresh,
                                           uint32_t& xdpi, uint32_t& ydpi )
{
    ALOG_ASSERT( pPhysical );

    Timing t;
    if ( pPhysical->copyDisplayTiming( timingIndex, t ) )
    {
        width = t.getWidth();
        height = t.getHeight( );
        refresh = t.getRefresh( );
        xdpi = pPhysical->getXdpiForTiming( t );
        ydpi = pPhysical->getYdpiForTiming( t );
    }
    else
    {
        ALOGE( "Failed to get display timing for timing index %d", timingIndex );
    }

    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : readCurrentAttributes %ux%u@%u dpi%ux%u", width, height, refresh, xdpi, ydpi );
}

void LogicalDisplayManager::createFakeDisplay( void )
{
    if ( mFakeDisplay == -1 )
    {
        if ( !mPrimaryWidth || !mPrimaryHeight )
        {
            mPrimaryWidth = INTEL_UFO_HWC_DEFAULT_FAKE_DISPLAY_WIDTH;
            mPrimaryHeight = INTEL_UFO_HWC_DEFAULT_FAKE_DISPLAY_HEIGHT;
        }
        Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager creating fake primary display (%ux%u)", mPrimaryWidth, mPrimaryHeight );
        mpFakePhysical = new FakeDisplay( mHwc, mPrimaryWidth, mPrimaryHeight );
        if ( mpFakePhysical )
        {
            ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : createFakeDisplay physical display %p", mpFakePhysical );
            if ( mPhysicalDisplayManager.registerDisplay( mpFakePhysical ) == INVALID_DISPLAY_ID )
            {
                ALOGE( "Failed to register physical fake display" );
                delete mpFakePhysical;
                mpFakePhysical = NULL;
            }
            else
            {
                LogicalDisplay* pLDPT = LogicalDisplay::instantiate( "PASSTHROUGH", mHwc, *this, mPhysicalDisplayManager,
                                                   HWC_DISPLAY_PRIMARY, LogicalDisplay::eITPhysical, mpFakePhysical->getDisplayManagerIndex());
                if ( pLDPT )
                {
                    pLDPT->setPhysical( mpFakePhysical );
                    pLDPT->setTag( "FAKE" );
                    pLDPT->setSize( mPrimaryWidth, mPrimaryHeight );
                    mFakeDisplay = addLogicalDisplay( pLDPT );
                    ALOGD_IF( LOGDISP_DEBUG,
                        "LogicalDisplayManager : Created fake logical display (PASSTHROUGH) L%u/%p/%s [mFakeDisplay %d mpFakePhysical %p]",
                        mLogicalDisplays, pLDPT, pLDPT->dump().string(), mFakeDisplay, mpFakePhysical );
                    ALOG_ASSERT( mFakeDisplay != -1 );
                }
            }
        }
    }
}

void LogicalDisplayManager::createLogical( void )
{
    ALOGD_IF( LOGDISP_DEBUG, "---- CREATING LOGICAL DISPLAYS -----------------------------------------------------------" );

    // Copy string for processing.
    String8 config = String8::format( "%s", mOptionConfig.getString() );

    // We should have zero logical displays here.
    ALOG_ASSERT( mLogicalDisplays == 0 );
    ALOG_ASSERT( mConfiguredDisplays == 0 );

    bool bAppendFallbackPassthrough = true;

    // Create/configure our logical displays - create mappings from configuration.
    const char* tp = config.string();

    uint32_t logicalDisplay = 0;

    for (;;)
    {
        const char* pTerm = strstr( tp, TERMINATE_CONFIG_STRING );
        tp = strchr( tp, '[' );

        // Check if "TERM" precedes the next logical display config.
        if ( pTerm && ( ( tp == NULL ) || ( pTerm < tp ) ) )
        {
            bAppendFallbackPassthrough = false;
            break;

        }

        // No more logical displays.
        if ( tp == NULL )
        {
            break;
        }

        // Check end of logical.
        ++tp;
        int32_t sfIndex;
        LogicalDisplay* pLD = NULL;

        uint32_t readParams = sscanf( tp, "SF:%d ", &sfIndex );

        if ( readParams == 1 )
        {
            tp += 6;
            // Parse and create a differrnt logical display from this config.
            pLD = LogicalDisplay::instantiate(tp, mHwc, *this, mPhysicalDisplayManager, sfIndex, LogicalDisplay::eITPhysical, 0);
            if(!pLD)
            {
                ALOGE( "LogicalDisplayManager : failed to create Logical Display %u \"%s\"", logicalDisplay, tp );
            }
        }
        else
        {
            ALOGE( "LogicalDisplayManager : Config malformed \"%s\" READ:%u", tp, readParams );
        }


        if ( pLD )
        {
            int32_t logicalIdx = addLogicalDisplay( pLD );
            if ( logicalIdx == -1 )
            {
                delete pLD;
            }
            else
            {
                ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Created added logical display L%u %s", logicalIdx, pLD->dump().string() );
                ++mConfiguredDisplays;
            }
        }

        tp = strdup(strchr( tp, ']' ) + 1);
        ++logicalDisplay;
    }
    if ( bAppendFallbackPassthrough )
    {
        // Create simple mappings here.
        // Create logical displays 0:HWC_DISPLAY_VIRTUAL-1.
        // The config is set to any physical/any timings - so the allocator in updateAvailability
        //  will just find and map first N plugged physical.
        //     This is equivalent to:
        //      "[SF:0 PASSTHROUGH SF:0 0x0@0]"
        //      "[SF:1 PASSTHROUGH SF:1 0x0@0]"
        for ( uint32_t d = 0; d < HWC_DISPLAY_VIRTUAL; ++d )
        {
            LogicalDisplay* pLDPT = LogicalDisplay::instantiate("PASSTHROUGH", mHwc, *this, mPhysicalDisplayManager, d, LogicalDisplay::eITNotionalSurfaceFlinger, d);
            if ( pLDPT )
            {
                pLDPT->setTag( "REAL" );
                ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : createLogical logical display (PASSTHROUGH) L%u/%p/%s",
                    mLogicalDisplays, pLDPT, pLDPT->dump().string() );
                addLogicalDisplay( pLDPT );
                ++mConfiguredDisplays;
            }
        }
    }

    // Add a virtual/widi display.
    for ( uint32_t d = 0; d < cMaxSupportedPhysicalDisplays; ++d )
    {
        if ( !isPhysicalAvailable( d ) )
            continue;

        AbstractPhysicalDisplay* pPhysical = mPhysicalDisplayManager.getPhysicalDisplay( d );
        if ( pPhysical && isVirtualType( pPhysical ) )
        {
            LogicalDisplay* pLDPT = LogicalDisplay::instantiate("PASSTHROUGH", mHwc, *this, mPhysicalDisplayManager, d, LogicalDisplay::eITPhysical, d);
            if ( pLDPT )
            {
                pLDPT->setTag( ( pPhysical->getDisplayType() == eDTVirtual ) ? "VIRTUAL" : "WIDI" );
                pLDPT->setPhysical( pPhysical );
                mVirtualDisplay = addLogicalDisplay( pLDPT );
                mpVirtualDisplay = pPhysical;
                ALOGD_IF( LOGDISP_DEBUG,
                    "LogicalDisplayManager : Created virtual logical display (PASSTHROUGH) L%u/%p/%s [mVirtualDisplay %d]",
                    mLogicalDisplays, pLDPT, pLDPT->dump().string(), mVirtualDisplay );
                ALOG_ASSERT( mVirtualDisplay != -1 );
            }
            break;
        }
    }

    // Set dirty.
    // The next call to reflectChanges will plug displays.
    mbDirtyPhys = true;

    ALOGD( "LogicalDisplayManager : createLogical result:\n%s", dump().string() );
}

int32_t LogicalDisplayManager::addLogicalDisplay( LogicalDisplay* pLD )
{
    ALOG_ASSERT( pLD );
    if ( mLogicalDisplays >= cMaxSupportedLogicalDisplays )
    {
        Log::aloge( true,  "LogicalDisplayManager out of space - could not add %s [logical displays %u, configured %u] *ERROR*",
            pLD->dump( ).string( ), mLogicalDisplays, mConfiguredDisplays );
        return -1;
    }
    mpLogicalDisplay[ mLogicalDisplays ] = pLD;
    pLD->setDisplayManagerIndex( mLogicalDisplays );
    ++mLogicalDisplays;
    return mLogicalDisplays - 1;
}

AbstractPhysicalDisplay* LogicalDisplayManager::findAvailable( LogicalDisplay::EIndexType eIT,
                                                               int32_t index,
                                                               bool bRequiredExclusive,
                                                               uint32_t& requiredWidth,
                                                               uint32_t& requiredHeight,
                                                               uint32_t& requiredRefresh,
                                                               int32_t& matchedTimingIndex )
{
    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : findAvailable %s %d required exc:%u %ux%u@%u",
        indexTypeToString( eIT), index, bRequiredExclusive, requiredWidth, requiredHeight, requiredRefresh );

    if ( eIT == LogicalDisplay::eITNotionalSurfaceFlinger )
    {
        // Look at displays that *WOULD* be available to SurfaceFlinger
        // if this logical display manager had *NOT* been present.
        if ( index >= 0 )
        {
            // Check explicit display.
            if ( (uint32_t)index < cMaxSupportedSFDisplays )
            {
                AbstractPhysicalDisplay* pPhysical = mpSurfaceFlingerToPhysical[ index ];
                if ( pPhysical && checkPhysicalAvailable( pPhysical,
                                                          bRequiredExclusive,
                                                          requiredWidth,
                                                          requiredHeight,
                                                          requiredRefresh,
                                                          matchedTimingIndex ) )
                {
                    return pPhysical;
                }
                return NULL;
            }
            ALOGE( "Logical display manager physical lookup index out of logical range (%u v %u)", index, cMaxSupportedSFDisplays );
            return NULL;
        }
        // Check first available and matching.
        for ( uint32_t sf = 0; sf < cMaxSupportedSFDisplays; ++sf )
        {
            AbstractPhysicalDisplay* pPhysical = mpSurfaceFlingerToPhysical[ sf ];
            if ( !pPhysical || ( ( pPhysical->getDisplayType() != eDTPanel )
                              && ( pPhysical->getDisplayType() != eDTExternal ) ) )
            {
                continue;
            }
            if ( checkPhysicalAvailable( pPhysical,
                                         bRequiredExclusive,
                                         requiredWidth,
                                         requiredHeight,
                                         requiredRefresh,
                                         matchedTimingIndex ) )
            {
                return pPhysical;
            }
        }
        return NULL;
    }

    ALOG_ASSERT( eIT == LogicalDisplay::eITPhysical );

    // Look at physical displays.
    if ( index >= 0 )
    {
        // Check explicit display.
        if ( (uint32_t)index < cMaxSupportedPhysicalDisplays )
        {
            AbstractPhysicalDisplay* pPhysical = mPhysicalDisplayManager.getPhysicalDisplay( index );
            if ( pPhysical && checkPhysicalAvailable( pPhysical,
                                                      bRequiredExclusive,
                                                      requiredWidth,
                                                      requiredHeight,
                                                      requiredRefresh,
                                                      matchedTimingIndex ) )
            {
                return pPhysical;
            }
            return NULL;
        }
        ALOGE( "Logical display manager physical lookup index out of physical range (%u v %u)", index, cMaxSupportedPhysicalDisplays );
        return NULL;
    }
    // Check first available and matching.
    for ( uint32_t pd = 0; pd < cMaxSupportedPhysicalDisplays; ++pd )
    {
        AbstractPhysicalDisplay* pPhysical = mPhysicalDisplayManager.getPhysicalDisplay( pd );
        if ( !pPhysical || ( ( pPhysical->getDisplayType() != eDTPanel )
                          && ( pPhysical->getDisplayType() != eDTExternal ) ) )
        {
            continue;
        }
        if ( checkPhysicalAvailable( pPhysical,
                                     bRequiredExclusive,
                                     requiredWidth,
                                     requiredHeight,
                                     requiredRefresh,
                                     matchedTimingIndex ) )
        {
            return pPhysical;
        }
    }
    return NULL;
}

void LogicalDisplayManager::acquirePhysical( AbstractPhysicalDisplay* pPhysical,
                                             bool bExclusive,
                                             uint32_t width,
                                             uint32_t height,
                                             uint32_t refresh,
                                             int32_t timingIndex )
{
    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : acquirePhysical %p exc:%d %ux%u@%u",
        pPhysical, bExclusive, width, height, refresh );

    ALOG_ASSERT( pPhysical );

    uint32_t pd = pPhysical->getDisplayManagerIndex();
    ALOG_ASSERT( pd != INVALID_DISPLAY_ID );
    ALOG_ASSERT( width || bExclusive );
    ALOG_ASSERT( height || bExclusive );
    ALOG_ASSERT( refresh || bExclusive );

    if ( bExclusive )
    {
        // Check the display isn't acquired already.
        ALOG_ASSERT( !( mPhysicalState[ pd ].mFlags & PhysicalState::FLAG_ACQUIRED ) );
        // Acquire exclusively.
        mPhysicalState[ pd ].mFlags |= PhysicalState::FLAG_EXCLUSIVE;
    }
    else
    {
        // Check the display isn't acquired exclusively already.
        ALOG_ASSERT( !( mPhysicalState[ pd ].mFlags & PhysicalState::FLAG_EXCLUSIVE ) );
    }

    if ( mPhysicalState[ pd ].mFlags & PhysicalState::FLAG_ACQUIRED )
    {
        // If already acquired then timing can't change.
        ALOG_ASSERT( mPhysicalState[ pd ].mWidth == width );
        ALOG_ASSERT( mPhysicalState[ pd ].mHeight == height );
        ALOG_ASSERT( mPhysicalState[ pd ].mRefresh == refresh );
    }
    else
    {
        // Acquired - assign physical timing attributes now.
        mPhysicalState[ pd ].mFlags |= PhysicalState::FLAG_ACQUIRED;
        mPhysicalState[ pd ].mWidth = width;
        mPhysicalState[ pd ].mHeight = height;
        mPhysicalState[ pd ].mRefresh = refresh;
        mPhysicalState[ pd ].mTimingIndex = timingIndex;
        mPhysicalState[ pd ].mOutSlot = mNumAcquiredPhysical;
        ++mNumAcquiredPhysical;
    }
}

const LogicalDisplayManager::PhysicalState& LogicalDisplayManager::getPhysicalState( uint32_t phyIndex ) const
{
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays );
    // Must only reference acquired physical.
    ALOG_ASSERT( mPhysicalState[ phyIndex ].mFlags & PhysicalState::FLAG_ACQUIRED );
    return mPhysicalState[ phyIndex ];
}

void LogicalDisplayManager::destroyLogical( void )
{
    mLogicalDisplays = 0;
    mConfiguredDisplays = 0;
    mVirtualDisplay = -1;
    mFakeDisplay = -1;
    resetAvailableLogical();
    for ( uint32_t d = 0; d < cMaxSupportedLogicalDisplays; ++d )
    {
        if ( mpLogicalDisplay[ d ] )
        {
            ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : destroying logical display L%u/%p/%s",
                d, mpLogicalDisplay[ d ], mpLogicalDisplay[ d ]->dump().string() );
            delete mpLogicalDisplay[ d ];
            mpLogicalDisplay[ d ] = NULL;
        }
    }
    if ( mpFakePhysical )
    {
        mPhysicalDisplayManager.unregisterDisplay( mpFakePhysical );
        delete mpFakePhysical;
        mpFakePhysical = NULL;
    }
    for ( uint32_t d = 0; d < cMaxSupportedSFDisplays; ++d )
    {
        mDisplayState[ d ].setDisplay( NULL );
    }
}

void LogicalDisplayManager::setAvailableLogical( uint32_t sfIndex, uint32_t logical )
{
    uint32_t setBit = (1<<logical);
    if ( !( mAvailableLogical & setBit ) )
    {
        mAvailableLogical |= setBit;
        ++mNumAvailableLogical;
    }

    // Maintain mapping of which logical displays are mapped in for SurfaceFlinger.
    mLogicalToSurfaceFlinger[ logical ] = sfIndex;
    mSurfaceFlingerToLogical[ sfIndex ] = logical;

    uint32_t phyIndex = INVALID_DISPLAY_ID;

    // Cancel one-to-one if ever use non-passthrough type or
    //  if the sfIndex and physicalID aren't equivalent.
    LogicalDisplay* pLD = mpLogicalDisplay[ logical ];
    if ( !pLD || ( pLD->getLogicalType() != LogicalDisplay::LOGICAL_TYPE_PASSTHROUGH ) )
    {
        ALOGD_IF( LOGDISP_DEBUG,
                "LogicalDisplayManager : not one-to-one due because SF %u is not mapped to a passthrough logical",
                sfIndex );
        mbOneToOne = false;
        mbPassthrough = false;
    }
    else
    {
        AbstractPhysicalDisplay* pPhysical = pLD->getPhysical();
        ALOG_ASSERT( pPhysical );
        phyIndex = pPhysical->getDisplayManagerIndex( );
        if ( logical != phyIndex )
        {
            ALOGD_IF( LOGDISP_DEBUG,
                    "LogicalDisplayManager : not one-to-one due because SF %u is mapped to passthrough physical %u",
                    sfIndex, pPhysical->getDisplayManagerIndex() );
            mbOneToOne = false;
        }
    }

    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : setAvailable SF%u->L%u->%s",
        sfIndex, logical, ( phyIndex == INVALID_DISPLAY_ID ) ? "N/A" : String8::format( "P%u", phyIndex ).string() );
}

void LogicalDisplayManager::resetAvailableLogical( void )
{
    mAvailableLogical = 0;
    mNumAvailableLogical = 0;
    mbPassthrough = true;
    mbOneToOne = true;
    for ( uint32_t d = 0; d < cMaxSupportedLogicalDisplays; ++d )
    {
        mLogicalToSurfaceFlinger[d] = -1;
    }
    for ( uint32_t d = 0; d < cMaxSupportedSFDisplays; ++d )
    {
        mSurfaceFlingerToLogical[d] = -1;
    }
    for ( uint32_t d = 0; d < cMaxSupportedPhysicalDisplays; ++d )
    {
        mPhysicalState[d].reset();
    }
    mNumAcquiredPhysical = 0;
}

bool LogicalDisplayManager::checkPhysicalAvailable( AbstractPhysicalDisplay* pPhysicalDisplay,
                                                    bool bRequiredExclusive,
                                                    uint32_t& requiredWidth,
                                                    uint32_t& requiredHeight,
                                                    uint32_t& requiredRefresh,
                                                    int32_t& matchedTimingIndex )
{
    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager::checkPhysicalAvailable %s", pPhysicalDisplay->dump().string());

    const uint32_t pd = pPhysicalDisplay->getDisplayManagerIndex( );
    if ( !(mAvailablePhysical & (1<<pd)) )
    {
        // No - it has not been notified as available.
        ALOGD_IF( LOGDISP_DEBUG, "  pd %u not available (availability has not been notified)", pd );
    }
    else if ( mPhysicalState[ pd ].mFlags & PhysicalState::FLAG_EXCLUSIVE )
    {
        // No - because already acquired exclusively already.
        ALOGD_IF( LOGDISP_DEBUG, "  pd %u not available (already used exclusively)", pd );
    }
    else if ( mPhysicalState[ pd ].mFlags & PhysicalState::FLAG_ACQUIRED )
    {
        if ( !bRequiredExclusive
          && ( !requiredWidth   || ( mPhysicalState[ pd ].mWidth == requiredWidth ) )
          && ( !requiredHeight  || ( mPhysicalState[ pd ].mHeight == requiredHeight ) )
          && ( !requiredRefresh || ( mPhysicalState[ pd ].mRefresh == requiredRefresh ) ) )
        {
            // Available - using existing acquired timing attributes.
            ALOGD_IF( LOGDISP_DEBUG, "  pd %u matched (using existing %ux%u@%u)",
                pd, mPhysicalState[ pd ].mWidth, mPhysicalState[ pd ].mHeight, mPhysicalState[ pd ].mRefresh );
            matchedTimingIndex = -1;
            requiredWidth = mPhysicalState[ pd ].mWidth;
            requiredHeight = mPhysicalState[ pd ].mHeight;
            requiredRefresh = mPhysicalState[ pd ].mRefresh;
            return true;
        }
        else
        {
            // No - because already acquired and doesn't match our requirements.
            ALOGD_IF( LOGDISP_DEBUG, "  pd %u not available (already used as %ux%u@%u)",
                pd, mPhysicalState[ pd ].mWidth, mPhysicalState[ pd ].mHeight, mPhysicalState[ pd ].mRefresh );
        }
    }
    else
    {
        int32_t timingIndex = checkTimingAvailable( pPhysicalDisplay, requiredWidth, requiredHeight, requiredRefresh );
        if ( timingIndex >= 0 )
        {
            // Available - with these specific timing attributes.
            matchedTimingIndex = (uint32_t)timingIndex;
            ALOGD_IF( LOGDISP_DEBUG, "  pd %u matched (timing index %u)", pd, matchedTimingIndex );
            return true;
        }
    }
    return false;
}

int32_t LogicalDisplayManager::checkTimingAvailable( AbstractPhysicalDisplay* pPhysical, uint32_t& requiredWidth, uint32_t& requiredHeight, uint32_t& requiredRefresh )
{
    ALOG_ASSERT( pPhysical );

    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : checkTimingAvailable from %s requiring %ux%u@%u",
        pPhysical->dump().string(), requiredWidth, requiredHeight, requiredRefresh );

    bool bExisting = false;
    int32_t timingIndex = 0;

    if ( ( requiredWidth == 0 )
      && ( requiredHeight == 0 )
      && ( requiredRefresh == 0 ) )
    {
        timingIndex = pPhysical->getTimingIndex( );
        bExisting = true;
    }
    else
    {
        // Do not fallback.
        Timing timing( requiredWidth, requiredHeight, requiredRefresh );
        timingIndex = pPhysical->findDisplayTiming( timing, 0 );
    }

    if ( timingIndex < 0 )
    {
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager :   did not find timing %ux%u@%u",
                    requiredWidth, requiredHeight, requiredRefresh );
    }
    else
    {
        Timing timing;
        pPhysical->copyDisplayTiming( timingIndex, timing );
        requiredWidth = timing.getWidth( );
        requiredHeight = timing.getHeight( );
        requiredRefresh = timing.getRefresh( );
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager :   found timing index %d %ux%u@%u%s",
                    timingIndex, requiredWidth, requiredHeight, requiredRefresh,
                    bExisting ? " (existing)" : "" );
    }

    return timingIndex;
}

void LogicalDisplayManager::updateAvailability( void )
{
    ALOGD_IF( LOGDISP_DEBUG, "---- UPDATING AVAILABLE DISPLAYS ---------------------------------------------------------" );
    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Updating available displays (x%u configured)", mConfiguredDisplays );

    // Reset available.
    resetAvailableLogical();

    // If we configured some displays then update them here.
    if ( mConfiguredDisplays )
    {
        // Process logical displays.
        // Map SF 0:HWC_DISPLAY_VIRTUAL-1 to first available logical displays.
        // This prioritises the primary which may have restrictions.
        for ( uint32_t sfIndex = 0; sfIndex < HWC_DISPLAY_VIRTUAL; ++sfIndex )
        {
            ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : SF%u", sfIndex );

            // Find first available logcal that is suitable.
            for ( uint32_t ld = 0; ld < mConfiguredDisplays; ++ld )
            {
                if ( mLogicalToSurfaceFlinger[ ld ] != -1 )
                {
                    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager :   Logical %u is already mapped as SF%u", ld, mLogicalToSurfaceFlinger[ ld ] );
                    continue;
                }

                LogicalDisplay* pLD = mpLogicalDisplay[ ld ];
                ALOG_ASSERT( pLD );

                uint32_t enforceWidth = 0;
                uint32_t enforceHeight = 0;

                // If we have a specific primary size requirement
                // (either because it is configured through the primary option or because
                //  we already started Android) then we must be sure to maintain the same.
                if ( ( sfIndex == HWC_DISPLAY_PRIMARY ) && ( mPrimaryWidth | mPrimaryHeight ) )
                {
                    enforceWidth = mPrimaryWidth;
                    enforceHeight = mPrimaryHeight;
                    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager :   Enforcing %ux%u", enforceWidth, enforceHeight );
                }

                // Can this logical display be used.
                if ( pLD->updateAvailability( *this, sfIndex, enforceWidth, enforceHeight ) )
                {
                    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager :   SF display %u -> Logical %u", sfIndex, ld );
                    setAvailableLogical( sfIndex, ld );
                    break;
                }
            }
        }
    }

    // Make fallback available if we didn't establish a primary.
    if ( mSurfaceFlingerToLogical[ HWC_DISPLAY_PRIMARY ] == -1 )
    {
        ALOGW( "LogicalDisplayManager : No primary display - adding fake display" );
        createFakeDisplay( );
        if ( mFakeDisplay == -1 )
        {
            ALOGE( "LogicalDisplayManager : Missing fake display" );
        }
        else
        {
            ALOG_ASSERT( mpFakePhysical );
            ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : setAvailable HWC_DISPLAY_PRIMARY mFakeDisplay %u", mFakeDisplay );
            acquirePhysical( mpFakePhysical, true, mPrimaryWidth, mPrimaryHeight );
            setAvailableLogical( HWC_DISPLAY_PRIMARY, mFakeDisplay );
        }
    }

    // Map virtual into reserved slot.
    if ( mVirtualDisplay != -1 )
    {
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : setAvailable HWC_DISPLAY_VIRTUAL mVirtualDisplay %u", mVirtualDisplay );
        acquirePhysical( mpVirtualDisplay, true, 0, 0 );
        setAvailableLogical( HWC_DISPLAY_VIRTUAL, mVirtualDisplay );
    }

    // Can not do 1:1 passthrough if display counts don't match.
    if ( mNumAcquiredPhysical != getNumAvailableLogical( ) )
    {
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : not one-to-one due to display count mismatch %u v %u",
            mNumAcquiredPhysical, getNumAvailableLogical( ) );
        mbOneToOne = false;
    }

    // Add or remove LogicalDisplayManager as a filter.
    if ( mbPassthrough && mbOneToOne )
    {
        if ( mbFilterActive )
        {
            FilterManager::getInstance().remove( *this );
            mbFilterActive = false;
        }
    }
    else if ( !mbFilterActive )
    {
        FilterManager::getInstance().add( *this, FilterPosition::DisplayManager );
        mbFilterActive = true;
    }

    // Displays delivered to PhysicalDisplayManager are only in SF order if
    // LDM is configured as passthrough and either it is one-to-one or the
    // remap optimization is applied.
    mPhysicalDisplayManager.setSFDisplayOrder( mbPassthrough );

    // Set/check primary size.
    const int32_t pri = mSurfaceFlingerToLogical[ HWC_DISPLAY_PRIMARY ];
    ALOG_ASSERT( pri != -1 );
    const LogicalDisplay* pPrimary = mpLogicalDisplay[ pri ];
    ALOG_ASSERT( pPrimary );
    const uint32_t priW = pPrimary->getSizeWidth( );
    const uint32_t priH = pPrimary->getSizeHeight( );
    if ( mPrimaryWidth | mPrimaryHeight )
    {
        LOG_FATAL_IF( ( priW != mPrimaryWidth )
                   || ( priH != mPrimaryHeight ),
                    "LogicalDisplayManager : Trying to modify primary size %ux%u -> %ux%u",
                    mPrimaryWidth, mPrimaryHeight, priW, priH );
    }
    else
    {
        mPrimaryWidth = priW;
        mPrimaryHeight = priH;
        Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager : Locked primary size to %ux%u", mPrimaryWidth, mPrimaryHeight );
    }

    ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : update results:\n%s", dump().string() );
    ALOGD_IF( LOGDISP_DEBUG, "------------------------------------------------------------------------------------------" );

    // Reset physical display manager remap.
    mPhysicalDisplayManager.resetRemap( );

    if ( mbPassthrough && !mbOneToOne )
    {
        //  Passthrough with remap.
        ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : Setting up passthrough remap" );
        for ( uint32_t sf = 0; sf < cMaxSupportedSFDisplays; ++sf )
        {
            const int32_t logicalIndex = mSurfaceFlingerToLogical[ sf ];
            if ( logicalIndex == -1 )
                continue;
            const LogicalDisplay* pLD = mpLogicalDisplay[ logicalIndex ];
            if ( pLD == NULL )
                continue;
            ALOG_ASSERT( pLD->getLogicalType() == LogicalDisplay::LOGICAL_TYPE_PASSTHROUGH );
            AbstractPhysicalDisplay* pPhysical = pLD->getPhysical( );
            ALOG_ASSERT( pPhysical );
            const uint32_t logIndex = pLD->getDisplayManagerIndex( );
            const uint32_t phyIndex = pPhysical->getDisplayManagerIndex( );
            ALOGD_IF( LOGDISP_DEBUG, "  Remap %u->%u", logIndex, phyIndex );
            mPhysicalDisplayManager.setRemap( logIndex, phyIndex );
        }
    }

    // Assume filter output change.
    mbGeometryChange = true;
}

void LogicalDisplayManager::reflectChanges( void )
{
    // Which displays to unplug/plug/change size.
    uint32_t unplug = 0;
    uint32_t plug = 0;
    uint32_t sizeChange = 0;

    // On any config change we must wait for all previous displays to be torn down.
    // We are limited here by SF which wants only one change per frame.
    if ( mbInConfigChange )
    {
        Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager : Waiting to complete config [SF Displays %u]", getNumSurfaceFlingerDisplays( ) );
        if ( getNumSurfaceFlingerDisplays( ) == 0 )
        {
            mbInConfigChange = false;
            mbDirtyConfig = false;
            // Destroy logical.
            destroyLogical( );
            // Complete config change.
            createLogical( );
            // This will flow through the mbDirtyPhys path below to plug newly available displays.
        }
        else
        {
            // We must wait for a config change to remove existing displays first.
            return;
        }
    }

    if ( mbDirtyConfig )
    {
        // Unplug everything and wait for all to be removed.
        mbInConfigChange = true;
        unplug = getAvailableLogical( );
        Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager : Unplug all to begin config change 0x%x", unplug );
    }
    else if ( mbDirtyPhys )
    {
        // Available before.
        const uint32_t before = getAvailableLogical( );
        // Sizes before.
        uint32_t sizeBefore[ cMaxSupportedSFDisplays ][2];
        for ( uint32_t sf = 0; sf < cMaxSupportedSFDisplays; ++sf )
        {
            int32_t logical = mSurfaceFlingerToLogical[ sf ];
            if ( logical >= 0 )
            {
                LogicalDisplay* pLogical = mpLogicalDisplay[ logical ];
                sizeBefore[ sf ][ 0 ] = pLogical->getSizeWidth( );
                sizeBefore[ sf ][ 1 ] = pLogical->getSizeHeight( );
                ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : SF%u size before %ux%u", sf, sizeBefore[ sf ][ 0 ], sizeBefore[ sf ][ 1 ] );
            }
            else
            {
                sizeBefore[ sf ][ 0 ] = 0;
                sizeBefore[ sf ][ 1 ] = 0;
            }
        }

        // Update logical displays.
        updateAvailability( );

        // Available after.
        const uint32_t after = getAvailableLogical();
        // Sizes after.
        uint32_t sizeAfter[ cMaxSupportedSFDisplays ][2];
        for ( uint32_t sf = 0; sf < cMaxSupportedSFDisplays; ++sf )
        {
            int32_t logical = mSurfaceFlingerToLogical[ sf ];
            if ( logical >= 0 )
            {
                LogicalDisplay* pLogical = mpLogicalDisplay[ logical ];
                sizeAfter[ sf ][ 0 ] = pLogical->getSizeWidth( );
                sizeAfter[ sf ][ 1 ] = pLogical->getSizeHeight( );
                ALOGD_IF( LOGDISP_DEBUG, "LogicalDisplayManager : SF%u size after %ux%u", sf, sizeAfter[ sf ][ 0 ], sizeAfter[ sf ][ 1 ] );
            }
            else
            {
                sizeAfter[ sf ][ 0 ] = 0;
                sizeAfter[ sf ][ 1 ] = 0;
            }
        }

        // Unplug/plug changes.
        unplug = before & ~after;
        plug = after & ~before;

        // Size changes.
        const uint32_t unchanged = before & after;
        for ( uint32_t d = 0; d  < cMaxSupportedLogicalDisplays; ++d )
        {
            if ( unchanged & (1<<d) )
            {
                uint32_t sf = mLogicalToSurfaceFlinger[ d ];
                if ( ( sizeBefore[ sf ][ 0 ] != sizeAfter[ sf ][ 0 ] )
                  || ( sizeBefore[ sf ][ 1 ] != sizeAfter[ sf ][ 1 ] ) )
                {
                    sizeChange |= (1<<d);
                }
            }
        }

        mbDirtyPhys = false;
    }

    if ( plug | unplug | sizeChange )
    {
        Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager reflectChanges : Unplug 0x%x Plug 0x%x Size change 0x%x", unplug, plug, sizeChange );
    }

    // Size changes.
    if ( sizeChange )
    {
        for ( uint32_t d = 0; d < cMaxSupportedLogicalDisplays; ++d )
        {
            if ( sizeChange & (1<<d) )
            {
                LogicalDisplay* pLogical = mpLogicalDisplay[ d ];
                LOG_FATAL_IF( pLogical->getSurfaceFlingerIndex() == HWC_DISPLAY_PRIMARY, "Unexpected size change on primary" );
                LOG_FATAL_IF( pLogical->getSurfaceFlingerIndex() == HWC_DISPLAY_VIRTUAL, "Unexpected size change on virtual" );
                Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager : Logical display size change notification L%u/%p/%s (SF%u)",
                    d, pLogical, pLogical->dump().string(), pLogical->getSurfaceFlingerIndex() );
                mHwc.notifyDisplayChangeSize( pLogical );
            }
        }
    }

    // Forward unplug notifications for the logical displays.
    if ( unplug )
    {
        for ( uint32_t d = 0; d < cMaxSupportedLogicalDisplays; ++d )
        {
            if ( unplug & (1<<d) )
            {
                LogicalDisplay* pLogical = mpLogicalDisplay[ d ];
                if ( pLogical )
                {
                    Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager : Logical display unavailable notification L%u/%p/%s (SF%u)",
                        d, pLogical, pLogical->dump().string(), pLogical->getSurfaceFlingerIndex() );
                    mHwc.notifyDisplayUnavailable( pLogical );
                }
            }
        }
    }

    // Forward plug notifications for the logical displays.
    if ( plug )
    {
        for ( uint32_t d = 0; d < cMaxSupportedLogicalDisplays; ++d )
        {
            if ( plug & (1<<d) )
            {
                LogicalDisplay* pLogical = mpLogicalDisplay[ d ];
                if ( pLogical )
                {
                    Log::alogd( LOGDISP_DEBUG, "LogicalDisplayManager : Logical display available notification L%u/%p/%s -> SF%u",
                        d, pLogical, pLogical->dump().string(), mLogicalToSurfaceFlinger[d] );
                    mHwc.notifyDisplayAvailable( pLogical, mLogicalToSurfaceFlinger[d] );
                }
            }
        }
    }

    if ( plug | unplug | sizeChange )
    {
        // New frame please.
        mHwc.forceGeometryChangeAndRedraw();
    }
}



}; // namespace hwc
}; // namespace ufo
}; // namespace intel

