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

#ifndef INTEL_UFO_HWC_COMBINEDCOMPOSER_H
#define INTEL_UFO_HWC_COMBINEDCOMPOSER_H

#include "AbstractComposer.h"
#include <utils/Vector.h>
#include "CompositionManager.h"

namespace intel {
namespace ufo {
namespace hwc {


// Originally, every composition utilize one composer to do composition, here the composer is shared and not need any private data.
// But after combined composer is introduced, 2 or more compositions will be created in the combined composer,
// the sub-compositions' data should be stored in the combined composer, but considering that the combined composer should also be shared by multiple compositions,
// different compositions (using the same combined composer) have the different subCompositions in the combined composer.
// So we have to allocate a class in composition to store private data of combined composer and also pass it to combined composer when use this composer.
// CombinedComposerCompositionState is used for store sub-composition state
//
// TODO: better and more flexible CombinedComposerCompositionState define
class CombinedComposerCompositionState : public AbstractComposer::CompositionState
{
public:
    CombinedComposerCompositionState( uint32_t subCompositionNum ):
        mLayerCnt(0), mbLocked(false)
    {
        mSubCompositions.resize(subCompositionNum);
    }
    virtual ~CombinedComposerCompositionState()
    {
        unlock();
        mSubCompositions.clear();
        mLayerList.clear();
    }

    void lock( void )
    {
        ALOG_ASSERT( !mbLocked );
        for ( uint32_t sc = 0; sc < mSubCompositions.size(); ++sc )
        {
            CompositionManager::getInstance().lockComposition( mSubCompositions[sc].mComposition );
        }
        mbLocked = true;
    }

    void unlock( void )
    {
        if ( mbLocked )
        {
            for ( uint32_t sc = 0; sc < mSubCompositions.size(); ++sc )
            {
                CompositionManager::getInstance().unlockComposition( mSubCompositions[sc].mComposition );
            }
            mbLocked = false;
        }
    }

    class SubComposition
    {
    public:
         SubComposition(): mComposition(NULL){};
         ~SubComposition(){}
        AbstractComposition *mComposition;    // SubComposition of combined composer.
        Content::LayerStack mSrcLayerStack;   // Input layer to the subComposition, which is split from the parent composition's input layer.
    };

 public:
    Vector<SubComposition> mSubCompositions;  // SubComposition vector.
    std::vector<Layer>     mLayerList;        // Store the layer stack since input layer stack need to be modified in same case.
    uint32_t mLayerCnt;                       // Input layer count of combined composer.
    bool mbLocked:1;                          // Have the subcompositions been locked down?
};


class CombinedComposer : public AbstractComposer
{
public:
    CombinedComposer():mbReentering(false) {}
    ~CombinedComposer(){}

    virtual const char* getName() const = 0;

    // Evaluate this combined composer:
    // It will return dynamically allocated state block, which will be freed if this composer is not the best one.
    //
    // 1. preprocess input layerstack and  setup composition chain:
    //     1). Decide how many compositions will be used for the first stage.
    //     2). Decide how to distribute layers into different compositions
    //     For example: there are 5 layers, layer 0~4
    //          For Widi 2 stage composer:
    //               First stage:
    //                   layer 0~4  ==>  firstStageCompositions[0]  ==> RT0
    //               Second stage:
    //                   RT0            ==> secondStageComposition  ==> RT (NV12)
    //
    //          For low loss composer("high quality frame for low resolution"):
    //               First stage:
    //                   layer 0~4  ==>  modify layers' destination parameters to make it do 1:1 composition
    //                                    ==> firstStageCompositions[0]  ==> RT0
    //               Second stage:
    //                   RT0     ==> modify RT0's destination parameters to match RT's so to do downscaling
    //                              ==> secondStageComposition  ==> RT
    //
    //         Generic model: layer0~4 ' RT
    //               First stage:
    //                   layer 0~1  ==>  modify layer parameters if need  ==> firstStageCompositions[0]  ==>  RT0
    //                   layer 2~3  ==>  modify layer parameters if need  ==> firstStageCompositions[1]  ==>  RT1
    //              Second stage:
    //                   RT0, RT1, layer 4  ==> modify layer parameters if need  ==> secondStageComposition  ==> RT
    //
    // 2. request composition.
    //     1) request firstStageCompositions
    //     2) request secondStageComposition
    // 3. Get the cost for this composer
    virtual float onEvaluate(const Content::LayerStack& src, const  Layer& target, AbstractComposer::CompositionState** ppState, Cost type = Power) = 0;
    virtual void onCompose(const Content::LayerStack& src, const Layer& target, AbstractComposer::CompositionState* pState ) = 0;

public:
    // TODO: Better method to protect against re-entrancy.
    bool mbReentering;   // Is onEvaluate()  reentering?  It used to avoid nest calling onEvaluate().

};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_VPPCOMPOSER_H
