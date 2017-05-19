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


#ifndef INTEL_UFO_HWC_DRMNUCLEARPAGEFLIPHANDLER_H
#define INTEL_UFO_HWC_DRMNUCLEARPAGEFLIPHANDLER_H

#include "DrmPageFlipHandler.h"
#include "Timeline.h"
#include "Drm.h"

#if VPG_DRM_HAVE_ATOMIC_NUCLEAR

namespace intel {
namespace ufo {
namespace hwc {

class Drm;
class DrmDisplay;

enum drm_blend_factor {
        DRM_BLEND_FACTOR_AUTO,
        DRM_BLEND_FACTOR_ZERO,
        DRM_BLEND_FACTOR_ONE,
        DRM_BLEND_FACTOR_SRC_ALPHA,
        DRM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        DRM_BLEND_FACTOR_CONSTANT_ALPHA,
        DRM_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
        DRM_BLEND_FACTOR_CONSTANT_ALPHA_TIMES_SRC_ALPHA,
        DRM_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA_TIMES_SRC_ALPHA,
};

#define DRM_BLEND_FUNC(src_factor, dst_factor)          \
        (DRM_BLEND_FACTOR_##src_factor << 16 | DRM_BLEND_FACTOR_##dst_factor)


class DrmNuclearHelper
{
public:
    DrmNuclearHelper(DrmDisplay& display);

    class Properties;

    // Generate the properties to update a plane.
    void updatePlane(const Layer* pLayer, Properties& props, uint32_t drmPlaneId);

    // Generate the properties to update a mode.
    void updateMode(bool active, uint32_t drmModeId, Properties& props);

    // drm Wrapper call.
    int drmAtomic(uint32_t flags, const Properties& props, uint32_t user_data);

    String8 dump(const Properties& props) const;

    // Nuclear equivalent of setCrtc
    int setCrtcNuclear( const drmModeModeInfoPtr pModeInfo, const Layer* pLayer );

private:
    // Get property if it is valid.
    uint32_t getPropertyIDIfValid(const char *name);

    // Get blend func and color.
    uint32_t getBlendFunc(const Layer & layer);
    uint64_t getBlendColor(const Layer & layer);

private:

    // Do drrs via atomic apis.
    static Option           sOptionNuclearDrrs;

    // Display.
    DrmDisplay&             mDisplay;

    // Drm.
    Drm&                    mDrm;

    // Property ids
    uint32_t                mPropCrtc;
    uint32_t                mPropFb;
    uint32_t                mPropDstX;
    uint32_t                mPropDstY;
    uint32_t                mPropDstW;
    uint32_t                mPropDstH;
    uint32_t                mPropSrcX;
    uint32_t                mPropSrcY;
    uint32_t                mPropSrcW;
    uint32_t                mPropSrcH;
    uint32_t                mPropCrtcMode;
    uint32_t                mPropCrtcActive;
    uint32_t                mPropRot;
    uint32_t                mPropEnc;
    uint32_t                mPropRC;
    uint32_t                mProcBlendFunc;
    uint32_t                mProcBlendColor;
};

// Drm display flip handler class for atomic Drm.
class DrmNuclearPageFlipHandler : public DrmPageFlipHandler::AbstractImpl
{
public:

    DrmNuclearPageFlipHandler( DrmDisplay& display );
    virtual ~DrmNuclearPageFlipHandler( );

    // Tests whether atomic API is available for use by setting blanking.
    // Returns true if successful.
    static bool test( DrmDisplay& display );

protected:

    // Flip the next frame to the display.
    // Returns true if the flip event request is successfully issued.
    virtual bool doFlip( DisplayQueue::Frame* pNewFrame, bool bMainBlanked, uint32_t flipEvData );
private:

    // Do drrs via atomic apis.
    static Option           sOptionNuclearDrrs;

    // Display.
    DrmDisplay&             mDisplay;

    // Drm.
    Drm&                    mDrm;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif

#endif // INTEL_UFO_HWC_DRMNUCLEARPAGEFLIPHANDLER_H
