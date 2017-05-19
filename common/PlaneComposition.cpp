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
#include "PlaneComposition.h"
#include "CompositionManager.h"
#include "DisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

PlaneComposition::PlaneComposition() :
    mpCompositionManager(NULL),
    mZOrder(0),
    mpDisplayInput(NULL)
    // Initialization should be in the clear function
{
    clear();
}

PlaneComposition::~PlaneComposition()
{
}

void PlaneComposition::clear()
{
    for (uint32_t i = 0; i < MAX_PLANES; i++)
    {
        PlaneState& state = mPlaneState[i];
        state.mStartIndex = -1; // Index -1 indicates uninitialised
        state.mpComposition = NULL;
        state.mbIsPreprocessed = false;
    }
    mDisplayOutput.disable();
}

const Layer& PlaneComposition::getTarget()
{
    // Has no meaning for this multi target composer
    ALOG_ASSERT(0);
    return Layer::Empty();
}

void PlaneComposition::onUpdate(const Content::LayerStack& src)
{
    // Simply run through all our composers and pass on the update
    for (uint32_t i = 0; i < MAX_PLANES; i++)
    {
        PlaneState& state = mPlaneState[i];
        if (state.mpComposition)
        {
            if (state.mbIsPreprocessed)
            {
                // This is preprocessed layer. Update the source frame state to reflect the input state
                state.mLayerPPSrc.onUpdateFrameState(src[state.mStartIndex]);
                ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::onUpdate Preprocessed Source Layer %i: %s", i, state.mLayerPPSrc.dump().string());
            }
            state.mpComposition->onUpdate(state.mLayers);
        }
    }

    // Ensure all flags are consistent
    mDisplayOutput.editLayerStack().updateLayerFlags(src);

    return;
}

void PlaneComposition::onUpdate(const Content::Display& src)
{
    onUpdate(src.getLayerStack());
    setDisplayInput(&src);

    // If we have an output target layer, then pass it onto the first plane.
    const Layer* pOut = src.getOutputLayer();
    if (pOut && mPlaneState[0].mpComposition)
    {
        mPlaneState[0].mpComposition->onUpdateOutputLayer(*pOut);
    }
}


void PlaneComposition::onUpdateOutputLayer(const Layer&)
{
    // Plane composer issues this call, it should never receive it.
    ALOG_ASSERT(false);
}

void PlaneComposition::onCompose()
{
    ALOG_ASSERT( mpDisplayInput );

    // Simply run through all our composers and compose
    for (uint32_t i = 0; i < MAX_PLANES; i++)
    {
        PlaneState& state = mPlaneState[i];
        if (state.mpComposition)
        {
            state.mpComposition->onCompose();
            if (state.mbIsPreprocessed)
            {
                // This is preprocessed layer. Update the destination frame state to reflect the composition results
                state.mLayerPPDst.onUpdateFrameState(state.mpComposition->getTarget());
                ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::onCompose Preprocessed Dest Layer %i: %s", i, state.mLayerPPDst.dump().string());
            }
        }
    }

    mDisplayOutput.setFrameIndex( mpDisplayInput->getFrameIndex() );
    mDisplayOutput.setFrameReceivedTime( mpDisplayInput->getFrameReceivedTime() );


    return;
}

bool PlaneComposition::onAcquire()
{
    ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::onAcquire zorder:%d", mZOrder);

    uint32_t numLayers = 0;
    // Simply run through all our composers and acquire
    for (uint32_t i = 0; i < MAX_PLANES; i++)
    {
        PlaneState& state = mPlaneState[i];

        // Look for the maximum created layer
        if (state.mStartIndex >= 0)
        {
            numLayers = i + 1;

            if (state.mpComposition)
            {
                ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::onAcquire Layer %d Composition", i);
                if (!state.mpComposition->onAcquire())
                {
                    // Got a failure, need to release anything we acquired already
                    for (uint32_t j = 0; j < i; j++)
                    {
                        PlaneState& state = mPlaneState[j];
                        if (state.mpComposition)
                            state.mpComposition->onRelease();
                    }
                    return false;
                }
            }
            else
            {
                ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::onAcquire Layer %d Dedicated", i);
            }
        }
        else
        {
            ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::onAcquire Layer %d Disabled", i);
        }
    }

    // Update the display out function
    mDisplayOutput.updateDisplayState(*mpDisplayInput);
    const Content::LayerStack inputLayers = mpDisplayInput->getLayerStack();
    Content::LayerStack& layers = mDisplayOutput.editLayerStack();
    layers.resize(numLayers);
    for (uint32_t i = 0; i < numLayers; i++)
    {
        PlaneState& state = mPlaneState[i];
        if (state.mStartIndex >= 0)
        {
            if (state.mpComposition == NULL)
            {
                // This is a dedicated layer
                layers.setLayer(i, &inputLayers.getLayer(state.mStartIndex));
            }
            else if (state.mbIsPreprocessed)
            {
                // This is preprocessed layer. Take a copy of the target and adjust the destination to match the input layer destination
                // Frame state needs to be updated after composition when the render target is valid
                layers.setLayer(i, &state.mLayerPPDst);
            }
            else
            {
                // This is a composition, so use the result layer directly
                layers.setLayer(i, &state.mpComposition->getTarget());
            }

        }
        else
        {
            // Uninitialised layer, just set it to empty
            layers.setLayer(i, &Layer::Empty());
        }
    }
    layers.updateLayerFlags(inputLayers);

    // If we have an output target layer, then pass it onto the first plane.
    const Layer* pOut = mpDisplayInput->getOutputLayer();
    if (pOut && mPlaneState[0].mpComposition)
    {
        mPlaneState[0].mpComposition->onUpdateOutputLayer(*pOut);
    }

    ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::onAcquire Output:\n%s", layers.dump().string());
    return true;
}

void PlaneComposition::onRelease()
{
    // Simply run through all our composers and release
    for (uint32_t i = 0; i < MAX_PLANES; i++)
    {
        PlaneState& state = mPlaneState[i];
        if (state.mpComposition)
            state.mpComposition->onRelease();
    }
    clear();
}

bool PlaneComposition::addFullScreenComposition(const DisplayCaps& caps, uint32_t overlayIndex, uint32_t srcLayerIndex, uint32_t numLayers, int32_t colorFormat)
{
    ALOG_ASSERT(overlayIndex < MAX_PLANES);
    ALOG_ASSERT(mpCompositionManager);

    ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::addFullScreenComposition ov:%d srcIndex:%d Num:%d to input format %s", overlayIndex, srcLayerIndex, numLayers, getHALFormatShortString(colorFormat));

    PlaneState& state = mPlaneState[overlayIndex];
    ALOG_ASSERT(state.mStartIndex < 0); // Should never initialise a layer twice

    state.mStartIndex = srcLayerIndex;
    state.mLayers.resize(numLayers);
    state.mbIsPreprocessed = false;
    Content::LayerStack inputLayers = mpDisplayInput->getLayerStack();
    for (uint32_t ly = 0; ly < numLayers; ly++)
    {
        state.mLayers.setLayer(ly, &inputLayers.getLayer(srcLayerIndex + ly));
    }
    state.mLayers.updateLayerFlags();

    const DisplayCaps::PlaneCaps& planeCaps = caps.getPlaneCaps(overlayIndex);
    for (unsigned compIdx = 0; ; compIdx++)
    {
        ECompressionType compression = planeCaps.getCompression(compIdx, colorFormat);
        // TODO: Optimise in the future to use max source extents rather than full screen.
        state.mpComposition = mpCompositionManager->requestComposition(state.mLayers, mpDisplayInput->getWidth(), mpDisplayInput->getHeight(), colorFormat, compression);
        if (state.mpComposition != NULL)
        {
            break;
        }
        if (compression == COMPRESSION_NONE)
        {
            ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::addFullScreenComposition requestComposition Failed of layers\n%s", state.mLayers.dump().string());
            clear();
            return false;
        }
    }

    return true;
}

bool PlaneComposition::addSourcePreprocess(const DisplayCaps& caps, uint32_t overlayIndex, uint32_t srcLayerIndex, int32_t colorFormat)
{
    ALOG_ASSERT(overlayIndex < MAX_PLANES);
    ALOG_ASSERT(mpCompositionManager);

    ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::addSourcePreprocess ov:%d srcIndex:%d Format:%s", overlayIndex, srcLayerIndex, getHALFormatShortString(colorFormat));

    PlaneState& state = mPlaneState[overlayIndex];
    ALOG_ASSERT(state.mStartIndex < 0); // Should never initialise a layer twice

    Content::LayerStack inputLayers = mpDisplayInput->getLayerStack();
    state.mStartIndex = srcLayerIndex;
    state.mLayerPPSrc = inputLayers.getLayer(srcLayerIndex);
    state.mbIsPreprocessed = true;

    // Remove the offset in the Source layer's destination so that the composition always renders to 0, 0 destination.
    hwc_rect_t& rsd = state.mLayerPPSrc.editDst();
    rsd.right -= rsd.left;
    rsd.left = 0;
    rsd.bottom -= rsd.top;
    rsd.top = 0;

    // Set the source of the Dest layer's source to the source above.
    state.mLayerPPDst = inputLayers.getLayer(srcLayerIndex);
    hwc_frect_t& rds = state.mLayerPPDst.editSrc();
    rds.right = rsd.right;
    rds.left = 0;
    rds.bottom = rsd.bottom;
    rds.top = 0;
    // The composition should apply any transforms so clear them on the dst.
    state.mLayerPPDst.setTransform(ETransform::NONE);

    state.mLayers.resize(1);
    state.mLayers.setLayer(0, &state.mLayerPPSrc);
    state.mLayers.updateLayerFlags();

    const DisplayCaps::PlaneCaps& planeCaps = caps.getPlaneCaps(overlayIndex);
    for (unsigned compIdx = 0; ; compIdx++)
    {
        ECompressionType compression = planeCaps.getCompression(compIdx, colorFormat);
        state.mpComposition = mpCompositionManager->requestComposition(state.mLayers, rsd.right, rsd.bottom, colorFormat, compression);
        if (state.mpComposition != NULL)
        {
            break;
        }
        if (compression == COMPRESSION_NONE)
        {
            ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::addSourcePreprocess requestComposition Failed for layers:\n%s", state.mLayers.dump().string());
            clear();
            return false;
        }
    }
    return true;
}


bool PlaneComposition::addDedicatedLayer(uint32_t overlayIndex, uint32_t srcLayerIndex)
{
    ALOG_ASSERT(overlayIndex < MAX_PLANES);
    ALOG_ASSERT(mpCompositionManager);

    ALOGD_IF(COMPOSITION_DEBUG, "PlaneComposition::addDedicatedLayer ov:%d srcIndex:%d", overlayIndex, srcLayerIndex);

    PlaneState& state = mPlaneState[overlayIndex];
    ALOG_ASSERT(state.mStartIndex < 0); // Should never initialise a layer twice

    state.mStartIndex = srcLayerIndex;
    state.mLayers.resize(0);
    state.mLayers.updateLayerFlags();
    state.mpComposition = NULL;
    state.mbIsPreprocessed = false;
    return true;
}

void PlaneComposition::fallbackToSurfaceFlinger(uint32_t display)
{
    Log::alogd( COMPOSITION_DEBUG, "D%d fallbackToSurfaceFlinger!", display );

    // Reset state
    clear();

    // And set up as a composition from SurfaceFlinger
    PlaneState& state = mPlaneState[0];
    state.mStartIndex = 0;
    state.mLayers.resize(0);
    state.mLayers.updateLayerFlags();
    state.mpComposition = mpCompositionManager->fallbackToSurfaceFlinger(display);
    state.mbIsPreprocessed = false;

    // This fallback method has to acquire itself (as its called when an onAcquire fails)
    onAcquire();
}

String8 PlaneComposition::dump(const char* pIdentifier) const
{
    return mDisplayOutput.dump(pIdentifier);
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
