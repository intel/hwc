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
#include "HswDisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

HswDisplayCaps::HswDisplayCaps(uint32_t pipe) :
    DisplayCaps(),
    mPipe(pipe)
{
}

void HswDisplayCaps::probe()
{
    ALOGI_IF( sbLogViewerBuild, "DisplayCaps construct Haswell class caps for display pipe %d", mPipe);

    // TODO:
    // Add correct CAP_OPAQUE_CONTROL support.
    // NOTE:
    //  We don't strictly support CAP_OPAQUE_CONTROL yet since we ALWAYS blend on VLV
    //  and do not force blending off when required. If we remove CAP_OPAQUE
    //  then we do not get the NavigationBar going to overlay because it
    //  is unblended but has an alpha channel.


    if ( getNumPlanes() > 0 )
    {
        // First plane is the main plane
        PlaneCaps& caps = editPlaneCaps(0);

        // Enable main capabilities.
        caps.enableDisable( );
        caps.setMaxSourcePitch( 32*1024 );
    }


    if ( getNumPlanes() > 1 )
    {
        // Subsequent planes are sprite planes
        for ( uint32_t s = 1; s < getNumPlanes(); ++s )
        {
            PlaneCaps& caps = editPlaneCaps( s );

            // Enable sprite capabilities.
            caps.enableDisable( );
            caps.enableDecrypt( );
            caps.enableWindowing( );
            caps.enableSourceOffset( );
            caps.enableSourceCrop( );
            caps.setMaxSourcePitch( 32*1024 );


            // Set transforms to NONE/ROT180.
            static const ETransform transforms[] =
            {
                ETransform::NONE,
                ETransform::ROT_180
            };
            caps.setTransforms( DISPLAY_CAPS_COUNT_OF( transforms ), transforms );
        }
    }

    // Note, this needs to be called after adding planes
    updateZOrderMasks();
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
