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
#include "PhysicalDisplayManager.h"
#include "Log.h"
#include "PlaneAllocatorJB.h"
#include "AbstractPhysicalDisplay.h"


#if INTEL_HWC_INTERNAL_BUILD
#include "DebugFilter.h"
#endif

namespace intel {
namespace ufo {
namespace hwc {

// The minimum number of frames over which a display will remain 'idle' before
// it can be considered 'active' again.
// Must be >= 1 for the display idle optimizations to be active.
static const uint32_t cFramesKeptAtIdle = 2;

PhysicalDisplayManager::PhysicalDisplayManager( Hwc& hwc, CompositionManager& compositionManager ) :
    mHwc(hwc),
    mCompositionManager(compositionManager),
    mpDisplayNotificationReceiver(NULL),
    mbSFDisplayOrder( true ),
    mPhysicalDisplays( 0 ),
    mbRemapIndices( false ),
    mIdleTimeout(hwc),
    mEnablePlaneAllocator("planealloc", 1, false)
{
    for ( uint32_t d = 0; d < cMaxSupportedPhysicalDisplays; ++d )
    {
        mDisplayState[ d ].setIndex( d );
        mpPhysicalDisplay[ d ] = NULL;
    }
}

PhysicalDisplayManager::~PhysicalDisplayManager()
{
}

uint32_t PhysicalDisplayManager::registerDisplay(AbstractPhysicalDisplay* pDisplay)
{
    ALOG_ASSERT( pDisplay );

    ALOG_ASSERT( pDisplay->getDisplayManagerIndex( ) == INVALID_DISPLAY_ID );
    if ( mPhysicalDisplays >= cMaxSupportedPhysicalDisplays )
    {
        ALOGE( "Hardware display out of space (already registered %u physical displays)", mPhysicalDisplays );
        return INVALID_DISPLAY_ID;
    }

    for ( uint32_t d = 0; d < cMaxSupportedPhysicalDisplays; ++d )
    {
        if ( mpPhysicalDisplay[ d ] == NULL )
        {
            mpPhysicalDisplay[ d ] = pDisplay;
            mDisplayState[ d ].setHwDisplay( pDisplay );
            pDisplay->setDisplayManagerIndex( d );

            ++mPhysicalDisplays;

            return d;
        }
    }

    ALOG_ASSERT( 0 );
    return INVALID_DISPLAY_ID;
}

void PhysicalDisplayManager::unregisterDisplay( AbstractPhysicalDisplay* pDisplay )
{
    ALOG_ASSERT( pDisplay );

    const uint32_t phyIndex = pDisplay->getDisplayManagerIndex( );

    ALOG_ASSERT( phyIndex != INVALID_DISPLAY_ID );
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays );
    ALOG_ASSERT( mpPhysicalDisplay[ phyIndex ] == pDisplay );
    ALOG_ASSERT( mPhysicalDisplays > 0 );

    mpPhysicalDisplay[ phyIndex ] = NULL;
    pDisplay->setDisplayManagerIndex( INVALID_DISPLAY_ID );
    mDisplayState[ phyIndex ].setHwDisplay( NULL );
    --mPhysicalDisplays;
}

AbstractPhysicalDisplay* PhysicalDisplayManager::getPhysicalDisplay( uint32_t phyIndex )
{
    if ( phyIndex == INVALID_DISPLAY_ID )
        return NULL;
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays );
    return mpPhysicalDisplay[ phyIndex ];
}

int PhysicalDisplayManager::onPrepare(const Content& ref)
{
    bool bIdleShouldReAnalyse = mIdleTimeout.shouldReAnalyse();

    for (uint32_t d = 0; d < ref.size(); d++)
    {
        const Content::Display& display = ref.getDisplay(d);

        // NOTE:
        //  The LDM determines if we can assume SF display order here.
        const int32_t sfIndex = getSFDisplayOrder() ? d : -1;
        const uint32_t displayIndex = display.getDisplayManagerIndex();
        const uint32_t phyIndex = remap( displayIndex );

        AbstractPhysicalDisplay* pHwDisplay = getPhysicalDisplay( phyIndex );
        if ( pHwDisplay == NULL )
        {
            continue;
        }

        DisplayState& state = mDisplayState[ pHwDisplay->getDisplayManagerIndex() ];

        // Inform the composition manager of the currently active input buffers
        // This enables it to invalidate any previous results
        mCompositionManager.onAccept(display, d);

        state.setFrameIndex( display.getFrameIndex( ) );
        state.setFrameReceivedTime( display.getFrameReceivedTime( ) );

        // Give the display the chance to adapt to the display format.
        pHwDisplay->updateOutputFormat( display.getFormat() );

        bool bGeomChange = display.isGeometryChanged();

        // Check that blank or enable changes have an associated geometry change.
        // TODO:
        //  This section is redundant if geometry changes are always managed correctly and consistently.
        //  1/ Enable/Disable is managed by InputAnalyzer which does currently correctly update geometry changes.
        //  2/ Blanking is applied async from SF and/or via MDF extended mode - there is some question about whether
        //     geometry change is always going to be updated correctly. This could be addressed by managing blanking
        //     via a single common "blanking" filter.
        if ( !bGeomChange )
        {
            const Content::Display& current = state.getContent();
            if ( current.isBlanked() != display.isBlanked() )
            {
                ALOGW( "PhysicalDisplayManager Blank change %d->%d without geometry change", current.isBlanked(), display.isBlanked() );
                bGeomChange = true;
            }
            if ( current.isEnabled() != display.isEnabled() )
            {
                ALOGW( "PhysicalDisplayManager Enable change %d->%d without geometry change", current.isEnabled(), display.isEnabled() );
                bGeomChange = true;
            }
        }

        PlaneComposition &pc = state.getPlaneComposition();
        if ( bGeomChange || bIdleShouldReAnalyse )
        {
            ALOGD_IF(PHYDISP_DEBUG, "PhysicalDisplayManager::onPrepare Display D%d Geometry Changed", d);
            ALOGD_IF(PHYDISP_DEBUG, "%s", display.dump().string());
            ATRACE_NAME_IF(DISPLAY_TRACE, "PlaneAllocation");

            // Indicate that we no longer require the resources from the previous composition
            // This will also clear the output to disabled.
            pc.onRelease();

            // reinitialise the PlaneComposition record
            pc.setCompositionManager(&mCompositionManager);
            pc.setDisplayInput(&display);

            // Allocate planes/compositions if there is something to display.
            if ( display.isEnabled() && !display.isBlanked() )
            {
                bool bOK;

                if (mEnablePlaneAllocator)
                {
                    // This path allocates via Jason's algorithm
                    PlaneAllocatorJB planeAllocator(mIdleTimeout.frameIsIdle());
                    bOK = planeAllocator.analyze(display, pHwDisplay->getDisplayCaps(), pc);
                }
                else
                {
                    // This path always composes to a full screen layer
                    bOK = pc.addFullScreenComposition(pHwDisplay->getDisplayCaps(), 0, 0, display.getNumLayers(), display.getFormat());
                }

                // Indicate that we intend to commit these resources to a display now
                if (bOK)
                {
                    bOK = pc.onAcquire();
                    ALOGD_IF(PHYDISP_DEBUG, "PhysicalDisplayManager::onPrepare PlaneAllocator returned:\n%s", pc.dump().string());
                }

                if (!bOK)
                {
                    // Failed to acquire the resources for this composition.
                    // Fall back to full surfaceflinger composition (if possible!)
                    // TODO:
                    //  SF fallback to be replaced.
                    if ( sfIndex != -1 )
                    {
                        ALOGD_IF( PHYDISP_DEBUG, "PhysicalDisplayManager::onPrepare  D:%d Display:%d RPD:%d (sf:%d) onAcquire() Failed, falling back to SF composition", d, displayIndex, phyIndex, sfIndex );
                        ALOGE_IF( display.isFrontBufferRendered(), "SurfaceFlingerComposer fallback used with front buffer rendered content\n%s", display.dump().string() );
                        pc.fallbackToSurfaceFlinger( sfIndex );
                    }
                    else
                    {
                        ALOGW( "Can not fallback to SurfaceFlinger composition for remapped physical displays" );
                    }
                }

                // Notify the idle logic that the display can benefit from the timeout.
                // Only if 1) Multiple planes, 2) No planes are used for FBR.
                const Content::Display& outDisplay = pc.getDisplayOutput();
                mIdleTimeout.setCanOptimize(d, ( outDisplay.getNumEnabledLayers() > 1 )
                                            && ( !outDisplay.isFrontBufferRendered() ) );
            }
        }
        else
        {
            ALOGD_IF(PHYDISP_DEBUG, "PhysicalDisplayManager::onPrepare Display D%d Geometry Same", d);
            pc.onUpdate(display);
        }
        ALOGD_IF(PHYDISP_DEBUG, "PhysicalDisplayManager::onPrepare Display D%d Planes will display:\n%s", d, pc.dump().string());
    }

    // Re-set the idle timer for the next frame.
    mIdleTimeout.nextFrame();

    return OK;
}

int PhysicalDisplayManager::onSet(const Content& ref)
{
    for (uint32_t d = 0; d < ref.size(); d++)
    {
        ALOGD_IF(PHYDISP_DEBUG, " ---- DISPLAY D%d FRAME %03d ----", d, mHwc.getRedrawFrames());

        const Content::Display& display = ref.getDisplay(d);

        uint32_t phyIndex = display.getDisplayManagerIndex();

        phyIndex = remap( phyIndex );

        AbstractPhysicalDisplay* pHwDisplay = getPhysicalDisplay( phyIndex );

        // NOTE:
        //  A blanked display is still attached and must be processed,
        //  Else a display with no layers is not attached/unused and should be skipped.
        if ( pHwDisplay && ( display.getNumEnabledLayers() || display.isBlanked() ) )
        {
            DisplayState& state = mDisplayState[ pHwDisplay->getDisplayManagerIndex() ];
            Content::Display& current = state.getContent();

            // Keep display state blank/unblank aligned with the display content.
            bool bChange = false;
            state.onBlank( display.isBlanked(), BLANK_CONTENT, bChange );

            ALOGD_IF( PHYDISP_DEBUG, "PhysicalDisplayManager::onSet Display D%d [%sx%u layers]. Physical display %p [%sx%u layers]",
                d, display.isBlanked() ? "Blanked " : "", display.getNumEnabledLayers(),
                pHwDisplay, current.isBlanked() ? "Blanked ": "", current.getNumEnabledLayers() );

            // Perform any compositions required prior to sending to display
            PlaneComposition &pc = state.getPlaneComposition();
            pc.onCompose();

            // Log the new physical display state
            const Content::Display& out = pc.getDisplayOutput();
            const Content::LayerStack& stack = out.getLayerStack();

            // Update the hardware state
            int retireFence = -1;
            pHwDisplay->onSet(out, pc.getZOrder(), &retireFence);
            ALOGD_IF( PHYDISP_DEBUG, "PhysicalDisplayManager Display %d onSet( ) returned retireFence %d", d, retireFence );

            // *ALL* display types must return a retire fence.
            INTEL_HWC_DEV_ASSERT( retireFence >= 0 );

            if ( pHwDisplay->getDisplayType() == eDTVirtual )
            {
                // For genuine virtual display, the retire fence is redundant.
                Timeline::closeFence( &retireFence );
                retireFence = -1;
            }

#if INTEL_HWC_INTERNAL_BUILD
            //
            // NOTE:
            //   These checks are dev asserts because some builds (e.g. BXT PRE-SI)
            //   have broken fences; in those builds these checks will fail.
            //
            // Check the retire fence is valid.
            // The virtual display *MUST* return no retire fence (-1).
            // All other displays *SHOULD ALWAYS* provide a valid retire fence (>=0).
            // This is true even when the display drops the frame.
            INTEL_HWC_DEV_ASSERT( ( ( pHwDisplay->getDisplayType() == eDTVirtual ) && ( retireFence == -1 ) )
                     || ( ( pHwDisplay->getDisplayType() != eDTVirtual ) && ( retireFence >= 0  ) ),
                        "Unexpected fence for Hwc display D%d %d", d, retireFence );

            // Check the release fences are valid.
            // The layer release fences *MUST* be unspecified (-1) or a valid value (>=0).
            for(uint32_t ly = 0; ly < stack.size(); ly++)
            {
                const Layer& layer = stack.getLayer(ly);
                HWC_UNUSED( layer );
                INTEL_HWC_DEV_ASSERT( layer.getReleaseFence() >= -1,
                          "Unexpected fence for Hwc display D%d layer %d %d",
                          d, ly, layer.getReleaseFence() );
            }
#endif

            // Return a retire fence (even if -1)
            // Only do this for the first (master) display to this retirefence slot.
            if ( display.getRetireFenceReturn() && ( display.getRetireFence() == -1 ) )
            {
                ALOGD_IF(PHYDISP_DEBUG, "PhysicalDisplayManager Display D%d Retire fence %p/%d",
                    d, display.getRetireFenceReturn(), retireFence );
                display.returnCompositionRetireFence( retireFence );
            }
            else
            {
                ALOGD_IF(PHYDISP_DEBUG, "PhysicalDisplayManager Display D%d No retire fence return, dropping fence %d", d, retireFence );
                Timeline::closeFence( &retireFence );
            }

            // Update the current state to match the new state.
            current.updateDisplayState(display);
            current.editLayerStack() = stack;

            // Dump trace at end to capture final replicated release fence state.
            Log::add( current, String8::format( "P%u %s", phyIndex, pHwDisplay->getName() ).string() );
            ALOGD_IF(PHYDISP_DEBUG, "%s", out.dump(pHwDisplay->getName()).string());

#if INTEL_HWC_INTERNAL_BUILD
            if (sbInternalBuild)
            {
                DebugFilter::get().dumpHardwareFrame( d, out );
            }
#endif
        }

        // Close the input frame's acquire fences.
        display.closeAcquireFences( );
    }

    return 0;
}

void PhysicalDisplayManager::setRemap( uint32_t displayIndex, uint32_t physicalIndex )
{
    ALOG_ASSERT( displayIndex < cMaxSupportedLogicalDisplays );
    mPhyIndexRemap[ displayIndex ] = physicalIndex;
    mbRemapIndices = true;
}

void PhysicalDisplayManager::resetRemap( void )
{
    mbRemapIndices = false;
}

uint32_t PhysicalDisplayManager::remap( uint32_t displayIndex )
{
    return mbRemapIndices && ( displayIndex < cMaxSupportedLogicalDisplays ) ?
                mPhyIndexRemap[ displayIndex ]
              : displayIndex;
}

void PhysicalDisplayManager::vSyncEnable( uint32_t phyIndex, bool bEnableVSync )
{
    Log::alogd( PHYDISP_DEBUG || VSYNC_DEBUG, "PhysicalDisplayManager P%u VSYNC %s", phyIndex, bEnableVSync ? "Enabled" : "Disabled" );
    ALOG_ASSERT( phyIndex < cMaxSupportedPhysicalDisplays);
    DisplayState& state = mDisplayState[ phyIndex ];
    state.onVSyncEnable( bEnableVSync );
}

int PhysicalDisplayManager::blank( uint32_t phyIndex, bool bEnableBlank, BlankSource source )
{
    Log::alogd( PHYDISP_DEBUG, "PhysicalDisplayManager onBlank P%u enable:%d", phyIndex, bEnableBlank );
    ALOG_ASSERT(phyIndex < cMaxSupportedPhysicalDisplays);
    DisplayState& state = mDisplayState[ phyIndex ];
    bool bChange = false;
    return state.onBlank( bEnableBlank, source, bChange );
}

String8 PhysicalDisplayManager::dump( void )
{
    String8 str;
    for ( uint32_t d = 0; d < cMaxSupportedPhysicalDisplays; ++d )
    {
        if ( !mDisplayState[d].isAttached() )
            continue;
        if ( str.length() )
            str += "\n";
        str += String8::format( " P%u %s ", d,  mDisplayState[d].getHwDisplay()->dump().string() );
    }
    return str;
}

String8 PhysicalDisplayManager::dumpDetail( void )
{
    return dump( );
}

void PhysicalDisplayManager::open( void )
{
    ALOGD_IF( PHYDISP_DEBUG, "PhysicalDisplayManager : open" );
}

void PhysicalDisplayManager::flush( uint32_t frameIndex, nsecs_t timeoutNs )
{
    for (uint32_t d = 0; d < cMaxSupportedPhysicalDisplays; d++)
    {
        AbstractDisplay* pDisplay = mDisplayState[d].getHwDisplay();
        if ( pDisplay != NULL )
        {
            ALOGD_IF( PHYDISP_DEBUG || HWC_SYNC_DEBUG, "Flush hardware display %p (slot %u) - frameIndex %u", pDisplay, d, frameIndex);
            pDisplay->flush( frameIndex, timeoutNs );
        }
    }
}

void PhysicalDisplayManager::setNotificationReceiver( PhysicalDisplayNotificationReceiver* pNotificationReceiver )
{
    mpDisplayNotificationReceiver = pNotificationReceiver;
}

void PhysicalDisplayManager::notifyPhysicalAvailable( AbstractPhysicalDisplay* pPhysical)
{
    ALOG_ASSERT( pPhysical );

    ALOGE_IF( !mpDisplayNotificationReceiver, "Missing mpDisplayNotificationReceiver" );
    if ( mpDisplayNotificationReceiver )
    {
        mpDisplayNotificationReceiver->notifyDisplayAvailable(pPhysical);
    }
}

void PhysicalDisplayManager::notifyPhysicalUnavailable( AbstractPhysicalDisplay* pPhysical )
{
    ALOG_ASSERT( pPhysical );

    // Forward notification to complete unplug.
    ALOGE_IF( !mpDisplayNotificationReceiver, "Missing mpDisplayNotificationReceiver" );
    if ( mpDisplayNotificationReceiver )
    {
        mpDisplayNotificationReceiver->notifyDisplayUnavailable( pPhysical );
    }
}

void PhysicalDisplayManager::notifyPhysicalChangeSize( AbstractPhysicalDisplay* pPhysical )
{
    ALOG_ASSERT( pPhysical );

    // Forward notification to complete size change.
    ALOGE_IF( !mpDisplayNotificationReceiver, "Missing mpDisplayNotificationReceiver" );
    if ( mpDisplayNotificationReceiver )
    {
        mpDisplayNotificationReceiver->notifyDisplayChangeSize( pPhysical );
    }
}

void PhysicalDisplayManager::notifyPhysicalVSync( AbstractPhysicalDisplay* pPhysical, nsecs_t timeStampNs )
{
    ALOG_ASSERT( pPhysical );

    // Forward notification to issue vsync event.
    ALOGE_IF( !mpDisplayNotificationReceiver, "Missing mpDisplayNotificationReceiver" );
    if ( mpDisplayNotificationReceiver )
    {
        mpDisplayNotificationReceiver->notifyDisplayVSync( pPhysical, timeStampNs );
    }
}

void PhysicalDisplayManager::notifyPlugChangeCompleted( void )
{
    // Nop
}

void PhysicalDisplayManager::endOfFrame( void )
{
    // Nop.
}

PhysicalDisplayManager::DisplayState::DisplayState() :
    mIndex(0),
    mpHwDisplay(NULL),
    mBlankMask(BLANK_CONTENT),
    mFrameIndex(0),
    mFrameReceivedTime(0),
    mbValid(false),
    mbVSyncEnabled(false)
{
}

PhysicalDisplayManager::DisplayState::~DisplayState()
{
}

void PhysicalDisplayManager::DisplayState::setHwDisplay( AbstractPhysicalDisplay* pDisp )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    Mutex::Autolock _l( mLock );
    mpHwDisplay = pDisp;
    // NOTE:
    //  When a display is added we reset the blank mask to a known default state.
    //  This means:
    //    SF/User blanking will be cancelled across unplug/plug.
    //    Frame based blanking will be applied as required on first/next frame.
    mBlankMask = 0;
}

void PhysicalDisplayManager::DisplayState::onVSyncEnable( bool bEnableVSync )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    Mutex::Autolock _l( mLock );
    if ( mpHwDisplay == NULL )
    {
        mbVSyncEnabled = false;
        return;
    }
    if ( mbVSyncEnabled == bEnableVSync )
    {
        return;
    }
    if ( mpHwDisplay->onVSyncEnable( bEnableVSync ) == OK )
    {
        mbVSyncEnabled = bEnableVSync;
    }
}

int PhysicalDisplayManager::DisplayState::onBlank(bool bEnableBlank, BlankSource source, bool& bChange)
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );

    ALOGD_IF( PHYDISP_DEBUG, "PhysicalDisplayManager DisplayState::onBlank( enable:%d, source:%x ) [mIndex %u]", bEnableBlank, source, mIndex );

    Mutex::Autolock _l( mLock );

    AbstractDisplay* pHwDisplay = mpHwDisplay;

    if ( pHwDisplay == NULL )
    {
        Log::aloge( true, "PhysicalDisplayManager DisplayState %u %s failed. Display not attached.",
                    mIndex, bEnableBlank ? "Blank" : "Unblank" );
        return -1;
    }

    Log::alogd( PHYDISP_DEBUG, "PhysicalDisplayManager Display P%u DisplayState %u %s %s. Current blanking=[%s|%s|%s].",
                pHwDisplay->getDisplayManagerIndex(),
                mIndex,
                bEnableBlank ? "Blank" : "Unblank",
                source == BLANK_CONTENT        ? "NO CONTENT" :
                source == BLANK_SURFACEFLINGER ? "SF" :
                source == BLANK_HWCSERVICE     ? "SERVICE" :
                source == BLANK_PROXYREDIRECT  ? "PROXY REDIRECT" :
                "UNKNOWN",
                mBlankMask & (1<<BLANK_SURFACEFLINGER) ? "SF"  : "-",
                mBlankMask & (1<<BLANK_CONTENT)        ? "NO CONTENT" : "-",
                mBlankMask & (1<<BLANK_HWCSERVICE)     ? "SERVICE" : "-",
                mBlankMask & (1<<BLANK_PROXYREDIRECT)  ? "PROXY REDIRECT" : "-"
                );

    const bool bWasBlanked = ( mBlankMask != 0 );

    // Establish new blanking state (assuming we succesfully apply the change).
    uint32_t newMask = mBlankMask;
    if (bEnableBlank)
        newMask |= 1 << source;
    else
        newMask &= ~(1 << source);

    const bool bIsBlanked = ( newMask != 0 );

    ALOGD_IF( PHYDISP_DEBUG, "Display P%u mBlankMask:%x wasBlanked:%d newMask:%d isBlanked:%d",
        pHwDisplay->getDisplayManagerIndex(), mBlankMask, bWasBlanked, newMask, bIsBlanked );

    int ret = OK;

    if ( bIsBlanked != bWasBlanked )
    {
        bChange =  true;

        // Drop any queued frames if entering blanking.
        if ( bIsBlanked )
        {
            Log::alogd( PHYDISP_DEBUG, "Display P%u dropping all queued frames", pHwDisplay->getDisplayManagerIndex());
            pHwDisplay->dropAllFrames( );
        }

        Log::alogd( PHYDISP_DEBUG, "Display P%u issuing %s",
            pHwDisplay->getDisplayManagerIndex(), bEnableBlank ? "Blank" : "Unblank" );

        // Forward blanking downstream.
        ret = pHwDisplay->onBlank( bIsBlanked, (source==BLANK_SURFACEFLINGER) );

        if ( ret == OK )
        {
            // Apply change.
            mBlankMask = newMask;

            // Finally, flush display updates if the blanking source is SF.
            // Release the DisplayState lock first - this ensures a SF blank can't block the main thread while it completes.
            // State updates (i.e. mBlankMask) MUST be set prior to this point.
            mLock.unlock();
            if ( source == BLANK_SURFACEFLINGER )
            {
                Log::alogd( PHYDISP_DEBUG, "Display P%u flushing blank", pHwDisplay->getDisplayManagerIndex());
                pHwDisplay->flush( );
            }
            mLock.lock();
        }

        Log::alogd( PHYDISP_DEBUG, "PhysicalDisplayManager P%u DisplayState %u %s %s. %s. New blanking=[%s|%s|%s].",
                    pHwDisplay->getDisplayManagerIndex(),
                    mIndex,
                    bEnableBlank ? "Blank" : "Unblank",
                    source == BLANK_CONTENT        ? "NO CONTENT" :
                    source == BLANK_SURFACEFLINGER ? "SF" :
                    source == BLANK_HWCSERVICE     ? "SERVICE" :
                    source == BLANK_PROXYREDIRECT  ? "PROXY REDIRECT" :
                    "UNKNOWN",
                    ( ret == OK ) ? "OK" : "FAILED",
                    mBlankMask & (1<<BLANK_SURFACEFLINGER) ? "SF"  : "-",
                    mBlankMask & (1<<BLANK_CONTENT)        ? "NO CONTENT" : "-",
                    mBlankMask & (1<<BLANK_HWCSERVICE)     ? "SERVICE" : "-",
                    mBlankMask & (1<<BLANK_PROXYREDIRECT)  ? "PROXY REDIRECT" : "-"
                    );
    }

    return ret;
}

PhysicalDisplayManager::IdleTimeout::IdleTimeout(Hwc& hwc) :
    mHwc(hwc),
    mOptionIdleTimeout("idletimeout", 600), // Set to zero to disable
    mOptionIdleTimein ("idletimein", 400),
    mIdleTimer(*this),
    mFramesToExitIdle(0),
    mbForceReAnalyse(false)
{
}

bool PhysicalDisplayManager::IdleTimeout::shouldReAnalyse()
{
    bool r = false;
    if (frameComingOutOfIdle())
    {
        Log::alogd(PHYDISP_DEBUG, "Idle exit");
        r = true;
    }
    else if (mbForceReAnalyse)
    {
        r = true;
    }
    mbForceReAnalyse = false;
    return r;
}

void PhysicalDisplayManager::IdleTimeout::setCanOptimize(unsigned display, bool can)
{
    ALOG_ASSERT(display < (sizeof(mDisplaysCanOptimise) * CHAR_BIT));
    if (can) {
        mDisplaysCanOptimise.markBit(display);
    } else {
        mDisplaysCanOptimise.clearBit(display);
    }
}

void PhysicalDisplayManager::IdleTimeout::idleTimeoutHandler()
{
    // We received no updates in a while.
    if (mOptionIdleTimeout && (!mDisplaysCanOptimise.isEmpty() || mFramesToExitIdle))
    {
        // If the last frame wasn't idle then we need to force a full prepare.
        bool switchToIdle = !mFramesToExitIdle;

        // Set the minimum number of frames we need to see issued
        // within the timeout period before we will exit idle.
        // Must be +1 since we use the value '1' to denote that the display is
        // coming out of idle.
        mFramesToExitIdle = 1 + cFramesKeptAtIdle;
        if ( switchToIdle )
        {
            Log::alogd(PHYDISP_DEBUG, "Idle enter");
            mbForceReAnalyse = true;
            mHwc.forceRedraw();
        }
    }
}

void PhysicalDisplayManager::IdleTimeout::resetIdleTimer()
{
    // If the optimization is disabled then we have nothing to do.
    if ( (cFramesKeptAtIdle < 1) || !mOptionIdleTimeout)
    {
        return;
    }

    // Update the state of the idle display optimization.
    // Count down frames issued. If we issue x mFramesToExitIdle frames within
    // the period of the idle timer then we exit idle. While mFramesToExitIdle
    // remains at 0 then we are not in idle.
    if (mFramesToExitIdle)
    {
        --mFramesToExitIdle;
    }

    // Set-up a timer to timeout if we recieve no updates in a while.
    mIdleTimer.set((mFramesToExitIdle) ? mOptionIdleTimein : mOptionIdleTimeout);
}


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

