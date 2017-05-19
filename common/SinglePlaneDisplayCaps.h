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


#ifndef INTEL_UFO_HWC_DISPLAY_CAPS_SIMPLE_H
#define INTEL_UFO_HWC_DISPLAY_CAPS_SIMPLE_H

#include "DisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

// ********************************************************************
// Display Capabilities for a simple single plane display device.
// ********************************************************************
class SinglePlaneDisplayCaps : public DisplayCaps
{
public:
    // Construct simple capabilities.
    SinglePlaneDisplayCaps( const char *pName, int32_t defaultFormat, bool bNativeBuffersRequired = true );
    void probe();

    // Update capabilities to the display output format.
    void updateOutputFormat( int32_t format );

private:
    int32_t     mDefaultFormat;
    PlaneCaps   mPlane;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DISPLAY_CAPS_SIMPLE_H
