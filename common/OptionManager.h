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

#ifndef INTEL_UFO_HWC_HWCOPTIONMANAGER_H
#define INTEL_UFO_HWC_HWCOPTIONMANAGER_H

#include "Hwc.h"
#include "Singleton.h"
#include "Option.h"
#include <utils/Mutex.h>
#include <utils/Vector.h>

namespace intel {
namespace ufo {
namespace hwc {

class OptionManager : public Singleton<OptionManager>
{
public:
    void initialize(Hwc& hwc)
    {
        mpHwc = &hwc;
    }

    void add(Option* pOption);
    void remove(Option* pOption);
    android::String8 dump();

    void forceGeometryChange()
    {
        if (mpHwc)
        {
            // Apply the forced geometry change and synchronize with the flip
            // queue to ensure its complete
            mpHwc->forceGeometryChange();
            mpHwc->synchronize();
        }
    }

    // Static accessor to search registered options for a name match
    // Can be a partial match
    static Option* find(const char* name, bool bExact = true);

private:
    friend class            Singleton<OptionManager>;

    Option*                 findInternal(const char* name, bool bExact);

    Mutex                   mOptionsMutex;
    Vector<Option*>         mOptions;
    Hwc*                    mpHwc;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
#endif // INTEL_UFO_HWC_HWCOPTIONMANAGER_H
