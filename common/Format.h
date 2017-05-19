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

#ifndef INTEL_UFO_HWC_FORMAT_H
#define INTEL_UFO_HWC_FORMAT_H

#include <ufo/graphics.h>

namespace intel {
namespace ufo {
namespace hwc {

// Bitmasks describing the tiling capabilities of the device
enum ETilingFormat
{
    TILE_UNKNOWN    = 0,
    TILE_LINEAR     = 1 << 0,
    TILE_X          = 1 << 1,
    TILE_Y          = 1 << 2,
    TILE_Yf         = 1 << 3,
    TILE_Ys         = 1 << 4,
};

// Values describing the compression capabilities of the device
// ECompressionType is defined in platform specific code only.
// ECompressionType is forward declared here and used only with the constants
// defined here in common code.  This allows common code to compare and pass around
// values safely without knowing the enum.
enum class ECompressionType;
const ECompressionType COMPRESSION_NONE = static_cast<ECompressionType>(0);
const ECompressionType COMPRESSION_ARCH_START = static_cast<ECompressionType>(1);

// Note, define blending modes as a bitfield (for PlaneCaps support)
enum class EBlendMode : uint32_t {
    NONE          = 0, // No Blending
    PREMULT       = 1, // ONE / ONE_MINUS_SRC_ALPHA
    COVERAGE      = 2, // SRC_ALPHA / ONE_MINUS_SRC_ALPHA
};
const uint32_t BLENDING_MASK = static_cast<uint32_t>(EBlendMode::PREMULT) |
                               static_cast<uint32_t>(EBlendMode::COVERAGE);


enum class ETransform : uint32_t {
    NONE          = 0, // No transform
    FLIP_H        = 1, // flip source image horizontally
    FLIP_V        = 2, // flip source image vertically
    ROT_90        = 4, // Rotate image by 90
    ROT_180       = 3, // Rotate image by 180
    ROT_270       = 7, // Rotate image by 270
    FLIP_H_90     = 5,
    FLIP_V_90     = 6,
};
inline bool isTranspose(ETransform t)   { return static_cast<uint32_t>(t) &  static_cast<uint32_t>(ETransform::ROT_90); }
inline bool isFlipH(ETransform t)       { return static_cast<uint32_t>(t) &  static_cast<uint32_t>(ETransform::FLIP_H); }
inline bool isFlipV(ETransform t)       { return static_cast<uint32_t>(t) &  static_cast<uint32_t>(ETransform::FLIP_V); }

// Buffering mode hint flags.
enum EBufferModeFlags
{
    FRONT_BUFFER_RENDER = (1 << 0)  // Rendering may occur to the current presented buffer.
};

enum class EDataSpaceStandard : uint8_t {
    Unspecified               = 0,
    BT709                     = 1,
    BT601_625                 = 2,
    BT601_625_UNADJUSTED      = 3,
    BT601_525                 = 4,
    BT601_525_UNADJUSTED      = 5,
    BT2020                    = 6,
    BT2020_CONSTANT_LUMINANCE = 7,
    BT470M                    = 8,
    FILM                      = 9,
};

enum class EDataSpaceTransfer : uint8_t {
    Unspecified               = 0,
    Linear                    = 1,
    SRGB                      = 2,
    SMPTE_170M                = 3,
    GAMMA2_2                  = 4,
    GAMMA2_8                  = 5,
    ST2084                    = 6,
    HLG                       = 7,
};

enum class EDataSpaceRange : uint8_t {
    Unspecified               = 0,
    Full                      = 1,
    Limited                   = 2,
};

enum class EDataSpaceCustom : uint16_t {
    Unspecified               =      0,
    Arbitrary                 =      1,
    Depth                     = 0x1000,
};

struct DataSpace {
    EDataSpaceCustom   custom   : 16;
    EDataSpaceStandard standard :  6;
    EDataSpaceTransfer transfer :  5;
    EDataSpaceRange    range    :  3;
};

static inline bool operator==(const DataSpace a, const DataSpace b)
{
    return a.custom == b.custom && a.standard == b.standard && a.transfer == b.transfer && a.range == b.range;
}

static inline bool operator!=(const DataSpace a, const DataSpace b)
{
    return !(a == b);
}

// Common dataspace constants
const DataSpace DataSpace_Unknown     { .custom = EDataSpaceCustom::Unspecified };
const DataSpace DataSpace_Arbitrary   { .custom = EDataSpaceCustom::Arbitrary };

const DataSpace DataSpace_SRGB_Linear { .standard = EDataSpaceStandard::BT709,     .transfer = EDataSpaceTransfer::Linear,     .range = EDataSpaceRange::Full };
const DataSpace DataSpace_SRGB        { .standard = EDataSpaceStandard::BT709,     .transfer = EDataSpaceTransfer::SRGB,       .range = EDataSpaceRange::Full };
const DataSpace DataSpace_JFIF        { .standard = EDataSpaceStandard::BT601_625, .transfer = EDataSpaceTransfer::SMPTE_170M, .range = EDataSpaceRange::Full };
const DataSpace DataSpace_BT601_625   { .standard = EDataSpaceStandard::BT601_625, .transfer = EDataSpaceTransfer::SMPTE_170M, .range = EDataSpaceRange::Limited };
const DataSpace DataSpace_BT601_525   { .standard = EDataSpaceStandard::BT601_525, .transfer = EDataSpaceTransfer::SMPTE_170M, .range = EDataSpaceRange::Limited };
const DataSpace DataSpace_BT709       { .standard = EDataSpaceStandard::BT709,     .transfer = EDataSpaceTransfer::SMPTE_170M, .range = EDataSpaceRange::Limited };

// Non-color
const DataSpace DataSpace_Depth       { .custom = EDataSpaceCustom::Depth };


// Utility function - returns human-readable string from a HAL format number.
const char* getHALFormatString( int32_t halFormat );
const char* getHALFormatShortString( int32_t halFormat );
// Utility function - returns human-readable string from a DRM format number.
const char* getDRMFormatString( int32_t drmFormat );
// Utility function - returns human-readable string from a Tiling format number.
const char* getTilingFormatString( ETilingFormat halFormat );
// Utility function - returns human-readable string from a Dataspace number.
android::String8 getDataSpaceString( DataSpace dataspace );


} // namespace hwc
} // namespace ufo
} // namespace intel


#endif // INTEL_UFO_HWC_FORMAT_H
