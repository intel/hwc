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

#ifndef INTEL_UFO_HWC_VPPCOMPOSER_H
#define INTEL_UFO_HWC_VPPCOMPOSER_H

#include "AbstractComposer.h"

namespace intel {
namespace ufo {
namespace hwc {

class VppComposer : public AbstractComposer
{
public:
    VppComposer();
    virtual ~VppComposer();

    virtual const char* getName() const;
    virtual float onEvaluate(const Content::LayerStack& source, const Layer& target, AbstractComposer::CompositionState** pState, Cost type = Power);
    virtual void onCompose(const Content::LayerStack& source, const Layer& target, AbstractComposer::CompositionState* pState);
    virtual ResourceHandle onAcquire(const Content::LayerStack& source, const Layer& target);
    virtual void onRelease(ResourceHandle hResource);

private:
    bool mIsContextValid;
    unsigned int mCtxID;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_VPPCOMPOSER_H
