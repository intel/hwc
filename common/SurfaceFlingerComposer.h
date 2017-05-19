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

#ifndef INTEL_UFO_HWC_SURFACEFLINGERCOMPOSER_H
#define INTEL_UFO_HWC_SURFACEFLINGERCOMPOSER_H

#include "AbstractComposer.h"
#include "AbstractFilter.h"

namespace intel {
namespace ufo {
namespace hwc {

class BufferQueue;


// This is a special composer that manages the SurfaceFlinger composition resources.
// Its also a filter. Its expected to run on unmodified layer state from the InputAnalyzer.
// As a filter, it turns unsupportable state into a supported render target input for the
// rest of the filter/composition engines to manage. As such, its the only component that
// has to handle skip planes
class SurfaceFlingerComposer : public AbstractComposer, public AbstractFilter
{
public:
    SurfaceFlingerComposer();
    virtual ~SurfaceFlingerComposer();

    virtual const char* getName() const;
    virtual float onEvaluate(const Content::LayerStack& source, const Layer& target, AbstractComposer::CompositionState** ppState, Cost type = Power);
    virtual void onCompose(const Content::LayerStack& source, const Layer& target, AbstractComposer::CompositionState* pState);
    virtual ResourceHandle onAcquire(const Content::LayerStack& source, const Layer& target);
    virtual void onRelease(ResourceHandle hResource);

    // Entrypoints to this composer to inform it of the layer lists
    void onPrepareBegin(size_t numDisplays, hwc_display_contents_1_t** ppDisplayContents, nsecs_t now);

    // This function needs to update the flags on the SF inputs to indicate what compositions
    // this composition engine is required to perform
    void onPrepareEnd();

    // This function informs the composer about the render target provided by the SurfaceFlinger
    void onSet(size_t numDisplays, hwc_display_contents_1_t** ppDisplayContents, nsecs_t now);

    // This function returns the actual target of a composition.
    const Layer& getTarget(ResourceHandle hResource);

    // This function is normally a fallback situation. Something went wrong, we need to fall back
    // to SF composition for all layers. This function cannot fail.
    AbstractComposition* handleAllLayers(uint32_t display);

    // AbstractFilter interfaces
    const Content& onApply(const Content& ref);
    String8 dump();

private:
    void findUnsupportedLayerRange(uint32_t d, const Content::Display& in);

    // return the matching display index or -1
    int findMatch(const Content::LayerStack& source, int32_t &min, int32_t &max) const;

private:
    nsecs_t                         mTimestamp;
    uint32_t                        mNumDisplays;
    hwc_display_contents_1_t**      mppDisplayContents;

    // Per display state
    class Composition : public AbstractComposition
    {
    public:
        Composition();
        ~Composition();

        const Layer&    getTarget();
        const char*     getName() const;

        void            onUpdatePending(nsecs_t frameTime);
        void            onUpdateFrameState(hwc_layer_1& layer, nsecs_t frameTime);
        void            onUpdateAll(hwc_layer_1& layer, nsecs_t frameTime);
        void            onUpdate(const Content::LayerStack& src);
        void            onUpdateOutputLayer(const Layer& target);
        void            onCompose();
        bool            onAcquire();
        void            onRelease();

        float           getEvaluationCost() { return  AbstractComposer::Eval_Cost_Max; }
        String8         dump(const char* pIdentifier = "") const;

        const Layer&  getRenderTarget() const { return mRenderTarget; }
    private:
        friend SurfaceFlingerComposer;

        int32_t composeMin() const { return (mComposeMin < 0) ? mUnsupportedMin : mComposeMin; }
        int32_t composeMax() const { return (mComposeMax < 0) ? mUnsupportedMax : mComposeMax; }

        // Min and max ranges. -1 Max indicates no such range. 0 min, 0 max means just layer 0 is unsupported.
        int32_t mUnsupportedMin;
        int32_t mUnsupportedMax;
        int32_t mComposeMin;
        int32_t mComposeMax;
        int32_t mLastComposedMin;
        int32_t mLastComposedMax;
        Layer   mRenderTarget;
        int32_t mRenderTargetFormat;                // Expected render target format. Defaults a default format, will correct itself on the first actual render target.
        ETilingFormat mRenderTargetTilingFormat;    // Expected render target tiling format. Defaults a default format, will correct itself on the first actual render target.

        bool    mbForceGeometryChange;
    } mCompositions[cMaxSupportedSFDisplays];

    Content  mOutRef;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_SURFACEFLINGERCOMPOSER_H
