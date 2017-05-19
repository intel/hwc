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
#include "Layer.h"
#include "Utils.h"
#include "Log.h"
#include "Timeline.h"
#include "ufo/graphics.h"
#include "AbstractBufferManager.h"

#include <ui/Rect.h>
#include <ui/GraphicBufferMapper.h>

namespace intel {
namespace ufo {
namespace hwc {

Layer::FramerateTracker::FramerateTracker() :
    mLastTimestamp(0),
    mPeriod(0),
    mFps(0)
{
    reset(systemTime(CLOCK_MONOTONIC));
}

void Layer::FramerateTracker::reset(nsecs_t start, uint32_t defaultFps)
{
    ALOG_ASSERT(defaultFps != 0);

    mPeriod = 1000000000 / defaultFps;
    mLastTimestamp = start;
    mFps = defaultFps;
}

// This attempts to track the frame rate. It uses a decaying average algorithm, where
// a frame cFramesToTrack ago has approximately 35% of the influence that the last
// frame had on the average rate. However, its also looking for discontinuities in the
// frame rate where the rate suddenly changes.
void  Layer::FramerateTracker::update(nsecs_t now, bool bHandleChanged)
{
    uint32_t multiplier = cFramesToTrack;
    nsecs_t lastFramePeriod = (now - mLastTimestamp);

    // Just in case update gets called with the same time twice.
    if (lastFramePeriod == 0)
        return;

    if (bHandleChanged)
    {
        // Frame change. Decay the contribution from the previous frames by one frame
        multiplier--;

        // Look for a rapid change in period. We want to react quickly to user events and any obvious
        // sudden change in performance. We could choose any arbitrary number here greater than two.
        // However, 4x means that we will only react to a sudden change when the framerate goes
        // from 60Hz to sub 15Hz or from sub 15 Hz to 60Hz
        if (((lastFramePeriod * 4) < mPeriod) || (lastFramePeriod > mPeriod * 4))
        {
            mPeriod = lastFramePeriod;
        }
    }

    // Recalculate the period
    mPeriod = ((mPeriod * multiplier) + lastFramePeriod) >> cFramesToTrackShift;
    mLastTimestamp = now;
    mFps = (1000000000 + mPeriod/2) / mPeriod;
}

String8 Layer::FramerateTracker::dump() const
{
    if (!sbInternalBuild)
        return String8();

    return String8::format("Layer::FramerateTracker mPeriod:%" PRIu64 " mult:%d, shift:%d mLastTimestamp:%" PRIu64 , mPeriod, cFramesToTrack, cFramesToTrackShift, mLastTimestamp);
}

Layer::Layer()
    // Constructor state should happen in the clear function
{
    clear();
    onUpdateFlags(); // Just to initialise the flags.
}


Layer::Layer(hwc_layer_1_t& hwc_layer)
    // Constructor state should happen in the clear function
{
    clear();
    onUpdateAll(hwc_layer, 0);
}

Layer::Layer(buffer_handle_t handle)
{
    clear();
    onUpdateAll(handle);
}

void Layer::clear()
{
    mSourceAcquireFence.clear();
    mSourceReleaseFence.clear();
    mpComposition = NULL;
    mHandle = 0;
    mHints = 0;
    mFlags = 0;
    mBlending = EBlendMode::NONE;
    mTransform = ETransform::NONE;
    mPlaneAlpha = 0;
    mDataSpace = DataSpace_Unknown;

    mSrc.left = 0;
    mSrc.right = 0;
    mSrc.top = 0;
    mSrc.bottom = 0;
    mDst.left = 0;
    mDst.right = 0;
    mDst.top = 0;
    mDst.bottom = 0;

    mBufferDetails.clear( );
}

void Layer::onUpdatePending(nsecs_t now)
{
    mFrameRate.update(now, true);
    mHandle = NULL;

    mBufferDetails.clear( );
}

void Layer::onUpdateFrameState(buffer_handle_t handle, nsecs_t now)
{
    bool bHandleChanged = handle != mHandle;
    mFrameRate.update(now, bHandleChanged);
    mHandle = handle;

    // The API allows us to assume an unchanged handle is unchanged buffer state
    if (bHandleChanged)
    {
        onUpdateBufferState();
    }
}

void Layer::onUpdateFrameState(hwc_layer_1& layer, nsecs_t now)
{
    onUpdateFrameState(layer.handle, now);

    setAcquireFenceReturn( &layer.acquireFenceFd );
    setReleaseFenceReturn( &layer.releaseFenceFd );
}

void Layer::onUpdateFrameState(const Layer& layer)
{
    mFrameRate              = layer.mFrameRate;
    mHandle                 = layer.mHandle;

    mBufferDetails          = layer.mBufferDetails;

    mbVideo                 = layer.mbVideo;
    mbAlpha                 = layer.mbAlpha;
    mbScale                 = layer.mbScale;
    mWidthScaleFactor       = layer.mWidthScaleFactor;
    mHeightScaleFactor      = layer.mHeightScaleFactor;
    mbOversized             = layer.mbOversized;
    mbBlend                 = layer.mbBlend;
    mbSrcOffset             = layer.mbSrcOffset;
    mbSrcCropped            = layer.mbSrcCropped;
    mPlaneAlpha             = layer.mPlaneAlpha;
    mDataSpace              = layer.mDataSpace;
    mbFrontBufferRendered   = layer.mbFrontBufferRendered;

    mSourceAcquireFence.setLocation( layer.getAcquireFenceReturn() );
    mSourceReleaseFence.setLocation( layer.getReleaseFenceReturn() );
    mpComposition           = layer.mpComposition;
}

void Layer::onUpdateFences(const Layer& layer)
{
    mSourceAcquireFence.setLocation( layer.getAcquireFenceReturn() );
    mSourceReleaseFence.setLocation( layer.getReleaseFenceReturn() );
}

void Layer::onUpdateFlags()
{
    mbOversized = (getBufferWidth() != getBufferAllocWidth()) || (getBufferHeight() != getBufferAllocHeight());
    mbScale =
        (isTranspose(mTransform))
          ? (getDstWidth() != getSrcHeight()) || (getDstHeight() != getSrcWidth())
          : (getDstWidth() != getSrcWidth()) || (getDstHeight() != getSrcHeight());
    if( mbScale && getDstWidth() && getDstHeight() && (getSrcWidth() > 0.0) && (getDstHeight() > 0.0) )
    {
          mWidthScaleFactor  = isTranspose(mTransform) ? getDstHeight() / getSrcWidth() : getDstWidth() / getSrcWidth();
          mHeightScaleFactor = isTranspose(mTransform) ? getDstWidth() / getSrcHeight() : getDstHeight() / getSrcHeight();
    }
    else
    {
          mWidthScaleFactor = 1.0;
          mHeightScaleFactor = 1.0;
    }
    mbSrcOffset = ( mSrc.left != 0 ) || ( mSrc.top != 0 );
    mbSrcCropped = ( mSrc.right < getBufferWidth()) || ( mSrc.bottom < getBufferHeight());
    mbVideo = intel::ufo::hwc::isVideo(getBufferFormat());
    mbAlpha = intel::ufo::hwc::isAlpha(getBufferFormat());
    mbBlend = ((mBlending != EBlendMode::NONE) && mbAlpha) || isPlaneAlpha();
    mbFrontBufferRendered = ( getBufferModeFlags() & FRONT_BUFFER_RENDER );

    if (mbVideo)
    {
        mDataSpace = DataSpace_BT601_625;
        mDataSpace.range = getBufferDetails().getColorRange();
    }
    else
    {
        mDataSpace = DataSpace_SRGB_Linear;
    }

}

static ETransform convertHwc1Transform(uint32_t transform)
{
    // Older hwcomposer_defs.h files no not always have this defined.
    const uint32_t HWC_TRANSFORM_NONE = 0;
    const uint32_t HWC_TRANSFORM_FLIP_H_ROT_90 = HAL_TRANSFORM_FLIP_H | HAL_TRANSFORM_ROT_90;
    const uint32_t HWC_TRANSFORM_FLIP_V_ROT_90 = HAL_TRANSFORM_FLIP_V | HAL_TRANSFORM_ROT_90;

    ETransform t = ETransform::NONE;
    switch (transform)
    {
        case HWC_TRANSFORM_NONE          : t = ETransform::NONE     ; break;
        case HWC_TRANSFORM_FLIP_H        : t = ETransform::FLIP_H   ; break;
        case HWC_TRANSFORM_FLIP_V        : t = ETransform::FLIP_V   ; break;
        case HWC_TRANSFORM_ROT_90        : t = ETransform::ROT_90   ; break;
        case HWC_TRANSFORM_ROT_180       : t = ETransform::ROT_180  ; break;
        case HWC_TRANSFORM_ROT_270       : t = ETransform::ROT_270  ; break;
        case HWC_TRANSFORM_FLIP_H_ROT_90 : t = ETransform::FLIP_H_90; break;
        case HWC_TRANSFORM_FLIP_V_ROT_90 : t = ETransform::FLIP_V_90; break;
    }
    return t;
}

static EBlendMode convertHwc1Blending(uint32_t blend)
{
    EBlendMode b = EBlendMode::NONE;
    switch (blend)
    {
        case HWC_BLENDING_NONE       : b = EBlendMode::NONE;           break;
        case HWC_BLENDING_PREMULT    : b = EBlendMode::PREMULT;        break;
        case HWC_BLENDING_COVERAGE   : b = EBlendMode::COVERAGE;       break;
    }
    return b;
}

// This is called as a result of a geometry change normally. We may have a full set of layer state to update.
void Layer::onUpdateAll(hwc_layer_1& layer, nsecs_t now, bool bForceOpaque)
{
    // A lot of geometry changes involve adding a new layer at the front of the stack or moving a layer
    // in the stack. We can trap a lot of these. However, initially for simplicity, just look to
    // see if the source layer is identical to the previous layer - it catches a number of these situtations
    // Doesnt matter too much if we get this wrong, the tracking can adjust later. However, it may cause us to
    // enter a higher power usage state until it corrects.
    if (isEqual(layer))
    {
        mFrameRate.update(now, true);
    }
    else
    {
        mFrameRate.reset(now);
    }

    mHints               = layer.hints          ;
    mFlags               = layer.flags          ;
    mHandle              = layer.handle         ;
    mTransform           = convertHwc1Transform(layer.transform);
    mDst                 = layer.displayFrame   ;
    setAcquireFenceReturn( &layer.acquireFenceFd );
    setReleaseFenceReturn( &layer.releaseFenceFd );
    mpComposition        = NULL;

    mBlending            = bForceOpaque ? EBlendMode::NONE : convertHwc1Blending(layer.blending);

    setSrc(layer.sourceCropf);

    mPlaneAlpha = static_cast<float>(layer.planeAlpha)/255;

    mDataSpace = DataSpace_Unknown; // Default assumption

    mVisibleRegions.clear();
    if (layer.visibleRegionScreen.numRects > 0)
    {
        mVisibleRegions.insertArrayAt(layer.visibleRegionScreen.rects, 0, layer.visibleRegionScreen.numRects);
    }
    else
    {
        mVisibleRegions.resize(1);
        mVisibleRegions.editItemAt(0) = mDst;
    }

    onUpdateBufferState();
    onUpdateFlags();
}

void Layer::onUpdateAll(buffer_handle_t handle, bool bForceOpaque)
{
    mHandle     = handle;
    mBlending   = bForceOpaque ? EBlendMode::NONE : EBlendMode::PREMULT;

    onUpdateBufferState(); // Requires mHandle and mBlending. Has to be before the getBufferXXX functions below.

    mHints      = 0;
    mFlags      = 0;
    mTransform  = ETransform::NONE;
    mDst.left   = 0;
    mDst.right  = getBufferWidth();
    mDst.top    = 0;
    mDst.bottom = getBufferHeight();
    mSrc.left   = 0;
    mSrc.right  = getBufferWidth();
    mSrc.top    = 0;
    mSrc.bottom = getBufferHeight();

    mSourceAcquireFence.clear();
    mSourceReleaseFence.clear();
    mpComposition = NULL;
    mPlaneAlpha = 1.0f;
    mDataSpace = DataSpace_Unknown;

    mFrameRate.reset(0);

    mVisibleRegions.resize(1);
    mVisibleRegions.editItemAt(0) = mDst;

    onUpdateFlags();
}

bool Layer::doWaitAcquireFence(nsecs_t timeoutNs) const
{
    ATRACE_NAME_IF( BUFFER_WAIT_TRACE, "Layer::waitAcquireFence" );
    uint32_t timeoutMs = ns2ms(timeoutNs);
    if ( timeoutMs == 0 )
    {
        ALOGD_IF( CONTENT_DEBUG, "Layer %s: Checking fence %s",
            dump().string(), mSourceAcquireFence.dump().string() );
        return mSourceAcquireFence.checkAndClose( );
    }
    else
    {
        ALOGD_IF( CONTENT_DEBUG, "Layer %s: Waiting for fence %s timeout %u",
            dump().string(), mSourceAcquireFence.dump().string(), timeoutMs );
        return mSourceAcquireFence.waitAndClose( timeoutMs );
    }
}

void Layer::onUpdateBufferState()
{
    AbstractBufferManager::get().getLayerBufferDetails( this, &mBufferDetails );
}

void Layer::setBufferPavpSession(uint32_t session, uint32_t instance, uint32_t isEncrypted)
{
    if (mHandle != NULL)
    {
        AbstractBufferManager::get().setPavpSession( mHandle, session, instance, isEncrypted );
    }
    mBufferDetails.setEncrypted( isEncrypted );
    mBufferDetails.setPavpSessionID( session );
    mBufferDetails.setPavpInstanceID( instance );
}

bool Layer::waitRendering(nsecs_t timeoutNs) const
{
    int acquireFence = getAcquireFence();
    if (acquireFence >= 0)
    {
        return doWaitAcquireFence( timeoutNs );
    }
    else
    {
        return AbstractBufferManager::get().wait( mHandle, timeoutNs );
    }
}

const Layer& Layer::Empty()
{
    static Layer sEmpty;
    return sEmpty;
}

bool Layer::isEqual(const hwc_layer_1& layer) const
{
    // Note, cant use a memcmp here as internal pointers and flags may change.
    if (mHandle == layer.handle &&
        mDst.left == layer.displayFrame.left && mDst.right == layer.displayFrame.right &&
        mDst.top == layer.displayFrame.top && mDst.bottom == layer.displayFrame.bottom &&
        mSrc.left == layer.sourceCropf.left && mSrc.right == layer.sourceCropf.right &&
        mSrc.top == layer.sourceCropf.top && mSrc.bottom == layer.sourceCropf.bottom &&
        (mFlags & HWC_SKIP_LAYER) == (layer.flags & HWC_SKIP_LAYER) &&
        mPlaneAlpha == layer.planeAlpha &&
        mTransform == convertHwc1Transform(layer.transform) &&
        mBlending == convertHwc1Blending(layer.blending) &&
        mDataSpace == DataSpace_Unknown)
    {
        return true;
    }
    return false;
}

bool Layer::matches( const Layer& other, bool* pbMatchesHandle ) const
{
    // All the rendering state needs to be checked
    if ( getTransform()         != other.getTransform()   ||
         getBlending()          != other.getBlending()    ||
         getPlaneAlpha()        != other.getPlaneAlpha()  ||
         isEncrypted()          != other.isEncrypted()    ||
         getSrc()               != other.getSrc()         ||
         getDst()               != other.getDst()         ||
         getBufferCompression() != other.getBufferCompression() )
    {
        ALOGD_IF( CONTENT_DEBUG, "Mismatched Transform %d=%d Blending %d=%d planeAlpha %1.2f=%1.2f Encrypted %x=%x Src(%4.1f,%4.1f,%4.1f,%4.1f)=(%4.1f,%4.1f,%4.1f,%4.1f) Dst(%d,%d,%d,%d)=(%d,%d,%d,%d) Compression %d=%d",
            getTransform(),  other.getTransform(),
            getBlending(),   other.getBlending(),
            getPlaneAlpha(), other.getPlaneAlpha(),
            isEncrypted(),   other.isEncrypted(),
            getSrc().left, getSrc().top, getSrc().right, getSrc().bottom,
            other.getSrc().left, other.getSrc().top, other.getSrc().right, other.getSrc().bottom,
            getDst().left, getDst().top, getDst().right, getDst().bottom,
            other.getDst().left, other.getDst().top, other.getDst().right, other.getDst().bottom,
            (int)getBufferCompression(), (int)other.getBufferCompression());
        return false;
    }
    if ( pbMatchesHandle )
    {
        *pbMatchesHandle = ( getHandle() == other.getHandle() );
    }
    return true;
}

void Layer::snapshotOf( const Layer& other )
{
    // Copy all state.
    *this = other;
    // Remove composition reference.
    mpComposition = NULL;
    // Copy buffer details (will dereference composition buffer details if applicable).
    mBufferDetails = other.getBufferDetails();
}

bool Layer::isFullScreenVideo(uint32_t outWidth, uint32_t outHeight) const
{
    if (!isVideo())
    {
        return false;
    }

    bool bFullScreenVideo = false;

    // Check for any full screen video.
    const uint32_t dstWidth = getDstWidth();
    const uint32_t dstHeight = getDstHeight();

    // 1. Width of target display frame == width of target device, with 1 pixel of tolerance.
    if ( abs( int32_t( dstWidth - outWidth ) ) <= 1 )
    {
        ALOGD_IF( FILTER_DEBUG,
            "isLayerFullScreenVideo: Layer %s : Full screen video due to rule 1 %u v %u",
                dump().string(), dstWidth, outWidth );
        bFullScreenVideo = true;
    }
    // 2. OR - Height of target display frame == height of target device, with 1 pixel of tolerance.
    else if ( abs( int32_t( dstHeight - outHeight ) ) <= 1 )
    {
        ALOGD_IF( FILTER_DEBUG,
            "isLayerFullScreenVideo: Layer %s : Full screen video due to rule 2 %u v %u",
                dump().string(), dstHeight, outHeight );
        bFullScreenVideo = true;
    }
    // 3. OR - width * height of display frame > 90% of width * height of display device.
    // Ingnore the case when display dimensions is not set!
    else if ( outWidth && outHeight
              && ( ((( dstWidth * dstHeight ) * 100 ) / ( outWidth * outHeight )) > 90 ) )
    {
        ALOGD_IF( FILTER_DEBUG,
            "isLayerFullScreenVideo: Layer %s : Full screen video due to rule 3 df(%u x %u) target(%u x %u) [==%u%%]",
                dump().string(), dstWidth, dstHeight, outWidth, outHeight,
                ((( dstWidth * dstHeight ) * 100 ) / ( outWidth * outHeight )) );
        bFullScreenVideo = true;
    }

    return bFullScreenVideo;
}

String8 Layer::dump(const char* pPrefix) const
{
    if (!sbLogViewerBuild)
        return String8();

    String8 output;

    if (pPrefix)
        output = String8::format("%s", pPrefix);

    output.appendFormat("%14p:", getHandle());
    if (isBufferDeviceIdValid())
        output.appendFormat("%2" PRIu64 "", getBufferDeviceId());
    else
        output.appendFormat("--");
    output.appendFormat(":%d", mTransform);

    output.appendFormat(" %2d %s",
        getFps(),
        mBlending == EBlendMode::NONE ? "OP" :
        mBlending == EBlendMode::PREMULT ? "BL" :
        mBlending == EBlendMode::COVERAGE ? "CV" : "??");

    output.appendFormat(":%1.2f", mPlaneAlpha);

    String8 format = String8::format("%s:%s",
                        getHALFormatShortString(mBufferDetails.getFormat()),
                        getTilingFormatString(mBufferDetails.getTilingFormat()));
    output.appendFormat(" %-7.7s ", format.string());

    output.appendFormat("%4dx%-4d ",
        mBufferDetails.getWidth(), mBufferDetails.getHeight());

    output.appendFormat("%6.1f,%6.1f,%6.1f,%6.1f %4d,%4d,%4d,%4d %-3d %-3d V:",
        mSrc.left, mSrc.top, mSrc.right, mSrc.bottom,
        mDst.left, mDst.top, mDst.right, mDst.bottom,
        getAcquireFence(), getReleaseFence());

    for (const hwc_rect_t& rect : mVisibleRegions)
    {
        output.appendFormat("%4d,%4d,%4d,%4d ",
            rect.left, rect.top, rect.right, rect.bottom);
    }

    output += getDataSpaceString(mDataSpace).string();

    output.appendFormat(" U:%08x", mBufferDetails.getUsage());

    output.appendFormat(" Hi:%x%s%s Fl:%x%s",
        mHints,
        mHints & HWC_HINT_TRIPLE_BUFFER ? ":TRIPLE" : "",
        mHints & HWC_HINT_CLEAR_FB ? ":CLR" : "",
        mFlags,
        mFlags & HWC_SKIP_LAYER ? ":SKIP" : "");
#if defined(HWC_DEVICE_API_VERSION_1_4)
    output.appendFormat("%s", mFlags & HWC_IS_CURSOR_LAYER ? ":CURSOR" : "");
#endif

    if (isAlpha())                  output.appendFormat(" A");
    if (isOpaque())                 output.appendFormat(" OP");
    if (isBlend())                  output.appendFormat(" BL");
    if (isVideo())                  output.appendFormat(" V");
    if (isPlaneAlpha())             output.appendFormat(" PA");
    if (isDisabled())               output.appendFormat(" DISABLE");
    if (isEncrypted())              output.appendFormat(" ENCRYPT(S:%u, I:%u)", getBufferPavpSessionID(), getBufferPavpInstanceID());
    if (isComposition())            output.appendFormat(" CO");
    if (isScale())                  output.appendFormat(" S");
    if (isOversized())              output.appendFormat(" OS(%dx%d)", mBufferDetails.getAllocWidth(), mBufferDetails.getAllocHeight());
    if (isSrcOffset())              output.appendFormat(" SO");
    if (isSrcCropped())             output.appendFormat(" SC");
    if (isFrontBufferRendered())    output.appendFormat(" FBR");
    if (getBufferCompression() != COMPRESSION_NONE)
    {
        output.appendFormat(" RC(%s)", AbstractBufferManager::get().getCompressionName(getBufferCompression()));
    }

    if (mpComposition)
        output.appendFormat(" %s", mpComposition->getName());

    if(getMediaTimestamp())
        output.appendFormat(" vTS:%" PRIu64, getMediaTimestamp());

    if(getMediaFps())
        output.appendFormat(" vFps:%u", getMediaFps());

    return output;
}

bool Layer::dumpContentToTGA(const String8& name) const
{
    if (!sbInternalBuild || mHandle == NULL)
        return false;

    GraphicBufferMapper& mapper = GraphicBufferMapper::get();

    uint8_t* pBufferPixels = 0;
    const uint32_t width = getBufferWidth();
    const uint32_t height = getBufferHeight();
    const uint32_t pitch = getBufferPitch();

    status_t r = mapper.lock(getHandle(), GRALLOC_USAGE_SW_READ_OFTEN, Rect(0, 0, width, height), (void **)&pBufferPixels);
    if (r != OK || !pBufferPixels)
    {
        ALOGE("dumpContentToTGA: Failed to lock surface");
        return false;
    }

    bool bRet = true;
    String8 filename = String8::format( "/data/hwc/%s.tga", name.string() );
    ALOGD( "Dumping %s to %s", dump().string(), filename.string() );

    FILE* fp = fopen( filename,"wb" );

    if ( fp )
    {
#pragma pack(1)
        struct
        {
            uint8_t  mIDLength;
            uint8_t  mColorMapType;
            uint8_t  mImageType;
            int16_t  mColorMapOrigin;
            int16_t  mColorMapLength;
            uint8_t  mColorMapDepth;
            int16_t  mOriginX;
            int16_t  mOriginY;
            uint16_t mWidth;
            uint16_t mHeight;
            uint8_t  mBPP;
            uint8_t  mImageDesc;
        } TGAHeader;
#pragma pack()

        const uint32_t szTGAHeader = 18;
        ALOG_ASSERT( sizeof( TGAHeader ) == szTGAHeader );
        memset( &TGAHeader, 0, szTGAHeader );
        TGAHeader.mImageType = 2;       // Uncompressed BGR
        TGAHeader.mWidth     = width;   // Width in pixels
        TGAHeader.mHeight    = height;  // Height in lines
        TGAHeader.mBPP       = 32;      // BGRA
        TGAHeader.mImageDesc = 32;      // Top-left origin

        fwrite( &TGAHeader, 1, szTGAHeader, fp );

        uint32_t argb;

#define MAKE_ARGB( A, R, G, B ) (((uint32_t)(A) << 24) | ((uint32_t)(R) << 16) | ((uint32_t)(G) << 8) | ((uint32_t)(B)))
#define CLAMP( X ) (uint32_t)((X)<0?0:(X)>255?255:(X))
#define MAKE_ARGB_FROM_YCrCb( Y, Cb, Cr )                                                        \
    ((0xFF000000                                                                       ) |   \
     (CLAMP( ( 298 * ((Y)-16)                    + 409 * ((Cr)-128) +128 ) >> 8 ) << 16) |   \
     (CLAMP( ( 298 * ((Y)-16) - 100 * ((Cb)-128) - 208 * ((Cr)-128) +128 ) >> 8 ) << 8 ) |   \
     (CLAMP( ( 298 * ((Y)-16) + 516 * ((Cb)-128)                    +128 ) >> 8 )      ))

        switch ( getBufferFormat() )
        {
            // 32Bit Red first, Alpha/X last.
            case HAL_PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_RGBX_8888:
            {
                const uint32_t nextLine = pitch - 4*width;
                for ( uint32_t y = 0; y < height; ++y )
                {
                    for ( uint32_t x = 0; x < width; ++x )
                    {
                        argb = MAKE_ARGB( pBufferPixels[3],    //A|X
                                          pBufferPixels[0],    //R
                                          pBufferPixels[1],    //G
                                          pBufferPixels[2] );  //B
                        fwrite( &argb, 1, 4, fp );
                        pBufferPixels += 4;
                    }
                    pBufferPixels += nextLine;
                }
            }
            break;

            // 32Bit Blue first, Alpha last.
            case HAL_PIXEL_FORMAT_BGRA_8888:
            {
                const uint32_t nextLine = pitch - 4*width;
                for ( uint32_t y = 0; y < height; ++y )
                {
                    for ( uint32_t x = 0; x < width; ++x )
                    {
                        argb = MAKE_ARGB( pBufferPixels[3],    //A|X
                                          pBufferPixels[2],    //R
                                          pBufferPixels[1],    //G
                                          pBufferPixels[0] );  //B
                        fwrite( &argb, 1, 4, fp );
                        pBufferPixels += 4;
                    }
                    pBufferPixels += nextLine;
                }
            }
            break;

            // 24Bit Blue last.
            case HAL_PIXEL_FORMAT_RGB_888:
            {
                const uint32_t nextLine = pitch - 3*width;
                for ( uint32_t y = 0; y < height; ++y )
                {
                    for ( uint32_t x = 0; x < width; ++x )
                    {
                        argb = MAKE_ARGB( 0xFF,
                                          pBufferPixels[0],    //R
                                          pBufferPixels[1],    //G
                                          pBufferPixels[2] );  //B
                        fwrite( &argb, 1, 4, fp );
                        pBufferPixels += 3;
                    }
                    pBufferPixels += nextLine;
                }
            }
            break;

            // 16Bit RRRRR GGGGGG BBBBB.
            case HAL_PIXEL_FORMAT_RGB_565:
            {
                const uint32_t nextLine = pitch - 2*width;
                for ( uint32_t y = 0; y < height; ++y )
                {
                    for ( uint32_t x = 0; x < width; ++x )
                    {
                        uint16_t px = *(uint16_t*)pBufferPixels;
                        argb = MAKE_ARGB( 0xFF,
                                          (px>>8)&0xF8,        //R
                                          (px>>3)&0xFC,        //G
                                          (px<<3)&0xF8 );      //B
                        fwrite( &argb, 1, 4, fp );
                        pBufferPixels += 2;
                    }
                    pBufferPixels += nextLine;
                }
            }
            break;

            // 16Bit Planar YUV formats (420).
            case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
            {
                uint8_t* pY = pBufferPixels;
                uint8_t* pUV = pBufferPixels + width * height;
                const uint32_t nextLine = pitch - width;
                for ( uint32_t y = 0; y < height; ++y )
                {
                    for ( uint32_t x = 0; x < width; ++x )
                    {
                        argb = MAKE_ARGB_FROM_YCrCb( pY[0],         // Y
                                                     pUV[0],        // U(Cb)
                                                     pUV[1] );      // V(Cr)
                        fwrite( &argb, 1, 4, fp );
                        pY += 1;
                        // Repeat UV pair x2.
                        if ( x&1 )
                        {
                            pUV += 2;
                        }
                    }
                    pY += nextLine;
                    if ( y&1 )
                    {
                        pUV += nextLine;
                    }
                    else
                    {
                        // Repeat UV row x2.
                        pUV -= width;
                    }
                }
            }
            break;

            // 16Bit Packed YUV formats (422).
            case HAL_PIXEL_FORMAT_YCbCr_422_I:
            {
                uint8_t Y0, Y1, U, V;
                Y0 = 0; U = 1; Y1 = 2; V = 3;

                const uint32_t nextLine = pitch - 2*width;

                for ( uint32_t y = 0; y < height; ++y )
                {
                    for ( uint32_t x = 0; x < width/2; ++x )
                    {
                        argb = MAKE_ARGB_FROM_YCrCb( pBufferPixels[Y0],  // Y
                                                     pBufferPixels[U],   // U(Cb)
                                                     pBufferPixels[V] ); // V(Cr)
                        fwrite( &argb, 1, 4, fp );
                        argb = MAKE_ARGB_FROM_YCrCb( pBufferPixels[Y1],  // Y
                                                     pBufferPixels[U],   // U(Cb)
                                                     pBufferPixels[V] ); // V(Cr)
                        fwrite( &argb, 1, 4, fp );
                        pBufferPixels += 4;
                    }
                    pBufferPixels += nextLine;
                }
            }
            break;

            default:
            break;
        }

        if ( ftell( fp ) <= (long)szTGAHeader )
        {
            ALOGE( "Failed to dump %s", dump().string() );
            bRet = false;
        }

        fclose( fp );
    }
    else
    {
        ALOGE( "Failed to open output file %s", filename.string() );
        bRet = false;
    }

    mapper.unlock(getHandle());
    return bRet;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
