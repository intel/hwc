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

#ifndef INTEL_UFO_HWC_COMMON_H
#define INTEL_UFO_HWC_COMMON_H

#include "Debug.h"

/* for PRI?64 */
#if !defined(__STDC_FORMAT_MACROS)
#define __STDC_FORMAT_MACROS 1
#endif
#include <inttypes.h>


#define INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT  HAL_PIXEL_FORMAT_RGBA_8888
#define INTEL_HWC_DEFAULT_REFRESH_RATE      60
#define INTEL_HWC_DEFAULT_REFRESH_PERIOD_NS (1000000000/(INTEL_HWC_DEFAULT_REFRESH_RATE))
#define INTEL_HWC_DEFAULT_BITS_PER_CHANNEL  16
#define INTEL_HWC_DEFAULT_INTERNAL_PANEL_DPI   160
#define INTEL_HWC_DEFAULT_EXTERNAL_DISPLAY_DPI 75

#define HWC_UNUSED(x)   ((void)&(x))
#define UNUSED          __attribute__ ((unused))

#define ALWAYS_INLINE   __attribute__ ((always_inline))
#define NEVER_INLINE    __attribute__ ((noinline))

namespace intel {
namespace ufo {
namespace hwc {

#if INTEL_HWC_LOGVIEWER_BUILD
const static bool sbLogViewerBuild = true;
#else
const static bool sbLogViewerBuild = false;
#endif

#if INTEL_HWC_INTERNAL_BUILD
const static bool sbInternalBuild = true;
#else
const static bool sbInternalBuild = false;
#endif

// This constant is used to indicate the maximum supported physical displays.
// This must be sufficient to cover panels, externals, virtuals, fakes and proxies etc.
const static unsigned int cMaxSupportedPhysicalDisplays = 8;

// This constant is used to indicate the maximum number of logical displays.
// A logical display can mux/demux between SurfaceFlinger displays and physical displays.
// A pool of logical displays can be created of which only some will be made available to SurfaceFlinger.
const static unsigned int cMaxSupportedLogicalDisplays  = 8;

// This constant is used to indicate the maximum supported displays from SurfaceFlinger.
// TODO: Should come via a SF constant.
const static unsigned int cMaxSupportedSFDisplays       = 3;


// Some generic constants
enum
{
    INVALID_DISPLAY_ID = 0xFFFF,  // Display ID used to mean uninitialized or unspecified display index
};

// Display types.
enum EDisplayType
{
    eDTPanel,
    eDTExternal,
    eDTVirtual,
    eDTWidi,
    eDTFake,
    eDTUnspecified,
};

inline String8 dumpDisplayType( EDisplayType eDT )
{
#define DISPLAYTYPE_TO_STRING( A ) case eDT##A: return String8( #A );
    switch( eDT )
    {
        DISPLAYTYPE_TO_STRING( Panel       );
        DISPLAYTYPE_TO_STRING( External    );
        DISPLAYTYPE_TO_STRING( Virtual     );
        DISPLAYTYPE_TO_STRING( Widi        );
        DISPLAYTYPE_TO_STRING( Fake        );
        DISPLAYTYPE_TO_STRING( Unspecified );
    }
#undef DISPLAYTYPE_TO_STRING
    return String8( "<?>" );
};

/**
 */
class NonCopyable
{
protected:
    NonCopyable() {}
private:
    NonCopyable(NonCopyable const&) = delete;
    void operator=(NonCopyable const&) = delete;
};

/**
 */
template <typename T>
static inline T min(T a, T b) { return a < b ? a : b; }

/**
 */
template <typename T>
static inline T max(T a, T b) { return a > b ? a : b; }


// Generic swap template.
// It may be possible to improve implementation with && and rvalue, but favor a simple implementation.
template <typename T>
static inline void swap(T &a, T &b)
{
    T tmp = a;
    a = b;
    b = tmp;
}

// Alignment template. Align must be a power of 2.
template <typename T>
inline T alignTo(T value, T align)
{
    ALOG_ASSERT(align > 1 && !(align & (align - 1)));
    return (value + (align-1)) & ~(align-1);
}


static inline bool operator==(const hwc_rect_t& a, const hwc_rect_t& b)
{
    return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
}

static inline bool operator!=(const hwc_rect_t& a, const hwc_rect_t& b)
{
    return !(a == b);
}

static inline bool operator==(const hwc_frect_t& a, const hwc_frect_t& b)
{
    return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
}

static inline bool operator!=(const hwc_frect_t& a, const hwc_frect_t& b)
{
    return !(a == b);
}

} // namespace hwc
} // namespace ufo
} // namespace intel


#endif // INTEL_UFO_HWC_COMMON_H
