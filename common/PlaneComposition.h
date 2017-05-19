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

#ifndef INTEL_UFO_HWC_PLANECOMPOSITION_H
#define INTEL_UFO_HWC_PLANECOMPOSITION_H

#include "AbstractComposition.h"
#include "AbstractComposer.h"
#include "Layer.h"

namespace intel {
namespace ufo {
namespace hwc {

class CompositionManager;
class DisplayCaps;

// This class is responsible for handling the construction of multiple
// compositions from an input layer stack. Its generally initialised with a list of
// current compositions on a GeometryChange and between updates, it manages the layer
// frame state of its compositions itself
// Its usually used as an input to a PlaneAllocator and its results will be used as
// output by a Display
class PlaneComposition : public AbstractComposition
{
public:
    PlaneComposition();
    virtual ~PlaneComposition();

    // This should be initialised to the maximum number of planes we expect to have on any display
    static const uint32_t MAX_PLANES = 4;

    void            clear();

    const char*     getName() const { return "PlaneComposition"; }
    const Layer&    getTarget();

    void            onUpdate(const Content::LayerStack& src);
    void            onUpdate(const Content::Display& src);
    void            onUpdateOutputLayer(const Layer& target);
    void            onCompose();
    bool            onAcquire();
    void            onRelease();
    uint32_t        onLock( void ) { return 0; }
    uint32_t        onUnlock( void ) { return 0; }
    float           getEvaluationCost() { return AbstractComposer::Eval_Cost_Max; }

    String8         dump(const char* pIdentifier = "" ) const;

    // Functions to register compositons
    bool            addFullScreenComposition(const DisplayCaps& caps, uint32_t overlayIndex, uint32_t srcLayerIndex, uint32_t numLayers, int32_t colorFormat);
    bool            addSourcePreprocess(const DisplayCaps& caps, uint32_t overlayIndex, uint32_t srcLayerIndex, int32_t colorFormat);
    bool            addDedicatedLayer(uint32_t overlayIndex, uint32_t srcLayerIndex);

    uint32_t        getZOrder() const                   { return mZOrder; }
    void            setZOrder(uint32_t zOrder)          { mZOrder = zOrder; }

    // Should only be used by PhysicalDisplayManager when it knows it can
    void            fallbackToSurfaceFlinger(uint32_t display);

    void            setCompositionManager(CompositionManager* pCompositionManager) { mpCompositionManager = pCompositionManager; };
    void            setDisplayInput(const Content::Display* input) { ALOG_ASSERT(input), mpDisplayInput = input; };

    const Content::Display& getDisplayOutput() const { return mDisplayOutput; }


private:
    CompositionManager*         mpCompositionManager;
    uint32_t                    mZOrder;

    // Each plane will have a plane state structure that holds its composition
    class PlaneState
    {
    public:
        PlaneState() :
            mStartIndex(-1),
            mpComposition(NULL),
            mbIsPreprocessed(false)
        {
        }

        int32_t                 mStartIndex;
        Content::LayerStack     mLayers;
        AbstractComposition*    mpComposition;
        Layer                   mLayerPPSrc;
        Layer                   mLayerPPDst;
        bool                    mbIsPreprocessed:1;
    };

    const Content::Display*     mpDisplayInput;
    Content::Display            mDisplayOutput;
    PlaneState                  mPlaneState[MAX_PLANES];

};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_PLANECOMPOSITION_H
