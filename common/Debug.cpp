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

#include "Common.h"
#include "Layer.h"
#include "Format.h"
#include "Timing.h"

#include <system/graphics.h>
#include <ufo/graphics.h>
#include <drm_fourcc.h>
#include <utils/Mutex.h>

namespace intel {
namespace ufo {
namespace hwc {

String8 printLayer(hwc_layer_1_t& hwc_layer)
{
    if (!sbInternalBuild)
        return String8();

    // Composition type isnt part of the Layer state. Add it explicitly
    const char* pCompositionType;
    switch (hwc_layer.compositionType)
    {
    case HWC_FRAMEBUFFER:
        pCompositionType = "FB";
        break;
    case HWC_BACKGROUND:
        pCompositionType = "BG";
        break;
    case HWC_OVERLAY:
        pCompositionType = "OV";
        break;
    case HWC_FRAMEBUFFER_TARGET:
        pCompositionType = "TG";
        break;
#if defined(HWC_DEVICE_API_VERSION_1_4)
    case HWC_SIDEBAND:
        pCompositionType = "SB";
        break;
    case HWC_CURSOR_OVERLAY:
        pCompositionType = "CS";
        break;
#endif
    default:
        pCompositionType = "  ";
        break;
    }

    Layer layer(hwc_layer);
    return layer.dump(pCompositionType);
}

/** Dump display content
 * \see Hwc::onSet
 * \see Hwc::onPrepare
 */
void dumpDisplayContents(
        const char *pIdentifier,
        hwc_display_contents_1_t* pDisp,
        uint32_t frameIndex)
{
    if (!sbInternalBuild)
        return;

    ALOG_ASSERT(pDisp);

    // Debug - report to logcat the status of everything that we are being asked to set.
    ALOGD("%s frame:%u retireFenceFd:%d outbuf:%p outbufAcquireFenceFd:%d flags:%x numHwLayers:%zd",
        pIdentifier, frameIndex, pDisp->retireFenceFd, pDisp->outbuf, pDisp->outbufAcquireFenceFd, pDisp->flags, pDisp->numHwLayers);

    for (size_t l = 0; l < pDisp->numHwLayers; l++)
    {
        hwc_layer_1_t& layer = pDisp->hwLayers[l];

        String8 debug = printLayer(layer);
        ALOGD(" %zd %s", l, debug.string());
    }
}

void dumpDisplaysContents(
        const char *pIdentifier,
        size_t numDisplays,
        hwc_display_contents_1_t** displays,
        uint32_t frameIndex)
{
    if (!sbInternalBuild)
        return;

    for (size_t d = 0; d < numDisplays; d++)
    {
        hwc_display_contents_1_t* pDisp = displays[d];
        if (!pDisp)
            continue;
        dumpDisplayContents(String8::format("%s Display:%zd", pIdentifier, d), pDisp, frameIndex);
    }
}

// TODO: Port to Layer
#if INTEL_HWC_INTERNAL_BUILD && 0

void dumpLayersToDisk( const char* pchPrefix,
                       const hwc_layer_1_t* pLayers,
                       const uint32_t numHwLayers,
                       const uint32_t layerMask,
                       uint32_t outputDumpMask )
{
    String8 filename = String8::format( DEBUG_DUMP_DISK_ROOT "%s.txt", pchPrefix );
    FILE* fp = fopen( filename, "wt" );
    if ( fp )
    {
        fputs( pchPrefix, fp );
        fputs( "\n", fp );
    }
    else
    {
        ALOGE( "Failed to open output file %s", filename.string() );
    }
    for (size_t ly = 0; ly < numHwLayers; ly++)
    {
        if ( !( layerMask & (1<<ly) ) )
            continue;
        const hwc_layer_1_t& layer = pLayers[ly];
        filename = String8::format( "%s_L%zu", pchPrefix, ly );
        if ( fp )
        {
            String8 desc = printLayer( layer );
            String8 info = String8::format( "%s Layer %zd: %s [filename:%s]", pchPrefix, ly, desc.string( ), filename.string( ) );
            fputs( info.string( ), fp );
            ALOGD_IF( DUMP_BUFFER_DEBUG, "%s", info.string( ) );
            fputs( "\n", fp );
        }
        if ( !dumpGrallocBufferToDisk( filename, layer.handle, outputDumpMask ) && fp )
        {
            fputs( "!ERROR!\n", fp );
        }
    }
    if ( fp )
    {
        fputs( "--END--\n", fp );
        fclose(fp);
    }
}

#endif // INTEL_HWC_INTERNAL_BUILD


// Return a 5 character format name where possible
const char* getHALFormatShortString( int32_t halFormat )
{
#define HAL_FMT_CASE(F, S)  case HAL_PIXEL_FORMAT_##F : return S;
    switch ( halFormat )
    {
        HAL_FMT_CASE( RGBA_8888,                            "RGBA")
        HAL_FMT_CASE( RGBX_8888,                            "RGBX")
        HAL_FMT_CASE( RGB_888,                              "RGB ")
        HAL_FMT_CASE( RGB_565,                              "565 ")
        HAL_FMT_CASE( BGRA_8888,                            "BGRA")
        HAL_FMT_CASE( YV12,                                 "YV12")
        HAL_FMT_CASE( BLOB,                                 "BLOB")
        HAL_FMT_CASE( IMPLEMENTATION_DEFINED,               "IMPL")
        HAL_FMT_CASE( YCbCr_422_SP,                         "422s")
        HAL_FMT_CASE( YCrCb_420_SP,                         "420s")
        HAL_FMT_CASE( YCbCr_422_I,                          "422i")
        HAL_FMT_CASE( NV12_X_TILED_INTEL,                   "NV12X")
        HAL_FMT_CASE( NV12_Y_TILED_INTEL,                   "NV12Y")
        HAL_FMT_CASE( NV12_LINEAR_PACKED_INTEL,             "NV12P")
        HAL_FMT_CASE( NV12_LINEAR_INTEL,                    "NV12L")
        HAL_FMT_CASE( NV12_LINEAR_CAMERA_INTEL,             "NV12C")
        HAL_FMT_CASE( YUV420PackedSemiPlanar_Tiled_INTEL,   "NV12T")
        HAL_FMT_CASE( YUV420PackedSemiPlanar_INTEL,         "NV12L")
        HAL_FMT_CASE( A2R10G10B10_INTEL,                    "A2RGB")
        HAL_FMT_CASE( A2B10G10R10_INTEL,                    "A2BGR")
        HAL_FMT_CASE( P010_INTEL,                           "P010")
    default:
            return "???";
    }
#undef HAL_FMT_CASE
}

const char* getHALFormatString( int32_t halFormat )
{
#define HAL_FMT_CASE(F)  case HAL_PIXEL_FORMAT_##F : return #F;
    switch ( halFormat )
    {
        HAL_FMT_CASE( RGBA_8888 )
        HAL_FMT_CASE( RGBX_8888 )
        HAL_FMT_CASE( RGB_888 )
        HAL_FMT_CASE( RGB_565 )
        HAL_FMT_CASE( BGRA_8888 )
        HAL_FMT_CASE( YV12 )
        HAL_FMT_CASE( BLOB )
        HAL_FMT_CASE( IMPLEMENTATION_DEFINED )
        HAL_FMT_CASE( YCbCr_422_SP )
        HAL_FMT_CASE( YCrCb_420_SP )
        HAL_FMT_CASE( YCbCr_422_I )
        HAL_FMT_CASE( NV12_X_TILED_INTEL )
        HAL_FMT_CASE( NV12_Y_TILED_INTEL )
        HAL_FMT_CASE( NV12_LINEAR_PACKED_INTEL )
        HAL_FMT_CASE( NV12_LINEAR_INTEL )
        HAL_FMT_CASE( NV12_LINEAR_CAMERA_INTEL )
        HAL_FMT_CASE( YUV420PackedSemiPlanar_Tiled_INTEL )
        HAL_FMT_CASE( YUV420PackedSemiPlanar_INTEL )
        HAL_FMT_CASE( A2R10G10B10_INTEL )
        HAL_FMT_CASE( A2B10G10R10_INTEL )
        HAL_FMT_CASE( P010_INTEL )
    default:
            return "???";
    }
#undef HAL_FMT_CASE
}

const char* getDRMFormatString( int32_t drmFormat )
{
#define DRM_FMT_CASE(F)  case DRM_FORMAT_##F : return #F;
    switch ( drmFormat )
    {
        // Formats supported with gralloc HAL mappings at time of writing:
        DRM_FMT_CASE( ABGR8888    )
        DRM_FMT_CASE( XBGR8888    )
        DRM_FMT_CASE( ARGB8888    )
        DRM_FMT_CASE( BGR888      )
        DRM_FMT_CASE( RGB565      )
        DRM_FMT_CASE( NV12        )
        DRM_FMT_CASE( YUYV        )
        // Misc variants:
        DRM_FMT_CASE( RGB888      )
        DRM_FMT_CASE( XRGB8888    )
        DRM_FMT_CASE( RGBX8888    )
        DRM_FMT_CASE( BGRX8888    )
        DRM_FMT_CASE( RGBA8888    )
        DRM_FMT_CASE( BGRA8888    )
        DRM_FMT_CASE( YVYU        )
        DRM_FMT_CASE( UYVY        )
        DRM_FMT_CASE( VYUY        )
        DRM_FMT_CASE( XRGB2101010 )
        DRM_FMT_CASE( XBGR2101010 )
        DRM_FMT_CASE( RGBX1010102 )
        DRM_FMT_CASE( BGRX1010102 )
        DRM_FMT_CASE( ARGB2101010 )
        DRM_FMT_CASE( ABGR2101010 )
        DRM_FMT_CASE( RGBA1010102 )
        DRM_FMT_CASE( BGRA1010102 )
    default:
        return String8::format("?=%x(%c%c%c%c)", drmFormat, ((drmFormat >> 0) & 0xff), ((drmFormat >> 8) & 0xff), ((drmFormat >> 16) & 0xff), ((drmFormat >> 24) & 0xff));
    }
#undef DRM_FMT_CASE
}

const char* getTilingFormatString( ETilingFormat tileFormat )
{
    switch(tileFormat)
    {
        case TILE_UNKNOWN: return "?";
        case TILE_LINEAR: return "L";
        case TILE_X: return "X";
        case TILE_Y: return "Y";
        case TILE_Yf: return "Yf";
        case TILE_Ys: return "Ys";
        default:
            return "?";
    }
}

static const char* getDataSpaceStandard( EDataSpaceStandard standard )
{
    switch (standard)
    {
    case EDataSpaceStandard::Unspecified              : return "Unsp";
    case EDataSpaceStandard::BT709                    : return "709";
    case EDataSpaceStandard::BT601_625                : return "601";
    case EDataSpaceStandard::BT601_625_UNADJUSTED     : return "601u";
    case EDataSpaceStandard::BT601_525                : return "601_525";
    case EDataSpaceStandard::BT601_525_UNADJUSTED     : return "601u525";
    case EDataSpaceStandard::BT2020                   : return "2020";
    case EDataSpaceStandard::BT2020_CONSTANT_LUMINANCE: return "2020C";
    case EDataSpaceStandard::BT470M                   : return "470M";
    case EDataSpaceStandard::FILM                     : return "FILM";
    }
    return "UNKNOWN";
}

static const char* getDataSpaceTransfer(EDataSpaceTransfer transfer)
{
    switch(transfer)
    {
    case EDataSpaceTransfer::Unspecified: return "Unsp:";
    case EDataSpaceTransfer::Linear     : return "L:";
    case EDataSpaceTransfer::SRGB       : return "sRGB:";
    case EDataSpaceTransfer::SMPTE_170M : return "";
    case EDataSpaceTransfer::GAMMA2_2   : return "G22:";
    case EDataSpaceTransfer::GAMMA2_8   : return "G28:";
    case EDataSpaceTransfer::ST2084     : return "ST2084:";
    case EDataSpaceTransfer::HLG        : return "HLG:";
    }
};

static const char* getDataSpaceRange(EDataSpaceRange range)
{
    switch(range)
    {
    case EDataSpaceRange::Unspecified: return "U";
    case EDataSpaceRange::Full       : return "F";
    case EDataSpaceRange::Limited    : return "L";
    }
    return "UNKNOWN";
};


String8 getDataSpaceString( DataSpace dataspace )
{
    return String8::format("%s:%s%s", getDataSpaceStandard(dataspace.standard), getDataSpaceTransfer(dataspace.transfer), getDataSpaceRange(dataspace.range));
}

String8 Timing::dumpRatio(EAspectRatio t)
{
    return (uint32_t)t ? String8::format(" %d:%d", (uint32_t)t >> 16, (uint32_t)t & 0xffff) : String8("");
}

String8 Timing::dump() const
{
    return String8::format("%dx%d%s %s%dHz%s%s %d.%dMHz (%ux%u)", mWidth, mHeight,
            mFlags & Flag_Interlaced ? "i" : "",
            (mMinRefresh != mRefresh) ? String8::format("%d-", mMinRefresh) : "",
            mRefresh,
            dumpRatio(mRatio).string(),
            mFlags & Flag_Preferred ? " Preferred" : "",
            mPixelClock/1000, (mPixelClock/100) % 10,
            mHTotal, mVTotal);
}

#if VPG_HAVE_DEBUG_MUTEX

Mutex::Mutex( ) :
    mbInit(1), mTid(0), mAcqTime(0), mWaiters(0)
{
}

Mutex::~Mutex( )
{
    mbInit = 0;
    ALOG_ASSERT( mTid == 0 );
    ALOG_ASSERT( !mWaiters );
}

int Mutex::lock( )
{
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Acquiring mutex %p thread %u", this, gettid() );
    ALOG_ASSERT( mbInit );
    if ( mTid == gettid() )
    {
        ALOGE( "Thread %u has already acquired mutex %p", gettid(), this );
        ALOG_ASSERT( 0 );
    }
    ATRACE_INT_IF( MUTEX_CONDITION_DEBUG, String8::format( "W-Mutex-%p", this ).string(), 1 );
    nsecs_t timeStart = systemTime(SYSTEM_TIME_MONOTONIC);
    uint64_t timeNow, timeEla;
    for (;;)
    {
        timeNow = systemTime(SYSTEM_TIME_MONOTONIC);
        if ( mMutex.tryLock( ) == 0 )
            break;
        usleep( mSpinWait );
        timeEla = (uint64_t)int64_t( timeNow - timeStart );
        if ( timeEla > mLongTime )
        {
            ALOGE( "Thread %u blocked by thread %u waiting for mutex %p", gettid( ), mTid, this );
            timeStart = timeNow;
        }
    }
    ATRACE_INT_IF( MUTEX_CONDITION_DEBUG, String8::format( "W-Mutex-%p", this ).string(), 0 );
    ATRACE_INT_IF( MUTEX_CONDITION_DEBUG, String8::format( "A-Mutex-%p", this ).string(), 1 );
    mTid = gettid( );
    mAcqTime = timeNow;
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Acquired mutex %p thread %u", this, gettid() );
    return 0;
}

int Mutex::unlock( )
{
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Releasing mutex %p thread %u", this, gettid() );
    ALOG_ASSERT( mbInit );
    if ( mTid != gettid() )
    {
        ALOGE( "Thread %u has not acquired mutex %p [mTid %u]", gettid(), this,  mTid );
        ALOG_ASSERT( 0 );
    }
    uint64_t timeNow = systemTime(SYSTEM_TIME_MONOTONIC);
    uint64_t timeEla = (uint64_t)int64_t( timeNow - mAcqTime );
    ALOGE_IF( timeEla > mLongTime, "Thread %u held mutex %p for %" PRIu64"ms", mTid, this, timeEla / 1000000 );
    mTid = 0;
    ATRACE_INT_IF( MUTEX_CONDITION_DEBUG, String8::format( "A-Mutex-%p", this ).string(), 0 );
    mMutex.unlock( );
    return 0;
}

bool Mutex::isHeld( void )
{
    return ( mTid == gettid( ) );
}

void Mutex::incWaiter( void )
{
    ALOG_ASSERT( mbInit );
    ALOG_ASSERT( mTid == mTid );
    ++mWaiters;
}

void Mutex::decWaiter( void )
{
    ALOG_ASSERT( mbInit );
    ALOG_ASSERT( mTid == mTid );
    --mWaiters;
}

uint32_t Mutex::getWaiters( void )
{
    return mWaiters;
}

Mutex::Autolock::Autolock( Mutex& m ) : mMutex( m ) { mMutex.lock( ); }
Mutex::Autolock::~Autolock( ) { mMutex.unlock( ); }

Condition::Condition( ) :
    mbInit(1), mWaiters(0)
{
}

Condition::~Condition( )
{
    mbInit = 0;
    ALOG_ASSERT( !mWaiters );
}

int Condition::waitRelative( Mutex& mutex, nsecs_t timeout )
{
    ALOG_ASSERT( mbInit );
    ALOG_ASSERT( mutex.mTid == gettid( ) );
    mutex.mTid = 0;
    mutex.incWaiter( );
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Condition %p releasing mutex %p waiters %u/%u",
        this, &mutex, mWaiters, mutex.getWaiters() );
    int ret = mCondition.waitRelative( mutex.mMutex, timeout );
    mutex.decWaiter( );
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Condition %p acquired mutex %p waiters %u/%u",
        this, &mutex, mWaiters, mutex.getWaiters() );
    mutex.mTid = gettid( );
    mutex.mAcqTime = systemTime(SYSTEM_TIME_MONOTONIC);
    return ret;
}
int Condition::wait( Mutex& mutex )
{
    ALOG_ASSERT( mbInit );
    ALOG_ASSERT( mutex.mTid == gettid( ) );
    mutex.mTid = 0;
    mutex.incWaiter( );
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Condition %p releasing mutex %p waiters %u/%u",
        this, &mutex, mWaiters, mutex.getWaiters() );
    int ret = mCondition.wait( mutex.mMutex );
    mutex.decWaiter( );
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Condition %p acquired mutex %p waiters %u/%u",
        this, &mutex, mWaiters, mutex.getWaiters() );
    mutex.mTid = gettid( );
    mutex.mAcqTime = systemTime(SYSTEM_TIME_MONOTONIC);
    return ret;
}
void Condition::signal( )
{
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Condition %p signalled [waiters:%u]", this, mWaiters );
    ALOG_ASSERT( mbInit );
    mCondition.signal( );
}
void Condition::broadcast( )
{
    ALOGD_IF( MUTEX_CONDITION_DEBUG, "Condition %p broadcast [waiters:%u]", this, mWaiters );
    ALOG_ASSERT( mbInit );
    mCondition.broadcast( );
}

#endif // VPG_HAVE_DEBUG_MUTEX

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
