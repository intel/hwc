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

#ifndef __IVP_INTEL_H__
#define __IVP_INTEL_H__

__BEGIN_DECLS

/*
  Advance filter bit masks
  Use these masks to set filter bits in iVP_layer_t.VPfilters
  Ex. to enable DN filter.
    iVP_layer_t.VPfilters       |= FILTER_DENOISE;      // turn on the filter
    iVP_layer_t.fDenoiseFactor   = 1.0;                 // set DN parameter
 */
#define FILTER_DENOISE              0x0001             // Denoise filter bit mask is 1
#define FILTER_DEINTERLACE          0x0002             // Deinterlace filter bit masks is 2.
#define FILTER_SHARPNESS            0x0004             // Sharpness filter bit masks is 4.
#define FILTER_AUTOCONTRAST         0x0008             // AutoContrast enchancement bit mask is 8.
#define FILTER_3P                   0x0010             // 3P filter bit masks is 16.
#define FILTER_COLORBALANCE         0x0020             // Colorbalance filter bit masks is 32.
#define FILTER_SKINTONEENHANCEMENT  0x0040             // Skintone enchancement bit mask is 64.
#define FILTER_TOTALCOLORCORRECTION 0X0080             // TotalColorCorrection bit masks is 128
#define FILTER_IMAGESTABILIZATION   0x0100			   // Image Stabilization
#define FILTER_FRAMERATECONVERSION  0x0200			   // Frame Rate Conversion
#define FILTER_MMC_DECOMPRESS       0x0400			   // Media Memory Compression

#define FILTER_COLORBALANCE_PARAM_SIZE         4       // Colorbalance filter parameters size is 4.
#define FILTER_TOTALCOLORCORRECTION_PARAM_SIZE 6       // TotalColorCorrection filter parameter size is 6

// width and height could be any value, it is just used by VAAPI.
#define IVP_DEFAULT_WIDTH    1280
#define IVP_DEFAULT_HEIGHT   720

#define IVP_SUPPORTS_LEVEL_EXPANSION    1      // flag to indicate whether level expansion is supported

typedef enum _IVP_CAPABLILITY{
    IVP_DEFAULT_CAPABLILITY = 0,   // All supported VEBOX feature
    IVP_3P_CAPABILITY              // 3P + All supported VEBOX feature
}IVP_CAPABILITY;

//Status Code for iVP
typedef enum _IVP_STATUS
{
    IVP_STATUS_ERROR   = -1,                  // error result
    IVP_STATUS_SUCCESS = 0x00000000,          // successful result
    IVP_ERROR_OUT_OF_MEMORY,                  // out of memory
    IVP_ERROR_INVALID_CONTEXT,                // invalid context content
    IVP_ERROR_INVALID_OPERATION,              // invalid operation
    IVP_ERROR_INVALID_PARAMETER,              // invalid parameters
    IVP_ERROR_NOT_SUPPORTED,                  // unsupported CAPABILITY
}iVP_status;

//Avaiable iVP buffer types
typedef enum _IVP_BUFFER_TYPE
{
    IVP_INVALID_HANDLE,                       // invalid buffer handle
    IVP_GRALLOC_HANDLE,                       // gralloc buffer handle
    IVP_DRM_FLINK,                            // drm buffer flink
}iVP_buffer_type;

//iVP rectangle definition
typedef struct _IVP_RECT
{
    int left;                                 // X value of upper-left point
    int top;                                  // Y value of upper-left point
    int width;                                // width of the rectangle
    int height;                               // height of the rectangle
} iVP_rect_t;

//Avaiable rotation types
typedef enum
{
    IVP_ROTATE_NONE = 0,                      // VA_ROTATION_NONE which is defined in va.h
    IVP_ROTATE_90   = 1,                      // VA_ROTATION_90 which is defined in va.h
    IVP_ROTATE_180  = 2,                      // VA_ROTATION_180 which is defined in va.h
    IVP_ROTATE_270  = 3,                      // VA_ROTATION_270 which is defined in va.h
} iVP_rotation_t;

//Avaiable flip types
typedef enum
{
    IVP_FLIP_NONE = 0,                        // VA_MIRROR_NONE which is defined in va.h
    IVP_FLIP_H    = 1,                        // VA_MIRROR_HORIZONTAL which is defined in va.h
    IVP_FLIP_V    = 2,                        // VA_MIRROR_VERTICAL which is defined in va.h
} iVP_flip_t;

//Avaiable filter types
typedef enum
{
    IVP_FILTER_HQ   = 0,                      // high-quality filter (AVS scaling)
    IVP_FILTER_FAST = 1,                      // fast filter (bilinear scaling)
} iVP_filter_t;

// Avaiable blending types
typedef enum
{
    /// No blending
    IVP_BLEND_NONE = 0,

    /// There is blending, the source pixel is assumed to be
    /// alpha-premultiplied and the effective alpha is "Sa Pa" (per-pixel
    /// alpha times the per-plane alpha). The implemented equation is:
    ///
    /// Drgba' = Srgba Pa + Drgba (1 - Sa Pa)
    ///
    /// Where
    ///     Drgba' is the result of the blending,
    ///     Srgba is the premultiplied source colour,
    ///     Pa is a per-plane defined alpha (the "alpha" field in iVP_layer_t),
    ///     Drgba is the framebuffer content prior to the blending,
    ///     Sa is the alpha component from the Srgba vector.
    ///
    /// Note that Pa gets out of the equation when its value is 1.0.
    IVP_ALPHA_SOURCE_PREMULTIPLIED_TIMES_PLANE,

    /// There is blending, the source pixel is assumed to be premultiplied
    /// and the effective alpha is "Sa" (per-pixel alpha). The implemented
    /// equation is:
    ///
    /// Drgba' = Srgba + Drgba (1 - Sa)
    ///
    /// Where
    ///     Drgba' is the result of the blending,
    ///     Srgba is the premultiplied source colour,
    ///     Drgba is the framebuffer content prior to the blending,
    ///     Sa is the alpha component from the Srgba vector.
    IVP_ALPHA_SOURCE_PREMULTIPLIED,

    /// There is blending, the source pixel is assumed to be
    /// non alpha-premultiplied and the effective alpha is "Pa" (per-plane
    /// alpha). The implemented equation is:
    ///
    /// Drgba' = Srgba Pa + Drgba (1 - Pa)
    ///
    /// Where
    ///     Drgba' is the result of the blending,
    ///     Srgba is the non-premultiplied source colour,
    ///     Pa is a per-plane defined alpha (the "alpha" field in iVP_layer_t),
    ///     Drgba is the framebuffer content prior to the blending.
    ///
    /// Note that the per-pixel alpha is ignored, up to the point that
    /// the source alpha is not even implicitly present premultipliying
    /// the source colour.
    IVP_ALPHA_SOURCE_CONSTANT,

} iVP_blend_t;

// Avaiable deinterlace type
typedef enum
{
    IVP_DEINTERLACE_BOB = 0,                    // BOB DI
    IVP_DEINTERLACE_ADI = 1,                    // ADI
} iVP_deinterlace_mode_t;

// Avaiable sample types
typedef enum
{
    IVP_SAMPLETYPE_PROGRESSIVE = 0,             // progressive sample
    IVP_SAMPLETYPE_TOPFIELD,                    // top filed sample
    IVP_SAMPLETYPE_BOTTOMFIELD                  // bottom filed sample
} iVP_sample_type_t;

typedef enum
{
    IVP_STREAM_TYPE_NORMAL = 0,                 //  normal video clip
    IVP_STREAM_TYPE_CAMERA,                     // Camera recorded video
    IVP_STREAM_TYPE_MAX                         // Maximum type supported
} iVP_streamtype_t;

typedef union _ivp_kerneldump_t
{
    struct
    {
        unsigned int    perf    :1;              //dump FPS, kernel exec time
        unsigned int    surface :1;              //dump input/output surface
    };
    unsigned int value;
}iVP_kernel_dump_t;

// 3P plug-in info
typedef struct _IVP_3P_INFO_T
{
    bool                    bEnable3P;            // enable 3P filter
    iVP_streamtype_t        stStreamType;         // Camera, VideoEditor
    float                   fFrameRate;           // framerate of the stream
    bool                    bReconfig;            // reconfig 3P plug-in. For debugging only and ignored for production
    iVP_kernel_dump_t       eKernelDumpBitmap;    // enable kernel runtime dump. For debugging only and ignored for production
} iVP_3p_info_t;

//deinterlace parameter
typedef struct _IVP_DEINTERLACE_T
{
    bool                    bBottomFieldFirstFlag; // bottom field first, if this is not set then assumes top field first.
    bool                    bBottomFieldFlag;      // Bottom field used in deinterlacing. if this is not set then assumes top field is used.
    bool                    bOneFieldFlag;         // A single field is stored in the input frame. if this is not set then assumes the frame contains two interleaved fields.
    iVP_deinterlace_mode_t  eDeinterlaceMode;      // deinterlace algorithm mode
}iVP_deinterlace_t;

typedef struct _IVP_TOTOALCOLORCORRECTION_T
{
	float					fRed;				   // TotalColorCorrection Red value
	float					fGreen; 			   // TotalColorCorrection Green value
	float					fBlue;				   // TotalColorCorrection Blue value
	float					fCyan;				   // TotalColorCorrection Cyan value
	float					fMagenta;			   // TotalColorCorrection Magenta value
	float					fYellow;			   // TotalColorCorrection Yellow value

}iVP_TotalColorCorrection_t;

typedef enum _IVP_IMAGESTABILIZATION_MODE_T
{
    IVP_IMAGESTABILIZATION_MODE_NONE = 0,
    IVP_IMAGESTABILIZATION_MODE_CROP,              //crops the frame by the app provided percentage
    IVP_IMAGESTABILIZATION_MODE_MINZOOM,           //crops and then upscales the frame to half the black boundary
    IVP_IMAGESTABILIZATION_MODE_FULLZOOM,          //crops and upscales the frame to original size
    IVP_IMAGESTABILIZATION_MODE_COUNT              // number of Image Stabilization Type
} iVP_imageStabilization_mode_t;

typedef struct _IVP_IMAGESTABILIZATION_T
{
    iVP_imageStabilization_mode_t	  eMode;
    float							  fCrop; //crop percentage
    unsigned int					  uPerfType;
}iVP_ImageStabilization_t;

typedef struct _IVP_FRAMERATECONVERSION_T
{
    unsigned int					  uInputFps;
    unsigned int					  uOutputFps;
	unsigned int					  uCyclicCounter;
    bool							  bFrameRepeat;
}iVP_FrameRateConversion_t;

// Available color range types
typedef enum
{
    IVP_COLOR_RANGE_NONE = 0,                 // default value
    IVP_COLOR_RANGE_PARTIAL,                  // all set partial range
    IVP_COLOR_RANGE_FULL                      // all set full range
} iVP_color_range_t;

// Avalible color standard types
typedef enum _IVP_COLOR_STANDARD_T
{
  IVP_COLOR_STANDARD_NONE = 0,
  IVP_COLOR_STANDARD_BT601,                                     //brief ITU-R BT.601.
  IVP_COLOR_STANDARD_BT709,                                     //brief ITU-R BT.709.
  IVP_COLOR_STANDARD_BT2020,                                    //brief ITU-R BT.2020.
}iVP_color_standard_t;

// layer information pass to iVP
typedef struct _IVP_LAYER_T
{
    union
    {
        buffer_handle_t        gralloc_handle;                   // buffer is allocated from gralloc
        int                    gem_handle;                       // buffer is allocated from drm directly
    };

    iVP_buffer_type            bufferType;                       // input buffer type
    iVP_rect_t                 *srcRect;                         // source rectangle
    iVP_rect_t                 *destRect;                        // dest rectangle
    iVP_rotation_t             rotation;                         // rotation info
    iVP_flip_t                 flip;                             // flip info
    iVP_filter_t               filter;                           // filtering quality
    iVP_color_range_t          colorRange;                       // color range
    iVP_color_standard_t       colorStandard;                    // color standard;
    iVP_blend_t                blend;                            // blending mode
    union {
        float                  alpha;                            // Plane alpha
        float                  blend_global_alpha;               // Deprecated name for the alpha field
    };
    float                      blend_min_luma;                   // Minimum luma value
    float                      blend_max_luma;                   // Maximum luma value

    iVP_sample_type_t          sample_type;                      // BOB DI

	buffer_handle_t            *pBackwardReferences;             //backward references handle
	int                        iNumBackwardReferences;           //number of backward references
    buffer_handle_t            *pForwardReferences;              //forward references handle
	int                        iNumForwardReferences;            //number of backward references

    long long int              VPfilters;                        // VP filter

    float                      fDenoiseFactor;                   // DN VP filter parameter

    float                      fSkinToneEnchancementFactor;      // SkinTone VP filter parameter

    iVP_deinterlace_t          stDeinterlaceParameter;           // DI VP filter parameter

    float                      fSharpnessFactor;                 // sharpness VP filter parameter

    float                      fColorBalanceHue;                 // ColorBalance Hue value
    float                      fColorBalanceSaturation;          // ColorBalance saturation value
    float                      fColorBalanceBrightness;          // ColorBalance brightness value
    float                      fColorBalanceContrast;            // ColorBalance Contrast value

    iVP_TotalColorCorrection_t stTotalColorCorrectionParameter;  // TotoalColorCorrection parameter

    iVP_3p_info_t              st3pInfo;                         // 3P plug-in info

    iVP_ImageStabilization_t   stImageStabilizationParameter;    // Image Stabilization
    iVP_FrameRateConversion_t  stFrameRateConversionParameter;   // Frame Rate Conversion

}iVP_layer_t;

typedef unsigned int iVPCtxID;                        //Context of the iVP


//*****************************************************************************
//
// iVP_create_context
//  @param: ctx[OUT]  iVP Context ID
//          width     should be any non-zero value
//          height    should be any non-zero value
//
//*****************************************************************************
iVP_status iVP_create_context(iVPCtxID *ctx, unsigned int width, unsigned int height, unsigned int vpCapabilityFlag);

//*****************************************************************************
//
//  @Function iVP_exec
//                  Supports CSC/Scaling/Composition/AlphaBlending/Sharpnes/procamp
//  @param:
//       ctx:       iVP Context ID
//       primary:   primary surface for VPP
//       subSurfs:  sub surfaces for composition [optional for CSC/Scaling]
//       numOfSubs: number of sub surfaces
//       outSurf:   output buffer for VP
//       syncFlag:  determine whether need to do vaSyncSurface
//*****************************************************************************
iVP_status iVP_exec(iVPCtxID     *ctx,
        iVP_layer_t        *primarySurf,
        iVP_layer_t        *subSurfs,
        unsigned int       numOfSubs,
        iVP_layer_t        *outSurf,
        bool               syncFlag
        );
//*****************************************************************************
//
//  @Function iVP_exec_multiOut
//                  Support multiple output, but just support widi dual output now
//  @param:
//       ctx:       iVP Context ID
//       primary:   primary surface for VPP
//       subSurfs:  sub surfaces for composition [optional for CSC/Scaling]
//       numOfSubs: number of sub surfaces
//       outSurfs:  output buffer for VP
//       numOfOuts: number of out surfaces
//       syncFlag:  determine whether need to do vaSyncSurface
//*****************************************************************************
iVP_status iVP_exec_multiOut(iVPCtxID *ctx,
        iVP_layer_t       *primarySurf,
        iVP_layer_t        *subSurfs,
        unsigned int       numOfSubs,
        iVP_layer_t        *outSurfs,
        unsigned int       numOfOuts,
        bool               syncFlag
        );

//*****************************************************************************
//
// iVP_destroy_context
//  @param:
//       ctx  iVP Context ID
//
//
//*****************************************************************************
iVP_status iVP_destroy_context(iVPCtxID *ctx);

__END_DECLS
#endif /* __IVP_INTEL_VAAPI_H__ */

