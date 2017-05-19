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

#ifndef INTEL_UFO_HWC_DRMMODEHELPER_H
#define INTEL_UFO_HWC_DRMMODEHELPER_H

#include "PhysicalDisplay.h"
#include <drm_mode.h>

namespace intel {
namespace ufo {
namespace hwc {

// Notionally, this could be in Display::Timing, however, we dont really want to force the drm_mode_modeinfo
// structure to be a global, so handle it differently
inline Timing::EAspectRatio getDrmModeAspectRatio(uint32_t in)
{

#if defined(DRM_PICTURE_ASPECT_RATIO)
    // Imin_legacy codepath
    switch(in)
    {
        case HDMI_PICTURE_ASPECT_4_3:
            return Timing::EAspectRatio::R4_3;
        case HDMI_PICTURE_ASPECT_16_9:
            return Timing::EAspectRatio::R16_9;
        default:
            break;
    }
#elif defined(DRM_MODE_FLAG_PARMASK)
    // Gmin codepath
    switch(in & DRM_MODE_FLAG_PARMASK)
    {
        case DRM_MODE_FLAG_PAR4_3:
            return Timing::EAspectRatio::R4_3;
        case DRM_MODE_FLAG_PAR16_9:
            return Timing::EAspectRatio::R16_9;
        default:
            break;
    }
#else
    HWC_UNUSED(in);
#endif
    return Timing::EAspectRatio::Any;
};

inline Timing::EAspectRatio getDrmModeAspectRatio(const struct drm_mode_modeinfo &m)
{
#ifdef DRM_PICTURE_ASPECT_RATIO
    return getDrmModeAspectRatio(m.picture_aspect_ratio);
#else
    return getDrmModeAspectRatio(m.flags);
#endif
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DRMMODEHELPER_H
