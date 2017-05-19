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

#ifndef INTEL_UFO_HWC_ABSTRACTDISPLAYMANAGER_H
#define INTEL_UFO_HWC_ABSTRACTDISPLAYMANAGER_H

#include "AbstractDisplay.h"

namespace intel {
namespace ufo {
namespace hwc {

// An abstract interface representing a display manager providing functions
// like adding and removing displays.
class AbstractDisplayManager
{
public:
    // Which mechanism is modifying the blank state of the display?
    // Multiple mechanisms exist. The display is blank while any of these has set blank.
    // The display is not blank while none of the these have set blank.
    // See onBlank().
    enum BlankSource
    {
        BLANK_CONTENT        = 0,   // No layer lists
        BLANK_SURFACEFLINGER = 1,   // SurfaceFlinger requested
        BLANK_HWCSERVICE     = 2,   // Service requested
        BLANK_PROXYREDIRECT  = 3,   // Display Proxy requested
    };

    // D'tor.
    virtual                     ~AbstractDisplayManager() {}

    // Start of day start up.
    // This is called after platform open, so all displays should be registered and made available.
    // The display manager must complete plug of its initial displays at this point.
    virtual void                open( void ) = 0;

    // Flush all work through all displays.
    // On return, all displays will be displaying the most recently issued work.
    // If frameIndex is specified then will sync only to the specified frame.
    // If timeoutNs is zero then this is blocking.
    virtual void                flush( uint32_t frameIndex = 0, nsecs_t timeoutNs = AbstractDisplay::mTimeoutForFlush ) = 0;

    // Called at the end of each frame.
    virtual void                endOfFrame( void ) = 0;

    // Dump a little info about the display state.
    virtual String8             dump( void ) = 0;

    // Dump detailed info about the display state.
    virtual String8             dumpDetail( void ) = 0;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_ABSTRACTDISPLAYMANAGER_H
