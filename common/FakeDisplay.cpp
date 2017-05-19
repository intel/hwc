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

#include "FakeDisplay.h"

namespace intel {
namespace ufo {
namespace hwc {

FakeDisplay::FakeDisplay(Hwc& hwc, uint32_t x, uint32_t y) :
    PhysicalDisplay(hwc),
    mCaps("Fake", INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT)
{
    // PhysicalDisplay requires initialized options.
    initializeOptions( "fake", 0 );
    setDisplayType(eDTFake);

    // Its a fake display, initialize some fake attributes
    setVSyncPeriod( INTEL_HWC_DEFAULT_REFRESH_PERIOD_NS );

    // Construct a list of available timings, for Fake just set a default mode
    uint32_t flags = 0;
    flags |= Timing::Flag_Preferred;
    Timing t( x, y, INTEL_HWC_DEFAULT_REFRESH_RATE, /*pixelclock*/ 0, 0, 0, Timing::EAspectRatio::Any, flags );
    mDisplayTimings.push_back(t);

    notifyTimingsModified( );

    setInitialTiming( 0 );

    registerDisplayCaps( &mCaps );

    // Init timeline for fake fence
    String8 name = String8::format( "HWC.FAKEDISPLAY" );
    if ( !mTimeline.init( name ) )
    {
        ALOGE( "Failed to create sync timeline for %s", name.string() );
    }
    mLastTimelineIndex = 0;
}

FakeDisplay::~FakeDisplay()
{
    // Uninit timeline for fake fence
    mTimeline.uninit( );
    // Clear timeline index
    mLastTimelineIndex = 0;
}

void FakeDisplay::onSet(const Content::Display& display, uint32_t /*zorder*/, int* pRetireFenceFd)
{
    ALOG_ASSERT(pRetireFenceFd);

    // Create fake fence as retire fence, which will be returned to SF.
    *pRetireFenceFd = mTimeline.createFence( &mLastTimelineIndex );

    // Replicate frame retire fence to layers' release fences.
    const Content::LayerStack& layerStack = display.getLayerStack();
    layerStack.setAllReleaseFences( *pRetireFenceFd );

    // Advance timeline to release all frames.
    int32_t delta = int32_t( mLastTimelineIndex - mTimeline.getCurrentTime() );
    if ( delta > 0 )
    {
        mTimeline.advanceTo( mLastTimelineIndex );
    }
}


}; // namespace hwc
}; // namespace ufo
}; // namespace intel
