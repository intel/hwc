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

#include "VpgBufferManager.h"
#include "Drm.h"
#include "DrmFormatHelper.h"
#include "GrallocClient.h"
#include "Utils.h"
#include <drm_fourcc.h>

using namespace intel::ufo::gralloc;

namespace intel {
namespace ufo {
namespace hwc {

// Conditional build in FBR.
#define INTEL_UFO_HWC_HAVE_GRALLOC_FBR (defined(INTEL_UFO_GRALLOC_USAGE_PRIVATE_FBR) && 1)

// Setup defaults for Y tiling and RC support. Initially, if libdrm doesnt support
// these, then dont even try to use them.
// TODO: Add runtime detection of capabilities
#if defined(DRM_MODE_FB_MODIFIERS) && 1
#define OPTION_DEFAULT_Y_TILING            1
#define OPTION_DEFAULT_RC                  1
#else
#define OPTION_DEFAULT_Y_TILING            0
#define OPTION_DEFAULT_RC                  0
#endif

#if defined(INTEL_UFO_GRALLOC_MODULE_PERFORM_BO_FALLOCATE)
#define INTEL_UFO_HWC_HAVE_FALLOC          1
#else
#define INTEL_UFO_HWC_HAVE_FALLOC          0
#endif

// Enable purging if we have fallocate.
// - or - if debug/testing then the paths can still be run (albeit with zero effect).
#define INTEL_UFO_HWC_WANT_PURGE           (INTEL_UFO_HWC_HAVE_FALLOC || 0)

#if INTEL_HWC_INTERNAL_BUILD
// Utility to get memory allocation.
static uint32_t getFreeMemory(void)
{
    FILE* fp = fopen( "/proc/meminfo", "rt" );
    if ( !fp )
    {
        return 0;
    }
    const uint32_t lineSize = 128;
    char line[ lineSize ];
    const char memFreeStr[] = "MemFree:";
    const uint32_t memFreeChars = strlen( memFreeStr );
    for (;;)
    {
        if ( fgets( line, lineSize, fp ) == NULL )
        {
            fclose(fp);
            return 0;
        }
        if ( !strncmp( line, memFreeStr, memFreeChars ) )
        {
            fclose(fp);
            return atoi( line+memFreeChars ) * 1024;
        }
    }
}
#else
static inline uint32_t getFreeMemory(void) { return 0; }
#endif

AbstractBufferManager& AbstractBufferManager::get()
{
    return VpgBufferManager::getInstance();
}

int32_t remapDeprecatedFormats(int32_t format)
{
    // Replace the deprecated NV12 formats with official ones. Its much simpler in HWC code if we
    // only need to look at the intended formats
    // TODO: Should this go into Gralloc?
    switch (format)
    {
        case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL:
             ALOGD_IF( BUFFER_MANAGER_DEBUG, "Renaming deprecated format HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL to HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL" );
             return HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL;
        case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL:
             ALOGD_IF( BUFFER_MANAGER_DEBUG, "Renaming deprecated format HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL to HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL" );
             return HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL;
    }
    return format;
}

VpgBufferManager::Buffer::Buffer( GrallocClient& gralloc, Drm& drm, buffer_handle_t handle, const buffer_details_t* pBi ) :
    mGralloc( gralloc ),
    mDrm( drm ),
    mBoHandle( 0 ),
    mPrimeFd( -1 ),
    mFbBlend( 0 ),
    mFbOpaque( 0 ),
    mDmaBuf( -1 ),
    mLastUsedFrame( 0 ),
    mHandle( handle ),
#if INTEL_HWC_INTERNAL_BUILD
    mAccessed( 0 ),
#endif
    mbOrphaned( false ),
    mbDeviceIdAllocFailed( false ),
    mbDmaBufFromPrime( false ),
    mbPurged( false ),
    mSurfaceFlingerRT( -1 ),
    mUsageFlags(0)
{
    if ( pBi )
    {
        mInfo = *pBi;
        mInfo.format = remapDeprecatedFormats(mInfo.format);
        setPrime();
        mbSetInfo = true;
    }
    else
    {
        clearBufferInfo( );
        mbSetInfo = false;
    }
    Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: Created managed buffer %s", dump().string() );
}

VpgBufferManager::Buffer::~Buffer( )
{
    Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: Destroying managed buffer %s", dump().string() );
    ALOG_ASSERT( mbOrphaned );
    if ( mFbBlend )
    {
        mDrm.removeFb( mFbBlend );
    }
    if ( mFbOpaque )
    {
        mDrm.removeFb( mFbOpaque );
    }
    if ( ( mDmaBuf >= 0 ) && !mbDmaBufFromPrime )
    {
        close( mDmaBuf );
    }
    if ( mBoHandle )
    {
        mDrm.closeBuffer( mBoHandle );
    }
}

void VpgBufferManager::Buffer::setPrime( void )
{
    mPrimeFd = -1;
    ALOGW_IF( mInfo.prime <= 0, "Gralloc info prime %d", mInfo.prime );
    if ( mInfo.prime >= 0 )
        mPrimeFd = mInfo.prime;
}

uint32_t VpgBufferManager::Buffer::purge( void )
{
#if INTEL_UFO_HWC_WANT_PURGE
    int32_t m1 = getFreeMemory();
#if INTEL_UFO_HWC_HAVE_FALLOC
    // If mGralloc.fallocate returns not implemented, then there is little point calling it all the time.
    // Early out here. Realize should never be called if a purge didnt happen.
    static bool bGrallocFallocateNotImplemented = false;
    if (bGrallocFallocateNotImplemented)
        return 0;

    int error = mGralloc.fallocate( mHandle, I915_GEM_FALLOC_UNCOMMIT, 0, mInfo.size );
    if (error == -ENOSYS)
    {
        bGrallocFallocateNotImplemented = true;
        Log::aloge(true, "Fallocate not implemented. Expect a higher memory footprint until its supported");
    }

    if (error == 0)
#endif
    {
        int32_t m2 = getFreeMemory();
        mbPurged = true;
        Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: Purged %s [MEMINFO:%s]",
            dump().string(), m1 ? String8::format( "%u->%u/%+d KB", m1, m2, (m2-m1)/1024 ).string() : "UNKNOWN" );
        return mInfo.size;
    }
#endif
    ALOGD_IF( BUFFER_MANAGER_DEBUG, "Could not purge buffer %s", dump().string() );
    return 0;
}

uint32_t VpgBufferManager::Buffer::realize( void )
{
#if INTEL_UFO_HWC_WANT_PURGE
    int32_t m1 = getFreeMemory();
#if INTEL_UFO_HWC_HAVE_FALLOC
    if ( mGralloc.fallocate( mHandle, I915_GEM_FALLOC_COMMIT, 0, mInfo.size ) == 0 )
#endif
    {
        int32_t m2 = getFreeMemory();
        mbPurged = false;
        Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: Realized %s [MEMINFO:%s]",
            dump().string(),  m1 ? String8::format( "%u->%u/%+d KB", m1, m2, (m2-m1)/1024 ).string() : "UNKNOWN" );
        return mInfo.size;
    }
#endif
    ALOGD_IF( BUFFER_MANAGER_DEBUG, "Could not realize buffer %s", dump().string() );
    return 0;
}

void VpgBufferManager::Buffer::clearBufferInfo( void )
{
    // Default all buffer info state we know or care about.
    mInfo.width         = 0;
    mInfo.height        = 0;
    mInfo.format        = 0;
    mInfo.usage         = 0;
    mInfo.prime         = 0;
    mInfo.fb            = 0;
    mInfo.fb_format     = 0;
    mInfo.pitch         = 0;
    mInfo.size          = 0;
    mInfo.allocWidth    = 0;
    mInfo.allocHeight   = 0;
    mInfo.allocOffsetX  = 0;
    mInfo.allocOffsetY  = 0;
#if INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_1
    mInfo.rc.aux_pitch  = 0;
    mInfo.rc.aux_offset = 0;
#endif
}

String8 VpgBufferManager::Buffer::dump( bool bExpand )
{
    String8 str;
    String8 expand;
    if ( bExpand )
    {
        String8 formatTilingStr = String8::format( "%3u/%5s:%s",
            mInfo.format, getHALFormatShortString( mInfo.format ),
            getTilingFormatString( mTilingFormat ) );
        expand = String8::format( " [%4ux%4u %11s 0x%08x]",
            mInfo.width, mInfo.height, formatTilingStr.string(), mInfo.usage );
    }
    str = String8::format( "%p GRALLOC %p%s"
              " prime %3d [Gralloc prime %d]"
              " hwc bo %3u fb %3u/%3u dmaBuf %3d"
              " setInfo %d bytes %5d KB deviceIdAllocFailed %d"
              " refs %u status %s|%s|%s [LU:%5u] %s",
              // Key handles and optional expanded description
              this, mHandle, expand.string(),
              mPrimeFd, mInfo.prime,
              // DRM handles
              mBoHandle, mFbBlend, mFbOpaque, mDmaBuf,
              // Sizes
              mbSetInfo, mbSetInfo ? mInfo.size/1024: -1, mbDeviceIdAllocFailed,
              // Refs
              getStrongCount(),
              // Status (flags)
              mbOrphaned ? "O" : "-",
              mbPurged ? "P" : "-",
              mSurfaceFlingerRT >= 0 ? String8::format( "S%d", mSurfaceFlingerRT ).string() : "--",
              // Usage
              mLastUsedFrame,
              // Descriptor
              mTag );
    return str;
}

VpgBufferManager::VpgBufferManager( ) :
    mDrm( Drm::get() ),
    mGralloc( GrallocClient::getInstance() ),
    mGrallocCallbacks( this ),
    mFrameCounter( 0 ),
    mOptionFbLinear      ("fblinear",       1,                       false),
    mOptionFbXTile       ("fbxtile",        1,                       false),
    mOptionFbYTile       ("fbytile",        OPTION_DEFAULT_Y_TILING, false),
    mOptionMaxYTileWidth ("maxytilewidth",  4096,                    false),
    mOptionRenderCompress("rendercompress", OPTION_DEFAULT_RC,       false)
{
    // Register with Gralloc.
    if ( mGralloc.registerHwcProcs( mGrallocCallbacks.getProcs( ) ) != 0 )
    {
        ALOGE( "Failed to register Gralloc HWC procs" );
        ALOG_ASSERT( false );
    }

    // disable the render compression option if it's enabled but the kernel doesnt support it
    if (mOptionRenderCompress)
        mOptionRenderCompress.set(mDrm.useRenderCompression());
}

VpgBufferManager::~VpgBufferManager( )
{
}

void VpgBufferManager::registerTracker( Tracker& tracker )
{
    HWC_UNUSED( tracker );
    Mutex::Autolock _l( mTrackerLock );
    for ( uint32_t i = 0; i < mTrackers.size(); ++i )
    {
        if ( mTrackers[i] == &tracker )
            return;
    }
    mTrackers.push_back( &tracker );
}

void VpgBufferManager::unregisterTracker( Tracker& tracker )
{
    HWC_UNUSED( tracker );
    Mutex::Autolock _l( mTrackerLock );
    mTrackers.erase(std::remove(mTrackers.begin(), mTrackers.end(), &tracker), mTrackers.end());
}

uint32_t VpgBufferManager::getTilingMask()
{
    Mutex::Autolock _l( mTLLock );
    return mTLTileMask[gettid()];
}

void VpgBufferManager::setTilingMask(uint32_t mask)
{
    Mutex::Autolock _l( mTLLock );
    mTLTileMask[gettid()] = mask;
}

void VpgBufferManager::resetTilingMask()
{
    Mutex::Autolock _l( mTLLock );
    mTLTileMask[gettid()] = 0;
}

sp<GraphicBuffer> VpgBufferManager::createGraphicBuffer(const char* pchTag, uint32_t w, uint32_t h, int32_t format, uint32_t usage)
{
    // Avoid Y tiling on internal allocations to reduce DBUF pressure on Gen9+
    setTilingMask( ~INTEL_UFO_BUFFER_FLAG_Y_TILED );
    auto ret = BufferManager::createGraphicBuffer(pchTag, w, h, format, usage);
    resetTilingMask();
    return ret;
}

sp<GraphicBuffer> VpgBufferManager::createGraphicBuffer(const char* pchTag, uint32_t w, uint32_t h, int32_t format, uint32_t usage,
                                               uint32_t stride, native_handle_t* handle, bool keepOwnership)
{
    // Avoid Y tiling on internal allocations to reduce DBUF pressure on Gen9+
    setTilingMask( ~INTEL_UFO_BUFFER_FLAG_Y_TILED );
    auto ret = BufferManager::createGraphicBuffer(pchTag, w, h, format, usage, stride, handle, keepOwnership);
    resetTilingMask();
    return ret;
}

void VpgBufferManager::reallocateGraphicBuffer(sp<GraphicBuffer>& pGB, const char* pchTag, uint32_t w, uint32_t h, int32_t format, uint32_t usage)
{
    // Avoid Y tiling on internal allocations to reduce DBUF pressure on Gen9+
    setTilingMask( ~INTEL_UFO_BUFFER_FLAG_Y_TILED );
    BufferManager::reallocateGraphicBuffer(pGB, pchTag, w, h, format, usage);
    resetTilingMask();
}

void VpgBufferManager::getLayerBufferDetails( Layer* pLayer, Layer::BufferDetails* pBufferDetails )
{
    ALOG_ASSERT( pLayer );
    ALOG_ASSERT( pBufferDetails );

    buffer_details_t bd;
    intel_ufo_buffer_media_details_t md;
#if HAVE_GRALLOC_RC_API
    intel_ufo_buffer_resolve_details_t rd;
#else
    struct { uint32_t state; } rd;  // Anon struct - just to make the rest of the code cleaner.
#endif
    uint64_t bufferDeviceId;
    bool bufferDeviceIdValid = false;
    ETilingFormat tilingFormat = TILE_LINEAR;

    // Note, this function is called prior to any cached flags values get calculated in
    // the Layer class. Hence, make sure not to use any flag helpers (eg isBlend())

    const buffer_handle_t handle = pLayer->getHandle( );
    const bool bBlend = pLayer->getBlending() != EBlendMode::NONE;

    bool bHaveBd = false;
    bool bHaveMd = false;
    bool bHaveRd = false;

    if ( handle )
    {
        if ( getBufferDetails( handle, bBlend, bd, bufferDeviceId, bufferDeviceIdValid, tilingFormat) )
        {
            bHaveBd = true;
            if ( getMediaDetails( handle, md ) )
            {
                bHaveMd = true;
            }
            else
            {
                ALOGE( "Failed to get media details for gralloc handle %p", handle );
            }
#if HAVE_GRALLOC_RC_API
            if ( getResolveDetails( handle, rd ) )
            {
                bHaveRd = true;
            }
            else
            {
                ALOGE( "Failed to get resolve details for gralloc handle %p", handle );
            }
#endif
        }
        else
        {
            ALOGE( "Failed to get buffer details for gralloc handle %p", handle );
        }
    }

    if ( !bHaveBd )
    {
        memset(&bd, 0, sizeof(bd));
        bufferDeviceId = 0;
        bufferDeviceIdValid = false;
    }
    if ( !bHaveMd )
    {
        memset(&md, 0, sizeof(md));
    }
    if ( !bHaveRd )
    {
        memset(&rd, 0, sizeof(rd));
    }

    // This is only specified as being relevant for yuv surfaces. Any RGB surface is assumed
    // to be full range whatever this value is set to
    switch(md.yuv_color_range)
    {
        case INTEL_UFO_BUFFER_COLOR_RANGE_FULL:
            pBufferDetails->setColorRange(EDataSpaceRange::Full);
            break;
        case INTEL_UFO_BUFFER_COLOR_RANGE_LIMITED:
        default:
            pBufferDetails->setColorRange(EDataSpaceRange::Limited);
        break;
    }

    pBufferDetails->setDeviceId        ( bufferDeviceId, bufferDeviceIdValid );
    pBufferDetails->setWidth           ( bd.width );
    pBufferDetails->setHeight          ( bd.height );
    pBufferDetails->setFormat          ( bd.format );
    pBufferDetails->setUsage           ( bd.usage );
    pBufferDetails->setPitch           ( bd.pitch );
    pBufferDetails->setSize            ( bd.size );
    pBufferDetails->setAllocWidth      ( bd.allocWidth );
    pBufferDetails->setAllocHeight     ( bd.allocHeight );
    pBufferDetails->setPavpSessionID   ( md.pavp_session_id );
    pBufferDetails->setPavpInstanceID  ( md.pavp_instance_id );
    pBufferDetails->setEncrypted       ( md.is_encrypted );
    ECompressionType compressionType = COMPRESSION_NONE;
    if (md.is_mmc_capable && (md.compression_mode !=  0))
    {
        compressionType = ECompressionType::MMC;
    }
#if HAVE_GRALLOC_RC_API
    else if (rd.state == INTEL_UFO_BUFFER_STATE_COMPRESSED)
    {
        // GL won't output CLEAR_RC buffers so this is the only choice.
        compressionType = ECompressionType::GL_RC;
    }
#endif
    pBufferDetails->setCompression     ( compressionType );
    pBufferDetails->setKeyFrame        ( md.is_key_frame );
    pBufferDetails->setInterlaced      ( md.is_interlaced );
    pBufferDetails->setTilingFormat    ( tilingFormat );
    // This is ugly, we need to know whether we have timestamp in gralloc or not, - it is in
    // mainline but not 15_33 nor L_MR1_*, Limit to M-Dessert builds.
#if ((ANDROID_VERSION >= 600) && (INTEL_UFO_GRALLOC_MEDIA_DETAILS_LEVEL))
    pBufferDetails->setMediaTimestampFps  ( md.timestamp, md.fps );
#elif ((ANDROID_VERSION >= 600) && (INTEL_UFO_GRALLOC_MEDIA_API_STAGE >= 2))
    pBufferDetails->setMediaTimestampFps  ( md.timestamp, 0 );
#endif
    pBufferDetails->setBufferModeFlags(
#if INTEL_UFO_HWC_HAVE_GRALLOC_FBR
        (bd.usage & (INTEL_UFO_GRALLOC_USAGE_PRIVATE_FBR)) ? FRONT_BUFFER_RENDER :
#endif
        0
    );
}


bool VpgBufferManager::wait( buffer_handle_t handle, nsecs_t timeoutNs )
{
    ALOG_ASSERT( handle );

    sp<VpgBufferManager::Buffer> pBuffer = acquireCompleteBuffer( handle );
    if ( pBuffer == NULL )
    {
        return true;
    }

    ATRACE_NAME_IF( BUFFER_WAIT_TRACE, "waitBufferObject" );
    if ( mDrm.waitBufferObject( pBuffer->mBoHandle, timeoutNs ) != Drm::SUCCESS )
    {
        ALOGW_IF( timeoutNs > 0, "Buffer manager waitBufferObject Failed [bo %u]", pBuffer->mBoHandle );
        return false;
    }

    return true;
}

void VpgBufferManager::setPavpSession( buffer_handle_t handle, uint32_t session, uint32_t instance, uint32_t isEncrypted )
{
    mGralloc.setBufferPavpSession( handle, session, instance, isEncrypted );
}

sp<AbstractBufferManager::Buffer> VpgBufferManager::acquireBuffer( buffer_handle_t handle )
{
    ALOG_ASSERT( handle );
    return acquireCompleteBuffer( handle );
}

void VpgBufferManager::requestCompression( buffer_handle_t handle, ECompressionType compression )
{
#if HAVE_GRALLOC_RC_API
    uint32_t hint = INTEL_UFO_BUFFER_HINT_RC_FULL_RESOLVE;  // No compression
    if (mOptionRenderCompress)
    {
        switch(compression)
        {
            case ECompressionType::NONE:        break;
            case ECompressionType::GL_RC:       hint = INTEL_UFO_BUFFER_HINT_RC_PARTIAL_RESOLVE; break;
            case ECompressionType::GL_CLEAR_RC: hint = INTEL_UFO_BUFFER_HINT_RC_DISABLE_RESOLVE; break;
            case ECompressionType::MMC:         hint = INTEL_UFO_BUFFER_HINT_MMC_COMPRESSED; break;
        }

        Log::alogd( BUFFER_MANAGER_DEBUG,
            "BufferManager: Handle %p compression hint set to %u", handle, hint );
    }
    mGralloc.setBufferCompressionHint( handle, hint );
#else
    HWC_UNUSED(handle);
    HWC_UNUSED(compression);
#endif
}

void VpgBufferManager::setBufferUsage( buffer_handle_t handle, BufferUsage usage )
{
    sp<Buffer> pBuffer = acquireCompleteBuffer( handle );
    if (pBuffer != NULL)
    {
        pBuffer->mUsageFlags |= (1 << usage);
    }
}

uint32_t VpgBufferManager::getBufferSizeBytes( buffer_handle_t handle )
{
    sp<Buffer> pBuffer = acquireCompleteBuffer( handle );
    if (pBuffer == NULL)
    {
        return 0;
    }
    return pBuffer->mInfo.size;
}

void VpgBufferManager::validate( sp<AbstractBufferManager::Buffer> pBuffer, buffer_handle_t handle, uint64_t deviceId )
{
    HWC_UNUSED( handle );
    HWC_UNUSED( deviceId );
    HWC_UNUSED( pBuffer );
#if INTEL_HWC_INTERNAL_BUILD
    ALOG_ASSERT( pBuffer->getStrongCount() );
    ALOG_ASSERT( pBuffer != NULL );
    VpgBufferManager::Buffer* pVpgBuffer = (VpgBufferManager::Buffer*)pBuffer.get();
    ALOG_ASSERT( pVpgBuffer != NULL );
    ALOG_ASSERT( pVpgBuffer->mHandle == handle );
    if ( ( deviceId != pVpgBuffer->mFbBlend )
      && ( deviceId != pVpgBuffer->mFbOpaque ) )
    {
        ALOGE( "Expected BufferManager buffer %p with devicedId %" PRIu64" but deviceId is %" PRIu64"/%u",
            pBuffer.get(), deviceId, (uint64_t)pVpgBuffer->mFbBlend, pVpgBuffer->mFbOpaque );
        ALOG_ASSERT( false );
    }
#endif
}

void VpgBufferManager::onEndOfFrame( void )
{
#if INTEL_HWC_INTERNAL_BUILD
    validateCache( true );
#endif
    processBufferHints();
    ++mFrameCounter;
}

bool VpgBufferManager::isCompressionSupportedByGL(ECompressionType compression)
{
    return ((compression == COMPRESSION_NONE)
         || (compression == ECompressionType::GL_RC)
         || (compression == ECompressionType::GL_CLEAR_RC));
}


const char* VpgBufferManager::getCompressionName( ECompressionType compression )
{
    switch(compression)
    {
        case ECompressionType::NONE: return "NONE";
        case ECompressionType::GL_RC: return "GL";
        case ECompressionType::GL_CLEAR_RC: return "GL-CLEAR";
        case ECompressionType::MMC: return "MMC";
    }
    return "UNKNOWN";
}

ECompressionType VpgBufferManager::getSurfaceFlingerCompression()
{
    return mOptionRenderCompress ? ECompressionType::GL_RC : COMPRESSION_NONE;
}

String8 VpgBufferManager::dump( void )
{
    Mutex::Autolock _l( mLock );
    uint32_t countBuffers = mManagedBuffers.size();
    uint32_t totalBytes = 0, totalRealizedBytes = 0, countPurged = 0, countSFRTs = 0;
    String8 output("");
    output += String8::format( "Hardware Composer Managed Buffers:\n" );
    for ( auto pair : mManagedBuffers )
    {
        auto pBuffer = pair.second;
        output += String8::format( "%s\n", pBuffer->dump( true ).string() );
        if ( pBuffer->mbSetInfo )
        {
            uint32_t szBytes = pBuffer->mInfo.size;
            totalBytes += szBytes;
            if ( pBuffer->mbPurged )
                ++countPurged;
            else
                totalRealizedBytes += szBytes;
            if ( pBuffer->mSurfaceFlingerRT != -1 )
                ++countSFRTs;
        }
    }
    output += String8::format( "Frame:%u Buffers:%u Bytes:%u KB SFRTs:%u Purged:%u %u KB Realized:%u KB\n",
        mFrameCounter, countBuffers, totalBytes/1024, countSFRTs, countPurged, (totalBytes-totalRealizedBytes)/1024, totalRealizedBytes/1024 );
    return output;
}

void VpgBufferManager::notifyBufferAlloc( buffer_handle_t handle, const intel_ufo_buffer_details_t* pBi )
{
    ALOG_ASSERT( handle );
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );

    Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: Notification alloc buffer handle %p", handle );
    {
        Mutex::Autolock _l( mLock );
#if INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_1 && (INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL < 1)
        // intel_ufo_buffer_details_t is not intel_ufo_buffer_details_1_t
        addBuffer( handle, NULL );
        HWC_UNUSED( pBi );
#else
        addBuffer( handle, pBi );
#endif
    }

    // Forward notification to trackers.
    {
        Mutex::Autolock _l( mTrackerLock );
        for ( uint32_t i = 0; i < mTrackers.size(); ++i )
        {
            mTrackers[i]->notifyBufferAlloc( handle );
        }
    }

#if INTEL_HWC_INTERNAL_BUILD
    validateCache( );
#endif
}

void VpgBufferManager::notifyBufferFree( buffer_handle_t handle )
{
    ALOG_ASSERT( handle );
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );

    Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: Notification free buffer handle %p", handle );
    {
        Mutex::Autolock _l( mLock );
        const sp<Buffer> pBuffer = mManagedBuffers[handle];
        if ( pBuffer != NULL )
        {
            removeBuffer( handle );
        }
    }

    // Forward notification to trackers.
    {
        Mutex::Autolock _l( mTrackerLock );
        for ( uint32_t i = 0; i < mTrackers.size(); ++i )
        {
            mTrackers[i]->notifyBufferFree( handle );
        }
    }

#if INTEL_HWC_INTERNAL_BUILD
    validateCache( );
#endif
}

bool VpgBufferManager::getBufferDetails( buffer_handle_t handle, bool bBlend, buffer_details_t& bi, uint64_t& deviceId, bool& bufferDeviceIdValid, ETilingFormat& tilingFormat)
{
    ALOG_ASSERT( handle );
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );

    bufferDeviceIdValid = false;
    deviceId = 0;

    sp<VpgBufferManager::Buffer> pBuffer = acquireCompleteBuffer( handle, &bBlend );
    if ( pBuffer == NULL )
    {
        ALOGE( "Buffer manager getBufferDetails failed for handle %p", handle );
        return false;
    }

    // Return buffer info.
    bi = pBuffer->mInfo;
    tilingFormat = pBuffer->mTilingFormat;

    // Return deviceId.
    deviceId = bBlend ? pBuffer->mFbBlend : pBuffer->mFbOpaque;
    bufferDeviceIdValid = (deviceId != 0);


    if ( pBuffer->mSurfaceFlingerRT == -1 )
    {
        // Record frame counter for regular buffers.
        // NOTE:
        //   SurfaceFlingerRT last used frame are managed entirely through the
        //   dedicated purge/realizeSurfaceFlingerRenderTargets() methods.
        pBuffer->mLastUsedFrame = mFrameCounter;
    }

    return true;
}

void VpgBufferManager::setBufferKeyFrame( buffer_handle_t handle, bool isKeyFrame )
{
    mGralloc.setBufferKeyFrame(handle, isKeyFrame);
}

bool VpgBufferManager::getMediaDetails( buffer_handle_t handle, intel_ufo_buffer_media_details_t& md )
{
    ALOG_ASSERT( handle );
    // The details in intel_ufo_buffer_media_details_t can change at any time so this can not be cached.
    md.magic = sizeof( intel_ufo_buffer_media_details_t );
    if ( mGralloc.queryMediaDetails( handle, &md ) != 0 )
    {
        ALOGE("Buffer manager queryMediaDetails Failed [handle %p]", handle );
        memset(&md, 0, sizeof(md));
        return false;
    }
    return true;
}

void VpgBufferManager::setSurfaceFlingerRT( buffer_handle_t handle, uint32_t displayIndex )
{
    ALOG_ASSERT( handle );
    sp<VpgBufferManager::Buffer> pBuffer = acquireCompleteBuffer( handle );
    if ( pBuffer != NULL )
    {
        // We do not expect a SF buffer to be tagged as an RT on multiple displays.
        ALOG_ASSERT( ( pBuffer->mSurfaceFlingerRT == -1 )
                  || ( (uint32_t)pBuffer->mSurfaceFlingerRT == displayIndex ) );
        pBuffer->mSurfaceFlingerRT = displayIndex;
    }
}

void VpgBufferManager::purgeSurfaceFlingerRenderTargets( uint32_t displayIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    Mutex::Autolock _l( mLock );
    int32_t m1 = getFreeMemory();
    uint32_t changes = 0, memory = 0;

    for ( auto pair : mManagedBuffers )
    {
        auto pBuffer = pair.second;
        if ( (uint32_t)pBuffer->mSurfaceFlingerRT != displayIndex )
            continue;
        if ( pBuffer->mbPurged )
            continue;
        // Never purge a buffer that is still referenced.
        // NOTE:
        // Refs will be at least
        //   1 for the mManagedBuffers[] ref
        //  +1 for *this* pBuffer referfence.
        if ( pBuffer->getStrongCount() > 2 )
            continue;
        uint32_t elapsedFramesSinceUsed = (uint32_t)int32_t( mFrameCounter - pBuffer->mLastUsedFrame );
        if ( elapsedFramesSinceUsed >= mPurgeSurfaceFlingerRTThreshold )
        {
            mLock.unlock();
            memory += pBuffer->purge();
            mLock.lock();
            ++changes;
            // Current policy is to purge at most one buffer per frame.
            // This is to distribute the work.
            break;
        }
    }
    if ( changes )
    {
        int32_t m2 = getFreeMemory();
        Log::alogd( BUFFER_MANAGER_DEBUG,
            "BufferManager: Frame %u Purged %u SF RTs for display %u %dKB [MEMINFO:%s]",
            mFrameCounter, changes, displayIndex, memory/1024,
            m1 ? String8::format( "%+dKB", (m2-m1)/1024 ).string() : "UNKNOWN" );
    }
}

void VpgBufferManager::realizeSurfaceFlingerRenderTargets( uint32_t displayIndex )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    Mutex::Autolock _l( mLock );
    int32_t m1 = getFreeMemory();
    uint32_t changes = 0, memory = 0;
    for ( auto pair : mManagedBuffers )
    {
        auto pBuffer = pair.second;
        if ( (uint32_t)pBuffer->mSurfaceFlingerRT != displayIndex )
            continue;
        pBuffer->mLastUsedFrame = mFrameCounter;
        if ( !pBuffer->mbPurged )
            continue;
        mLock.unlock();
        memory += pBuffer->realize();
        mLock.lock();
        ++changes;
    }
    if ( changes )
    {
        int32_t m2 = getFreeMemory();
        Log::alogd( BUFFER_MANAGER_DEBUG,
            "BufferManager: Frame %u Realized %u SF RTs for display %u %dKB [MEMINFO:%s]",
            mFrameCounter, changes, displayIndex, memory/1024,
            m1 ? String8::format( "%+dKB", (m2-m1)/1024 ).string() : "UNKNOWN" );

    }
}

uint32_t VpgBufferManager::purgeBuffer( buffer_handle_t handle )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    sp<VpgBufferManager::Buffer> pBuffer = acquireCompleteBuffer( handle, NULL );
    if ( ( pBuffer != NULL ) && !pBuffer->mbPurged )
    {
        return pBuffer->purge();
    }
    return 0;
}

uint32_t VpgBufferManager::realizeBuffer( buffer_handle_t handle )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    sp<VpgBufferManager::Buffer> pBuffer = acquireCompleteBuffer( handle, NULL );
    if ( ( pBuffer != NULL ) && pBuffer->mbPurged )
    {
        return pBuffer->realize();
    }
    return 0;
}

#if HAVE_GRALLOC_RC_API
bool VpgBufferManager::getResolveDetails( buffer_handle_t handle, intel_ufo_buffer_resolve_details_t& rd )
{
    ALOG_ASSERT( handle );
    // The details in intel_ufo_buffer_resolve_details_t can change at any time so this can not be cached.
    rd.magic = sizeof( intel_ufo_buffer_resolve_details_t );
    if ( mGralloc.getBufferResolveDetails( handle, &rd ) != 0 )
    {
        ALOGE("Buffer manager getResolveDetails Failed [handle %p]", handle );
        memset(&rd, 0, sizeof(rd));
        return false;
    }
    return true;
}
#endif

#if INTEL_HWC_INTERNAL_BUILD
void VpgBufferManager::validateCache( bool bEndOfFrame )
{
    Mutex::Autolock _l( mLock );

    uint32_t accessed = 0;
    uint32_t totalLookups = 0;

    ALOGD_IF( BUFFER_MANAGER_DEBUG, "Buffer manager x%zu buffers", mManagedBuffers.size( ) );

    for ( auto i = mManagedBuffers.begin(); i != mManagedBuffers.end(); ++i )
    {
        auto pBi = i->second;
        ALOGD_IF( BUFFER_MANAGER_DEBUG,
                  "Buffer manager buffer %s was accessed x%u",
                  pBi->dump().string(),
                  pBi->mAccessed );

        if ( pBi->mAccessed )
            ++accessed;
        totalLookups += pBi->mAccessed;

        for ( auto j = std::next(i,1); j != mManagedBuffers.end(); ++j )
        {
            auto pBj = j->second;
            // Assert that handles are unique.
            ALOG_ASSERT( ( pBi->mHandle != pBj->mHandle ),
                "Buffer manager validation error - Gralloc handles not unique\ni %s v\nj %s", pBi->dump().string(), pBj->dump().string() );

            if ( !pBi->mAccessed || !pBj->mAccessed )
                continue;

            // Assert that every buffer that was accessed since the
            // last validation has a unique bo, fb, and dmaBuf.
#define BUFMAN_CHECK( FIELD, UNINITIALISED )\
            if ( ( pBi->FIELD != UNINITIALISED )  && ( pBi->FIELD == pBj->FIELD ) ) {  \
                String8 str = String8::format( "Buffer manager validation error - " #FIELD " not unique\ni %s v\nj %s",         \
                        pBi->dump().string(), pBj->dump().string() );                                                           \
                Log::aloge( true, str.string() );                                                                               \
                ALOG_ASSERT( 0 );                                                                                               \
            }
            BUFMAN_CHECK( mBoHandle ,  0 );
            BUFMAN_CHECK( mFbBlend  ,  0 );
            BUFMAN_CHECK( mFbOpaque ,  0 );
            BUFMAN_CHECK( mDmaBuf   , -1 );
#undef BUFMAN_CHECK
        }

        if ( bEndOfFrame )
        {
            pBi->mAccessed = 0;
        }
    }
    ALOGD_IF( BUFFER_MANAGER_DEBUG,
              "Buffer manager accessed %u buffers with %u total lookups since last validate",
              accessed, totalLookups );
}
#endif

sp<VpgBufferManager::Buffer> VpgBufferManager::acquireCompleteBuffer( buffer_handle_t handle, bool* pbBlend )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    ALOG_ASSERT( handle );

#if INTEL_HWC_INTERNAL_BUILD
    validateCache( );
#endif

    Mutex::Autolock _l( mLock );

    const sp<VpgBufferManager::Buffer> pBuffer = mManagedBuffers[handle];
    sp<VpgBufferManager::Buffer> pUpdateBuffer = NULL;
    if ( pBuffer == NULL )
    {
        // The Gralloc buffer is not part of the managed set.
        // Create a Buffer record for it "just in time".
        // Once this Buffer record has no remaining references then it should be destroyed.
        // => It is effectively already orphaned.
        Log::alogd( true, "BufferManager: Handle %p is not known - acquire record jit", handle );
        pUpdateBuffer = new Buffer( mGralloc, Drm::get(), handle, NULL );
        if ( pUpdateBuffer == NULL )
        {
            ALOGE( "Failed to create 'jit' buffer record" );
            return NULL;
        }
        pUpdateBuffer->mbOrphaned = true;
    }
    else
    {
        pUpdateBuffer = pBuffer;
    }

#if INTEL_HWC_INTERNAL_BUILD
    ++pUpdateBuffer->mAccessed;
#endif

    // Check buffer details are completed.
    if ( ( !pUpdateBuffer->mbSetInfo )
      || ( !pUpdateBuffer->mBoHandle )
      || ( pbBlend && (( *pbBlend && !pUpdateBuffer->mFbBlend )
      || ( !*pbBlend && !pUpdateBuffer->mFbOpaque ))))
    {
        // Release lock while calling Gralloc to avoid risk of deadlock.
        mLock.unlock( );
        completeDetails( pUpdateBuffer, handle, pbBlend );
        mLock.lock( );
    }
#if INTEL_HWC_INTERNAL_BUILD
    else
    {
        mLock.unlock( );
        validateDetails( pUpdateBuffer, handle );
        mLock.lock( );
    }
#endif

    Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: Acquired complete managed buffer %s", pUpdateBuffer->dump().string() );
    return pUpdateBuffer;
}

sp<VpgBufferManager::Buffer> VpgBufferManager::addBuffer( buffer_handle_t handle,
                                                 const buffer_details_t* pBi )
{
    ALOG_ASSERT( handle );
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLock );

    if ( mManagedBuffers[handle] != NULL )
    {
        ALOGE( "Buffer manager add buffer handle %p for existing buffer - removing previous instance", handle );
        removeBuffer( handle );
    }

    sp<Buffer> pNewBuffer = new Buffer( mGralloc, Drm::get(), handle, pBi );
    if ( pNewBuffer == NULL )
    {
        ALOGE( "Failed to create managed buffer" );
        return NULL;
    }

    mManagedBuffers[handle] = pNewBuffer;

    return pNewBuffer;
}

void VpgBufferManager::removeBuffer( buffer_handle_t handle )
{
    ALOG_ASSERT( handle );
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLock );

    sp<VpgBufferManager::Buffer> pDeleteBuffer = mManagedBuffers[handle];
    ALOG_ASSERT( pDeleteBuffer != NULL );
    mManagedBuffers.erase(handle);
    pDeleteBuffer->mbOrphaned = true;
    Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: Orphaning managed buffer %s", pDeleteBuffer->dump().string() );
}

void VpgBufferManager::completeDetails( sp<Buffer> pBuffer, buffer_handle_t handle, bool* pbBlend )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    ALOG_ASSERT( pBuffer != NULL );

    // Complete info.
    if ( !pBuffer->mbSetInfo )
    {
        if ( mGralloc.getBufferInfo( handle, &pBuffer->mInfo ) == Drm::SUCCESS )
        {
            pBuffer->mbSetInfo = true;
            pBuffer->mInfo.format = remapDeprecatedFormats(pBuffer->mInfo.format);
            pBuffer->setPrime();
            ALOGD_IF( BUFFER_MANAGER_DEBUG, "Buffer manager set info for handle %p", handle );
        }
        else
        {
            ALOGE( "Buffer manager getBufferInfo Failed to get Gralloc info [handle %p]", handle );
        }
    }

    // Complete bo.
    if ( pBuffer->mbSetInfo && !pBuffer->mBoHandle )
    {
        Log::alogd( BUFFER_MANAGER_DEBUG, "Buffer manager opening managed buffer %p handle %p prime %d [Gralloc prime %d]",
            pBuffer.get(), handle, pBuffer->mPrimeFd, pBuffer->mInfo.prime );
        if ( ( pBuffer->mPrimeFd >= 0 ) && ( mDrm.openPrimeBuffer( pBuffer->mPrimeFd, &pBuffer->mBoHandle ) != Drm::SUCCESS ) )
        {
            ALOGE( "Buffer manager completeDetails failed to establish bo from prime %d [Gralloc prime %d]",
                pBuffer->mPrimeFd, pBuffer->mInfo.prime );
            ALOG_ASSERT( !pBuffer->mBoHandle );
        }

        pBuffer->mTilingFormat = mDrm.getTilingFormat(pBuffer->mBoHandle);
    }
    ALOGE_IF( pBuffer->mbSetInfo && !pBuffer->mBoHandle, "Buffer manager missing bo for handle %p", handle );

    // Complete fb.
    // We can only create fbs once we have the bo and know the blending status.
    if ( pBuffer->mBoHandle && pbBlend )
    {
        ALOG_ASSERT( pBuffer->mbSetInfo );

        uint32_t* pFb;
        bool bDiscardAlpha;
        uint32_t fbFormat;

        if ( *pbBlend )
        {
            pFb = &pBuffer->mFbBlend;
            bDiscardAlpha = false;
        }
        else
        {
            pFb = &pBuffer->mFbOpaque;
            bDiscardAlpha = true;
        }

        // Create the required fb on first access.
        // Don't keep trying if registration has already failed.
        // TODO:
        // We don't currently set mbDeviceIdAllocFailed for failure on this path.
        if ( ( *pFb == 0 ) && !pBuffer->mbDeviceIdAllocFailed )
        {
            fbFormat = convertHalFormatToDrmFormat( pBuffer->mInfo.format, bDiscardAlpha );

            // Calculate uv stride and offset for NV12
            uint32_t uvPitch = 0, ufOffset = 0;
            if (fbFormat == DRM_FORMAT_NV12)
            {
                uvPitch = pBuffer->mInfo.pitch;
                ufOffset = pBuffer->mInfo.pitch * pBuffer->mInfo.allocHeight;
            }

            // Subsampled formats have to have enough memory allocated to support even pixels.
            // Note that when we rotate these formats, the extra padding may get in the way. Not
            // much we can do about this given that they dont want to fix this in the kernel.
            int32_t width = pBuffer->mInfo.width;
            int32_t height = pBuffer->mInfo.height;
            if (fbFormat == DRM_FORMAT_NV12 || fbFormat == DRM_FORMAT_YUYV)
            {
                // Round up width to even for fb allocation as the kernel fails odd fb widths/heights
                width = (width+1) & ~1;
                if (fbFormat == DRM_FORMAT_NV12)
                {
                    // Also height on NV12
                    height = (height+1) & ~1;
                }
            }
            ALOG_ASSERT(width <= (int32_t)pBuffer->mInfo.allocWidth);
            ALOG_ASSERT(height <= (int32_t)pBuffer->mInfo.allocHeight);

            // Filter formats that Drm does not support as native framebuffers.
            // We can never present these directly to the display.
            if ( fbFormat == 0 )
            {
                Log::alogd( BUFFER_MANAGER_DEBUG,
                          "BufferManager: Skipped adding fb for managed buffer %s blend %d (blendformat %x/%s)",
                          pBuffer->dump().string(), *pbBlend, fbFormat, Drm::fbFormatToString( fbFormat ).string() );
            }
            else
            {
                if ( mDrm.addFb( width, height, fbFormat, pBuffer->mBoHandle,
                                 pBuffer->mInfo.pitch, uvPitch, ufOffset, pFb
#if INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_1
                                 , pBuffer->mInfo.rc.aux_pitch, pBuffer->mInfo.rc.aux_offset
#endif
                               ) == Drm::SUCCESS )
                {
                    Log::alogd( BUFFER_MANAGER_DEBUG,
                              "BufferManager: Added fb %u for managed buffer %s blend %d (blendformat %x/%s)",
                              *pFb, pBuffer->dump().string(), *pbBlend, fbFormat, Drm::fbFormatToString( fbFormat ).string() );
                    ALOGE_IF( *pFb == 0, "Buffer manager missing fb for handle %p (blend:%d)", handle, *pbBlend );
                }
                else
                {
                    // Its expected that addfb may fail with some formats, such as NV12
                    ALOGE_IF( BUFFER_MANAGER_DEBUG, "Buffer manager addFb Failed to create fb [bo %d]", pBuffer->mBoHandle );
                }
            }
        }
    }

}

#if INTEL_HWC_INTERNAL_BUILD
void VpgBufferManager::validateDetails( sp<Buffer> pBuffer, buffer_handle_t handle )
{
    ALOG_ASSERT( pBuffer->mbSetInfo );
    buffer_details_t details;
    if ( mGralloc.getBufferInfo( handle, &details ) == Drm::SUCCESS )
    {
#define INTEL_HWC_FATAL_IF_DIFFERS( FIELD ) INTEL_HWC_DEV_ASSERT( pBuffer->mInfo.FIELD == details.FIELD,                         \
                                                          "Validate details inconsistency GRALLOC %p " #FIELD " was %d now %d",  \
                                                          handle, pBuffer->mInfo.FIELD, details.FIELD )
        INTEL_HWC_FATAL_IF_DIFFERS( width );
        INTEL_HWC_FATAL_IF_DIFFERS( height );
        INTEL_HWC_FATAL_IF_DIFFERS( format );
        INTEL_HWC_FATAL_IF_DIFFERS( usage );
        INTEL_HWC_FATAL_IF_DIFFERS( allocWidth );
        INTEL_HWC_FATAL_IF_DIFFERS( allocHeight );
        INTEL_HWC_FATAL_IF_DIFFERS( allocOffsetX );
        INTEL_HWC_FATAL_IF_DIFFERS( allocOffsetY );
        INTEL_HWC_FATAL_IF_DIFFERS( pitch );
        INTEL_HWC_FATAL_IF_DIFFERS( size );
        INTEL_HWC_FATAL_IF_DIFFERS( prime );
#if INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_1
        INTEL_HWC_FATAL_IF_DIFFERS( rc.aux_pitch );
        INTEL_HWC_FATAL_IF_DIFFERS( rc.aux_offset );
#endif
#undef INTEL_HWC_FATAL_IF_DIFFERS
    }
    else
    {
        INTEL_HWC_DEV_ASSERT( false, "validate details GRALLOC %p failed to get buffer info", handle );
    }
}
#endif

void VpgBufferManager::processBufferHints( )
{
#if HAVE_GRALLOC_RC_API
    // Buffer-hint pair.
    class BufferHint
    {
        public:
            sp<Buffer> mpBuffer;
            ECompressionType mComp;
    };

    // List of buffer-hint pairs for updating.
    uint32_t numBuffers = 0;
    uint32_t numUsedBuffers = 0;

    // Process managed buffers with lock held.
    mLock.lock();
    numBuffers = mManagedBuffers.size( );
    BufferHint *bufferHints = new BufferHint[ numBuffers ];
    if( bufferHints == NULL )
    {
        ALOGE("VpgBufferManager::processBufferHints - failed to allocate memory!");
        return;
    }

    for ( auto pair : mManagedBuffers )
    {
        auto pBuffer = pair.second;
        if (pBuffer->mUsageFlags != 0)
        {
            ECompressionType comp = ECompressionType::GL_RC;

            const uint32_t flags = pBuffer->mUsageFlags;
            const bool bDisplay = flags & (1 << eBufferUsage_Display);
            const bool bGL = flags & (1 << eBufferUsage_GL);
            const bool bVPP = flags & (1 << eBufferUsage_VPP);

            // The display can't handle any compression at the moment.
            // Likewise different renderers makes compression pointless.
            if (!bDisplay && (bGL != bVPP))
            {
                if (bGL)
                {
                    // GL can handle unresolved buffers
                    comp = ECompressionType::GL_CLEAR_RC;
                }
                else if (bVPP)
                {
                    // VPP can handle MCC compression.
                    comp = ECompressionType::MMC;
                }
            }

            // Add buffer-comp pair to our list for updating.
            // TODO:
            //   Avoid unnecessary repeat updates.
            ALOGE_IF( pBuffer->mbOrphaned, "Buffer manager buffer in managed list should never be orphaned: %s", pBuffer->dump().string() );
            bufferHints[ numUsedBuffers ].mpBuffer = pBuffer;
            bufferHints[ numUsedBuffers ].mComp = comp;
            ++numUsedBuffers;

            // Clear the usage for the next frame.
            pBuffer->mUsageFlags = 0;
        }
    }

    // Updates to managed buffers are complete.
    // Now send any updates to Gralloc without lock held.
    mLock.unlock();

    for ( uint32_t i = 0; i < numUsedBuffers; ++i )
    {
        // Assume that a buffer we used this frame can not be orphaned yet.
        ALOGE_IF( bufferHints[ i ].mpBuffer->mbOrphaned,
                  "Buffer manager buffer %s orphaned during hint update",
                  bufferHints[ i ].mpBuffer->dump().string() );
        requestCompression(bufferHints[ i ].mpBuffer->mHandle, bufferHints[ i ].mComp );
    }
    delete[] bufferHints;
#endif
}

static const char* getTilingMaskString( uint32_t flags )
{
#define GRALLOC_FLAG_ADD( F ) if ( flags & INTEL_UFO_BUFFER_FLAG_##F ) str += #F " "
    String8 str;
    GRALLOC_FLAG_ADD( X_TILED );
    GRALLOC_FLAG_ADD( Y_TILED );
    GRALLOC_FLAG_ADD( LINEAR );
    GRALLOC_FLAG_ADD( CURSOR );
#if ((ANDROID_VERSION >= 600) && (INTEL_UFO_GRALLOC_MEDIA_API_STAGE >= 2))
    GRALLOC_FLAG_ADD( RC );
#endif
    return str;
#undef GRALLOC_FLAG_ADD
}


int VpgBufferManager::GrallocCallbacks::preBufferAlloc( const struct intel_ufo_hwc_procs_t* procs,
                                                     int* width, int* height, int* format, int* usage,
                                                     uint32_t* fb_format, uint32_t* flags )
{
    ALOG_ASSERT( procs );
    ALOG_ASSERT( width );
    ALOG_ASSERT( height );
    ALOG_ASSERT( format );
    ALOG_ASSERT( usage );
    ALOG_ASSERT( fb_format );
    ALOG_ASSERT( flags );
    HWC_UNUSED( format );
    ALOG_ASSERT( *usage & GRALLOC_USAGE_HW_COMPOSER );

    ALOGE_IF( BUFFER_MANAGER_DEBUG, "Buffer manager preBufferAlloc In  %dx%d format:%d usage:%d fb_format:%u, flags:0x%x", *width, *height, *format, *usage, *fb_format, *flags );

    // Some formats (NV12, YUY2) require an even width or height allocation.
    // OGL Prerotation also requires even width/height allocations. Hence, default everything to even buffer sizes
    // Minimum allocation size of 4 lines or 4 pixels minimum, as small fbs dont work on all builds
    *width = alignTo(max(*width, 4), 2);
    *height = alignTo(max(*height, 4), 2);

    GrallocCallbacks* pBMGC = (GrallocCallbacks*)procs;
    ALOG_ASSERT( pBMGC->mpVpgBufferManager );
    VpgBufferManager& bm = *(pBMGC->mpVpgBufferManager);

    // Specify the tiling flags that the HWC thinks it may be able to handle
    *flags &= ~(INTEL_UFO_BUFFER_FLAG_Y_TILED | INTEL_UFO_BUFFER_FLAG_X_TILED | INTEL_UFO_BUFFER_FLAG_LINEAR);
    if ( bm.mOptionFbYTile )
    {
        // TODO: Do not allocate any buffers wider than mOptionMaxYTileWidth as Y tile - we need to address the DBUF limits first
        if (*width <= bm.mOptionMaxYTileWidth)
        {
            // TODO: Understand why small Y tiled surfaces fail to display correctly
            if (*width >= 128 || *height >= 128)
            {
                *flags |= INTEL_UFO_BUFFER_FLAG_Y_TILED;
            }
        }
    }

    if ( bm.mOptionFbXTile )
        *flags |= INTEL_UFO_BUFFER_FLAG_X_TILED;

    if ( bm.mOptionFbLinear )
    {
        if (*usage & (GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_SW_READ_MASK))
        {
            // Force linear if its software usage.
            *flags &= ~(INTEL_UFO_BUFFER_FLAG_Y_TILED | INTEL_UFO_BUFFER_FLAG_X_TILED);
        }
        *flags |= INTEL_UFO_BUFFER_FLAG_LINEAR;
    }

    // This is ugly, we need to know whether we have timestamp in gralloc or not, - it is in
    // mainline but not 15_33 nor L_MR1_*, Limit to M-Dessert builds.
#if ((ANDROID_VERSION >= 600) && (INTEL_UFO_GRALLOC_MEDIA_API_STAGE >= 2))
    if (bm.mOptionRenderCompress == 0)
        *flags &= ~INTEL_UFO_BUFFER_FLAG_RC;
#endif

    // Kernel erroneously disallows this case
    if (*format == HAL_PIXEL_FORMAT_BGRA_8888)
    {
        *flags &= ~INTEL_UFO_BUFFER_FLAG_RC;
    }

    // Mask resultant flags using thead-local override.
    if (bm.getTilingMask())
    {
        Log::alogd( BUFFER_MANAGER_DEBUG, "BufferManager: override mask [%d] %s", gettid(), getTilingMaskString(bm.getTilingMask()) );
        *flags &= bm.getTilingMask();
    }

    // We need to force on this flag as Android doesn't set it a lot of the time.
    // Gralloc needs this to allocate things like AUX bufers.
    *usage |= GRALLOC_USAGE_HW_RENDER;

    // The HWC replaces the fb path. However, many buffers are allocated historically with a FB flag.
    *usage &= ~GRALLOC_USAGE_HW_FB;

    ALOGE_IF( BUFFER_MANAGER_DEBUG, "Buffer manager preBufferAlloc Out %dx%d format:%d usage:%d fb_format:%u, flags:0x%x", *width, *height, *format, *usage, *fb_format, *flags );

    *fb_format = 0;
    return 0;
}

void VpgBufferManager::GrallocCallbacks::postBufferAlloc( const struct intel_ufo_hwc_procs_t* procs,
                                                       buffer_handle_t handle,
                                                       const intel_ufo_buffer_details_t *details)
{
    ALOG_ASSERT( procs );
    ALOG_ASSERT( handle );
    ALOG_ASSERT( details );
    GrallocCallbacks* pBMGC = (GrallocCallbacks*)procs;
    ALOG_ASSERT( pBMGC->mMagic == MAGIC );
    pBMGC->mpVpgBufferManager->notifyBufferAlloc( handle, details );
}

void VpgBufferManager::GrallocCallbacks::postBufferFree( const struct intel_ufo_hwc_procs_t* procs,
                                                     buffer_handle_t handle )
{
    ALOG_ASSERT( procs );
    ALOG_ASSERT( handle );
    GrallocCallbacks* pBMGC = (GrallocCallbacks*)procs;
    ALOG_ASSERT( pBMGC->mMagic == MAGIC );
    pBMGC->mpVpgBufferManager->notifyBufferFree( handle );
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
