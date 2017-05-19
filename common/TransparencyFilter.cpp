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
#include "TransparencyFilter.h"
#include "FilterManager.h"
#include "CompositionManager.h"
#include "Layer.h"
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <utils/Thread.h>
#include "AbstractBufferManager.h"
#include <Utils.h>

namespace intel {
namespace ufo {
namespace hwc {

#define TRANSPARENCY_FILTER_DEBUG 0
#define FRAMES_BEFORE_CHECK_BASE  30
#define FRAMES_BEFORE_CHECK_DELTA 10

// Factory class will self register
TransparencyFilter gTransparencyFilter;

class TransparencyFilter::DetectionThread : public Thread
{
public:
    DetectionThread(sp<GraphicBuffer> pLinearBuffer, hwc_frect_t activeRect);
    virtual ~DetectionThread();

    const char* getName() const                     { return "TransparencyFilter"; }
    const Content& onApply(const Content& ref);

    const Layer& getLayer()                         { return mDetectionLayer; }
    Layer& editLayer()                              { return mDetectionLayer; }
    bool isFinished()                               { return mbFinished; }
    bool isDetected(hwc_rect* pBlackMask);

private:
    void detect(uint32_t* pBuffer);

    // Detection will be performed in this thread
    virtual bool threadLoop();

private:
    // Private reference to hold modified state
    sp<GraphicBuffer>   mpLinearBuffer;     // Linear copy of the buffer for rapid processing
    hwc_frect_t         mActiveRect;
    Layer               mDetectionLayer;    // Layer currently being detected
    hwc_rect_t          mBlackMask;
    bool                mbFinished;
    bool                mbResult;
};

TransparencyFilter::DetectionThread::DetectionThread(sp<GraphicBuffer> pLinearBuffer, hwc_frect_t activeRect) :
    mpLinearBuffer(pLinearBuffer),
    mActiveRect(activeRect),
    mDetectionLayer(pLinearBuffer->handle),
    mBlackMask{},
    mbFinished(false),
    mbResult(false)
{
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter::TransparencyFilter::DetectionThread");
}

TransparencyFilter::DetectionThread::~DetectionThread()
{
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter::TransparencyFilter::~DetectionThread");
}

bool TransparencyFilter::DetectionThread::threadLoop()
{
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: threadLoop");
    mDetectionLayer.waitRendering(ms2ns( 1000 ));

    // Look for a some kind of transparent window possibly with a black outline
    // Abort the entire check if we find any non black, non transparent pixel

    GraphicBufferMapper& mapper = GraphicBufferMapper::get();
    void* pvBuffer = NULL;
    status_t r = mapper.lock(mDetectionLayer.getHandle(), GRALLOC_USAGE_SW_READ_OFTEN, Rect(0, 0, mDetectionLayer.getBufferWidth(), mDetectionLayer.getBufferHeight()), &pvBuffer);
    if (r != OK)
    {
        ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: Failed to lock surface");
        return false;
    }

    detect((uint32_t*)pvBuffer);

    ALOGD_IF (TRANSPARENCY_FILTER_DEBUG, "Detect result: %d", mbResult);

#ifdef DUMP_UNTRANSPARENT_LAYER
    if (!mbResult)
    {
        static int count = 0;
        mDetectionLayer.dumpContentToTGA(String8::format("NotTransparent%d", count));
        count++;
    }
#endif

    mapper.unlock(mDetectionLayer.getHandle());
    mpLinearBuffer = NULL;
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: threadLoop Finished");
    mbFinished = true;
    return false;
}

bool TransparencyFilter::DetectionThread::isDetected(hwc_rect* pBlackMask)
{
    if (mbResult)
        *pBlackMask = mBlackMask;

    return mbResult;
}

static inline bool checkRegionForColor(uint32_t color, uint32_t* pBuffer, uint32_t strideInPixels, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
    ATRACE_CALL_IF(DISPLAY_TRACE);
    pBuffer += y1 * strideInPixels;
    for (uint32_t y = y1; y < y2; y++)
    {
        for (uint32_t x = x1; x < x2; x++)
        {
            if (pBuffer[x] != color)
            {
                ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: checkRegionForColor %d, %d, %d, %d Failed at %d, %d", x1, y1, x2, y2, x, y);
                return false;
            }
        }
        pBuffer += strideInPixels;
    }
    return true;
}

static inline bool checkRegionForColor(uint32_t color1, uint32_t color2, uint32_t* pBuffer, uint32_t strideInPixels, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
    ATRACE_CALL_IF(DISPLAY_TRACE);
    pBuffer += y1 * strideInPixels;
    for (uint32_t y = y1; y < y2; y++)
    {
        for (uint32_t x = x1; x < x2; x++)
        {
            if (pBuffer[x] != color1 && pBuffer[x] != color2)
            {
                ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: checkRegionForColor %d, %d, %d, %d Failed at %d, %d", x1, y1, x2, y2, x, y);
                return false;
            }
        }
        pBuffer += strideInPixels;
    }
    return true;
}


static hwc_frect_t rotateRect (const hwc_frect_t& rect, ETransform transform)
{
    hwc_frect_t rotatedRect = rect;
    switch (transform)
    {
        case ETransform::NONE :
            break;
        case ETransform::ROT_270 :
            rotatedRect.left = -1 * rect.bottom;
            rotatedRect.top = rect.left;
            rotatedRect.right =  -1 * rect.top;
            rotatedRect.bottom = rect.right;
            break;
        case ETransform::ROT_180 :
            rotatedRect.left = -1 * rect.right;
            rotatedRect.top = -1 * rect.bottom;
            rotatedRect.right = -1 * rect.left;
            rotatedRect.bottom = -1 * rect.top;
            break;
        case ETransform::ROT_90 :
            rotatedRect.left = rect.top;
            rotatedRect.top = -1 * rect.right;
            rotatedRect.right = rect.bottom;
            rotatedRect.bottom = -1 * rect.left;
            break;
        default:
            break;
    }
    return rotatedRect;
}

void TransparencyFilter::DetectionThread::detect(uint32_t* pBuffer)
{
    ATRACE_CALL_IF(DISPLAY_TRACE);

    const static int BLACK = 0xFF000000;
    const static int TRANSPARENT = 0x00000000;

    uint32_t w = mDetectionLayer.getBufferWidth();
    uint32_t h = mDetectionLayer.getBufferHeight();
    uint32_t s = mDetectionLayer.getBufferPitch() / 4;
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: detect %dx%d %d", w, h, s);

    // Now we only detect the layers which intersacted with video
    hwc_rect_t overlappedRect;
    if (!computeOverlap (floatToIntRect(mActiveRect), mDetectionLayer.getDst(), &overlappedRect))
    {
        ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Not intersacted with video layer, skip it");
        return;
    }
    mActiveRect = intToFloatRect(overlappedRect);

    // Compute the transparent area based on video rect
    hwc_frect_t inCordSpace = rotateRect(intToFloatRect(mDetectionLayer.getDst()), mDetectionLayer.getTransform());
    hwc_frect_t outCordSpace = mDetectionLayer.getSrc();
    hwc_frect_t activeSrcRect = rotateRect(mActiveRect, mDetectionLayer.getTransform());
    hwc_frect_t activeDstRect;
    computeRelativeRect ( inCordSpace, outCordSpace, activeSrcRect, activeDstRect );

    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "UI DST: %d %d %d %d, InCordSpace: %f %f %f %f, OutCordSpace: %f %f %f %f",
                                        mDetectionLayer.getDst().left, mDetectionLayer.getDst().top, mDetectionLayer.getDst().right, mDetectionLayer.getDst().bottom,
                                        inCordSpace.left, inCordSpace.top, inCordSpace.right, inCordSpace.bottom,
                                        outCordSpace.left, outCordSpace.top, outCordSpace.right, outCordSpace.bottom);
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Video SRC: %f %f %f %f, Video SRC rotate: %f %f %f %f, Video DST: %f, %f, %f, %f, Transform: %d",
                                        mActiveRect.left, mActiveRect.top, mActiveRect.right, mActiveRect.bottom,
                                        activeSrcRect.left, activeSrcRect.top, activeSrcRect.right, activeSrcRect.bottom,
                                        activeDstRect.left, activeDstRect.top, activeDstRect.right, activeDstRect.bottom,
                                        mDetectionLayer.getTransform());
    uint32_t bl = activeDstRect.left;
    uint32_t bt = activeDstRect.top;
    uint32_t br = activeDstRect.right;
    uint32_t bb = activeDstRect.bottom;
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Blackmask %d %d %d %d", bl, bt, br, bb);

    // Perform a complete check of a the whole layer.
    // For the case that the layer is full transparent, we need check black and transparent simultaneously for non-video region
    if (!checkRegionForColor(BLACK, TRANSPARENT, pBuffer, s, 0,  bb, w,  h )) return;   // Bottom
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Bottom check pass, %d %d %d %d", 0, bb, w, h);
    if (!checkRegionForColor(BLACK, TRANSPARENT, pBuffer, s, 0,  0,  w,  bt)) return;   // Top
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Top check pass, %d %d %d %d", 0, 0, w, bt);
    if (!checkRegionForColor(BLACK, TRANSPARENT, pBuffer, s, 0,  bt, bl, bb)) return;   // Left
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Left check pass, %d %d %d %d", 0, bt, bl, bb);
    if (!checkRegionForColor(BLACK, TRANSPARENT, pBuffer, s, br, bt, w,  bb)) return;   // Right
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Right check pass, %d %d %d %d", br, bt, w, bb);
    if (!checkRegionForColor(TRANSPARENT, pBuffer, s, bl, bt, br, bb)) return;   // Middle
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Middle check pass, %d %d %d %d", bl, bt, br, bb);

    // Setup the detected blackmask region.
    mBlackMask.left = bl;
    mBlackMask.top = bt;
    mBlackMask.right = br;
    mBlackMask.bottom = bb;

    mbResult = true;
}

TransparencyFilter::DetectionItem::DetectionItem() :
    mBM( AbstractBufferManager::get() ),
    mCurrentHandle(0),
    mRepeatCount(0),
    mbEnabled(0),
    mpLinearBuffer(NULL),
    mFramesBeforeCheck(0),
    mpDetectionThread(NULL),
    mbFirstEnabledFrame(0),
    mbFirstDisabledFrame(0)
{
    memset(&mBlackMask, 0, sizeof(mBlackMask));
}

TransparencyFilter::DetectionItem::~DetectionItem()
{
}

void TransparencyFilter::DetectionItem::reset()
{
    mRepeatCount = 0;
    mCurrentHandle = 0;
}

void TransparencyFilter::DetectionItem::updateRepeatCounts(const Layer& ly)
{
    if (ly.getBufferFormat() == HAL_PIXEL_FORMAT_RGBA_8888 &&
        ly.getHandle() && !ly.isComposition())
    {
        // Only need to change the behaviour when the handles change. Technically, we should
        // invalidate on any geometry change, however this can cause a lot of costly extra
        // gpu composition and checking, so we only reset if we havnt yet checked the buffer contents
        // TODO: Hook into gralloc delete callback to invalidate instead.
        if (ly.getHandle() != mCurrentHandle)
        {
            mCurrentHandle = ly.getHandle();
            mRepeatCount = 0;
        }
        else
        {
            mRepeatCount++;
        }
    }
    else
    {
        mRepeatCount = 0;
        mCurrentHandle = 0;
    }
}

void TransparencyFilter::DetectionItem::initiateDetection(const Layer& layer, hwc_frect_t activeRect)
{
    ATRACE_CALL_IF(DISPLAY_TRACE);
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: initiateDetection");

    // Double check to ensure that detection isnt already running.
    if (mpDetectionThread != NULL)
    {
        ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: Already running");
        return;
    }

    // Check if we need re-allocate a graphic buffer
    if (mpLinearBuffer == NULL || mpLinearBuffer->getWidth() != layer.getBufferWidth() ||
        (mpLinearBuffer->getWidth() == layer.getBufferWidth() && layer.getBufferHeight() > mpLinearBuffer->getHeight()))
    {
        mpLinearBuffer = mBM.createGraphicBuffer( "TRFILTER",
                                                  layer.getBufferWidth(), layer.getBufferHeight(),
                                                  HAL_PIXEL_FORMAT_RGBA_8888,
                                                  GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER );

        ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Re-allocate linear buffer, origin size: %d %d, requested size: %d %d, handle: %p",
                 mpLinearBuffer->getWidth(), mpLinearBuffer->getHeight(), layer.getBufferWidth(), layer.getBufferHeight(), mpLinearBuffer->handle);
    }

    if (mpLinearBuffer == NULL)
    {
        ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: Failed to allocate linear buffer");
        return;
    }

    mpDetectionThread = new DetectionThread(mpLinearBuffer, activeRect);

    // we only need copy the whole bufer but don't want to use other original info like src rect, dst rect, rotation flag....
    Layer clonedLayer[1];
    clonedLayer[0].onUpdateAll(layer.getHandle());
    CompositionManager::getInstance().performComposition(Content::LayerStack(clonedLayer, 1), mpDetectionThread->getLayer());

    // Set needed info to detection layer
    mpDetectionThread->editLayer().setSrc (layer.getSrc());
    mpDetectionThread->editLayer().setDst (layer.getDst());
    mpDetectionThread->editLayer().setTransform (layer.getTransform());

    // Start the low priority detection thread
    mpDetectionThread->run("Detect_thread", PRIORITY_BACKGROUND);
}

void TransparencyFilter::DetectionItem::filterLayers(Content& ref)
{
    for (uint32_t d = 0; d < ref.size(); d++)
    {
        const Content::Display& display = ref.getDisplay(d);
        const Content::LayerStack& layers = display.getLayerStack();
        uint32_t numLayers = layers.size();

        if (numLayers >= 2)
        {
            for (uint32_t i = 1; i < numLayers; i++)
            {
                if (layers[i].getHandle() == mCurrentHandle)
                {
                    Content::LayerStack& layers = ref.editDisplay(d).editLayerStack();

                     // Remove the transparent layer
                    layers.removeLayer(i);

                    // Update our layer flags as this layer may change our flags.
                    layers.updateLayerFlags();
                    --numLayers;
               }
            }
        }
    }
}

void TransparencyFilter::DetectionItem::garbageCollect(void)
{
    if (mpLinearBuffer != NULL)
    {
        Log::alogd(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter : Garbage collect linear buffer %p", mpLinearBuffer->handle );
        mpLinearBuffer = NULL;
    }
}

String8 TransparencyFilter::DetectionItem::dump()
{
    if (!sbInternalBuild)
        return String8();

    return String8::format("DetectionItem %s (%d,%d,%d,%d)", mbEnabled ? "true" : "false",
                            mBlackMask.left, mBlackMask.top, mBlackMask.right, mBlackMask.bottom);
}

TransparencyFilter::TransparencyFilter() :
    mDetectionNum(0)
{
    // Add this filter to the front of the filter list
    FilterManager::getInstance().add(*this, FilterPosition::Transparency);

    // Number of frames to pass without the buffer handle changing before we trigger the check
    for (uint32_t i = 0; i < MAX_DETECT_LAYERS; i++)
    {
        mDetection[i].mFramesBeforeCheck = FRAMES_BEFORE_CHECK_BASE + i * FRAMES_BEFORE_CHECK_DELTA;
    }
}

TransparencyFilter::~TransparencyFilter()
{
    // remove this filter
    FilterManager::getInstance().remove(*this);
}

void TransparencyFilter::skipFilter(void)
{
    // Garbage collect if we have been running analysis.
    if (mDetectionNum > 0)
    {
        for (uint32_t i = 0; i < MAX_DETECT_LAYERS; i++)
        {
            mDetection[i].garbageCollect();
        }
    }
    // Set number currently detected to zero so that we reset all counters when analysis restarts.
    mDetectionNum = 0;
}

const Content& TransparencyFilter::onApply(const Content& ref)
{
    const Content::Display& display = ref.getDisplay(0);
    const Content::LayerStack& layers = display.getLayerStack();

    if (ref.size() < 1 || layers.size() < 2)
    {
        skipFilter();
        return ref;
    }

    // Check if have video
    hwc_frect_t activeRect;
    uint32_t videoIndex;
    bool bHaveVideo = false;
    for (uint32_t i = 0; i < layers.size(); i++)
    {
        if (layers[i].isVideo())
        {
            activeRect = intToFloatRect(layers[i].getDst());
            bHaveVideo = true;
            videoIndex = i;
            break;
        }
    }
    if (!bHaveVideo)
    {
        skipFilter();
        return ref;
    }

    // There are two cases that we need modify the ref
    // 1. there is one layer transtion from "enabled" to "disabled"
    // 2. there is one layer "enabled"
    // Note: "enabled" means we can remove this layer
    bool bNeedChangeRef = false;

    uint32_t detectionNum = min(MAX_DETECT_LAYERS, (int)(layers.size()));
    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG && mDetectionNum != detectionNum, "TransparencyFilter: need detect %d layers", detectionNum);

    // Reset counters on any new layers since the last frame
    if (detectionNum > mDetectionNum)
    {
        for (uint32_t i = mDetectionNum; i < detectionNum; i++)
        {
            mDetection[i].reset();
        }
    }
    mDetectionNum = detectionNum;

    for (uint32_t i = 0; i < mDetectionNum; i++)
    {
        DetectionItem *curD = &mDetection[i];

        // Firstly, look for an unchanging RGBA layer in front of a video layer
        curD->updateRepeatCounts(layers[i]);

        // If we were enabled previously, and now we have a new frame,
        if (curD->mbEnabled && curD->mRepeatCount < curD->mFramesBeforeCheck)
        {
            ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "TransparencyFilter: %dth layer, Last frame, Disable %d %d",
                     i, curD->mbEnabled, curD->mRepeatCount);
            // Just need to provoke one final geometry change and disable for the next frame.
            curD->mbFirstDisabledFrame = true;
            bNeedChangeRef = true;
            curD->mbEnabled = false;
            curD->mpDetectionThread = NULL;
        }

        const Content::LayerStack& layers = ref.getDisplay(0).getLayerStack();
        // If the repeat count matches, then we need to trigger a check for enable
        if (curD->mpDetectionThread == NULL && curD->mRepeatCount == curD->mFramesBeforeCheck)
        {
            // For some cases, there might have layers beneath video layer
            // If these layers are not transparent, we should combine their dst rect with video's
            // and then use the new rect for subsequential detections.
            for (uint32_t j = 0; j < videoIndex; j++)
            {
                if (!mDetection[j].mbEnabled)
                {
                    combineRect(activeRect, intToFloatRect(layers[j].getDst()));
                    ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Combine with layer %d, Adjusted video rect to %f %f %f %f",
                             j, activeRect.left, activeRect.top, activeRect.right, activeRect.bottom);
                }
            }

            ALOGD_IF(TRANSPARENCY_FILTER_DEBUG, "Start to detect %dth layer", i);
            curD->initiateDetection(layers[i], activeRect);
        }

        if (!curD->mbEnabled && curD->mpDetectionThread != NULL && curD->mpDetectionThread->isFinished())
        {
            if (curD->mpDetectionThread->isDetected(&curD->mBlackMask) && curD->mRepeatCount >= curD->mFramesBeforeCheck)
            {
                curD->mbEnabled = true;
                curD->mbFirstEnabledFrame = true;
            }
            curD->mpDetectionThread = NULL;
        }

        if (curD->mbEnabled)
        {
            bNeedChangeRef = true;
        }
    }

    // Nothing change needed, just return the original ref
    if (!bNeedChangeRef)
    {
        return ref;
    }

    // Copy the content, the intent now is to change something
    mReference = ref;

    // Start to modify ref
    for (uint32_t i = 0; i < mDetectionNum; i++)
    {
        DetectionItem *curD = &mDetection[i];

        if (curD->mbFirstEnabledFrame || curD->mbFirstDisabledFrame)
        {
            mReference.setGeometryChanged(true);
            curD->mbFirstEnabledFrame = false;
            curD->mbFirstDisabledFrame = false;
        }

        // Actually remove the layers
        if (curD->mbEnabled)
        {
            curD->filterLayers(mReference);
        }
    }

    return mReference;
}

String8 TransparencyFilter::dump()
{
    if (!sbInternalBuild)
        return String8();

    String8 output;

    output += String8::format("Detect %d layers", mDetectionNum);
    for (uint32_t i = 0; i < mDetectionNum; i++)
    {
        output += String8(" ") + mDetection[i].dump();
    }

    return output;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
