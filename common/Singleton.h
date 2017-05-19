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

#ifndef INTEL_UFO_HWC_SINGLETON_H
#define INTEL_UFO_HWC_SINGLETON_H

namespace intel {
namespace ufo {
namespace hwc {

template <typename TYPE>
class Singleton : NonCopyable
{
public:
    // This function should be inlined as much as possible. The first access should
    // provide a mutex protected creation of the instance
    static TYPE& getInstance() ALWAYS_INLINE
    {
        if (CC_LIKELY(spInstance))
            return *spInstance;
        return getInstanceInternal();
    }

private:
    // This function does not want to be inlined. This saves 23 instructions of assembly per getInstance call.
    static TYPE& getInstanceInternal() NEVER_INLINE
    {
        static TYPE sInstance;
        spInstance = &sInstance;
        return sInstance;
    }

    static TYPE *spInstance;
};

template<typename TYPE> TYPE* Singleton< TYPE >::spInstance;

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_SINGLETON_H
