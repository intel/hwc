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

#ifndef INTEL_UFO_HWC_ABSTRACTCOMPOSITION_H
#define INTEL_UFO_HWC_ABSTRACTCOMPOSITION_H

#include "Content.h"

namespace intel {
namespace ufo {
namespace hwc {


// This class is an abstract class that enables a caller to perform a composition.
// Its generally attached to a Layer class in order to update the layer handles
// to the composed result prior to delivering to hardware.
class AbstractComposition
{
public:
    AbstractComposition() {}
    virtual ~AbstractComposition() {};

    // This returns the name of the composer
    virtual const char* getName() const = 0;

    // This function returns the render target destination for this composition.
    // The target layer will be partially complete until the onCompose call is made
    virtual const Layer& getTarget() = 0;

    // This updates any source layer changes for this composition. It should be called every
    // frame whether anything changed or not.
    virtual void onUpdate(const Content::LayerStack& src) = 0;

    // This will be used to update the output layer for a composition.
    virtual void onUpdateOutputLayer(const Layer& target) = 0;

    // This entrypoint actually performs the composition. It will do nothing if the composition
    // is already valid for the current state
    virtual void onCompose() = 0;

    // Acquire and release any resources required for this composition.
    // Acquire can fail if the resources required are already committed elsewhere.
    virtual bool onAcquire() = 0;
    virtual void onRelease() = 0;

    // Get the best cost of evaluation.
    virtual float getEvaluationCost() = 0;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_ABSTRACTCOMPOSITION_H
