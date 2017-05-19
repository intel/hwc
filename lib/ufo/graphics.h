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

/**
 * This <ufo/graphics.h> file contains UFO specific pixel formats.
 *
 * \remark This file is Android specific
 * \remark Do not put any internal definitions here!
 */

#ifndef INTEL_UFO_GRAPHICS_H
#define INTEL_UFO_GRAPHICS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the corresponding pixel format memo */
#define INTEL_UFO_GRAPHICS_FORMATS_VERSION_MAJOR 1
#define INTEL_UFO_GRAPHICS_FORMATS_VERSION_MINOR 5


/**
 * UFO specific pixel format definitions
 *
 * Range 0x100 - 0x1FF is reserved for pixel formats specific to the HAL implementation
 *
 * \see #include <system/graphics.h>
 */
enum {

#if UFO_GRALLOC_ENABLE_NV12_GENERIC_FORMAT
    /**
     * NV12 format:
     *
     * YUV 4:2:0 image with a plane of 8 bit Y samples followed by an interleaved
     * U/V plane containing 8 bit 2x2 subsampled colour difference samples.
     *
     * Microsoft defines this format as follows:
     *
     * A format in which all Y samples are found first in memory as an array
     * of unsigned char with an even number of lines (possibly with a larger
     * stride for memory alignment). This is followed immediately by an array
     * of unsigned char containing interleaved Cb and Cr samples. If these
     * samples are addressed as a little-endian WORD type, Cb would be in the
     * least significant bits and Cr would be in the most significant bits
     * with the same total stride as the Y samples.
     *
     * NV12 is the preferred 4:2:0 pixel format.
     *
     * Layout information:
     * - Y plane with even height and width
     * - U/V are interlaved and 1/2 width and 1/2 height
     *
     *       ____________w___________ .....
     *      |Y0|Y1                   |    :
     *      |                        |    :
     *      h                        h    :
     *      |                        |    :
     *      |                        |    :
     *      |________________________|....:
     *      |U|V|U|V                 |    :
     *     h/2                      h/2   :
     *      |____________w___________|....:
     *
     */
    HAL_PIXEL_FORMAT_NV12_GENERIC_INTEL = 0x1FF,
    HAL_PIXEL_FORMAT_NV12_GENERIC_FOURCC = 0x3231564E,
#endif

    /**
     * Intel NV12 format Y tiled.
     *
     * \see HAL_PIXEL_FORMAT_NV12_GENERIC_FOURCC
     *
     * Additional layout information:
     * - stride: aligned to 128 bytes
     * - height: aligned to 64 lines
     * - tiling: always Y tiled
     *
     *       ____________w___________ ____
     *      |Y0|Y1                   |    |
     *      |                        |    |
     *      h                        h    h'= align(h,64)
     *      |                        |    |
     *      |                        |    |
     *      |____________w___________|    |
     *      :                             |
     *      :________________________ ____|
     *      |U|V|U|V                 |    :
     *     h/2                      h/2   :
     *      |____________w___________|    h"= h'/2
     *      :.............................:
     *
     *      stride = align(w,128)
     *
     */
    HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL = 0x100,

    /**
     * Intel NV12 format linear.
     *
     * \see HAL_PIXEL_FORMAT_NV12_GENERIC_FOURCC
     *
     * Additional layout information:
     * - stride: aligned to 512, 1024, 1280, 2048, 4096
     * - height: aligned to 32 lines
     * - tiling: always linear
     *
     *       ____________w___________ ____
     *      |Y0|Y1                   |    |
     *      |                        |    |
     *      h                        h    h'= align(h,32)
     *      |                        |    |
     *      |                        |    |
     *      |____________w___________|    |
     *      :                             |
     *      :________________________ ____|
     *      |U|V|U|V                 |    :
     *     h/2                      h/2   :
     *      |____________w___________|    h"= h'/2
     *      :.............................:
     *
     *      stride = align(w,512..1024..1280..2048..4096)
     *
     */
    HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL = 0x101,

    /**
     * Layout information:
     * - U/V are 1/2 width and full height
     * - stride: aligned to 128
     * - height: aligned to 64
     * - tiling: always Y tiled
     *
     *       ____________w___________ ____
     *      |Y                       |    |
     *      |                        |    |
     *      h                        h    h' = align(h,64)
     *      |____________w___________|    |
     *      :                             |
     *      :_____________________________|
     *      |V           |                :
     *      |            |                :
     *      h            h                h' = align(h,64)
     *      |_____w/2____|                :
     *      :____________ ................:
     *      |U           |                :
     *      |            |                :
     *      h            h                h' = align(h,64)
     *      |_____w/2____|                :
     *      :.............................:
     *
     *      stride = align(w,128)
     *
     */
    HAL_PIXEL_FORMAT_YCrCb_422_H_INTEL = 0x102, // YV16

    /**
     * Intel NV12 format packed linear.
     *
     * \see HAL_PIXEL_FORMAT_NV12_GENERIC_FOURCC
     *
     * Additional layout information:
     * - stride: same as width
     * - height: no alignment
     * - tiling: always linear
     *
     *       ____________w___________
     *      |Y0|Y1                   |
     *      |                        |
     *      h                        h
     *      |                        |
     *      |                        |
     *      |____________w___________|
     *      |U|V|U|V                 |
     *     h/2                      h/2
     *      |____________w___________|
     *
     */
    HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL = 0x103,

    /**
     * Three planes, 8 bit Y plane followed by 8 bit 2x1 subsampled U and V planes.
     * Similar to IMC3 but U/V are full height.
     * The width must be even.
     * There are no specific restrictions on pitch, height and alignment.
     * It can be linear or tiled if required.
     * Horizontal stride is aligned to 128.
     * Vertical stride is aligned to 64.
     *
     *       ________________________ .....
     *      |Y0|Y1                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :____________ ________________:
     *      |U0|U1       |                :
     *      |            |                :
     *     h|            |                h'= align(h,64)
     *      |_____w/2____|                :
     *      :____________ ................:
     *      |V0|V1       |                :
     *      |            |                :
     *     h|            |                h'= align(h,64)
     *      |______w/2___|                :
     *      :.............................:
     *
     *      stride = align(w,128)
     */
    HAL_PIXEL_FORMAT_YCbCr_422_H_INTEL = 0x104, // YU16

    /**
     * Intel NV12 format X tiled.
     * This is VXD specific format.
     *
     * \see HAL_PIXEL_FORMAT_NV12_GENERIC_FOURCC
     *
     * Additional layout information:
     * - stride: aligned to 512, 1024, 2048, 4096
     * - height: aligned to 32 lines
     * - tiling: always X tiled
     *
     *       ____________w___________ ____
     *      |Y0|Y1                   |    |
     *      |                        |    |
     *      h                        h    h'= align(h,32)
     *      |                        |    |
     *      |                        |    |
     *      |____________w___________|    |
     *      :                             |
     *      :________________________ ____|
     *      |U|V|U|V                 |    :
     *     h/2                      h/2   :
     *      |____________w___________|    h"= h'/2
     *      :.............................:
     *
     *      stride = align(w,512..1024..2048..4096)
     *
     */
    HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL = 0x105,

    /**
     * Legacy RGB formats.
     * These formats were removed from Android framework code,
     * but they are still required by EGL functionality (EGL image).
     * Re-introduced here to support conformance EGL tests.
     */
    HAL_PIXEL_FORMAT_RGBA_5551_INTEL = 0x106,
    HAL_PIXEL_FORMAT_RGBA_4444_INTEL = 0x107,

    /**
     * Only single 8 bit Y plane.
     * Horizontal stride is aligned to 128.
     * Vertical stride is aligned to 64.
     * Tiling is Y-tiled.
     *       ________________________ .....
     *      |Y0|Y1|                  |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :                             :
     *      :............stride...........:
     *
     *      stride = align(w,128)
     */
    HAL_PIXEL_FORMAT_GENERIC_8BIT_INTEL = 0x108,

    /**
     * Three planes, 8 bit Y plane followed by U, V plane with 1/4 width and full height.
     * The U and V planes have the same stride as the Y plane.
     * An width is multiple of 4 pixels.
     * Horizontal stride is aligned to 128.
     * Vertical stride is aligned to 64.
     *       ________________________ .....
     *      |Y0|Y1                   |    :
     *      |                        |    :
     *      h                        |    h'= align(h,64)
     *      |____________w___________|    :
     *      :_______ .....................:
     *      |U|U    |                     :
     *      |       |                     :
     *      h       |                     h'= align(h,64)
     *      |__w/4__|                     :
     *      :_______ .....................:
     *      |V|V    |                     :
     *      |       |                     :
     *      h       |                     h'= align(h,64)
     *      |__w/4__|                     :
     *      :............stride...........:
     *
     *      stride = align(w,128)
     */
    HAL_PIXEL_FORMAT_YCbCr_411_INTEL    = 0x109,

    /**
     * Three planes, 8 bit Y plane followed by U, V plane with 1/2 width and 1/2 height.
     * The U and V planes have the same stride as the Y plane.
     * Width and height must be even.
     * Horizontal stride is aligned to 128.
     * Vertical stride is aligned to 64.
     *       ________________________ .....
     *      |Y0|Y1                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :                             :
     *      :____________ ................:
     *      |U0|U1       |                :
     *   h/2|_______w/2__|                h"= h'/2
     *      :____________ ................:
     *      |V0|V1       |                :
     *   h/2|_______w/2__|                h"= h'/2
     *      :.................stride......:
     *
     *      stride = align(w,128)
     */
    HAL_PIXEL_FORMAT_YCbCr_420_H_INTEL  = 0x10A,

    /**
     * Three planes, 8 bit Y plane followed by U, V plane with full width and 1/2 height.
     * Horizontal stride is aligned to 128.
     * Vertical stride is aligned to 64.
     *       ________________________ .....
     *      |Y0|Y1                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :                             :
     *      :________________________ ....:
     *      |U0|U1                   |    :
     *   h/2|____________w___________|    h"= h'/2
     *      :________________________ ....:
     *      |V0|V1                   |    :
     *   h/2|____________w___________|    h"= h'/2
     *      :.............................:
     *
     *      stride = align(w,128)
     */
    HAL_PIXEL_FORMAT_YCbCr_422_V_INTEL  = 0x10B,

    /**
     * Three planes, 8 bit Y plane followed by U, V plane with full width and full height.
     * Horizontal stride is aligned to 128.
     * Vertical stride is aligned to 64.
     *       ________________________ .....
     *      |Y0|Y1                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :________________________ ....:
     *      |U0|U1                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :________________________ ....:
     *      |V0|V1                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :.............................:
     *
     *      stride = align(w,128)
     */
    HAL_PIXEL_FORMAT_YCbCr_444_INTEL    = 0x10C,

    /**
     * R/G/B components in separate planes
     * - all planes have full width and full height
     * - horizontal stride aligned to 128
     * - vertical stride same as height
     *
     *       ________________________ .....
     *      |R                       |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :________________________ ....:
     *      |G                       |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :________________________ ....:
     *      |B                       |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :.............................:
     *
     *      stride = align(w,128)
     */
    HAL_PIXEL_FORMAT_RGBP_INTEL         = 0x10D,

    /**
     * B/G/R components in separate planes
     * - all planes have full width and full height
     * - horizontal stride aligned to 128
     * - vertical stride same as height
     *
     * \see HAL_PIXEL_FORMAT_RGBP_INTEL
     *
     *       ____________w___________ .....
     *      |B                       |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :________________________ ....:
     *      |G                       |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :________________________ ....:
     *      |R                       |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :.............................:
     *
     *      stride = align(w,128)
     */
    HAL_PIXEL_FORMAT_BGRP_INTEL         = 0x10E,

    /**
     * Intel NV12 format for camera.
     *
     * \see HAL_PIXEL_FORMAT_NV12_GENERIC_FOURCC
     *
     * Additional layout information:
     * - height: must be even
     * - stride: aligned to 64
     * - vstride: same as height
     * - tiling: always linear
     *
     *       ________________________ .....
     *      |Y0|Y1                   |    :
     *      |                        |    :
     *      h                        h    h
     *      |                        |    :
     *      |                        |    :
     *      |____________w___________|....:
     *      |U|V|U|V                 |    :
     *     h/2                      h/2  h/2
     *      |____________w___________|....:
     *
     *      stride = align(w,64)
     *      vstride = h
     *
     */
    HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL = 0x10F,

    /**
     * Intel P010 format.
     *
     * Used for 10bit usage for HEVC/VP9 decoding and video processing.
     *
     * Layout information:
     * - each pixel being represented by 16 bits (2 bytes)
     * - Y plane with even height and width
     * - hstride multiple of 64 pixels (128 bytes)
     * - hstride is specified in pixels, not in bytes
     * - vstride is aligned to 64 lines
     * - U/V are interleaved and 1/2 width and 1/2 height
     * - memory is Y tiled
     *
     *       ____________w___________ ____
     *      |Y0|Y1                   |    |
     *      |                        |    |
     *      h                        h    h'= align(h,64)
     *      |                        |    |
     *      |                        |    |
     *      |____________w___________|    |
     *      :                             |
     *      :________________________ ____|
     *      |U|V|U|V                 |    :
     *     h/2                      h/2   :
     *      |____________w___________|    h"= h'/2
     *      :.............................:
     *
     *      pitch (in bytes) = align(w*2,128)
     *      size (in bytes) = pitch*3/2
     */
    HAL_PIXEL_FORMAT_P010_INTEL = 0x110,

    /**
     * Intel Z16 format.
     *
     * Used by DS4 camera to represent depth.
     *
     * Layout information:
     * - each pixel being represented by 16 bits (2 bytes)
     * - no width/height restrictions
     * - hstride is width
     * - hstride is specified in pixels, not in bytes
     * - vstride is height
     * - memory is linear
     *
     *       ____________w___________
     *      |Z0|Z1                   |
     *      |                        |
     *      h                        h
     *      |                        |
     *      |                        |
     *      |____________w___________|
     *
     *      pitch (in bytes) = w*2
     *      size (in bytes) = w*h*2
     */
    HAL_PIXEL_FORMAT_Z16_INTEL = 0x111,

    /**
     * Intel UVMAP format.
     *
     * Used by DS4 camera to represent depth to color map.
     *
     * Layout information:
     * - each pixel being represented by 64 bits (8 bytes)
     * - no width/height restrictions
     * - hstride is width
     * - hstride is specified in pixels, not in bytes
     * - vstride is height
     * - memory is linear
     *
     *       ____________w___________
     *      |m0|m1                   |
     *      |                        |
     *      h                        h
     *      |                        |
     *      |                        |
     *      |____________w___________|
     *
     *      pitch (in bytes) = w*8
     *      size (in bytes) = w*h*8
     */
    HAL_PIXEL_FORMAT_UVMAP64_INTEL = 0x112,

    /**
     * Intel A2RGB10 format.
     *
     * Used for 10bit video processing.
     *
     * \see GMM_FORMAT_B10G10R10A2_UNORM
     *
     *       ________________________ .....
     *      |BGRA|BGRA|              |    :
     *      |                        |    :
     *      h                        h    h'
     *      |____________w___________|    :
     *      :                             :
     *      :............stride...........:
     *
     *       bits
     *      +----+--------------------+--------------------+--------------------+
     *      |3130|29                20|19                10|09                 0|
     *      +----+--------------------+--------------------+--------------------+
     *      |A1A0|R9R8R7R6R5R4R3R2R1R0|G9G8G7G6G5G4G3G2G1G0|B9B8B7B6B5B4B3B2B1B0|
     *      +----+--------------------+--------------------+--------------------+
     *
     *       byte 0           byte 1           byte 2           byte 3
     *      +----------------+----------------+----------------+----------------+
     *      |B7B6B5B4B3B2B1B0|G5G4G3G2G1G0B9B8|R3R2R1R0G9G8G7G6|A1A0R9R8R7R6R5R4|
     *      +----------------+----------------+----------------+----------------+
     *
     */
    HAL_PIXEL_FORMAT_A2R10G10B10_INTEL = 0x113,

    /**
     * Intel A2BGR10 format.
     *
     * Used for 10bit video processing.
     *
     * \see GMM_FORMAT_R10G10B10A2_UNORM
     *
     *       ________________________ .....
     *      |RGBA|RGBA|              |    :
     *      |                        |    :
     *      h                        h    h'
     *      |____________w___________|    :
     *      :                             :
     *      :............stride...........:
     *
     *       bits
     *      +----+--------------------+--------------------+--------------------+
     *      |3130|29                20|19                10|09                 0|
     *      +----+--------------------+--------------------+--------------------+
     *      |A1A0|B9B8B7B6B5B4B3B2B1B0|G9G8G7G6G5G4G3G2G1G0|R9R8R7R6R5R4R3R2R1R0|
     *      +----+--------------------+--------------------+--------------------+
     *
     *       byte 0           byte 1           byte 2           byte 3
     *      +----------------+----------------+----------------+----------------+
     *      |R7R6R5R4R3R2R1R0|G5G4G3G2G1G0R9R8|B3B2B1B0B9B8B7B6|A1A0B9B8B7B6B5B4|
     *      +----------------+----------------+----------------+----------------+
     *
     */
    HAL_PIXEL_FORMAT_A2B10G10R10_INTEL = 0x114,

    /**
     * Intel YCrCb packed format (YUYV)
     *
     * Horizontal stride is aligned to 32 pixels.
     * Stride is specified in pixels, not in bytes.
     * Vertical stride is aligned to 64.
     *       ________________________ .....
     *      |YUYV|                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :.............................:
     *      stride = align(w,32)
     */
    HAL_PIXEL_FORMAT_YCrCb_NORMAL_INTEL  = 0x115,

    /**
     * Intel YCrCb packed format (VYUY)
     *
     * Horizontal stride is aligned to 32 pixels.
     * Stride is specified in pixels, not in bytes.
     * Vertical stride is aligned to 64.
     *       ________________________ .....
     *      |VYUY|                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :.............................:
     *      stride = align(w,32)
     */
    HAL_PIXEL_FORMAT_YCrCb_SWAPUVY_INTEL  = 0x116,

    /**
     * Intel YCrCb packed format (YVYU)
     *
     * Horizontal stride is aligned to 32 pixels.
     * Stride is specified in pixels, not in bytes.
     * Vertical stride is aligned to 64.
     *       ________________________ .....
     *      |YVYU|                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :.............................:
     *      stride = align(w,32)
     */
    HAL_PIXEL_FORMAT_YCrCb_SWAPUV_INTEL  = 0x117,

    /**
     * Intel YCrCb packed format (UYVY)
     *
     * Horizontal stride is aligned to 32 pixels.
     * Stride is specified in pixels, not in bytes.
     * Vertical stride is aligned to 64.
     *       ________________________ .....
     *      |UYVY|                   |    :
     *      |                        |    :
     *      h                        h    h'= align(h,64)
     *      |____________w___________|    :
     *      :.............................:
     *      stride = align(w,32)
     */
    HAL_PIXEL_FORMAT_YCrCb_SWAPY_INTEL  = 0x118,

    /**
     * Intel X2RGB10 format.
     *
     * Used for 10bit video processing.
     *
     * \see GMM_FORMAT_B10G10R10X2_UNORM
     *
     *       ________________________ .....
     *      |BGRX|BGRX|              |    :
     *      |                        |    :
     *      h                        h    h'
     *      |____________w___________|    :
     *      :                             :
     *      :............stride...........:
     *
     *       bits
     *      +----+--------------------+--------------------+--------------------+
     *      |3130|29                20|19                10|09                 0|
     *      +----+--------------------+--------------------+--------------------+
     *      |X1X0|R9R8R7R6R5R4R3R2R1R0|G9G8G7G6G5G4G3G2G1G0|B9B8B7B6B5B4B3B2B1B0|
     *      +----+--------------------+--------------------+--------------------+
     *
     *       byte 0           byte 1           byte 2           byte 3
     *      +----------------+----------------+----------------+----------------+
     *      |B7B6B5B4B3B2B1B0|G5G4G3G2G1G0B9B8|R3R2R1R0G9G8G7G6|X1X0R9R8R7R6R5R4|
     *      +----------------+----------------+----------------+----------------+
     *
     */
    HAL_PIXEL_FORMAT_X2R10G10B10_INTEL = 0x119,

    /**
     * Intel X2BGR10 format.
     *
     * Used for 10bit video processing.
     *
     * \see GMM_FORMAT_R10G10B10X2_USCALED
     *
     *       ________________________ .....
     *      |RGBX|RGBX|              |    :
     *      |                        |    :
     *      h                        h    h'
     *      |____________w___________|    :
     *      :                             :
     *      :............stride...........:
     *
     *       bits
     *      +----+--------------------+--------------------+--------------------+
     *      |3130|29                20|19                10|09                 0|
     *      +----+--------------------+--------------------+--------------------+
     *      |X1X0|B9B8B7B6B5B4B3B2B1B0|G9G8G7G6G5G4G3G2G1G0|R9R8R7R6R5R4R3R2R1R0|
     *      +----+--------------------+--------------------+--------------------+
     *
     *       byte 0           byte 1           byte 2           byte 3
     *      +----------------+----------------+----------------+----------------+
     *      |R7R6R5R4R3R2R1R0|G5G4G3G2G1G0R9R8|B3B2B1B0B9B8B7B6|X1X0B9B8B7B6B5B4|
     *      +----------------+----------------+----------------+----------------+
     *
     */
    HAL_PIXEL_FORMAT_X2B10G10R10_INTEL = 0x11A,

    /**
     * Intel P012 format.
     *
     * Used for 12bit HEVC/VP9 decoding and video processing.
     *
     * \see HAL_PIXEL_FORMAT_P010_INTEL
     *
     */
    HAL_PIXEL_FORMAT_P012_INTEL = 0x11B,

    /**
     * Intel P016 format.
     *
     * Used for 16bit HEVC/VP9 decoding and video processing.
     *
     * \see HAL_PIXEL_FORMAT_P010_INTEL
     *
     */
    HAL_PIXEL_FORMAT_P016_INTEL = 0x11C,

    /**
     * Intel Y210 (4:2:2) format packed linear.
     *
     * Used for 10bit video processing. Each channel stored in 16bit words,
     * with the six least significant bits set to zero.
     *
     * \see HAL_PIXEL_FORMAT_YCrCb_NORMAL_INTEL with half the storage.
     *
     * Layout information:
     * - each pixel pair being represented by 64 bits (8 bytes)
     * - U/V are interleaved and 1/2 width
     * - allocation width is rounded by GMM.
     * - stride is measured in elements (pixel pairs).
     * - tiling is
     *         + linear for GRALLOC_SW_READ/WRITE or _CAMERA
     *         + X or Y otherwise, determined by GMM
     *
     *       ________________________ .....
     *      |Y0UY1V|Y2UY3V           |    :
     *      |                        |    :
     *      h                        h    h'
     *      |____________w___________|    :
     *      :                             :
     *      :............stride...........:
     *
     *       word 0 (Y0)                       word 1 (U)
     *       byte 0           byte 1           byte 2           byte 3
     *      +----------------+----------------+----------------+----------------+
     *      |Y7Y6 0 0 0 0 0 0|YfYeYdYcYbYaY9Y8|U7U6 0 0 0 0 0 0|UfUeUdUcUbUaU9U8|
     *      +----------------+----------------+----------------+----------------+
     *
     *       word 2 (Y1)                       word 3 (V)
     *       byte 4           byte 5           byte 6           byte 7
     *      +----------------+----------------+----------------+----------------+
     *      |Y7Y6 0 0 0 0 0 0|YfYeYdYcYbYaY9Y8|V7V6 0 0 0 0 0 0|VfVeVdVcVbVaV9V8|
     *      +----------------+----------------+----------------+----------------+
     *
     */
    HAL_PIXEL_FORMAT_Y210_INTEL = 0x11D,

    /**
     * Intel Y216 (4:2:2) format.
     *
     * Used for 16bit video processing.
     *
     * \see HAL_PIXEL_FORMAT_Y210_INTEL except all 16bits are used.
     */
    HAL_PIXEL_FORMAT_Y216_INTEL = 0x11E,

    /**
     * Intel Y410 (4:4:4) format.
     *
     * Used for 10bit video processing.
     *
     * \see HAL_PIXEL_FORMAT_A2R10G10B10_INTEL/GMM_FORMAT_B10G10R10A2_UNORM
     *
     * Layout information:
     * - each pixel is 32 bits (4 bytes)
     * - allocation width is rounded by GMM.
     * - stride is measured in elements (single pixels).
     * - tiling is
     *         + linear for GRALLOC_SW_READ/WRITE or _CAMERA
     *         + X or Y otherwise, determined by GMM
     *
     *       ________________________ .....
     *      |UYVA|UYVA|              |    :
     *      |                        |    :
     *      h                        h    h'
     *      |____________w___________|    :
     *      :                             :
     *      :............stride...........:
     *
     *       bits
     *      +----+--------------------+--------------------+--------------------+
     *      |3130|29                20|19                10|09                 0|
     *      +----+--------------------+--------------------+--------------------+
     *      |A1A0|V9V8V7V6V5V4V3V2V1V0|Y9Y8Y7Y6Y5Y4Y3Y2Y1Y0|U9U8U7U6U5U4U3U2U1U0|
     *      +----+--------------------+--------------------+--------------------+
     *
     *       byte 0           byte 1           byte 2           byte 3
     *      +----------------+----------------+----------------+----------------+
     *      |U7U6U5U4U3U2U1U0|Y5Y4Y3Y2Y1Y0U9U8|V3V2V1V0Y9Y8Y7Y6|A1A0V9V8V7V6V5V4|
     *      +----------------+----------------+----------------+----------------+
     *
     */
    HAL_PIXEL_FORMAT_Y410_INTEL = 0x11F,

    /**
     * Intel Y416 (4:4:4) format.
     *
     * Used for 16bit video processing.
     *
     * Similar to RGBA 8888 with 16bit values for UVYA channels.
     *
     * Layout information:
     * - each pixel is 64 bits (8 bytes)
     * - allocation width is rounded by GMM.
     * - stride is measured in elements (single pixels).
     * - tiling is
     *         + linear for GRALLOC_SW_READ/WRITE or _CAMERA
     *         + X or Y otherwise, determined by GMM
     *
     *       ________________________ .....
     *      |UYVA|UYVA               |    :
     *      |                        |    :
     *      h                        h    h'
     *      |____________w___________|    :
     *      :                             :
     *      :............stride...........:
     *
     *       word 0 (U)                        word 1 (Y)
     *       byte 0           byte 1           byte 2           byte 3
     *      +----------------+----------------+----------------+----------------+
     *      |U7U6U5U4U3U2U1U0|UfUeUdUcUbUaU9U8|Y7Y6Y5Y4Y3Y2Y1Y0|YfYeYdYcYbYaY9Y8|
     *      +----------------+----------------+----------------+----------------+
     *
     *       word 2 (V)                        word 3 (A)
     *       byte 4           byte 5           byte 6           byte 7
     *      +----------------+----------------+----------------+----------------+
     *      |V7V6V5V4V3V2V1V0|VfVeVdVcVbVaV9V8|A7A6A5A4A3A2A1A0|AfAeAdAcAbAaA9A8|
     *      +----------------+----------------+----------------+----------------+
     *
     *
     */
    HAL_PIXEL_FORMAT_Y416_INTEL = 0x120,

    /**
     * Intel Y8I format.
     *
     * Used by RealSense depth camera.
     *
     * Layout information:
     * - each pixel being represented by 16 bits (2 bytes)
     * - no width/height restrictions
     * - hstride is width
     * - hstride is specified in pixels, not in bytes
     * - vstride is height
     * - memory is linear
     *
     *       ____________w___________
     *      |YI0|YI1                 |
     *      |                        |
     *      h                        h
     *      |                        |
     *      |                        |
     *      |____________w___________|
     *
     *      pitch (in bytes) = w*2
     *      size (in bytes) = w*h*2
     */
    HAL_PIXEL_FORMAT_Y8I_INTEL = 0x121,

    /**
     * Intel Y12I format.
     *
     * Used by RealSense depth camera.
     *
     * Layout information:
     * - each pixel being represented by 24 bits (3 bytes)
     * - no width/height restrictions
     * - hstride is width
     * - hstride is specified in pixels, not in bytes
     * - vstride is height
     * - memory is linear
     *
     *       ____________w___________
     *      |YI0|YI1                 |
     *      |                        |
     *      h                        h
     *      |                        |
     *      |                        |
     *      |____________w___________|
     *
     *      pitch (in bytes) = w*3
     *      size (in bytes) = w*h*3
     */
    HAL_PIXEL_FORMAT_Y12I_INTEL = 0x122,


    /*
     * convenience alias names
     */
    HAL_PIXEL_FORMAT_YUYV_INTEL = HAL_PIXEL_FORMAT_YCrCb_NORMAL_INTEL,
    HAL_PIXEL_FORMAT_YUY2_INTEL = HAL_PIXEL_FORMAT_YCrCb_NORMAL_INTEL,
    HAL_PIXEL_FORMAT_VYUY_INTEL = HAL_PIXEL_FORMAT_YCrCb_SWAPUVY_INTEL,
    HAL_PIXEL_FORMAT_YVYU_INTEL = HAL_PIXEL_FORMAT_YCrCb_SWAPUV_INTEL,
    HAL_PIXEL_FORMAT_UYVY_INTEL = HAL_PIXEL_FORMAT_YCrCb_SWAPY_INTEL,

    /**
     * \deprecated alias name
     * \see HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL
     */
    HAL_PIXEL_FORMAT_NV12_TILED_INTEL   = HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL,
    HAL_PIXEL_FORMAT_NV12_INTEL         = HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL,
    HAL_PIXEL_FORMAT_INTEL_NV12         = HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL,

    /**
     * \note THIS WILL BE GOING AWAY!
     *
     * \deprecated value out of range of reserved pixel formats
     * \see #include <openmax/OMX_IVCommon.h>
     * \see OMX_INTEL_COLOR_FormatYUV420PackedSemiPlanar
     * \see HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL
     */
    HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL = 0x7FA00E00,

    /**
     * \note THIS WILL BE GOING AWAY!
     *
     * \deprecated value out of range of reserved pixel formats
     * \see #include <openmax/OMX_IVCommon.h>
     * \see OMX_INTEL_COLOR_FormatYUV420PackedSemiPlanar
     * \see HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL
     */
    HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL = 0x7FA00F00,
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* INTEL_UFO_GRAPHICS_H */
