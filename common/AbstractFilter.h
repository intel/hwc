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

#ifndef INTEL_UFO_HWC_ABSTRACTFILTER_H
#define INTEL_UFO_HWC_ABSTRACTFILTER_H

#include "Content.h"

namespace intel {
namespace ufo {
namespace hwc {

class Hwc;

class AbstractFilter
{
public:
    AbstractFilter() {}
    virtual ~AbstractFilter() {};

    // This returns the name of the filter.
    virtual const char* getName() const = 0;

    // This returns true if the output of onPrepare can be delivered directly to physical displays.
    // The default implementation returns false indicating the filter is logical. This MUST be
    // overridden for filters that deliver directly to physical displays.
    // TODO: Make pure virtual (as required for strict Abstract class).
    virtual bool outputsPhysicalDisplays() const { return false; } // = 0;

    // This is called at the hwc prepare entrypoint. Each filter may choose to change the
    // layer list in some way
    virtual const Content& onApply(const Content& ref) = 0;

    // Called once displays are ready but before first frame(s).
    // This provides the filter with the context (Hwc) if it is required and
    // also gives the filter opportunity to run one-time initialization.
    // TODO: Make pure virtual (as required for strict Abstract class).
    virtual void onOpen( Hwc& /*hwc*/ ) { }; // = 0;

    // This is for the class to return some kind of status information for Dumpsys.
    // Note, dumpsys has a strict size limit, so be brief
    virtual String8 dump() = 0;

#if INTEL_HWC_INTERNAL_BUILD
    // NOTE:
    //   Strictly speaking, this should not exist in this Abstract class.
    //   It exists only for internal build validation and is compiled out for production builds.
    //   If a base-class is ever required then this should be moved into it.
    Content mOldOutput;
    std::vector<Layer>mOldLayers[ cMaxSupportedPhysicalDisplays ];
#endif
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_ABSTRACTFILTER_H
