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

#ifndef INTEL_UFO_HWC_DISPLAY_STATE_H
#define INTEL_UFO_HWC_DISPLAY_STATE_H

#include "Timing.h"

namespace intel {
namespace ufo {
namespace hwc {

// ********************************************************************
// Display State Class
// This holds the transitory display state that may be required by
// the plane allocator to resolve the composition.
// This state must be synchronized so that it will always be
// valid at the time of use (during analysis).
// TODO:
//  Make DisplayState private to BxtDisplayCaps.
// ********************************************************************
class DisplayState
{
public:
    DisplayState( const DisplayCaps& dc ) : mCaps( dc ), mNumActiveDisplays( 0 ) { }

    const Timing&           getTiming() const                               { return mTiming; }
    uint32_t                getNumActiveDisplays() const                    { return mNumActiveDisplays; }

    void                    setTiming(const Timing& t)                      { mTiming = t; }
    void                    setNumActiveDisplays( uint32_t activeDisplays ) { mNumActiveDisplays = activeDisplays; }

protected:
    const DisplayCaps&      mCaps;
    uint32_t                mNumActiveDisplays;                         // Count of active hardware displays.
    Timing                  mTiming;                                    // Current display timing if known.
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DISPLAY_STATE_H
