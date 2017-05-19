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

#ifndef INTEL_UFO_HWC_DRMFORMATHELPER_H
#define INTEL_UFO_HWC_DRMFORMATHELPER_H
#include <drm_fourcc.h>
#include <ufo/graphics.h>

namespace intel {
namespace ufo {
namespace hwc {

inline int32_t convertHalFormatToDrmFormat(uint32_t format, bool bDiscardAlpha = false)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_BGRA_8888:                    return bDiscardAlpha ? DRM_FORMAT_XRGB8888 : DRM_FORMAT_ARGB8888;
    case HAL_PIXEL_FORMAT_RGBA_8888:                    return bDiscardAlpha ? DRM_FORMAT_XBGR8888 : DRM_FORMAT_ABGR8888;
    case HAL_PIXEL_FORMAT_RGBX_8888:                    return DRM_FORMAT_XBGR8888;
    case HAL_PIXEL_FORMAT_RGB_888:                      return DRM_FORMAT_BGR888;
    case HAL_PIXEL_FORMAT_RGB_565:                      return DRM_FORMAT_RGB565;
    case HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL:           return DRM_FORMAT_NV12;
    case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:           return DRM_FORMAT_NV12;
    case HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL:            return DRM_FORMAT_NV12;
    case HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL:     return DRM_FORMAT_NV12;
    case HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL:     return DRM_FORMAT_NV12;
    case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL: return DRM_FORMAT_NV12; /* deprecated */
    case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL: return DRM_FORMAT_NV12; /* deprecated */
    case HAL_PIXEL_FORMAT_YCbCr_422_I:                  return DRM_FORMAT_YUYV; /* deprecated */
    case HAL_PIXEL_FORMAT_YCrCb_422_H_INTEL:            return DRM_FORMAT_YVU422; /* YV16 */
    case HAL_PIXEL_FORMAT_YCbCr_422_H_INTEL:            return DRM_FORMAT_YUV422; /* YU16 */
    case HAL_PIXEL_FORMAT_YCbCr_411_INTEL:              return DRM_FORMAT_YUV411;
    case HAL_PIXEL_FORMAT_YCbCr_420_H_INTEL:            return DRM_FORMAT_YUV420;
    case HAL_PIXEL_FORMAT_YCbCr_422_V_INTEL:            return DRM_FORMAT_YUV422;
    case HAL_PIXEL_FORMAT_YCbCr_444_INTEL:              return DRM_FORMAT_YUV444;

    case HAL_PIXEL_FORMAT_A2R10G10B10_INTEL:            return bDiscardAlpha ? DRM_FORMAT_XRGB2101010 : DRM_FORMAT_ARGB2101010 ;
    case HAL_PIXEL_FORMAT_A2B10G10R10_INTEL:            return bDiscardAlpha ? DRM_FORMAT_XBGR2101010 : DRM_FORMAT_ABGR2101010 ;


    // Unsupported formats, comment out for now.
    //case HAL_PIXEL_FORMAT_GENERIC_8BIT_INTEL:
    //case HAL_PIXEL_FORMAT_YV12:
    //case HAL_PIXEL_FORMAT_YCbCr_422_SP: /* deprecated */
    //case HAL_PIXEL_FORMAT_YCrCb_420_SP: /* deprecated */

    //case HAL_PIXEL_FORMAT_Y8:
    //case HAL_PIXEL_FORMAT_Y16:
    //case HAL_PIXEL_FORMAT_YCbCr_420_888:
    default:
        ALOGD_IF(MODE_DEBUG, "format %x is not supported by drm", format);
        return 0;
    }
}

inline uint32_t convertDrmFormatToHalFormat(int32_t format)
{
    switch (format) {
    case DRM_FORMAT_ARGB8888:       return HAL_PIXEL_FORMAT_BGRA_8888;
    case DRM_FORMAT_ABGR8888:       return HAL_PIXEL_FORMAT_RGBA_8888;
    case DRM_FORMAT_XBGR8888:       return HAL_PIXEL_FORMAT_RGBX_8888;
    case DRM_FORMAT_BGR888:         return HAL_PIXEL_FORMAT_RGB_888;
    case DRM_FORMAT_RGB565:         return HAL_PIXEL_FORMAT_RGB_565;
    case DRM_FORMAT_NV12:           return HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL;
    case DRM_FORMAT_YUYV:           return HAL_PIXEL_FORMAT_YCbCr_422_I;
    case DRM_FORMAT_YVU422:         return HAL_PIXEL_FORMAT_YCrCb_422_H_INTEL; /* YV16 */
    case DRM_FORMAT_YUV422:         return HAL_PIXEL_FORMAT_YCbCr_422_H_INTEL; /* YU16 */
    case DRM_FORMAT_YUV411:         return HAL_PIXEL_FORMAT_YCbCr_411_INTEL;
    case DRM_FORMAT_YUV420:         return HAL_PIXEL_FORMAT_YCbCr_420_H_INTEL;
    case DRM_FORMAT_YUV444:         return HAL_PIXEL_FORMAT_YCbCr_444_INTEL;

    case DRM_FORMAT_ARGB2101010:    return HAL_PIXEL_FORMAT_A2R10G10B10_INTEL;
    case DRM_FORMAT_ABGR2101010:    return HAL_PIXEL_FORMAT_A2B10G10R10_INTEL;
    default:
        ALOGD_IF(MODE_DEBUG, "Drm format %s is not supported by Android HAL", getDRMFormatString(format));
        return 0;
    }
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DRMFORMATHELPER_H
