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

#ifndef INTEL_UFO_HWC_ABSTRACTCOMPOSER_H
#define INTEL_UFO_HWC_ABSTRACTCOMPOSER_H

#include "Layer.h"

namespace intel {
namespace ufo {
namespace hwc {

class AbstractComposer
{
public:
    AbstractComposer() {}
    virtual ~AbstractComposer() {};

    enum Cost
    {
        Bandwidth,
        Power,
        Performance,
        Memory,
        Quality,
    };

    // onEvaluate() returns a float representing the cost of the composition, with <0 meaning
    // not supported and >0 being the relative cost with respect to other composers.
    constexpr static float Eval_Not_Supported = -1;
    constexpr static float Eval_Cost_Min = 0;
    constexpr static float Eval_Cost_Max = FLT_MAX;

    static float evalCostAdd(float a, float b)
    {
        if ((a <= Eval_Not_Supported) || (b <= Eval_Not_Supported))
        {
            return Eval_Not_Supported;
        }
        if (a >=  Eval_Cost_Max/2 || b >= Eval_Cost_Max/2)
            return Eval_Cost_Max;
        return a + b;
    }

    // Composer-composition specific data.
    // Composers may derive their own concrete class to store composer-composition specific state.
    // A composer can derive and allocate concrete class for each composition.
    // Then on compose it can cast back to access its own data.
    class CompositionState
    {
    public:
        CompositionState() {}
        virtual ~CompositionState() {};
    };

    typedef void * ResourceHandle;

    // This returns the name of the composer.
    virtual const char* getName() const = 0;

    // This entrypoint evaluates the cost of the composition using this composer in the units requested
    // The actual unit cost is abstract, it just has to be approximately correct relative to other composers
    // Lower is always better
    // Return of less than zero indicates that the composer cannot compose this stack, see Eval_* constants above.
    // The composer may optionally create and return composer-composition specific state in *ppState.
    // The AbstractComposerCompostionState will be stored with the composition and passed back in for each onCompose.
    virtual float onEvaluate(const Content::LayerStack& src, const  Layer& target, AbstractComposer::CompositionState** ppState, Cost type = Power) = 0;


    // This entrypoint actually performs the composition
    // AbstractComposerCompostionState will be the composer-composition specific state returned
    // previously from onEvaluate(), or NULL if no state was provided.
    virtual void onCompose(const Content::LayerStack& src, const Layer& target, AbstractComposer::CompositionState* pState ) = 0;

    // Acquire/Release any resources required for the specified composition.
    // onAcquire must return non-NULL on success.
    // Any acquired resources must be explicitly released when they are no-longer required.
    virtual ResourceHandle onAcquire(const Content::LayerStack& source, const Layer& target) = 0;
    virtual void onRelease(ResourceHandle hResource) = 0;
};

namespace old {
class AbstractComposer
{
public:
    AbstractComposer() {}
    virtual ~AbstractComposer() {};

    // TODO: Old Abstract Composer entrypoints: Remove as older implementations get updated
public:

    // This is the standard Hwc prepare entrypoint. In this function we look at all the source
    // layers and mark any that we want to handle as Overlay. We store internally the indices
    // of the layers that we want to handle.
    virtual int onPrepare(hwc_display_contents_1_t* pDisp) = 0;

    // This is the standard composition entrypoint. We are passed in the RenderTarget from any
    // previous compositions. The function either returns the input rendertarget if it has nothing
    // to compose or it returns a new render target otherwise.
    virtual hwc_layer_1_t* onCompose(hwc_display_contents_1_t* pDisp, hwc_layer_1_t* pRenderTarget) = 0;

    // This is the complete composition entrypoint for this display. This is called after the display
    // composition has completed with the appropriate release fence fd for the render target buffer.
    virtual int onComplete(int releaseFenceFd) = 0;
};
}; // namespace old

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_ABSTRACTCOMPOSER_H
