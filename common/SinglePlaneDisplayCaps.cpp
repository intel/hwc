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
#include "SinglePlaneDisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

SinglePlaneDisplayCaps::SinglePlaneDisplayCaps( const char *pName, int32_t defaultFormat, bool bNativeBuffersRequired ) :
    mDefaultFormat(defaultFormat)
{
    static const ETransform transforms[] =
    {
        ETransform::NONE,
    };

    mPlane.setTransforms( DISPLAY_CAPS_COUNT_OF( transforms ), transforms );
    mPlane.setDisplayFormats( 1, &mDefaultFormat );
    mpPlaneCaps.push( &mPlane );
    setDefaultOutputFormat(defaultFormat);
    setDeviceNativeBuffersRequred( bNativeBuffersRequired );
    setName(pName);

}

void SinglePlaneDisplayCaps::probe()
{
    // Nothing to do here, the constructor did everything already
}

void SinglePlaneDisplayCaps::updateOutputFormat( int32_t format )
{
    if ( format && ( getDefaultOutputFormat( ) != format ) )
    {
        ALOGD( "updateOutputFormat %s -> %s", getHALFormatString( getDefaultOutputFormat( ) ), getHALFormatString( format ) );
        setDefaultOutputFormat( format );
        ALOG_ASSERT( getNumPlanes() == 1 );
        DisplayCaps::PlaneCaps& planeCaps = editPlaneCaps( 0 );
        planeCaps.setDisplayFormats( 1, &format );
    }
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
