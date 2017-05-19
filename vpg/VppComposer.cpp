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

#include "Hwc.h"
#include "VppComposer.h"
#include "iVP.h"
#include "Log.h"
#include "Utils.h"
#include "VpgBufferManager.h"
#include <ufo/graphics.h>

// 15.33 iVP doesnt support the colourspace, but there is no identifying define
// to indicate what the iVP does support. However, the older iVP has this as a
// define, newer ones have it as an enum. Hence, this is a crude mechanism to
// ensure we do not compile in the colorspace support on older iVPs.
#define INTEL_HWC_IVP_SUPPORTS_COLORSPACE !defined(IVP_DEFAULT_CAPABLILITY)

namespace intel {
namespace ufo {
namespace hwc {

VppComposer::VppComposer()
{
    //width and height could be any value, it is just required by VAAPI
    iVP_status status = iVP_create_context(&mCtxID, IVP_DEFAULT_WIDTH, IVP_DEFAULT_HEIGHT, IVP_DEFAULT_CAPABLILITY);
    if (status != IVP_STATUS_SUCCESS)
    {
        mIsContextValid = false;
        ALOGE("Unable to create the iVP context (status %d)", (uint32_t)status);
    }
    else
    {
        mIsContextValid = true;
    }
}

VppComposer::~VppComposer()
{
    if (mIsContextValid)
    {
        iVP_status status = iVP_destroy_context(&mCtxID);
        if (status != IVP_STATUS_SUCCESS)
        {
            ALOGE("Unable to destroy the iVP context (status %d)", status);
        }
    }
}

static bool isOutputFormatSupported(int32_t format)
{
    switch(format)
    {
        case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_A2R10G10B10_INTEL:
        case HAL_PIXEL_FORMAT_A2B10G10R10_INTEL:
            return true;
        default:
            return false;
    }
}

// Return true if appropriate android format is supported by the renderer
static bool isInputFormatSupported(int32_t format)
{
    // Ideally this should become a part of iVP interface.
    // For the moment this function return true to all formats known
    // to vpapi_allocate_surface() - iVP_api.cpp

    switch(format)
    {
        case HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL:
        case HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL:
        case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
        case HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL:
        case HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL:
        case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL:
        case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_YCbCr_422_H_INTEL:
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_GENERIC_8BIT_INTEL:
        case HAL_PIXEL_FORMAT_YCbCr_420_H_INTEL:
        case HAL_PIXEL_FORMAT_YCbCr_411_INTEL:
        case HAL_PIXEL_FORMAT_YCbCr_422_V_INTEL:
        case HAL_PIXEL_FORMAT_YCbCr_444_INTEL:
        case HAL_PIXEL_FORMAT_P010_INTEL:
            return true;
        case HAL_PIXEL_FORMAT_YCrCb_422_H_INTEL:
        default:
            return false;
    }
}

const char* VppComposer::getName() const
{
    return "VppComposer";
}

/** \brief Translate from the Android transform to its libva equivalent

Android does a flip followed by a clockwise rotation, while libva/iVP does
a clockwise rotation and a flip (the other way around).
*/

static void getTransform(ETransform transform, iVP_rotation_t* pRotation, iVP_flip_t* pFlip)
{
    switch (transform)
    {
    case ETransform::NONE:
        // There is no transform
        *pRotation = IVP_ROTATE_NONE;
        *pFlip = IVP_FLIP_NONE;
        break;

    case ETransform::FLIP_H:
        // There is only a horizontal flip
        *pRotation = IVP_ROTATE_NONE;
        *pFlip = IVP_FLIP_H;
        break;

    case ETransform::FLIP_V:
        // There is only a vertical flip
        *pRotation = IVP_ROTATE_NONE;
        *pFlip = IVP_FLIP_V;
        break;

    case ETransform::ROT_180:
        // There is only a 180 degrees rotation
        *pRotation = IVP_ROTATE_180;
        *pFlip = IVP_FLIP_NONE;
        break;

    case ETransform::ROT_90:
        // There is only a 90 degrees clockwise rotation:
        // it will be done as a 90 degress clockwise
        *pRotation = IVP_ROTATE_90;
        *pFlip = IVP_FLIP_NONE;
        break;

    case ETransform::FLIP_H_90:
        // There is a horizontal flip followed by 90 degrees clockwise rotation:
        // it will be done with a 90 degrees clockwise followed by a vertical flip
        *pRotation = IVP_ROTATE_90;
        *pFlip = IVP_FLIP_V;
        break;

    case ETransform::FLIP_V_90:
        // There is a vertical flip followed by a 90 degrees clockwise rotation:
        // it will be done with a 90 degrees clockwise rotation followed by a horizontal flip
        *pRotation = IVP_ROTATE_90;
        *pFlip = IVP_FLIP_H;
        break;

    case ETransform::ROT_270:
        // There is a only a 270 degrees clockwise rotation:
        // it will be done with a 270 clockwise
        *pRotation = IVP_ROTATE_270;
        *pFlip = IVP_FLIP_NONE;
        break;
    }
}

static iVP_blend_t getBlending(EBlendMode blending)
{
    iVP_blend_t result;

    if (blending == EBlendMode::NONE)
    {
        result = IVP_BLEND_NONE;
    }
    else if (blending == EBlendMode::PREMULT)
    {
        result = IVP_ALPHA_SOURCE_PREMULTIPLIED;
    }
    else
    {
        result = IVP_BLEND_NONE;
        ALOGE("Unsupported blending mode");
    }

    return result;
}

float VppComposer::onEvaluate(const Content::LayerStack& source, const Layer& target, AbstractComposer::CompositionState** ppState, Cost type)
{
    HWC_UNUSED(ppState);
    // Check that the Vpp composer supports the output type
    if (!isOutputFormatSupported(target.getBufferFormat()))
    {
        ALOGD_IF(COMPOSITION_DEBUG, "VppComposer: Unsupported output format: %s", target.dump().string());
        return Eval_Not_Supported;
    }

    ECompressionType compression = target.getBufferCompression();
    if ( (compression != COMPRESSION_NONE)
        && (compression != ECompressionType::MMC) )
    {
        ALOGD_IF(COMPOSITION_DEBUG, "VppComposer: Unsupported output compression %s", target.dump().string());
        return Eval_Not_Supported;
    }

    // Check that the Vpp composer supports all the input layer types
    for (uint32_t ly = 0; ly < source.size(); ly++)
    {
        const Layer& layer = source[ly];
        if (!isInputFormatSupported(layer.getBufferFormat()))
        {
            return Eval_Not_Supported;
        }

        ECompressionType compression = layer.getBufferCompression();
        if ( (compression != COMPRESSION_NONE)
            && (compression != ECompressionType::MMC) )
        {
            ALOGD_IF(COMPOSITION_DEBUG, "VppComposer: Unsupported input compression %d: %s", ly, layer.dump().string());
            return Eval_Not_Supported;
        }
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
            bandwidth = (bandwidth * 3) / 2;    // Empirical measurements of Vpp Composition show that actual bandwidth usage is much higher than the theoretical.
            cost = bandwidth * target.getFps(); // Times the frames per second
        }
        break;
    case Memory:
        // This costs us a preallocated double buffered render target buffer.
        cost = target.getDstWidth() * target.getDstHeight() * 2;
        break;
    }

    // We artificially cut the cost of single plane video so that VPP is chosen.
    // hwcflatland figures show that iVP is more performant when handling video
    // CSC than any OGL shaders. Hence, we want to make this the default composer
    // in these situations
    // TODO: Implement full power based analysis of shader costs.
    if ((source.size() == 1) && isVideo(source[0].getBufferFormat()))
    {
        cost /= 4;
    }
    // VPP has higher scaling quality for single layer.
    // For the single scaled layer, it should be better to choose VPP.
    // Probably need to adjust evaluation depending on scale factor.
    else if ( (source.size() == 1) &&
              type == Quality &&
              (source[0].getWidthScaleFactor() < 0.5) && (source[0].getHeightScaleFactor() < 0.5 ))
    {
        cost /= 4;
    }

    // TODO: Very simple guestimate for now based on expected bandwidth usage
    ALOGD_IF(COMPOSITION_DEBUG, "VppComposer: Evaluation cost(%d) = %f", type, cost);
    return cost;
}

iVP_color_range_t dataSpaceToVpRange(EDataSpaceRange range)
{
    switch (range)
    {
        case EDataSpaceRange::Unspecified   : return IVP_COLOR_RANGE_NONE;
        case EDataSpaceRange::Full          : return IVP_COLOR_RANGE_FULL;
        case EDataSpaceRange::Limited       : return IVP_COLOR_RANGE_PARTIAL;
    }
    return IVP_COLOR_RANGE_NONE;
}

#if INTEL_HWC_IVP_SUPPORTS_COLORSPACE
iVP_color_standard_t dataSpaceToVpStandard(EDataSpaceStandard standard)
{
    switch (standard)
    {
        case EDataSpaceStandard::Unspecified              : return IVP_COLOR_STANDARD_NONE;
        case EDataSpaceStandard::BT709                    : return IVP_COLOR_STANDARD_BT709;
        case EDataSpaceStandard::BT601_625                : return IVP_COLOR_STANDARD_BT601;
        case EDataSpaceStandard::BT601_625_UNADJUSTED     : return IVP_COLOR_STANDARD_BT601;
        case EDataSpaceStandard::BT601_525                : return IVP_COLOR_STANDARD_BT709;
        case EDataSpaceStandard::BT601_525_UNADJUSTED     : return IVP_COLOR_STANDARD_BT601;
        case EDataSpaceStandard::BT2020                   : return IVP_COLOR_STANDARD_BT2020;
        case EDataSpaceStandard::BT2020_CONSTANT_LUMINANCE: return IVP_COLOR_STANDARD_BT2020;
        case EDataSpaceStandard::BT470M                   : return IVP_COLOR_STANDARD_NONE;
        case EDataSpaceStandard::FILM                     : return IVP_COLOR_STANDARD_NONE;
    }
    return IVP_COLOR_STANDARD_NONE;
}
#endif


void VppComposer::onCompose(const Content::LayerStack& source, const Layer& target, AbstractComposer::CompositionState* pState)
{
    ATRACE_NAME_IF(RENDER_TRACE, "VppComposer");
    HWC_UNUSED(pState);

    Log::add(source, target, "VppComposer ");

    if (!mIsContextValid)
        return;

    AbstractBufferManager& bufferManager = AbstractBufferManager::get();

    // Alloc rectangles and layers for the operations
    iVP_rect_t  srect [source.size()];
    iVP_rect_t  drect [source.size()];
    iVP_layer_t layer [source.size()];

    target.waitAcquireFence();
    for (uint32_t ly = 0; ly < source.size(); ly++)
    {
        const Layer& srcLayer = source[ly];
        // Wait for any acquire fence
        srcLayer.waitAcquireFence();

        // We know that the vp renderer is synchronous, indicate that here.
        srcLayer.returnReleaseFence(-1);

        bufferManager.setBufferUsage( srcLayer.getHandle(), (AbstractBufferManager::BufferUsage)VpgBufferManager::eBufferUsage_VPP );

        srect[ly].left   = srcLayer.getSrc().left;
        srect[ly].top    = srcLayer.getSrc().top;
        srect[ly].width  = ceil( srcLayer.getSrcWidth() );
        srect[ly].height = ceil( srcLayer.getSrcHeight() );

        drect[ly].left   = srcLayer.getDst().left;
        drect[ly].top    = srcLayer.getDst().top;
        drect[ly].width  = srcLayer.getDstWidth();
        drect[ly].height = srcLayer.getDstHeight();;

        // Zero-fill to get the default value for all the non-used or new fields
        memset(&layer[ly], 0, sizeof(layer[0]));

        layer[ly].srcRect        = &srect[ly];
        layer[ly].destRect       = &drect[ly];
        layer[ly].bufferType     = IVP_GRALLOC_HANDLE;
        layer[ly].gralloc_handle = srcLayer.getHandle();

        // Scaling filter
        layer[ly].filter = srcLayer.isVideo() ? IVP_FILTER_HQ : IVP_FILTER_FAST;

        // Set rotation and flip
        iVP_rotation_t rotation;
        iVP_flip_t flip;
        getTransform(srcLayer.getTransform(), &rotation, &flip);
        layer[ly].rotation = rotation;
        layer[ly].flip = flip;
        layer[ly].blend = getBlending(srcLayer.getBlending());

        layer[ly].colorRange = dataSpaceToVpRange(srcLayer.getDataSpace().range);
#if INTEL_HWC_IVP_SUPPORTS_COLORSPACE
        layer[ly].colorStandard = dataSpaceToVpStandard(srcLayer.getDataSpace().standard);
#endif
    }

    iVP_layer_t outputLayer;

    // Zero-fill to get the default value for all the non-used or new fields
    memset(&outputLayer, 0, sizeof(outputLayer));

    // The  output layer consists of the gralloc handle
    outputLayer.srcRect = NULL;
    outputLayer.destRect = NULL;
    outputLayer.bufferType = IVP_GRALLOC_HANDLE;
    outputLayer.gralloc_handle = target.getHandle();

    outputLayer.colorRange = dataSpaceToVpRange(target.getDataSpace().range);
#if INTEL_HWC_IVP_SUPPORTS_COLORSPACE
    outputLayer.colorStandard = dataSpaceToVpStandard(target.getDataSpace().standard);
#endif

    if (target.getBufferFormat() == HAL_PIXEL_FORMAT_YCbCr_422_I)
    {
        // YUY2 destinations are going to the sprite planes. Currently, these need to be full range.
        // TODO: We should ensure that the render target is correctly specified rather than hardcoding it here.
        outputLayer.colorRange = IVP_COLOR_RANGE_FULL;
    }

    for (uint32_t ly = 0; ly < source.size(); ly++)
    {
        ALOGD_IF(COMPOSITION_DEBUG, "VPP Src: %s", source[ly].dump().string());
    }
    ALOGD_IF(COMPOSITION_DEBUG, "VPP Dst: %s", target.dump().string());

    iVP_exec(&mCtxID, NULL, layer, source.size(), &outputLayer, false /* wait for rendering */);

    // We know that the vp renderer is synchronous, indicate that here.
    target.returnAcquireFence(-1);

    return;
}

AbstractComposer::ResourceHandle VppComposer::onAcquire(const Content::LayerStack& source, const Layer& target)
{
    HWC_UNUSED(source);
    HWC_UNUSED(target);
    return this;
}

void VppComposer::onRelease(ResourceHandle hResource)
{
    HWC_UNUSED(hResource);
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
