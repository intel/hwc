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

#ifndef INTEL_UFO_GRALLOC_CLIENT_H
#define INTEL_UFO_GRALLOC_CLIENT_H

#include <utils/Singleton.h>
#include <ufo/gralloc.h>

// from <GmmLib/.../GmmResourceInfoExt.h>
struct GMM_RESCREATE_PARAMS_REC;
typedef struct GMM_RESCREATE_PARAMS_REC GMM_RESCREATE_PARAMS;

namespace intel {
namespace ufo {
namespace gralloc {

using namespace android;

/** \deprecated */
typedef intel_ufo_buffer_details_t buffer_info_t;

/** This class is a wrapper to the functionality exposed by the UFO gralloc module.
 *
 * While this class currently just tracks and returns gralloc info, longer term
 * we are likely to want to track much more info about buffers.
 */
class ANDROID_API GrallocClient : public Singleton<GrallocClient>
{
public:
    static const GrallocClient& get() { return getInstance(); }

    bool check() const;

    int getFd(int *fd) const;
    int setDisplay(int display, uint32_t width, uint32_t height, uint32_t xdpi, uint32_t ydpi) const;
    int getBufferObject(buffer_handle_t handle, uint32_t *bo) const;
    int getBufferName(buffer_handle_t handle, uint32_t *name) const;
    int getBufferFrame(buffer_handle_t handle, uint32_t *fb) const;
    int getBufferInfo(buffer_handle_t handle, intel_ufo_buffer_details_t* info) const;
    int getBufferStatus(buffer_handle_t handle) const;
#if INTEL_UFO_GRALLOC_HAVE_FB_REF_COUNTING
    int acquireFrame(uint32_t fb) const;
    int releaseFrame(uint32_t fb) const;
#endif // INTEL_UFO_GRALLOC_HAVE_FB_REF_COUNTING
    int queryBufferSession(buffer_handle_t handle, intel_ufo_buffer_pavp_session_t *session) const;
    int queryMediaDetails(buffer_handle_t handle, intel_ufo_buffer_media_details_t*) const;
    int setBufferPavpSession(buffer_handle_t handle, uint32_t session, uint32_t instance, uint32_t is_encrypted) const;
    int setBufferColorRange(buffer_handle_t handle, uint32_t color) const;
    int setBufferClientId(buffer_handle_t handle, uint32_t client_id) const;
    int setBufferMmcMode(buffer_handle_t handle, uint32_t mmc_mode) const;
    int setBufferKeyFrame(buffer_handle_t handle, uint32_t is_key_frame) const;
    int setBufferCodecType(buffer_handle_t handle, uint32_t codec, uint32_t is_interlaced) const;
    int setBufferDirtyRect(buffer_handle_t handle, uint32_t valid, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom) const;
    int queryBufferGmmParams(buffer_handle_t handle, GMM_RESCREATE_PARAMS* params) const;
    int getBufferPrime(buffer_handle_t handle, int *prime) const;
    int registerHwcProcs(const intel_ufo_hwc_procs_t*) const;
    int setBufferFrameUpdatedFlag(buffer_handle_t handle, uint32_t is_updated) const;
    int setBufferFrameEncodedFlag(buffer_handle_t handle, uint32_t is_encoded) const;
    int setBufferCompressionHint(buffer_handle_t handle, uint32_t hint) const;
    int getBufferCompressionHint(buffer_handle_t handle, uint32_t* hint) const;
    int setBufferResolveDetails(buffer_handle_t handle, const intel_ufo_buffer_resolve_details_t*) const;
    int getBufferResolveDetails(buffer_handle_t handle, intel_ufo_buffer_resolve_details_t*) const;
    int setBufferCameraDetails(buffer_handle_t handle, const intel_ufo_buffer_camera_details_t*) const;
    int getBufferCameraDetails(buffer_handle_t handle, intel_ufo_buffer_camera_details_t*) const;
    int setBufferTimestamp(buffer_handle_t handle, uint64_t) const;
    int setBufferFps(buffer_handle_t handle, uint32_t) const;
    int setBufferMetadata(buffer_handle_t handle, uint32_t offset, uint32_t size, const void*) const;
    int getBufferMetadata(buffer_handle_t handle, uint32_t offset, uint32_t size, void*) const;
    int getBufferSerialNumber(buffer_handle_t handle, uint64_t*) const;
    int fallocate(buffer_handle_t handle, uint32_t mode, uint64_t offset, uint64_t size) const;

public: // overloaded variants
#if INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_0 && INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL != 0
    int getBufferInfo(buffer_handle_t handle, intel_ufo_buffer_details_0_t* info) const;
#endif
#if INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_1 && INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL != 1
    int getBufferInfo(buffer_handle_t handle, intel_ufo_buffer_details_1_t* info) const;
#endif
#if INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_2 && INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL != 2
    int getBufferInfo(buffer_handle_t handle, intel_ufo_buffer_details_2_t* info) const;
#endif

private:
    GrallocClient();
    friend class Singleton<GrallocClient>;

private:
    gralloc_module_t const * mGralloc;
};


/**
 * \return true if gralloc module was loaded and verified
 */
inline bool GrallocClient::check() const
{
    return mGralloc;
}


} // namespace gralloc
} // namespace ufo
} // namespace intel

#endif // INTEL_UFO_GRALLOC_CLIENT_H
