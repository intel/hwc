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

#ifndef INTEL_UFO_HWC_FILTERPOSITION_H
#define INTEL_UFO_HWC_FILTERPOSITION_H

namespace intel {
namespace ufo {
namespace hwc {

// List of standard filter positions. Space should be left between these in order
// to add addition filters at specific places in the list
// Notice: Nothing should be changed in the list between the initial analysis and the SurfaceFlingerComposer.
enum class FilterPosition : uint32_t
{
    Min                     =       1,
    //  |
    //  v
    // SurfaceFlinger display content is delivered to filter pipeline
    //  |
    //  v
    Debug                   =      50,
    ClonedVideoLayer        =     500,
    SurfaceFlinger          =     600,
    VisibleRect             =    5000,
    Empty                   =    5500,
    SyncFilter              =    6000,
    Rotate180               =    6500,
    Transparency            =    7000,
    VideoModeDetection      =    8000,
    Protected               =   11000,
    DisplayManager          =   13000,
    GlobalScaling           =   14000,
    //  |
    //  v
    // PhysicalDisplayManager receives output from last filter
    //
    Max                     = 1000000,
    Invalid                 = 9999999,
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_FILTERPOSITION_H
