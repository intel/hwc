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

#include "AbstractPlatform.h"
#include "PlatformServices.h"
#include "Singleton.h"
#include "Option.h"


#ifndef INTEL_UFO_HWC_VPGPLATFORM_H
#define INTEL_UFO_HWC_VPGPLATFORM_H

namespace intel {
namespace ufo {
namespace hwc {

class Hwc;

class VpgPlatform : public AbstractPlatform, public PlatformServices, public Singleton<VpgPlatform>
{
public:
    VpgPlatform();
    virtual ~VpgPlatform();

    // Obtain the DRM Master handle
    static int getDrmHandle();

    // Implements AbstractPlatform.
    virtual status_t open(Hwc* mHwc);

    // Return a platform services instance
    virtual PlatformServices& getPlatformServices() { return *this; }

    // Return Hwc pointer
    virtual Hwc* getHwc() { return mpHwc; }

private:
    friend class Singleton<VpgPlatform>;

    Hwc*            mpHwc;

    Option      mOptionVppComposer;
    Option      mOptionPartGlComp;
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_PLATFORM_H
