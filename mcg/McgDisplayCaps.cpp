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
#include "Layer.h"

#include "SinglePlaneDisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {


class MoorefieldPlaneCaps : public DisplayCaps::PlaneCaps
{
};

class MoorefieldDisplayCaps : public DisplayCaps
{
public:
    MoorefieldDisplayCaps(uint32_t pipe);
    void probe();
    static const unsigned int cPlaneCount = 4;
    DisplayCaps::PlaneCaps* createPlane(uint32_t planeIndex)
    {
        ALOG_ASSERT(planeIndex < cPlaneCount);
        return &mPlanes[planeIndex];
    }
private:
    MoorefieldPlaneCaps mPlanes[cPlaneCount];
    uint32_t mPipe;
};

MoorefieldDisplayCaps::MoorefieldDisplayCaps(uint32_t pipe) :
    mPipe(pipe)
{
}

void MoorefieldDisplayCaps::probe()
{
    ALOGI( "DisplayCaps construct Moorefield caps for display pipe %d", mPipe);
    // Drm will have populated this class at this point with a baseline state from kernel detection.
    // Tweak anything not detectable at this point.

    for ( uint32_t s = 0; s < getNumPlanes(); ++s )
    {
        PlaneCaps& caps = editPlaneCaps( s );

        // TODO Enable sprite capabilities. These may need changing for Moorefield
        caps.enablePlaneAlpha( false );
        caps.setBlendingMasks( static_cast<uint32_t>(EBlendMode::PREMULT) );
        caps.enableDisable( );
        caps.enableDecrypt( );
        caps.enableWindowing( );
        caps.enableSourceOffset( );
        caps.enableSourceCrop( );
    }
}

DisplayCaps* DisplayCaps::create(uint32_t hardwarePipe, uint32_t /* deviceId */)
{
    DisplayCaps* pDisplayCaps = NULL;
//  switch ( MCG CHIP TYPE )
//  {
//      case MOOREFIELD:
            pDisplayCaps = new MoorefieldDisplayCaps(hardwarePipe);
            ALOGE_IF( pDisplayCaps == NULL, "Failed to create Drm Baytrail class caps" );
//          break;
//      // Default future chips to generic (minimal capabilities) for now.
//      default:
//          pDisplayCaps = new SinglePlaneDisplayCaps(INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT);
//          ALOGE_IF( pDisplayCaps == NULL, "Failed to create Single Plane class caps" );
//          break;
//  }
    return pDisplayCaps;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
