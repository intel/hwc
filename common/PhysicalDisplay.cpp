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

#include "PhysicalDisplay.h"
#include "Utils.h"
#include "Log.h"
#include "SoftwareVsyncThread.h"
#include "DisplayCaps.h"
#include "DisplayState.h"

#include <ufo/graphics.h> // for HAL_PIXEL_FORMAT..._INTEL

#include <utils/Thread.h>
#include <cutils/properties.h>
#include <math.h>

namespace intel {
namespace ufo {
namespace hwc {

//*****************************************************************************
//
// Display class - responsible for handling everything related to a display
//
//*****************************************************************************

PhysicalDisplay::PhysicalDisplay(Hwc& hwc) :
    mHwc( hwc ),
    mPhysicalDisplayManager( hwc.getPhysicalDisplayManager() ),
    mSfIndex( INVALID_DISPLAY_ID ),
    mDmIndex( INVALID_DISPLAY_ID ),
    meDisplayType( eDTUnspecified ),
    mVsyncPeriod( INTEL_HWC_DEFAULT_REFRESH_PERIOD_NS ),
    mAppliedTimingIndex( UnknownDisplayTiming ),
    mRequestedTimingIndex( UnknownDisplayTiming ),
    mNotifiedTimingIndex( UnknownDisplayTiming ),
    mbRequestedTiming( false ),
    mbNotifiedTiming( false ),
    mWidthmm( 0 ),
    mHeightmm( 0 ),
    mbSoftwareVSyncEnabled( false ),
    mbRegisterWithHwc( true ),
    mbNotifiedAvailable( false ),
    mbProxyOnly( false ),
    mpDisplayCaps( NULL ),
    mUserTimingIndex( -1 )
{
    memset( &mGlobalScalingRequested,  0, sizeof(mGlobalScalingRequested) );
    memset( &mGlobalScalingActive, 0, sizeof(mGlobalScalingActive) );
}

void PhysicalDisplay::initializeOptions(const char* prefix, uint32_t optionIndex)
{
    ALOGD_IF( PHYDISP_DEBUG, "Phy %p initializeOptions", this );
    // Initialise our options. Note, we need an option index to be specified.
    // Do not force update with these options (to avoid unexpected sync from arbitrary threads,
    //  but also because the SetUser*** APIs should handle updates).
    mUserConfig.mMode.setPersistent(true);
    mUserConfig.mMode.initialize( String8::format("%smode%d", prefix, optionIndex), "0x0@0-0" );
    mUserConfig.mMode.setForceGeometryChange( false );
    mUserConfig.mScalingMode.setPersistent(true);
    mUserConfig.mScalingMode.initialize( String8::format("%sscalemode%d", prefix, optionIndex), HWCS_SCALE_FIT );
    mUserConfig.mScalingMode.setForceGeometryChange( false );
    mUserConfig.mOverscan.setPersistent(true);
    mUserConfig.mOverscan.initialize( String8::format("%soverscan%d", prefix, optionIndex), "0x0" );
    mUserConfig.mOverscan.setForceGeometryChange( false );
    Log::alogd( PHYDISP_DEBUG, "P%u Initialize options mMode        %s", getDisplayManagerIndex(), mUserConfig.mMode.dump().string() );
    Log::alogd( PHYDISP_DEBUG, "P%u Initialize options mScalingMode %s", getDisplayManagerIndex(), mUserConfig.mScalingMode.dump().string() );
    Log::alogd( PHYDISP_DEBUG, "P%u Initialize options mOverscan    %s", getDisplayManagerIndex(), mUserConfig.mOverscan.dump().string() );
}

PhysicalDisplay::~PhysicalDisplay()
{
    if (mpSoftwareVsyncThread != NULL)
    {
        mpSoftwareVsyncThread->terminate( );
    }
}

bool PhysicalDisplay::notifyNumActiveDisplays( uint32_t activeDisplays )
{
    // TODO:
    //  Remove once we have a generic notification framework.
    //  For now, punt through to caps displays state.
    if ( mpDisplayCaps )
    {
        DisplayState* pState = mpDisplayCaps->editState();
        if ( pState )
        {
            pState->setNumActiveDisplays( activeDisplays );
            // Return true to acknowledge the change.
            return true;
        }
    }
    return false;
}

void PhysicalDisplay::notifyDisplayTimingChange( const Timing& t )
{
    // TODO:
    //  Convert to a notification framework.
    //  For now, punt through to caps displays state.
    if ( mpDisplayCaps )
    {
        DisplayState* pState = mpDisplayCaps->editState();
        if ( pState )
        {
            pState->setTiming( t );
        }
    }
}

const char* PhysicalDisplay::getName() const
{
    return mpDisplayCaps ? mpDisplayCaps->getName() : "";
}

void PhysicalDisplay::setProxyOnly( bool bProxyOnly )
{
    mbProxyOnly = bProxyOnly;
}

bool PhysicalDisplay::getProxyOnly( void ) const
{
    return mbProxyOnly;
}

void PhysicalDisplay::setVSyncPeriod( uint32_t vsyncPeriod )
{
    ALOG_ASSERT( vsyncPeriod );
    mVsyncPeriod = vsyncPeriod;
    if ( mpSoftwareVsyncThread != NULL )
    {
        mpSoftwareVsyncThread->updatePeriod( vsyncPeriod );
    }
}

int PhysicalDisplay::onVSyncEnable( bool bEnable )
{
    if ( bEnable )
    {
        createSoftwareVSyncGeneration( );
        enableSoftwareVSyncGeneration( );
    }
    else
    {
        disableSoftwareVSyncGeneration( );
    }
    return OK;
}

void PhysicalDisplay::initUserConfig( void )
{
    // Timing.
    const char* buf = NULL;
    uint32_t width, height, refresh;
    Timing::EAspectRatio ratio;

    buf = mUserConfig.mMode;
    // Parse <xres>x<yres>@<Hz>-<ratio>, which was written in this format
    if ( ( buf == NULL) || ( sscanf( buf, "%ux%u@%u-%x", &width, &height, &refresh, &ratio ) != 4 ) )
    {
        width = 0;
        height = 0;
        refresh = 0;
        ratio = Timing::EAspectRatio::Any;
    }

    Timing t( width, height, refresh, 0, 0, 0, ratio );
    mUserTimingIndex = findDisplayTiming( t );
    copyDisplayTiming( mUserTimingIndex, mUserTiming );

    Log::alogd( MODE_DEBUG, "P%u Initialize user config - timing: %dx%d@%dHz-%s (matches timing %d %dx%d@%dHz-%s)",
        getDisplayManagerIndex(),
        width, height, refresh, Timing::dumpRatio(ratio).string(),
        mUserTimingIndex,
        mUserTiming.getWidth(), mUserTiming.getHeight(), mUserTiming.getRefresh(),
        Timing::dumpRatio(mUserTiming.getRatio()).string());

    GlobalScalingFilter& globalScalingFilter = mHwc.getGlobalScalingFilter( );

    // Scaling mode.
    uint32_t scalingMode = mUserConfig.mScalingMode;
    if (scalingMode < HWCS_SCALE_MAX_ENUM)
        globalScalingFilter.setUserScalingMode( getDisplayManagerIndex(), (EScalingMode)scalingMode );

    // Overscan
    int32_t xoverscan = 0, yoverscan = 0;
    const char *overscan_buf = NULL;

    overscan_buf = mUserConfig.mOverscan;

    if( overscan_buf != NULL )
    {
        if (sscanf(overscan_buf, "%dx%d", &xoverscan, &yoverscan ) == 2)
        {
            globalScalingFilter.setUserOverscan( getDisplayManagerIndex(), xoverscan, yoverscan );
        }
    }
}

void PhysicalDisplay::getUserOverscan(int32_t& xoverscan, int32_t& yoverscan) const
{
    GlobalScalingFilter& globalScalingFilter = mHwc.getGlobalScalingFilter( );
    globalScalingFilter.getUserOverscan( getDisplayManagerIndex(), xoverscan, yoverscan );
}

void PhysicalDisplay::setUserOverscan(int32_t xOverscan, int32_t yOverscan)
{
    Log::alogd( MODE_DEBUG, "P%u Set user overscan %d,%d", getDisplayManagerIndex(), xOverscan, yOverscan );

    mUserConfig.mOverscan.set( String8::format("%dx%d", xOverscan, yOverscan));

    // Implement Overscan via scaling filter.
    GlobalScalingFilter& scalingFilter = mHwc.getGlobalScalingFilter();
    scalingFilter.setUserOverscan( getDisplayManagerIndex(), xOverscan, yOverscan );

    mHwc.forceRedraw( );

    return;
}


void PhysicalDisplay::getUserScalingMode(EScalingMode& eScaling) const
{
    GlobalScalingFilter& globalScalingFilter = mHwc.getGlobalScalingFilter( );
    globalScalingFilter.getUserScalingMode( getDisplayManagerIndex(), eScaling );
}

void PhysicalDisplay::setUserScalingMode(EScalingMode eScaling)
{
    Log::alogd( MODE_DEBUG, "P%u Set user scaling mode %u", getDisplayManagerIndex(), eScaling );

    mUserConfig.mScalingMode.set( eScaling );

    // Implement Overscan via scaling filter.
    GlobalScalingFilter& scalingFilter = mHwc.getGlobalScalingFilter();
    scalingFilter.setUserScalingMode( getDisplayManagerIndex(), eScaling );

    mHwc.forceRedraw( );

    return;
}

void PhysicalDisplay::setDisplayTimings( Vector<Timing>& timings )
{
    Mutex::Autolock _l( mDisplayTimingsLock );

    mDisplayTimings.clear();

    for (uint32_t i = 0; i < timings.size(); i++)
    {
        const Timing& t = timings[i];
        mDisplayTimings.push( t );
    }

    notifyTimingsModified( );
}

void PhysicalDisplay::notifyTimingsModified( void )
{
    initUserConfig( );
}

void PhysicalDisplay::copyDisplayTimings( Vector<Timing>& timings ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );
    Mutex::Autolock _l( mDisplayTimingsLock );

    timings.clear();

    for (uint32_t i = 0; i < mDisplayTimings.size(); i++)
    {
        const Timing& t = mDisplayTimings[i];
        timings.push( t );
    }
}

bool PhysicalDisplay::copyDisplayTiming( uint32_t timingIndex, Timing& timing ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );
    Mutex::Autolock _l( mDisplayTimingsLock );

    if ( timingIndex < mDisplayTimings.size() )
    {
        timing = mDisplayTimings[ timingIndex ];
        return true;
    }
    return false;
}

int32_t PhysicalDisplay::getDefaultDisplayTiming( void ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );

    ALOGD_IF( MODE_DEBUG, "getDefaultDisplayTiming" );
    return mUserTimingIndex;
}

void PhysicalDisplay::copyDefaultDisplayTiming( Timing& timing ) const
{
    int32_t timingIndex = getDefaultDisplayTiming( );
    if ( !copyDisplayTiming( timingIndex, timing ) )
    {
        Log::aloge( true, "P%u default display timing not available (%d)", getDisplayManagerIndex(), timingIndex );
    }
}

int32_t PhysicalDisplay::findDisplayTiming( const Timing& requested, uint32_t findFlags ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );
    Mutex::Autolock _l( mDisplayTimingsLock );

    ALOGD_IF(MODE_DEBUG, "findDisplayTiming %s (Zero is wildcard): Flags %s|%s",
        requested.dump().string(),
        findFlags & FIND_MODE_FLAG_CLOSEST_REFRESH_MULTIPLE ? "CLOSEST REFRESH MULTIPLE" : "-",
        findFlags & FIND_MODE_FLAG_FALLBACK_TO_DEFAULT      ? "FALLBACK TO DEFAULT"      : "-");

    int matchedTimingIndex = -1;
    int preferredTimingIndex = -1;

    const uint32_t w = requested.getWidth();
    const uint32_t h = requested.getHeight();
    const uint32_t f = requested.getRefresh();
    const Timing::EAspectRatio r = requested.getRatio();

    for (uint32_t i = 0; i < mDisplayTimings.size(); i++)
    {
        const Timing& t = mDisplayTimings[i];

        ALOGD_IF(MODE_DEBUG, "findDisplayTiming Checking Timing %s", t.dump().string());

        if (t.isPreferred())
        {
            // Take a note of the first preferred display timing
            if (preferredTimingIndex < 0)
                preferredTimingIndex = i;

            // A request for the preferred mode will just match the first preferred mode listed
            // whatever the requested resolutions
            if (requested.isPreferred())
            {
                matchedTimingIndex = i;
                break;
            }

            // If we have wildcard width, height Freq, then may as well match the preferred mode now
            if (w == 0 && h == 0 && f == 0)
            {
                matchedTimingIndex = i;
                break;
            }
        }

        // Consider 0 as a wildcard, it matches anything.
        bool bMatchesGeometry = ((w == 0 || w == t.getWidth()) &&
                                 (h == 0 || h == t.getHeight()) &&
                                 (r == Timing::EAspectRatio::Any || r == t.getRatio()) &&
                                 (requested.isInterlaced() == t.isInterlaced()));

        bool bMatchesRefresh = (f == 0 || f == t.getRefresh());

        if (bMatchesGeometry && bMatchesRefresh)
        {
            ALOGD_IF(MODE_DEBUG, "findDisplayTiming Timing %s Matches Geometry and Refresh", t.dump().string());
            matchedTimingIndex = i;
            break;
        }

        if (bMatchesGeometry && ( findFlags & FIND_MODE_FLAG_CLOSEST_REFRESH_MULTIPLE))
        {
            if ((matchedTimingIndex < 0) || (mDisplayTimings[matchedTimingIndex].getRefresh() > t.getRefresh()))
            {
                if ((t.getRefresh() % f ) == 0)
                {
                    ALOGD_IF(MODE_DEBUG, "findDisplayTiming Timing %s Matches Geometry and Multiple of Refresh", t.dump().string());
                    matchedTimingIndex = i;
                }
            }
        }
    }

    if ( ( matchedTimingIndex < 0 ) && ( findFlags & FIND_MODE_FLAG_FALLBACK_TO_DEFAULT ) )
    {
        // Reverting to the preferred mode, if the user set mode is not present
        matchedTimingIndex = preferredTimingIndex;

        // If we dont match and have no preferred mode, just go for the first mode
        if (matchedTimingIndex < 0)
            matchedTimingIndex = 0;
    }

    if ( matchedTimingIndex >= 0 )
    {
        ALOGD_IF(MODE_DEBUG, "findDisplayTiming %s : Best match:%s", requested.dump().string(), mDisplayTimings[matchedTimingIndex].dump().string());
    }
    else
    {
        ALOGD_IF(MODE_DEBUG, "findDisplayTiming %s : Did not find match", requested.dump().string());
    }

    return matchedTimingIndex;
}

bool PhysicalDisplay::setSpecificDisplayTiming( uint32_t timingIndex, bool bSynchronize )
{
    Log::alogd( MODE_DEBUG, "P%u Set specific display timing index: %d sync: %d", getDisplayManagerIndex(), timingIndex, bSynchronize );
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );

    Timing t;
    if ( !copyDisplayTiming( timingIndex, t ) )
    {
        ALOGW( "setSpecificDisplayTiming index %d is out-of-range", timingIndex );
        return false;
    }

    // Update the requested mode.
    setRequestedTiming( timingIndex );

    // Synchronize with Hwc (blocking).
    // But only if the display is already present.
    if ( mbNotifiedAvailable && bSynchronize )
    {
        ALOGD_IF( MODE_DEBUG, "setSpecificDisplayTiming synchronize, index = %d, %s", timingIndex, t.dump().string());
        mHwc.synchronize( 0 );
        ALOGD_IF( MODE_DEBUG, "setSpecificDisplayTiming synchronize complete, mode = %d, %s", timingIndex, t.dump().string());
    }
    else
    {
        mHwc.forceRedraw( );
    }

    return true;
}

bool PhysicalDisplay::setDisplayTiming( const Timing& timing, bool bSynchronize, Timing* pResultantTiming )
{
    Log::alogd( MODE_DEBUG, "P%u Set display timing %s ", getDisplayManagerIndex(), timing.dump().string());
    int32_t timingIndex = findDisplayTiming( timing );

    if ( timingIndex != -1 )
    {
        if ( setSpecificDisplayTiming( timingIndex, bSynchronize ) )
        {
            if ( pResultantTiming )
            {
                copyDisplayTiming( timingIndex, *pResultantTiming );
            }
            return true;
        }
    }
    return false;
}

bool PhysicalDisplay::setUserDisplayTiming( const Timing& timing, bool bSynchronize )
{
    Log::alogd( MODE_DEBUG, "P%u Set user display timing %s", getDisplayManagerIndex(), timing.dump().string() );

    mUserConfig.mMode.set( String8::format("%dx%d@%d-%x",timing.getWidth(), timing.getHeight(), timing.getRefresh(), timing.getRatio()) );

    int32_t timingIndex = findDisplayTiming( timing );
    if ( timingIndex != -1 )
    {
        if ( setSpecificDisplayTiming( timingIndex, bSynchronize )  )
        {
            mUserTimingIndex = timingIndex;
            copyDisplayTiming( mUserTimingIndex, mUserTiming );
            Log::alogd( MODE_DEBUG, "P%u Succesful set user display timing (resultant timing %d %s)",
                getDisplayManagerIndex(), mUserTimingIndex, mUserTiming.dump().string() );
            return true;
        }
    }

    Log::alogd( MODE_DEBUG, "P%u Failed set user display timing", getDisplayManagerIndex() );
    return false;
}

bool PhysicalDisplay::getUserDisplayTiming( Timing& timing ) const
{
    // Return user timing.
    timing = mUserTiming;
    Log::alogd( MODE_DEBUG, "P%u Get user display timing %s", getDisplayManagerIndex(), timing.dump().string() );
    return true;
}

void PhysicalDisplay::resetUserDisplayTiming( void )
{
    Log::alogd( MODE_DEBUG, "P%u Reset user display timing", getDisplayManagerIndex() );
    // Request the default display timing.
    // This will reset user timing config to "0x0@0-0".
    Timing defaultTiming;
    setUserDisplayTiming( defaultTiming );
}

int PhysicalDisplay::onGetDisplayConfigs( uint32_t* paConfigHandles, uint32_t* pNumConfigs ) const
{
    // Total available configs is always returned.
    if (!pNumConfigs)
        return BAD_VALUE;

    ALOGD_IF( PHYDISP_DEBUG || MODE_DEBUG,
              "PhysicalDisplay::onGetDisplayConfigs paConfigHandles %p, pNumConfigs %p/%zd",
              paConfigHandles, pNumConfigs, *pNumConfigs );

    // configs are returned if num configs is non-zero on entry.
    if ((*pNumConfigs != 0) && !paConfigHandles)
        return BAD_VALUE;

    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );
    Mutex::Autolock _l( mDisplayTimingsLock );

    if (mDisplayTimings.size() < 1)
    {
        ALOGE( "getDisplayConfigs : SF%x/P%u has Zero Configs", mSfIndex, getDisplayManagerIndex() );
        return INVALID_OPERATION;
    }

    // Write out the minimum of either the number allocated or the number we have
    uint32_t writeOut = min( *pNumConfigs, uint32_t(mDisplayTimings.size()) );

    // API requires we always return the total available configs.
    *pNumConfigs = mDisplayTimings.size();

    for (uint32_t i = 0; i < writeOut; i++)
    {
        paConfigHandles[i] = CONFIG_HANDLE_BASE + i;
    }

    return OK;
}

int32_t PhysicalDisplay::getDefaultOutputFormat( void ) const
{
    return getDisplayCaps().getDefaultOutputFormat();
}

uint32_t PhysicalDisplay::getNotifiedRefresh( void ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mTimingLock );
    if ( mNotifiedTimingIndex < mDisplayTimings.size() )
    {
        return mDisplayTimings[ mNotifiedTimingIndex ].getRefresh();
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s notified mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mNotifiedTimingIndex, mDisplayTimings.size() );
    return INTEL_HWC_DEFAULT_REFRESH_RATE;
}

uint32_t PhysicalDisplay::getNotifiedWidth( void  ) const
{
    if ( mNotifiedTimingIndex < mDisplayTimings.size() )
    {
        return mDisplayTimings[ mNotifiedTimingIndex ].getWidth();
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s notified mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mNotifiedTimingIndex, mDisplayTimings.size() );
    return 0;
}

uint32_t PhysicalDisplay::getNotifiedHeight( void ) const
{
    if ( mNotifiedTimingIndex < mDisplayTimings.size() )
    {
        return mDisplayTimings[ mNotifiedTimingIndex ].getHeight();
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s notified mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mNotifiedTimingIndex, mDisplayTimings.size() );
    return 0;
}

int32_t PhysicalDisplay::getNotifiedXdpi( void ) const
{
    if ( mNotifiedTimingIndex < mDisplayTimings.size() )
    {
        return getXdpiForTiming( mDisplayTimings[ mNotifiedTimingIndex ] );
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s notified mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mNotifiedTimingIndex, mDisplayTimings.size() );
    return getDefaultDpi( );
}

int32_t PhysicalDisplay::getNotifiedYdpi( void ) const
{
    if ( mNotifiedTimingIndex < mDisplayTimings.size() )
    {
        return getYdpiForTiming( mDisplayTimings[ mNotifiedTimingIndex ] );
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s notified mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mNotifiedTimingIndex, mDisplayTimings.size() );
    return getDefaultDpi( );
}

uint32_t PhysicalDisplay::getNotifiedVSyncPeriod( void ) const
{
    if ( mNotifiedTimingIndex < mDisplayTimings.size() )
    {
        return convertRefreshRateToPeriodNs( mDisplayTimings[ mNotifiedTimingIndex ].getRefresh() );
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s notified mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mNotifiedTimingIndex, mDisplayTimings.size() );
    return ( 1000000000 / INTEL_HWC_DEFAULT_REFRESH_RATE );
}

Timing::EAspectRatio PhysicalDisplay::getNotifiedRatio( void ) const
{
    if ( mNotifiedTimingIndex < mDisplayTimings.size() )
    {
        return mDisplayTimings[ mNotifiedTimingIndex ].getRatio( );
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s notified mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mNotifiedTimingIndex, mDisplayTimings.size() );
    return Timing::EAspectRatio::Any;
}

uint32_t PhysicalDisplay::getAppliedWidth( void  ) const
{
    if ( mAppliedTimingIndex < mDisplayTimings.size() )
    {
        return mDisplayTimings[ mAppliedTimingIndex ].getWidth();
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s applied mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mAppliedTimingIndex, mDisplayTimings.size() );
    return 0;
}

uint32_t PhysicalDisplay::getAppliedHeight( void ) const
{
    if ( mAppliedTimingIndex < mDisplayTimings.size() )
    {
        return mDisplayTimings[ mAppliedTimingIndex ].getHeight();
    }
    ALOGD_IF( PHYDISP_DEBUG, "P%u%s applied mode %u is out of range (v %zd)",
        getDisplayManagerIndex(), __FUNCTION__, mAppliedTimingIndex, mDisplayTimings.size() );
    return 0;
}

int32_t PhysicalDisplay::getDefaultDpi() const
{
    int32_t dpi;
    if ( getDisplayType() == eDTPanel )
        dpi = INTEL_HWC_DEFAULT_INTERNAL_PANEL_DPI * 1000;
    else
        dpi = INTEL_HWC_DEFAULT_EXTERNAL_DISPLAY_DPI * 1000;
    return dpi;
}

int32_t PhysicalDisplay::getXdpiForTiming( const Timing& t ) const
{
    if (mWidthmm)
        return t.getWidth()* 25400 / mWidthmm;
    else
        return getDefaultDpi();
}

int32_t PhysicalDisplay::getYdpiForTiming( const Timing& t ) const
{
    if (mHeightmm)
        return t.getHeight() * 25400 / mHeightmm;
    else
        return getDefaultDpi();
}

int PhysicalDisplay::onGetDisplayAttribute(uint32_t configHandle, EAttribute attribute, int32_t* pValue) const
{
    ALOGD_IF( PHYDISP_DEBUG || MODE_DEBUG, "PhysicalDisplay::onGetDisplayAttribute config handle:%x, attribute:%d", configHandle, attribute );

    ALOG_ASSERT( pValue );

    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );
    Mutex::Autolock _l( mDisplayTimingsLock );

    // Figure out which config the caller requires.
    ALOG_ASSERT( configHandle >= CONFIG_HANDLE_BASE );
    uint32_t timingIndex = configHandle - CONFIG_HANDLE_BASE;

    if ( timingIndex >= mDisplayTimings.size() )
    {
        return INVALID_OPERATION;
    }
    const Timing& t = mDisplayTimings[ timingIndex ];

    ALOGD_IF( PHYDISP_DEBUG || MODE_DEBUG,
              "PhysicalDisplay::onGetDisplayAttribute Timing:%d %s",
              timingIndex, t.dump().string());

    switch (attribute)
    {
    case ATTRIB_VSYNC:
        // The vsync period in nanoseconds
        *pValue = convertRefreshRateToPeriodNs( t.getRefresh() );
       break;

    // The number of pixels in the horizontal and vertical directions
    case ATTRIB_WIDTH:
        *pValue = t.getWidth();
        break;
    case ATTRIB_HEIGHT:
        *pValue = t.getHeight();
        break;

    // The number of pixels per thousand inches of this configuration.
    case ATTRIB_XDPI:
        *pValue = getXdpiForTiming( t );
        break;
    case ATTRIB_YDPI:
        *pValue = getYdpiForTiming( t );
        break;

    default:
        *pValue = 0;
        ALOGE("PhysicalDisplay::onGetDisplayAttribute: UNKNOWN ATTRIBUTE %d", attribute);
        break;
    }

    ALOGD_IF( PHYDISP_DEBUG || MODE_DEBUG, "PhysicalDisplay::onGetDisplayAttribute: %s = %d",
        attribute == ATTRIB_VSYNC  ? "ATTRIB_VSYNC " :
        attribute == ATTRIB_WIDTH  ? "ATTRIB_WIDTH " :
        attribute == ATTRIB_HEIGHT ? "ATTRIB_HEIGHT" :
        attribute == ATTRIB_XDPI   ? "ATTRIB_XDPI  " :
        attribute == ATTRIB_YDPI   ? "ATTRIB_YDPI  " : "UNKNOWN",
        *pValue);

    return 0;
}

int PhysicalDisplay::onGetActiveConfig( void ) const
{
    // This entry point is used by SF to determine attributes for current mode.
    // i.e. next and subsequent frames. We *MUST* return the current notified index.
    Log::alogd( PHYDISP_DEBUG || MODE_DEBUG, "P%u Get active config:0x%x", getDisplayManagerIndex(), mNotifiedTimingIndex );
    return mNotifiedTimingIndex;
}

int PhysicalDisplay::onSetActiveConfig( uint32_t configIndex )
{
    ALOGD_IF( PHYDISP_DEBUG || MODE_DEBUG, "PhysicalDisplay::onSetActiveConfig config:%x", configIndex );
    // Set timing for this config.
    // This can not be synchronized because (at least for N-Dessert) SF will call
    // onSetActiveConfig from its main thread.
    const uint32_t timingIndex = configIndex;
    if ( setSpecificDisplayTiming( timingIndex ) )
    {
        return OK;
    }
    return -ENOENT;
}

void PhysicalDisplay::createSoftwareVSyncGeneration( void )
{
    if (mpSoftwareVsyncThread == NULL)
    {
        Log::alogd( VSYNC_DEBUG, "HWC:P%u SW VSYNC Created", getDisplayManagerIndex() );
        uint32_t initialPeriod = mVsyncPeriod ? mVsyncPeriod : INTEL_HWC_DEFAULT_REFRESH_PERIOD_NS;
        mpSoftwareVsyncThread = new SoftwareVsyncThread(mHwc, this, initialPeriod );
        if ( mpSoftwareVsyncThread == NULL )
        {
            Log::aloge( true, "HWC:P%u Failed to create sw vsync thread", getDisplayManagerIndex() );
            return;
        }
        mbSoftwareVSyncEnabled = false;
    }
}

void PhysicalDisplay::enableSoftwareVSyncGeneration( void )
{
    ALOGE_IF( mpSoftwareVsyncThread == NULL, "HWC:P%u Software vsync thread not created", getDisplayManagerIndex() );
    if ( mbSoftwareVSyncEnabled )
        return;
    ATRACE_INT_IF( VSYNC_DEBUG, String8::format( "HWC:P%u SW VSYNC", getDisplayManagerIndex() ).string(), 1 );
    Log::alogd( VSYNC_DEBUG, "HWC:P%u SW VSYNC Enabled", getDisplayManagerIndex() );
    mbSoftwareVSyncEnabled = true;
    if ( mpSoftwareVsyncThread != NULL )
    {
        mpSoftwareVsyncThread->enable();
    }
}

void PhysicalDisplay::disableSoftwareVSyncGeneration( void )
{
    if ( !mbSoftwareVSyncEnabled )
        return;
    if ( mpSoftwareVsyncThread != NULL )
    {
        mpSoftwareVsyncThread->disable( true );
    }
    ATRACE_INT_IF( VSYNC_DEBUG, String8::format( "HWC:P%u SW VSYNC", getDisplayManagerIndex() ).string(), 0 );
    Log::alogd( VSYNC_DEBUG, "HWC:P%u SW VSYNC Disabled", getDisplayManagerIndex() );
    mbSoftwareVSyncEnabled = false;
}

void PhysicalDisplay::destroySoftwareVSyncGeneration( void )
{
    if ( mpSoftwareVsyncThread != NULL )
    {
        Log::alogd( VSYNC_DEBUG, "HWC:P%u SW VSYNC Destroyed", getDisplayManagerIndex() );
        if ( mbSoftwareVSyncEnabled )
        {
            mpSoftwareVsyncThread->disable( true );
            mbSoftwareVSyncEnabled = false;
        }
        mpSoftwareVsyncThread = NULL;
    }
}

// If the display supports dynamic modes then we are given two identical modes
// with different values for refresh and we can set any value inbetween.
// This function processes the timings list and updates any modes to reflect the
// minimum refresh value they can be programmed to.
void PhysicalDisplay::processDynamicDisplayTimings( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );
    Mutex::Autolock _l( mDisplayTimingsLock );

    for (unsigned t = 0 ; t < mDisplayTimings.size(); ++t)
    {
        const Timing& tt = mDisplayTimings.itemAt(t);
        uint32_t thisRefresh = tt.getRefresh();
        uint32_t minRefresh = thisRefresh;

        // Search the list for an identical mode with the lowest refresh value.
        for (unsigned m = 0 ; m < mDisplayTimings.size(); ++m)
        {
            if (m == t) continue;

            const Timing& tm = mDisplayTimings.itemAt(m);
            if ((tt.getWidth() == tm.getWidth())
                && (tt.getHeight() == tm.getHeight())
                && (tt.getRatio() == tm.getRatio())
                && (tt.getFlags() == tm.getFlags()))
            {
                uint32_t refresh = tm.getRefresh();
                if (refresh < minRefresh)
                {
                    minRefresh = refresh;
                }
            }
        }

        if (minRefresh == thisRefresh)
        {
            ALOGD_IF(MODE_DEBUG, "Display processDynamicDisplayTimings %s", tt.dump().string());
        }
        else
        {
            // We have a new minimum so update the mode.
            Timing nt( tt.getWidth(), tt.getHeight(), tt.getRefresh(),
                       tt.getPixelClock(), tt.getHTotal(), tt.getVTotal(),
                       tt.getRatio(), tt.getFlags(), minRefresh);
            mDisplayTimings.editItemAt(t) = nt;
            ALOGD_IF(MODE_DEBUG, "Display processDynamicDisplayTimings %s", nt.dump().string());
        }
    }
}

void PhysicalDisplay::setAppliedTiming( uint32_t timingIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mTimingLock );
    Mutex::Autolock _l( mTimingLock );
    doSetAppliedTiming( timingIndex );
}

void PhysicalDisplay::doSetAppliedTiming( uint32_t timingIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mTimingLock );
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mDisplayTimingsLock );
    Timing t;
    if ( copyDisplayTiming( timingIndex, t ) )
    {
        Log::add( "P%u Applying video timing %d : %s", getDisplayManagerIndex(), timingIndex, t.dump().string() );
        setVSyncPeriod( convertRefreshRateToPeriodNs( t.getRefresh() ) );
        // Clear notified mode once it is applied.
        if ( mbNotifiedTiming && ( timingIndex == mNotifiedTimingIndex ) )
        {
            Log::alogd( MODE_DEBUG, "P%u Notified timing %u now applied", getDisplayManagerIndex(), mNotifiedTimingIndex );
            mbNotifiedTiming = false;
        }
    }
    else
    {
        setVSyncPeriod( INTEL_HWC_DEFAULT_REFRESH_PERIOD_NS );
    }
    mAppliedTimingIndex = timingIndex;
}

void PhysicalDisplay::setRequestedTiming( uint32_t timingIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mTimingLock );
    Mutex::Autolock _l( mTimingLock );
    ALOG_ASSERT( timingIndex != UnknownDisplayTiming );
    if ( timingIndex != UnknownDisplayTiming )
    {
        Timing t;
        if ( copyDisplayTiming( timingIndex, t ) )
        {
            if ( mRequestedTimingIndex != timingIndex )
            {
                Log::alogd( MODE_DEBUG, "P%u Set new requested timing %d -> %d : %s", getDisplayManagerIndex(), mRequestedTimingIndex, timingIndex, t.dump().string() );
                mRequestedTimingIndex = timingIndex;
                mbRequestedTiming = true;
            }
            else
            {
                Log::alogd( MODE_DEBUG, "P%u Skip set new requested timing (no change) %d : %s", getDisplayManagerIndex(), timingIndex, t.dump().string() );
            }
        }
        else
        {
            Log::aloge( true, "P%u Requested timing %u is not valid", getDisplayManagerIndex(), timingIndex );
        }
    }
}

void PhysicalDisplay::cancelRequestedTiming( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mTimingLock );
    Mutex::Autolock _l( mTimingLock );
    Log::alogd( MODE_DEBUG, "P%u Cancel requested timing %d", getDisplayManagerIndex(), mNotifiedTimingIndex, mRequestedTimingIndex );
    mRequestedTimingIndex = UnknownDisplayTiming;
    mbRequestedTiming = false;
}

void PhysicalDisplay::notifyNewRequestedTiming( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mTimingLock );
    Mutex::Autolock _l( mTimingLock );
    // Forward next notification once any previous notification has been applied.
    if ( mbRequestedTiming && !mbNotifiedTiming )
    {
        ALOG_ASSERT( mRequestedTimingIndex != UnknownDisplayTiming );
        Timing t;
        if ( copyDisplayTiming( mRequestedTimingIndex, t ) )
        {
            if ( mNotifiedTimingIndex != mRequestedTimingIndex )
            {
                Log::alogd( MODE_DEBUG, "P%u Notifying size change (timing %d -> %d) : %s", getDisplayManagerIndex(), mNotifiedTimingIndex, mRequestedTimingIndex, t.dump().string() );

                // Move requested mode into notified mode.
                mNotifiedTimingIndex = mRequestedTimingIndex;
                // Latch the special zero config index (if SF activates config 0 then this mode must be used).
                mbRequestedTiming = false;
                mbNotifiedTiming = true;

                // Notify change.
                mPhysicalDisplayManager.notifyPhysicalChangeSize( this );
            }
            else
            {
                Log::alogd( MODE_DEBUG, "P%u Skip notifying new timing (no change) %d : %s", getDisplayManagerIndex(), mNotifiedTimingIndex, t.dump().string() );
            }
        }
        else
        {
            Log::aloge( true, "P%u New requested timing %u is not valid", getDisplayManagerIndex(), mRequestedTimingIndex );
        }
    }
}

bool PhysicalDisplay::haveNotifiedTimingChange( uint32_t& timingIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mTimingLock );
    Mutex::Autolock _l( mTimingLock );
    if ( mbNotifiedTiming )
    {
        ALOG_ASSERT( mNotifiedTimingIndex != UnknownDisplayTiming );
        Log::alogd( MODE_DEBUG, "P%u Have notified timing index %d", getDisplayManagerIndex(), mNotifiedTimingIndex );
        timingIndex = mNotifiedTimingIndex;
        return true;
    }
    return false;
}

void PhysicalDisplay::setInitialTiming( uint32_t timingIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mTimingLock );
    Mutex::Autolock _l( mTimingLock );
    Timing t;
    if ( copyDisplayTiming( timingIndex, t ) )
    {
        // Set/align requested/notified state.
        mRequestedTimingIndex = timingIndex;
        mNotifiedTimingIndex = timingIndex;
        mbRequestedTiming = false;
        mbNotifiedTiming = false;
        // Apply the mode.
        Log::alogd( MODE_DEBUG, "P%u Set initial timing index %d : %s", getDisplayManagerIndex(), timingIndex, t.dump().string() );
        doSetAppliedTiming( timingIndex );
    }
    else
    {
        Log::aloge( true, "P%%u Initial timing index %d is not valid", getDisplayManagerIndex(), timingIndex );
    }
}

void PhysicalDisplay::notifyAvailable( void )
{
    mbNotifiedAvailable = true;
    mPhysicalDisplayManager.notifyPhysicalAvailable( this );
}

void PhysicalDisplay::notifyUnavailable( void )
{
    mbNotifiedAvailable = false;
    mPhysicalDisplayManager.notifyPhysicalUnavailable( this );
}

String8 PhysicalDisplay::dump( void ) const
{
    String8 str;
    str = String8::format( "RPD:%u %8s : PhysSize:%ux%umm Timing:Requested 0x%x Notified 0x%x [Res:%ux%u, Period:%uus] Applied 0x%x",
        getDisplayManagerIndex(), dumpDisplayType( getDisplayType() ).string(),
        mWidthmm, mHeightmm,
        mRequestedTimingIndex,
        mNotifiedTimingIndex, getNotifiedWidth( ), getNotifiedHeight( ), getNotifiedVSyncPeriod( )/1000,
        mAppliedTimingIndex );
    return str;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
