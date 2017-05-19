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

#include "Common.h"
#include "VirtualDisplay.h"
#include "Log.h"
#include "AbstractBufferManager.h"
#include "HwcService.h"

namespace intel {
namespace ufo {
namespace hwc {

VirtualDisplay::VirtualDisplay(Hwc& hwc) :
    PhysicalDisplay(hwc),
    mCaps("Virtual", INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT, false),
    mHandledLayerMask(0)
{
    // Add in decrypt support for the Virtual display's single plane.
    DisplayCaps::PlaneCaps& planeCaps = mCaps.editPlaneCaps( 0 );
    planeCaps.enableDecrypt();

    registerDisplayCaps( &mCaps );
    setDisplayType(eDTVirtual);

    setVSyncPeriod( INTEL_HWC_DEFAULT_REFRESH_PERIOD_NS );

    // Init a dummy timeline and a single pre-signalled fence.
    String8 name = String8::format( "HWC.VIRTUAL" );
    if ( !mTimeline.init( name ) )
    {
        ALOGE( "Failed to create sync timeline for %s", name.string() );
    }
    uint32_t index;
    mPreSignalledFence = mTimeline.createFence( &index );
    mTimeline.advanceTo( index );
    ALOGD_IF( VIRTUALDISPLAY_DEBUG, "Created pre-signalled dummy timeline/fence %d", mPreSignalledFence );
}

VirtualDisplay::~VirtualDisplay()
{
    Timeline::closeFence( &mPreSignalledFence );
    mTimeline.uninit( );
}

void VirtualDisplay::updateOutputFormat( int32_t format )
{
    // VirtualDisplay format *MUST* always follow the display output format.
    // Update the caps to force this.
    mCaps.updateOutputFormat( format );
}

void VirtualDisplay::onSet(const Content::Display& display, uint32_t /*zorder*/, int* pRetireFenceFd)
{
    ALOGD_IF(VIRTUALDISPLAY_DEBUG, "VirtualDisplay::onSet %s", display.dump().string());
    ALOG_ASSERT(pRetireFenceFd);
    *pRetireFenceFd = Timeline::dupFence( &mPreSignalledFence );

    int64_t needSetKeyFrameFilterHint;
    HwcService& hwcService = HwcService::getInstance();
    hwcService.notify(HwcService::eNeedSetKeyFrameHint, 1, &needSetKeyFrameFilterHint);
    const Content::LayerStack& layerstack = display.getLayerStack();

    if (layerstack.size() < 1)
    {
        return;
    }

    ALOG_ASSERT(layerstack.size() == 1);

    const Layer& layer = layerstack.getLayer(0);

    ALOG_ASSERT(layer.getHandle());

    if (needSetKeyFrameFilterHint)
    {
        AbstractBufferManager::get().setBufferKeyFrame(layer.getHandle(), true);
        ALOGD_IF(VIRTUALDISPLAY_DEBUG || FILTER_DEBUG, "Actual set the key frame flag in virtual display on buffer %p", layer.getHandle());
        Log::alogd(VIRTUALDISPLAY_DEBUG, "Set key frame flag on buffer %x", layer.getHandle());
    }

    // Nothing at all to do in a standard Virtual Display case, the composition manager will have already completed the work
    layerstack.setAllReleaseFences( -1 );
}

}   // hwc
}   // ufo
}   // intel
