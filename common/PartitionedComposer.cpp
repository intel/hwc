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

#include "Common.h"
#include "PartitionedComposer.h"
#include "Log.h"
#include "Utils.h"

#include <math.h>

#include <ui/Region.h>
#include <utils/Vector.h>

namespace intel {
namespace ufo {
namespace hwc {

PartitionedComposer::PartitionedComposer(std::shared_ptr<CellComposer> renderer):
    mpRenderer(renderer),
    mOptionPartitionVideo("partitionvideo", 1)
{
}

PartitionedComposer::~PartitionedComposer()
{
}

const char* PartitionedComposer::getName() const
{
    return "PartitionedComp";
}

float PartitionedComposer::onEvaluate(const Content::LayerStack& source, const Layer& target, AbstractComposer::CompositionState** ppState, Cost type)
{
    ALOGD_IF(COMPOSITION_DEBUG, "PartitionedComposer: Evaluating\n%sRT %s", source.dump().string(), target.dump().string());
    HWC_UNUSED(ppState);

    // Check that the Vpp composer supports all the layer types
    if (!mpRenderer->isLayerSupportedAsOutput(target))
    {
        ALOGD_IF(COMPOSITION_DEBUG, "PartitionedComposer: Unsupported output format: %s", target.dump().string());
        return Eval_Not_Supported;
    }

    bool unsupportedInput = false;
    for (uint32_t ly = 0; ly < source.size(); ly++)
    {
        const Layer& layer = source[ly];

        if (!mpRenderer->isLayerSupportedAsInput(layer))
        {
            ALOGD_IF(COMPOSITION_DEBUG, "PartitionedComposer: Unsupported input format of layer %d: %s", ly, layer.dump().string());
            unsupportedInput = true;
        }
    }

    // If the option is disabled then don't allow video to video composition
    // with this composer.
    if (!mOptionPartitionVideo && source.isVideo() && isVideo(target.getBufferFormat()))
    {
        ALOGD_IF(COMPOSITION_DEBUG, "PartitionedComposer: Video to Video composition disabled");
        return Eval_Not_Supported;
    }

    if (unsupportedInput)
    {
        if (!mpRenderer->canBlankUnsupportedInputLayers())
        {
            ALOGD_IF(COMPOSITION_DEBUG, "PartitionedComposer: Unsupported input layers");
            return Eval_Not_Supported;
        }

        ALOGD_IF(COMPOSITION_DEBUG, "PartitionedComposer: Evaluation cost(%d) = %f with blanked input!", type, Eval_Cost_Max);
        return Eval_Cost_Max;
    }

    float cost = Eval_Not_Supported;
    switch (type)
    {
        case Bandwidth:
        case Power:         // TODO: Implement, for now, default to bandwidth
        case Performance:   // TODO: Implement, for now, default to bandwidth
        case Quality:       // TODO: Implement, for now, default to bandwidth
        {
            float bandwidth = calculateBandwidthInKilobytes(target.getDstWidth(), target.getDstHeight(), target.getBufferFormat());
            for (uint32_t ly = 0; ly < source.size(); ly++)
            {
                const Layer& layer = source[ly];
                // 1 read of source per layer
                bandwidth += calculateBandwidthInKilobytes(layer.getSrcWidth(), layer.getSrcHeight(), layer.getBufferFormat());
            }
            cost = bandwidth * target.getFps(); // Times the frames per second
        }
        break;
    case Memory:
        // This costs us a preallocated double buffered render target buffer.
        cost = target.getDstWidth() * target.getDstHeight() * 2;
        break;
    }

    // TODO: Very simple guestimate for now based on expected bandwidth usage
    ALOGD_IF(COMPOSITION_DEBUG, "PartitionedComposer: Evaluation cost(%d) = %f", type, cost);
    return cost;
}


class Partition
{
public:
    Partition() {}
    Partition(const Rect &rect) : mRegion(rect) {}

    String8 dump(const char* pStr = "") const
    {
        String8 output = String8::format("%s numLayers:%zd ", pStr, mLayers.size());
        for(uint32_t i=0; i < mLayers.size(); i++)
            output += String8::format("%d,", mLayers[i]);

        size_t size;
        const Rect* pRects = mRegion.getArray(&size);

        output += String8::format(" numRects:%zd ", size);
        for (uint32_t i = 0; i < size; i++)
            output += String8::format("(%d, %d, %d, %d) ", pRects[i].left, pRects[i].top, pRects[i].right, pRects[i].bottom);
        return output;
    }

    Region           mRegion;
    Vector<uint32_t> mLayers;
};

// Intersect the current partition list with the layer specified in ly and any relevant lower layers
static void intersect(const Content::LayerStack& source, int32_t ly, Vector<Partition> &partitions, uint32_t pi)
{
    // Terminate the recursion when the layer count goes negative
    if (ly < 0)
        return;

    const Layer& layer = source.getLayer(ly);
    const hwc_rect_t& r = source.getLayer(ly).getDst();
    Rect rect(r.left, r.top, r.right, r.bottom);

    Region inside = partitions[pi].mRegion.intersect(rect);

    // If there is no intersection, leave this entry entirely alone and go to next layer
    if (inside.isEmpty())
    {
        intersect(source, ly-1, partitions, pi);
        return;
    }

    Region outside = partitions[pi].mRegion.subtract(rect);

    // If there is something left outside, create a new partition at the end of the list
    // and partition it with the next layer
    if (!outside.isEmpty())
    {
        Partition p;
        p.mRegion = outside;
        p.mLayers = partitions[pi].mLayers;
        partitions.push_back(p);

        // Partition this new partition with whatever is left
        intersect(source, ly-1, partitions, partitions.size()-1);

        // The inside region will only change if there was an outside to handle
        partitions.editItemAt(pi).mRegion = inside;
    }

    partitions.editItemAt(pi).mLayers.push_front(ly);

    // terminate partitioning at the first opaque layer
    if (!layer.isOpaque())
    {
        intersect(source, ly-1, partitions, pi);
    }
    return;
}


void PartitionedComposer::onCompose(const Content::LayerStack& source, const Layer& target, AbstractComposer::CompositionState* pState)
{
    ATRACE_NAME_IF(RENDER_TRACE, "PartitionedComposer");
    HWC_UNUSED(pState);

    ALOGD_IF(PARTITION_DEBUG, "PartitionedComposer: onCompose\n%sRT %s", source.dump().string(), target.dump().string());
    Log::add(source, target, "PartitionedComposer");

    target.waitAcquireFence();
    for (uint32_t index = 0; index < source.size(); ++index)
    {
        const Layer& srcLayer = source[index];

        // Wait for any acquire fence
        srcLayer.waitAcquireFence();

        // We know that the vp renderer is synchronous, indicate that here.
        srcLayer.returnReleaseFence(-1);
    }

    Vector<Partition> partitions;

    // Initialise partition list to top of stack
    const hwc_rect_t& r = target.getDst();
    partitions.push_back(Partition(Rect(r.left, r.top, r.right, r.bottom)));

    // Generate the partitions from frontmost to backmost
    intersect(source, source.size()-1, partitions, 0);

    // Start the frame
    mpRenderer->beginFrame(source, target);

    for (uint32_t pi = 0; pi < partitions.size(); pi++)
    {
        const Partition& p = partitions[pi];

        ALOGD_IF(PARTITION_DEBUG, "%s", p.dump().string());
        mpRenderer->drawLayerSet(p.mLayers.size(), p.mLayers.array(), p.mRegion);
    }

    mpRenderer->endFrame();
}

AbstractComposer::ResourceHandle PartitionedComposer::onAcquire(const Content::LayerStack& source, const Layer& target)
{
    HWC_UNUSED(source);
    HWC_UNUSED(target);
    return this;
}

void PartitionedComposer::onRelease(ResourceHandle hResource)
{
    HWC_UNUSED(hResource);
}

} // namespace hwc
} // namespace ufo
} // namespace intel
