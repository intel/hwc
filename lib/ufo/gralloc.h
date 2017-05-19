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
 * This <ufo/gralloc.h> file contains public extensions provided by UFO GRALLOC HAL.
 */

#ifndef INTEL_UFO_GRALLOC_H
#define INTEL_UFO_GRALLOC_H

#include <hardware/gralloc.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push,4)

/** Simple API level control
 * \see INTEL_UFO_GRALLOC_MODULE_VERSION_LATEST
 */
#define INTEL_UFO_GRALLOC_API_LEVEL 0
#define INTEL_UFO_GRALLOC_API_LEVEL_MINOR 33


/** Gralloc support for drm prime fds.
 * \note Mandatory (more secure) mechanism for buffer sharing.
 * \note if non-zero, then prime fds are supported and used as buffer sharing mechanism.
 * \note if zero or undefined, then prime fds are not supported (flink names are used instead).
 * \see INTEL_UFO_GRALLOC_HAVE_FLINK
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_INFO
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_PRIME
 */
#define INTEL_UFO_GRALLOC_HAVE_PRIME 1


/** Gralloc support for (legacy) flink names.
 * \note Deprecated due to security requirements.
 * \note if zero, then flink names are not available (prime fds are used instead).
 * \note if non-zero, then gralloc supports flink names.
 * \see INTEL_UFO_GRALLOC_HAVE_PRIME
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_NAME
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_INFO
 */
#define INTEL_UFO_GRALLOC_HAVE_FLINK !(INTEL_UFO_GRALLOC_HAVE_PRIME)

// Enable for FB reference counting.
#define INTEL_UFO_GRALLOC_HAVE_FB_REF_COUNTING 1
// Enable for PAVP query.
#define INTEL_UFO_GRALLOC_HAVE_QUERY_PAVP_SESSION 1
// Enable for Media query.
#define INTEL_UFO_GRALLOC_HAVE_QUERY_MEDIA_DETAILS 1

/**
 * stage1: media can use gem_datatype with legacy offsets/bits
 * stage2: media can use gem_datatype with new compressed offsets/bits
 * stage3: same as stage2 but additionally gralloc uses private data to store other bits that don't fit into gem_datatype
 * stage4: gralloc uses private data for all bits. only gralloc owns gem_datatype!
 */
#define INTEL_UFO_GRALLOC_MEDIA_API_STAGE 2


/** Gralloc internal metadata definition (hidden by default)
 */
#ifndef INTEL_UFO_GRALLOC_PUBLIC_METADATA
#define INTEL_UFO_GRALLOC_PUBLIC_METADATA 0
#endif


/** Gralloc deprecation mechanism (enabled by default)
 * \note To supress can be defined in local mk to disable deprecated attributes.
 */
#ifndef INTEL_UFO_GRALLOC_IGNORE_DEPRECATED
#define INTEL_UFO_GRALLOC_IGNORE_DEPRECATED 0
#endif

#if !INTEL_UFO_GRALLOC_IGNORE_DEPRECATED
// deprecate use of flink names if prime fds are enabled
#define INTEL_UFO_GRALLOC_DEPRECATE_FLINK       (INTEL_UFO_GRALLOC_HAVE_PRIME)
// deprecate use of datatype from media api stage3
#define INTEL_UFO_GRALLOC_DEPRECATE_DATATYPE    (INTEL_UFO_GRALLOC_MEDIA_API_STAGE >= 3)
#endif


/** Operations for the (*perform)() hook
 * \see gralloc_module_t::perform
 */

#define INTEL_UFO_GRALLOC_MODULE_PERFORM_CHECK_VERSION           0 // (void)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_DRM_FD              1 // (int*)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_DISPLAY             2 // (int display, uint32_t width, uint32_t height, uint32_t xdpi, uint32_t ydpi)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_HANDLE           3 // (buffer_handle_t, int*)
#if INTEL_UFO_GRALLOC_DEPRECATE_FLINK
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_NAME_DEPRECATED  4
#else
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_NAME             4 // (buffer_handle_t, uint32_t*)
#endif
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_FBID             5 // (buffer_handle_t, uint32_t*)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_INFO             6 // (buffer_handle_t, intel_ufo_buffer_details_t*)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_STATUS           7 // (buffer_handle_t)
#if INTEL_UFO_GRALLOC_HAVE_FB_REF_COUNTING
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_FB_ACQUIRE              8 // (uint32_t)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_FB_RELEASE              9 // (uint32_t)
#endif
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_PAVP_SESSION     10 // (buffer_handle_t, intel_ufo_buffer_pavp_session_t*)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_MEDIA_DETAILS    11 // (buffer_handle_t, intel_ufo_buffer_media_details_t*)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_PAVP_SESSION    12 // (buffer_handle_t, uint32_t session, uint32_t instance, uint32_t is_encrypted)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_COLOR_RANGE     13 // (buffer_handle_t, uint32_t color_range)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_CLIENT_ID       14 // (buffer_handle_t, uint32_t client_id)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_MMC_MODE        15 // (buffer_handle_t, uint32_t mmc_mode)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_KEY_FRAME       16 // (buffer_handle_t, uint32_t is_key_frame)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_CODEC_TYPE      17 // (buffer_handle_t, uint32_t codec, uint32_t is_interlaced)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_DIRTY_RECT      18 // (buffer_handle_t, uint32_t valid, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_GMM_PARAMS       19 // (buffer_handle_t, GMM_RESCREATE_PARAMS* params)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_PRIME           20 // (buffer_handle_t, int *prime)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_REGISTER_HWC_PROCS     21 // (const intel_ufo_hwc_procs_t*)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_FRAME_UPDATED   22 // (buffer_handle_t, uint32_t is_updated)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_FRAME_ENCODED   23 // (buffer_handle_t, uint32_t is_encoded)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_COMPR_HINT      24 // (buffer_handle_t, uint32_t hint)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_COMPR_HINT      25 // (buffer_handle_t, uint32_t *hint)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_RESOLVE_DETAILS    26 // (buffer_handle_t, const intel_ufo_buffer_resolve_details_t*)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_RESOLVE_DETAILS    27 // (buffer_handle_t, intel_ufo_buffer_resolve_details_t*)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_CAMERA_DETAILS     28 // (buffer_handle_t, const intel_ufo_buffer_camera_details_t *details)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_CAMERA_DETAILS     29 // (buffer_handle_t, intel_ufo_buffer_camera_details_t *details)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_TIMESTAMP       30 // (buffer_handle_t, uint64_t)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_METADATA        31 // (buffer_handle_t, uint32_t offset, uint32_t size, const void *data)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_METADATA        32 // (buffer_handle_t, uint32_t offset, uint32_t size, void *data)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_BO_FALLOCATE           33 // (buffer_handle_t, uint32_t mode, uint64_t offset, uint64_t bytes)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_SERIAL_NUMBER   34 // (buffer_handle_t, uint64_t *serial)
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_FPS             35 // (buffer_handle_t, uint32_t)

#if 1 // reserved for internal use only !
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_PRIVATE_0           -1000
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_PRIVATE_1           -1001
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_PRIVATE_2           -1002
#define INTEL_UFO_GRALLOC_MODULE_PERFORM_PRIVATE_3           -1003
#endif

// perform codes from gralloc1on0adaptor
enum {
    GRALLOC1_ADAPTER_PERFORM_FIRST = 10000,

    // void getRealModuleApiVersionMinor(..., int* outMinorVersion);
    GRALLOC1_ADAPTER_PERFORM_GET_REAL_MODULE_API_VERSION_MINOR =
        GRALLOC1_ADAPTER_PERFORM_FIRST,

    // void setUsages(..., buffer_handle_t buffer,
    //                     int producerUsage,
    //                     int consumerUsage);
    GRALLOC1_ADAPTER_PERFORM_SET_USAGES =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 1,

    // void getDimensions(..., buffer_handle_t buffer,
    //                         int* outWidth,
    //                         int* outHeight);
    GRALLOC1_ADAPTER_PERFORM_GET_DIMENSIONS =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 2,

    // void getFormat(..., buffer_handle_t buffer, int* outFormat);
    GRALLOC1_ADAPTER_PERFORM_GET_FORMAT =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 3,

    // void getProducerUsage(..., buffer_handle_t buffer, int* outUsage);
    GRALLOC1_ADAPTER_PERFORM_GET_PRODUCER_USAGE =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 4,

    // void getConsumerUsage(..., buffer_handle_t buffer, int* outUsage);
    GRALLOC1_ADAPTER_PERFORM_GET_CONSUMER_USAGE =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 5,

    // void getBackingStore(..., buffer_handle_t buffer,
    //                           uint64_t* outBackingStore);
    GRALLOC1_ADAPTER_PERFORM_GET_BACKING_STORE =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 6,

    // void getNumFlexPlanes(..., buffer_handle_t buffer,
    //                            int* outNumFlexPlanes);
    GRALLOC1_ADAPTER_PERFORM_GET_NUM_FLEX_PLANES =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 7,

    // void getStride(..., buffer_handle_t buffer, int* outStride);
    GRALLOC1_ADAPTER_PERFORM_GET_STRIDE =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 8,

    // void lockFlex(..., buffer_handle_t buffer,
    //                    int producerUsage,
    //                    int consumerUsage,
    //                    int left,
    //                    int top,
    //                    int width,
    //                    int height,
    //                    android_flex_layout* outLayout,
    //                    int acquireFence);
    GRALLOC1_ADAPTER_PERFORM_LOCK_FLEX =
        GRALLOC1_ADAPTER_PERFORM_FIRST + 9,
};

/** Simple version control
 * \see gralloc_module_t::perform
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_CHECK_VERSION
 */
enum {
    INTEL_UFO_GRALLOC_MODULE_VERSION_0 = ANDROID_NATIVE_MAKE_CONSTANT('I','N','T','C'),
    INTEL_UFO_GRALLOC_MODULE_VERSION_LATEST = INTEL_UFO_GRALLOC_MODULE_VERSION_0 + INTEL_UFO_GRALLOC_API_LEVEL,
};


/** Structure with detailed info about allocated buffer.
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_INFO
 * \deprecated
 */
typedef struct intel_ufo_buffer_details_0_t
{
  union {
   uint32_t magic;   // [in] size of this struct
   struct {
    int width;       // \see alloc_device_t::alloc
    int height;      // \see alloc_device_t::alloc
    int format;      // \see alloc_device_t::alloc
    int usage;       // \see alloc_device_t::alloc
#if INTEL_UFO_GRALLOC_HAVE_PRIME
    int prime;       // prime fd \note gralloc retains fd ownership
#elif INTEL_UFO_GRALLOC_DEPRECATE_FLINK
  union {
    int name __attribute__ ((deprecated));
    int name_DEPRECATED;
  };
#else
    int name;        // flink
#endif // INTEL_UFO_GRALLOC_HAVE_PRIME
    uint32_t fb;        // framebuffer id
    uint32_t fb_format; // framebuffer drm format
    int pitch;       // buffer pitch (in bytes)
    int size;        // buffer size (in bytes)

    int allocWidth;  // allocated buffer width in pixels.
    int allocHeight; // allocated buffer height in lines.
    int allocOffsetX;// horizontal pixel offset to content origin within allocated buffer.
    int allocOffsetY;// vertical line offset to content origin within allocated buffer.
   };
  };

#ifdef __cplusplus
    intel_ufo_buffer_details_0_t() : magic(sizeof(*this)) { }
#endif
} intel_ufo_buffer_details_0_t;


/** Structure with detailed info about allocated buffer.
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_INFO
 */
typedef struct intel_ufo_buffer_details_1_t
{
    uint32_t magic;         // [in] size of this struct

    int width;              // \see alloc_device_t::alloc
    int height;             // \see alloc_device_t::alloc
    int format;             // \see alloc_device_t::alloc \note resolved format (not flexible)
    int usage;              // \see alloc_device_t::alloc

    int name;               // serial number (or flink name)
    int prime;              // prime fd \note gralloc retains fd ownership

    uint32_t flags;         // \see INTEL_UFO_BUFFER_FLAG_*
    uint32_t fb;            // framebuffer id (only if gralloc owns FB)
    uint32_t fb_format;     // framebuffer drm format (only if gralloc owns FB)

    uint32_t size;          // buffer size (in bytes)

    uint32_t pitch;         // buffer pitch (in bytes)
    uint32_t allocWidth;    // allocated buffer width in pixels.
    uint32_t allocHeight;   // allocated buffer height in lines.
    union {
        int reserved[2];    // MBZ
        int allocOffsetX;   // DEPRECATED, DO NOT USE
        int allocOffsetY;   // DEPRECATED, DO NOT USE
    };

    union {
        struct {
            uint32_t aux_offset;    // offset (in bytes) to AUX/CCS surface
            uint32_t aux_pitch;     // pitch (in bytes) of AUX surface
            uint32_t reserved[2];   // TBD
        } rc;                       // \see INTEL_UFO_BUFFER_FLAG_RC
        struct {
            uint32_t reserved[4];   // TBD
        } mmc;                      // \see INTEL_UFO_BUFFER_FLAG_MMC
    };

    struct {
        uint32_t cb_offset;         // Cb/U offset for planar formats
        uint32_t cr_offset;         // Cr/V offset for planar formats
        uint32_t chroma_stride;     // Stride (AKA pitch) of chroma plane
        uint32_t chroma_step;       // 1=planar (individual cb/cr planes) 2=interleaved or semiplanar
    } ycbcr;

#ifdef __cplusplus
    intel_ufo_buffer_details_1_t() : magic(sizeof(*this)) { }
#endif
} intel_ufo_buffer_details_1_t;

#define INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_0 1
#define INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_1 1

/**
 * Buffer details interface
 * INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL defines the default.
 *
 * \see INTEL_UFO_GRALLOC_METADATA_BUFFER_DETAILS_LEVEL
*/
#define INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL 1
typedef struct intel_ufo_buffer_details_1_t intel_ufo_buffer_details_t;


/** Structure with additional info about buffer that could be changed after allocation.
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_MEDIA_DETAILS
 */
typedef struct intel_ufo_buffer_media_details_1_t
{
    uint32_t magic;             // [in] Size of this struct
    uint32_t pavp_session_id;   // PAVP Session ID.
    uint32_t pavp_instance_id;  // PAVP Instance.
    uint32_t yuv_color_range;   // YUV Color range.
    uint32_t client_id;         // HWC client ID.
    uint32_t is_updated;        // frame updated flag
    uint32_t is_encoded;        // frame encoded flag
    uint32_t is_encrypted;
    uint32_t is_key_frame;
    uint32_t is_interlaced;
    uint32_t is_mmc_capable;
    uint32_t compression_mode;
    uint32_t codec;
    struct {
        uint32_t is_valid;
        struct {
            uint32_t left;
            uint32_t top;
            uint32_t right;
            uint32_t bottom;
        } rect;
    } dirty;                    // Dirty region hint.
    uint64_t timestamp;

    // additional data added at Level1
    uint32_t fps;

#ifdef __cplusplus
    intel_ufo_buffer_media_details_1_t() : magic(sizeof(*this)) { }
#endif
} intel_ufo_buffer_media_details_1_t;


/**
 * Buffer details interface
 * INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL defines the default.
 */
#define INTEL_UFO_GRALLOC_MEDIA_DETAILS_LEVEL 1
typedef struct intel_ufo_buffer_media_details_1_t intel_ufo_buffer_media_details_t;


/**
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_PAVP_SESSION
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_MEDIA_DETAILS
 * \see intel_ufo_buffer_media_details_t.pavp_session
 */
enum {
#if (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 1) || (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 2) || (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 3)
    INTEL_UFO_BUFFER_PAVP_SESSION_MAX = 0xF,
#else  /* INTEL_UFO_GRALLOC_MEDIA_API_STAGE */
    INTEL_UFO_BUFFER_PAVP_SESSION_MAX = 0xFFFFFFFF,
#endif /* INTEL_UFO_GRALLOC_MEDIA_API_STAGE */
};


/**
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_PAVP_SESSION
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_MEDIA_DETAILS
 * \see intel_ufo_buffer_media_details_t.pavp_instance
 */
enum {
#if (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 1)
    INTEL_UFO_BUFFER_PAVP_INSTANCE_MAX = 0xFFFF,
#elif (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 2) || (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 3)
    INTEL_UFO_BUFFER_PAVP_INSTANCE_MAX = 0xF,
#else  /* INTEL_UFO_GRALLOC_MEDIA_API_STAGE */
    INTEL_UFO_BUFFER_PAVP_INSTANCE_MAX = 0xFFFFFFFF,
#endif /* INTEL_UFO_GRALLOC_MEDIA_API_STAGE */
};


/**
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_COLOR_RANGE
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_MEDIA_DETAILS
 * \see intel_ufo_buffer_media_details_t.yuv_color_range
 */
enum {
    INTEL_UFO_BUFFER_COLOR_RANGE_LIMITED = 0u,
    INTEL_UFO_BUFFER_COLOR_RANGE_FULL = 1u,
};


/**
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_COMPR_HINT
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_COMPR_HINT
 */
enum {
    // Render Compression (RC)
    INTEL_UFO_BUFFER_HINT_RC_UNDEFINED          = 0u, // No hint, treat as normal
    INTEL_UFO_BUFFER_HINT_RC_FULL_RESOLVE       = 1u, // Resolve fully
    INTEL_UFO_BUFFER_HINT_RC_PARTIAL_RESOLVE    = 2u, // Resolve to lossless compression
    INTEL_UFO_BUFFER_HINT_RC_DISABLE_RESOLVE    = 3u, // No resolve

    // Memory Media Compression (MMC)
    INTEL_UFO_BUFFER_HINT_MMC_UNCOMPRESSED      = 0u,
    INTEL_UFO_BUFFER_HINT_MMC_COMPRESSED        = 3u,
};


/** Buffer state indicating if AUX resource is in use
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_RESOLVE_DETAILS
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_RESOLVE_DETAILS
 */
enum {
    INTEL_UFO_BUFFER_STATE_AUX_DISABLED            = 0u,
    INTEL_UFO_BUFFER_STATE_NO_CONTENT              = 1u,
    INTEL_UFO_BUFFER_STATE_COMPRESSED              = 2u,
};


/** Structure with additional info about buffer used by 3D driver.
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_RESOLVE_DETAILS
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_RESOLVE_DETAILS
 */
typedef struct intel_ufo_buffer_resolve_details_t
{
    uint32_t magic;             // [in] size of this struct

    uint32_t hint;

    uint32_t state;

    struct {
        struct {
            float red;
            float green;
            float blue;
            float alpha;
        } fast_clear_color;

        int8_t aux_type;
        int8_t aux_resource_type;
        int8_t aux_mode;
        int8_t aux_state;
        int8_t slice_state;
        int8_t resource_type;
    } data;

} intel_ufo_buffer_resolve_details_t;


/** Helper struct - rectangle
 */
typedef struct intel_ufo_rect_t
{
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
} intel_ufo_rect_t;


/** Structure with additional info about buffer used by camera.
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_CAMERA_DETAILS
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_CAMERA_DETAILS
 */
typedef struct intel_ufo_buffer_camera_details_t
{
    uint32_t magic;             // [in] size of this struct

    uint32_t encode;
    uint32_t facing;

    uint32_t roi_num;
    intel_ufo_rect_t roi[32];

} intel_ufo_buffer_camera_details_t;


/** Structure with info about buffer PAVP sesssion.
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_PAVP_SESSION
 * \deprecated
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_QUERY_MEDIA_DETAILS
 */
typedef struct intel_ufo_buffer_pavp_session_t
{
    uint32_t sessionID; // Session ID.
    uint32_t instance;  // Instance.
} intel_ufo_buffer_pavp_session_t;


#if (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 1)

/** This structure defines how datatype bits are used.
 */
typedef union intel_ufo_bo_datatype_t {
    uint32_t value;
    struct {
        uint32_t color_range        :2;
        uint32_t is_updated         :1;
        uint32_t is_encoded         :1;
        uint32_t unused1            :5;
        uint32_t is_encrypted       :1;
        uint32_t unused2            :2;
        uint32_t pavp_session_id    :4;
        uint32_t pavp_instance_id   :16;
    };
} intel_ufo_bo_datatype_t;

#elif (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 2) || (INTEL_UFO_GRALLOC_MEDIA_API_STAGE == 3)

typedef union intel_ufo_bo_datatype_t {
    uint32_t value;
    struct {
        uint32_t compression_hint   :2; // MMC or RC
        uint32_t is_updated         :1;
        uint32_t is_encoded         :1;
        uint32_t is_interlaced      :1;
        uint32_t is_mmc_capable     :1; // MMC
        uint32_t compression_mode   :2; // MMC
        uint32_t color_range        :2;
        uint32_t is_key_frame       :1;
        uint32_t pavp_session_id    :8;
        uint32_t is_encrypted       :1;
        uint32_t pavp_instance_id   :4;
        uint32_t client_id          :8;
    };
} intel_ufo_bo_datatype_t;

#else /* INTEL_UFO_GRALLOC_MEDIA_API_STAGE */

/*
 * At this stage intel_ufo_bo_datatype_t is not exposed by gralloc!
 */
typedef union intel_ufo_bo_datatype_t {
    uint32_t value;
} intel_ufo_bo_datatype_t;

#endif /* INTEL_UFO_GRALLOC_MEDIA_API_STAGE */


#if INTEL_UFO_GRALLOC_PUBLIC_METADATA
/** This structure defines buffer metadata that is used by GrAlloc.
 * \note This is preliminary revision, subject to change.
 * \note For debug purposes only.
 */
typedef struct intel_ufo_buffer_metadata_t {
    uint32_t magic;
    union {
        uint32_t reserved;
        intel_ufo_bo_datatype_t datatype;
    };
    intel_ufo_buffer_details_t details; // \see INTEL_UFO_GRALLOC_METADATA_BUFFER_DETAILS_LEVEL
    intel_ufo_buffer_media_details_t media;
    intel_ufo_buffer_resolve_details_t resolve;
    intel_ufo_buffer_camera_details_t camera;
} intel_ufo_buffer_metadata_t;
// details member may not be the same level API as the defalt (which is INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL)
#define INTEL_UFO_GRALLOC_METADATA_BUFFER_DETAILS_LEVEL INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL
#endif /* INTEL_UFO_GRALLOC_PUBLIC_METADATA */


/** This structure defines private callback API from GrAlloc to HWC.
 * \see gralloc_module_t::perform
 * \see INTEL_UFO_GRALLOC_MODULE_PERFORM_REGISTER_HWC_PROCS
 */
typedef struct intel_ufo_hwc_procs_t
{
    /* This function will be called by gralloc during processing of alloc() request.
     * It will be called after gralloc initially resolves flexible HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED format.
     * It will be called before gralloc issues any allocation calls into kernel driver.
     * If this function returns an error then gralloc will allocate buffer using default settings.
     * \see alloc_device_t::alloc
     *
     * \param procs pointer to struct that was passed during registration
     * \param width pointer to requested buffer width; HWC may increase it to optimize allocation (cursor/full screen)
     * \param height pointer to requested buffer height; HWC may increase it to optimize allocation (cursor/full screen)
     * \param format pointer to effective buffer format; HWC may modify it only if HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
     * \param usage pointer to requested buffer usage; HWC may add new usage flags
     * \param fb_format pointer to FB format to be used by gralloc for this buffer; if set to zero then gralloc will not allocate FB for this buffer.
     * \param flags pointer to flags; TBD
     *
     * \return 0 on success or a negative error code on error.
     *
     * \note this function field is REQUIRED.
     */
    int (*pre_buffer_alloc)(const struct intel_ufo_hwc_procs_t* procs, int *width, int *height, int *format, int *usage, uint32_t *fb_format, uint32_t *flags);

    /* This function will be called by gralloc during processing of alloc() request.
     * It will be called after only after succesfull buffer memory allocation.
     * \see alloc_device_t::alloc
     *
     * \param procs pointer to struct that was passed during registration
     * \param buffer handle to just allocated buffer
     * \param details pointer to buffer details
     *
     * \note this function field is REQUIRED.
     */
    void (*post_buffer_alloc)(const struct intel_ufo_hwc_procs_t* procs, buffer_handle_t, const intel_ufo_buffer_details_t *details);

    /* This function will be called by gralloc during processing of free() request.
     * It will be called after only after succesfull buffer memory allocation.
     * \see alloc_device_t::free
     *
     * \param procs pointer to struct that was passed during registration
     * \param buffer handle to buffer that is about to be released by gralloc
     *
     * \note this function field is REQUIRED.
     */
    void (*post_buffer_free)(const struct intel_ufo_hwc_procs_t* procs, buffer_handle_t);

    /* Reserved for future use. Must be NULL. */
    void *reserved[5];
} intel_ufo_hwc_procs_t;


/**
 * \see intel_ufo_hwc_procs_t::pre_buffer_alloc
 * \see intel_ufo_buffer_details_t::flags
 */

#define INTEL_UFO_BUFFER_FLAG_NONE             0u

/**
 * This flag indicates that buffer was allocated as linear.
 * \see intel_ufo_buffer_details_t::flags
 *
 * HWC can set the INTEL_UFO_BUFFER_FLAG_LINEAR flag to indicate
 * that gralloc should use linear allocation for this buffer.
 * \see intel_ufo_hwc_procs_t::pre_buffer_alloc
 */
#define INTEL_UFO_BUFFER_FLAG_LINEAR  0x00000001u

/**
 * This flag indicates that buffer was allocated as X-tiled.
 * \see intel_ufo_buffer_details_t::flags
 *
 * HWC can set the INTEL_UFO_BUFFER_FLAG_X_TILED flag to indicate
 * that gralloc should use X tiled allocation for this buffer.
 * \see intel_ufo_hwc_procs_t::pre_buffer_alloc
 */
#define INTEL_UFO_BUFFER_FLAG_X_TILED 0x00000002u

/**
 * This flag indicates that buffer was allocated as Y-tiled.
 * \see intel_ufo_buffer_details_t::flags
 *
 * HWC can set the INTEL_UFO_BUFFER_FLAG_Y_TILED flag to indicate
 * that gralloc should use Y tiled allocation for this buffer.
 * \see intel_ufo_hwc_procs_t::pre_buffer_alloc
 */
#define INTEL_UFO_BUFFER_FLAG_Y_TILED 0x00000004u

/**
 * This flag indicates that buffer was allocated as a cursor.
 * \see intel_ufo_buffer_details_t::flags
 *
 * HWC can set the INTEL_UFO_BUFFER_FLAG_CURSOR flag to indicate
 * that gralloc should treat this buffer as cursor allocation.
 * \see intel_ufo_hwc_procs_t::pre_buffer_alloc
 */
#define INTEL_UFO_BUFFER_FLAG_CURSOR  0x10000000u

/**
 * This flag indicates that buffer was allocated as Render Compression ready.
 * \see intel_ufo_buffer_details_t::flags
 *
 * Gralloc will set INTEL_UFO_BUFFER_FLAG_RC flag in pre_buffer_alloc to
 * indicate that RC allocations are enabled in gralloc.
 * HWC can clear the INTEL_UFO_BUFFER_FLAG_RC flag to indicate
 * that gralloc should not try to allocate this buffer as RC-ready.
 * \see intel_ufo_hwc_procs_t::pre_buffer_alloc
 */
#define INTEL_UFO_BUFFER_FLAG_RC      0x20000000u

/**
 * This flag indicates that buffer was allocated as Media Memory Compression ready.
 * \see intel_ufo_buffer_details_t::flags
 *
 * Gralloc will set INTEL_UFO_BUFFER_FLAG_MMC flag in pre_buffer_alloc to
 * indicate that MMC allocations are enabled in gralloc.
 * HWC can clear the INTEL_UFO_BUFFER_FLAG_MMC flag to indicate
 * that gralloc should not try to allocate this buffer as MMC-ready.
 * \see intel_ufo_hwc_procs_t::pre_buffer_alloc
 */
#define INTEL_UFO_BUFFER_FLAG_MMC     0x40000000u


/**
 * Driver allocation of USAGE flags
 * \see GRALLOC_USAGE_PRIVATE_<n>
 */

/**
 * This flag indicates front buffer rendering, set by clients, consumed by
 * hardware composer for dedicated plane allocation.
 *
 * TODO: Remap this name in the event of the public API including it.
 */

#define INTEL_UFO_GRALLOC_USAGE_PRIVATE_FBR       GRALLOC_USAGE_PRIVATE_0


// deprecation internals...
#if INTEL_UFO_GRALLOC_DEPRECATE_FLINK
static const int INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_NAME __attribute__((deprecated)) = INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_NAME_DEPRECATED;
#endif

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* INTEL_UFO_GRALLOC_H */
