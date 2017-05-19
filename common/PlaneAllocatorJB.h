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

#ifndef INTEL_UFO_HWC_OVERLAYMANAGER_H
#define INTEL_UFO_HWC_OVERLAYMANAGER_H

#include "Common.h"
#include "DisplayCaps.h"
#include "DisplayState.h"

namespace intel {
namespace ufo {
namespace hwc {

// This is a class for managing (detecting and invoking)
// the overlay capabilities supported by the Drm driver.
class PlaneAllocatorJB : NonCopyable
{
public:
    class Options;

    PlaneAllocatorJB(bool bOptimizeIdleDisplay = false);
    ~PlaneAllocatorJB( );

    // Determine how to use overlays.
    // If numLayers is zero,
    //  - pCollapsedLayersRTs[0] should be used as sole source for presentation.
    // Otherwise,
    //  - Some of the layers may be assigned dedicated overlays.
    //  - the remainder (if any) will be collapsed down to one or more of the pCollapsedLayersRTs.
    // If pCollapsedLayersRTs is NULL then layer collapse will be disabled and all layers will be
    // routed to overlays (using per-layer CSC/scaling as required). Behaviour is undefined if layer
    // count exceeds available overlays.
    // Returns true if succesful.
    bool analyze( const Content::Display& display, const DisplayCaps& caps, PlaneComposition& out );

private:
    static Options* spOptions;
    bool mbOptimizeIdleDisplay : 1;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_OVERLAYMANAGER_H
