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

#ifndef INTEL_UFO_HWC_ABSTRACTPLATFORM_H
#define INTEL_UFO_HWC_ABSTRACTPLATFORM_H

namespace intel {
namespace ufo {
namespace hwc {

class Hwc;
class PlatformServices;

class AbstractPlatform
{
public:
    // Singleton type accessor for device specific platform class
    static AbstractPlatform& get();

    // Obtain the DRM Master handle
    static int getDrmHandle();

    AbstractPlatform() {}
    virtual ~AbstractPlatform() {}

    // Open the platform specific device
    virtual status_t open(Hwc* mHwc) = 0;

    // Return a platform services instance
    virtual PlatformServices& getPlatformServices() = 0;

    // Return Hwc pointer
    virtual Hwc* getHwc() = 0;
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_ABSTRACTPLATFORM_H
