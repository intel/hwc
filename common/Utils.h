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

#ifndef INTEL_UFO_HWC_UTILS_H
#define INTEL_UFO_HWC_UTILS_H
#include <ufo/graphics.h>
#include <math.h>
#include "Format.h"

namespace intel {
namespace ufo {
namespace hwc {

// Return true if buffer format can be used for direct driving the encoder (WiDi)
inline bool isEncoderReadyVideo(int format)
{
    return format == HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL
        || format == HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL;
}

inline bool isVideo(int format)
{
    return format == HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL
        || format == HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL
        || format == HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL
        || format == HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL
        || format == HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL
        || format == HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL
        || format == HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL
        || format == HAL_PIXEL_FORMAT_P010_INTEL
        || format == HAL_PIXEL_FORMAT_YCbCr_422_I
        || format == HAL_PIXEL_FORMAT_YV12;
}

inline bool isNV12(int format)
{
    return format == HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL
        || format == HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL
        || format == HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL
        || format == HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL
        || format == HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL
        || format == HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL
        || format == HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL;
}

inline bool isYUV422(int format)
{
    return format == HAL_PIXEL_FORMAT_YCbCr_422_I;
}

inline bool isYUV420Planar(int format)
{
    // Our YUV420planar formats are (currently) all NV12.
    return isNV12( format );
}

inline bool mustBeYTiled(int format)
{
    return ( format == HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL )
        || ( format == HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL );
}

inline bool mustBeXTiled(int format)
{
    return ( format == HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL );
}

inline bool mustBeLinear(int format)
{
    return ( format == HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL )
        || ( format == HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL )
        || ( format == HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL )
        || ( format == HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL );
}

inline ETilingFormat formatToTiling( int format )
{
    if ( mustBeYTiled( format ) )
    {
        return TILE_Y;
    }
    if ( mustBeXTiled( format ) )
    {
        return TILE_X;
    }
    if ( mustBeLinear( format ) )
    {
        return TILE_LINEAR;
    }
    return TILE_UNKNOWN;
}

inline bool isYTile( ETilingFormat eTileFormat )
{
    return ( eTileFormat == TILE_Y )
        || ( eTileFormat == TILE_Yf )
        || ( eTileFormat == TILE_Ys );
}

inline bool isPacked(int format)
{
    return format == HAL_PIXEL_FORMAT_RGBA_8888
        || format == HAL_PIXEL_FORMAT_RGBX_8888
        || format == HAL_PIXEL_FORMAT_RGB_888
        || format == HAL_PIXEL_FORMAT_RGB_565
        || format == HAL_PIXEL_FORMAT_BGRA_8888
        || format == HAL_PIXEL_FORMAT_YCbCr_422_I
        || format == HAL_PIXEL_FORMAT_A2R10G10B10_INTEL
        || format == HAL_PIXEL_FORMAT_A2B10G10R10_INTEL
        || format == HAL_PIXEL_FORMAT_P010_INTEL;
}

inline bool isAlpha(int format)
{
    return format == HAL_PIXEL_FORMAT_RGBA_8888
        || format == HAL_PIXEL_FORMAT_BGRA_8888
        || format == HAL_PIXEL_FORMAT_A2R10G10B10_INTEL
        || format == HAL_PIXEL_FORMAT_A2B10G10R10_INTEL;
}

inline int equivalentFormatWithAlpha(int format)
{
    switch ( format )
    {
    case HAL_PIXEL_FORMAT_RGBX_8888:
        return HAL_PIXEL_FORMAT_RGBA_8888;
    default:
        break;
    }
    return format;
}

inline int bitsPerPixelForFormat(int format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_A2R10G10B10_INTEL:
    case HAL_PIXEL_FORMAT_A2B10G10R10_INTEL:
    case HAL_PIXEL_FORMAT_P010_INTEL:
        return 32;
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_YCbCr_444_INTEL:
        return 24;
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_YCrCb_422_H_INTEL: /* YV16 */
    case HAL_PIXEL_FORMAT_YCbCr_422_H_INTEL: /* YU16 */
    case HAL_PIXEL_FORMAT_YCbCr_422_V_INTEL:
    case HAL_PIXEL_FORMAT_YCbCr_422_I:  /* deprecated */
    case HAL_PIXEL_FORMAT_YCbCr_422_SP: /* deprecated */
    case HAL_PIXEL_FORMAT_Y16:
        return 16;
    case HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL:
    case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
    case HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL:
    case HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL:
    case HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL:
    case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL: /* deprecated */
    case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL: /* deprecated */
    case HAL_PIXEL_FORMAT_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_411_INTEL:
    case HAL_PIXEL_FORMAT_YCbCr_420_H_INTEL:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP: /* deprecated */
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
        return 12;
    case HAL_PIXEL_FORMAT_GENERIC_8BIT_INTEL:
    case HAL_PIXEL_FORMAT_Y8:
        return 8;

    default:
        ALOGW("format %d unknown, assuming 32bpp", format);
        return 32;
    }
}

inline float calculateBandwidthInKilobytes(uint32_t width, uint32_t height, int32_t format)
{
    return (float(width) * height * bitsPerPixelForFormat(format)) / 1024;
}

inline int floatToFixed16(float v)
{
    return int(v * 65536.0f);
}


inline float fixed16ToFloat(int v)
{
    return float(v) / 65536.0f;
}

inline bool isInteger(float f)
{
    return (fabs(f - round(f)) < 0.000001f);
}

// swap two int32 values
inline void swap_int32(int32_t &a, int32_t &b)
{
    int32_t tmp = a;
    a = b;
    b = tmp;
}

// swap two uint32 values
inline void swap_uint32(uint32_t &a, uint32_t &b)
{
    uint32_t tmp = a;
    a = b;
    b = tmp;
}

// Percentage difference.
inline float pctDiff( const float a, const float b )
{
    const float diff = a-b;
    const float avg  = 0.5f*(a+b);
    if ( avg == 0.0f )
        return 0.0f;
    return 100.0f * fabs( diff/avg );
}

// Safe bitmask function for 32 bitMask.
// Return the bit[idx] set to 1.
// Return 0 if idx is out of range.
inline uint32_t bitMask32(uint32_t idx)
{
    if(idx < 32)
        return ((uint32_t)1)<<idx;
    else
        return 0;
}

inline hwc_rect_t floatToIntRect (const hwc_frect_t& fr)
{
    hwc_rect_t ir;
    ir.left = fr.left;
    ir.right = fr.right;
    ir.top = fr.top;
    ir.bottom = fr.bottom;
    return ir;
}

inline hwc_frect_t intToFloatRect (const hwc_rect_t& fr)
{
    hwc_frect_t ir;
    ir.left = fr.left;
    ir.right = fr.right;
    ir.top = fr.top;
    ir.bottom = fr.bottom;
    return ir;
}

inline bool computeOverlap (const hwc_rect_t &rect1, const hwc_rect_t &rect2, hwc_rect_t *newRect)
{
    newRect->left = max(rect1.left, rect2.left);
    newRect->right = min(rect1.right, rect2.right);
    newRect->top = max(rect1.top, rect2.top);
    newRect->bottom = min(rect1.bottom, rect2.bottom);
    if (newRect->left >= newRect->right || newRect->top >= newRect->bottom)
        return false;
    return true;
}

inline void combineRect(hwc_frect_t& src, const hwc_frect_t& dst)
{
    src.left = src.left < dst.left ? src.left : dst.left;
    src.top = src.top < dst.top ? src.top : dst.top;
    src.right = src.right > dst.right ? src.right : dst.right;
    src.bottom = src.bottom > dst.bottom ? src.bottom : dst.bottom;
}

inline void computeRelativeRect( const hwc_frect_t& inCoordSpace,
                                     const hwc_frect_t& outCoordSpace,
                                     const hwc_frect_t& rect,
                                     hwc_frect_t& dstRect)
{
    float x_ratio = (outCoordSpace.right - outCoordSpace.left) / (inCoordSpace.right - inCoordSpace.left);
    float y_ratio = (outCoordSpace.bottom - outCoordSpace.top) / (inCoordSpace.bottom - inCoordSpace.top);

    dstRect.left = outCoordSpace.left + (rect.left - inCoordSpace.left) * x_ratio;
    dstRect.right = outCoordSpace.left + (rect.right - inCoordSpace.left) * x_ratio;
    dstRect.top = outCoordSpace.top + (rect.top - inCoordSpace.top) * y_ratio;
    dstRect.bottom = outCoordSpace.top + (rect.bottom - inCoordSpace.top) * y_ratio;
}

} // namespace hwc
} // namespace ufo
} // namespace intel

#endif // INTEL_UFO_HWC_UTILS_H
