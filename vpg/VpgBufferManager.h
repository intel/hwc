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

#ifndef INTEL_UFO_HWC_VPGBUFFERMANAGER_H
#define INTEL_UFO_HWC_VPGBUFFERMANAGER_H

#include "Common.h"
#include "BufferManager.h"
#include "Singleton.h"
#include "GenCompression.h"
#include <map>
#include <vector>
#include <ufo/gralloc.h>

// This check is good enough for now...
#define HAVE_GRALLOC_RC_API ((ANDROID_VERSION >= 600) && (INTEL_UFO_GRALLOC_MEDIA_API_STAGE >= 2))


namespace intel {
namespace ufo {
namespace gralloc
{
    class GrallocClient;
};
namespace hwc {


class Drm;
class VpgPlatform;
class DisplayCaps;

using namespace intel::ufo::gralloc;

// VpgBufferManager is a platform specific class to track buffer allocations.
class VpgBufferManager : public BufferManager, public Singleton<VpgBufferManager>
{
public:
    VpgBufferManager( );
    virtual ~VpgBufferManager( );

    // Implements AbstractBufferManager.
    // Register a tracker to receive notifications of buffer allocations.
    // Use unregisterTracker to unregister.
    virtual void registerTracker( Tracker& tracker );

    // Implements AbstractBufferManager.
    // Unregister a previously registered tracker.
    virtual void unregisterTracker( Tracker& tracker );

    // Wrapper to create a graphic buffer.
    // If sucessful, returns succesfully allocated buffer, else returns NULL.
    virtual sp<GraphicBuffer> createGraphicBuffer( const char* pchTag,
                                                   uint32_t w, uint32_t h, int32_t format, uint32_t usage );

    // Wrapper to create a graphic buffer.
    // If sucessful, returns succesfully allocated buffer, else returns NULL.
    virtual sp<GraphicBuffer> createGraphicBuffer( const char* pchTag,
                                                   uint32_t w, uint32_t h, int32_t format, uint32_t usage,
                                                   uint32_t stride, native_handle_t* handle, bool keepOwnership );

    // Wrapper to reallocate a graphic buffer.
    // If sucessful, on return pGB will be the succesfully reallocated buffer, else pGB will be NULL.
    virtual void reallocateGraphicBuffer( sp<GraphicBuffer>& pGB,
                                          const char* pchTag,
                                          uint32_t w, uint32_t h, int32_t format, uint32_t usage );

    // Implements AbstractBufferManager.
    // Update layer details for its current buffer.
    virtual void getLayerBufferDetails( Layer* pLayer, Layer::BufferDetails* pBufferDetails );

    // Implements AbstractBufferManager.
    // Wait for any writes to the buffer to complete.
    // handle must be non-NULL.
    // This will wait for up to timeoutNs nanoseconds.
    // If timeoutNs is 0 then this is a polling test.
    // Returns false if the buffer still has work pending.
    virtual bool wait( buffer_handle_t handle, nsecs_t timeoutNs );

    // Implements AbstractBufferManager.
    // Forward Pavp session info to Gralloc.
    // handle must be non-NULL.
    virtual void setPavpSession( buffer_handle_t handle, uint32_t session, uint32_t instance, uint32_t isEncrypted );

    // Implements AbstractBufferManager.
    // Acquire a buffer, preventing it from being destroyed.
    virtual sp<AbstractBufferManager::Buffer> acquireBuffer( buffer_handle_t handle );

    virtual void requestCompression( buffer_handle_t handle, ECompressionType compression );

    // Platform specific buffer usage hints (extends BufferUsage).
    enum PlatformBufferUsage {
        // Implementation specific buffer usages.
        eBufferUsage_VPP = eBufferUsage_PLATFORM_START,
    };

    // Implements AbstractBufferManager.
    // Specify any buffer usage.
    virtual void setBufferUsage( buffer_handle_t handle, BufferUsage usage );

    // Get buffer size in bytes.
    virtual uint32_t getBufferSizeBytes( buffer_handle_t handle );

    // Implements AbstractBufferManager.
    // Assert that an acquired buffer matches expected handle and deviceId.
    virtual void validate( sp<AbstractBufferManager::Buffer> pBuffer, buffer_handle_t handle, uint64_t deviceId );

    // Implements AbstractBufferManager.
    // Post onSet entry point.
    // This is called at the end of each onSet.
    // It may be used to update/validate internal state.
    virtual void onEndOfFrame( void );

    // Implements AbstractBufferManager.
    // Return whether OGL supports the compression type.
    virtual bool isCompressionSupportedByGL(ECompressionType compression);

    // Implements AbstractBufferManager.
    // Get a string describing a given buffer compression.
    virtual const char* getCompressionName( ECompressionType compression );

    // Implements AbstractBufferManager.
    // Get the compression type used in SurfaceFlinger output.
    virtual ECompressionType getSurfaceFlingerCompression();

    // Implements AbstractBufferManager.
    // Specify this buffer as a SurfaceFlinger RenderTarget for a display.
    // handle must be non-NULL.
    virtual void setSurfaceFlingerRT( buffer_handle_t handle, uint32_t displayIndex );

    // Implements AbstractBufferManager.
    // This will be called before leaving onPrepare to inform the buffer manager
    // that SurfaceFlinger compositions will not be used on a display.
    virtual void purgeSurfaceFlingerRenderTargets( uint32_t displayIndex );

    // Implements AbstractBufferManager.
    // This will be called before leaving onPrepare to inform the buffer manager
    // that SurfaceFlinger compositions will be used on a display.
    // If the buffermanager implementation purges unused SF buffers then it MUST
    // implement this to ensure SF buffers are ready in time.
    virtual void realizeSurfaceFlingerRenderTargets( uint32_t displayIndex );

    // Implements AbstractBufferManager.
    // Purge the backing for this buffer.
    // Returns the full buffer size in bytes if succesful.
    // Returns zero if the call fails or is not implemented.
    virtual uint32_t purgeBuffer( buffer_handle_t handle );

    // Implements AbstractBufferManager.
    // Realize the backing for this buffer.
    // Returns the full buffer size in bytes if succesful.
    // Returns zero if the call fails or is not implemented.
    virtual uint32_t realizeBuffer( buffer_handle_t handle );

    // Implements AbstractBufferManager.
    // Dump info about the buffermanager.
    virtual String8 dump( void );

private:

    // Number of frames a SF RT must be unused for before its memory is purged.
    static const uint32_t mPurgeSurfaceFlingerRTThreshold = 1;

    // Gralloc Hwc Callbacks + VpgBufferManager.
    class GrallocCallbacks
    {
    public:

        GrallocCallbacks( VpgBufferManager* pVpgBufferManager ) :
            mMagic( MAGIC ),
            mpVpgBufferManager( pVpgBufferManager )
        {
            memset( &mHwcProcs, 0, sizeof( mHwcProcs ) );
            mHwcProcs.pre_buffer_alloc = GrallocCallbacks::preBufferAlloc;
            mHwcProcs.post_buffer_alloc = GrallocCallbacks::postBufferAlloc;
            mHwcProcs.post_buffer_free = GrallocCallbacks::postBufferFree;
        }

        const intel_ufo_hwc_procs_t* getProcs( void ) { return &mHwcProcs; }

    protected:
        enum
        {
            MAGIC = ANDROID_NATIVE_MAKE_CONSTANT( 'H', 'w', 'c', 'T' )
        };

        intel_ufo_hwc_procs_t   mHwcProcs;
        int                     mMagic;
        VpgBufferManager*       mpVpgBufferManager;

        // intel_ufo_hwc_procs_t callback functions.
        static int preBufferAlloc( const struct intel_ufo_hwc_procs_t* procs,
                                   int* width, int* height, int* format, int* usage,
                                   uint32_t* fb_format, uint32_t* flags );
        static void postBufferAlloc( const struct intel_ufo_hwc_procs_t* procs,
                                     buffer_handle_t handle,
                                     const intel_ufo_buffer_details_t* details );
        static void postBufferFree( const struct intel_ufo_hwc_procs_t* procs,
                                   buffer_handle_t );
    };

#if INTEL_UFO_GRALLOC_HAVE_BUFFER_DETAILS_1 && (INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL < 1)
    typedef intel_ufo_buffer_details_1_t buffer_details_t;
#else
    typedef intel_ufo_buffer_details_t buffer_details_t;
#endif

    // Managed Buffer.
    class Buffer : public BufferManager::Buffer
    {
    public:
        Buffer( GrallocClient& gralloc, Drm& drm, buffer_handle_t handle, const buffer_details_t* pBi );
        String8 dump( bool bExpand = false );
        void setPrime( void );
    private:
        ~Buffer( );
        void clearBufferInfo( void );
    private:
        friend class VpgBufferManager;
        GrallocClient&              mGralloc;       // Ref to Gralloc client.
        Drm&                        mDrm;           // Ref to Drm.
        uint32_t                    mBoHandle;      // Buffer object handle (zero or copy from mpBo).
        int                         mPrimeFd;       // Prime fd or -1 if not used.
        buffer_details_t            mInfo;          // Buffer details.
        ETilingFormat               mTilingFormat;  // Tiling format.
        uint32_t                    mFbBlend;       // Fb handle for blending (or 0 if not yet acquired).
        uint32_t                    mFbOpaque;      // Fb handle for opaque (or 0 if not yet acquired).
        int                         mDmaBuf;        // DmaBuf handle (or -1 if not yet acquired).
        uint32_t                    mLastUsedFrame; // The frame for which this buffer was last used.
        buffer_handle_t             mHandle;        // Gralloc Handle.
#if INTEL_HWC_INTERNAL_BUILD
        uint32_t                    mAccessed;      // Count of accesses since last validateCache invocation.
#endif
        bool                        mbAlpha:1;      // Is alpha blending format required.
        // Flags to indicate the immutable state have been retrieved.
        bool                        mbSetInfo:1;    // Have the buffer details been retrieved?
        bool                        mbOrphaned:1;   // Has the buffer been removed from the manager set?
        bool                        mbDeviceIdAllocFailed:1;// Initial attempt to allocate fb/dmabuf handles failed
                                                            // (implies that this buffer must be composed)
        bool                        mbDmaBufFromPrime:1;    // When true, mDmaBuf comes from Gralloc prime.
        bool                        mbPurged:1;             // Is this buffer purged?
        int32_t                     mSurfaceFlingerRT;      // Is this buffer a SF RT? (==displayIndex or -1 if not a SF RT).
        uint32_t                    mUsageFlags;    // Flags specifying where a buffer has been used.

        // Purge the buffer - releasing physical memory.
        // Returns size in bytes of memory released.
        uint32_t purge( void );

        // Realize the buffer - acquiring physical memory.
        // Returns size in bytes of memory acquired.
        uint32_t realize( void );
    };

    // When Gralloc creates a buffer we need to be notified.
    // This will add the buffer to the set of managed buffers.
    // intel_ufo_buffer_details_t can be specified in pBi if it is known; it will be retrieved if pBi is NULL.
    void notifyBufferAlloc( buffer_handle_t handle, const intel_ufo_buffer_details_t* pBi );

    // When Gralloc destroys a buffer we need to be notified.
    // This will remove the buffer from the set of managed buffers.
    // The removed buffer will be marked orphaned and then released.
    void notifyBufferFree( buffer_handle_t handle );

    // Wait for any writes to the buffer to complete.
    // handle must be non-NULL.
    void waitRendering( buffer_handle_t handle, nsecs_t timeoutNs );

    // Get buffer details for a buffer.
    // handle must be non-NULL.
    // Buffer details are returned in bi.
    // Returns true if buffer details are successfully returned.
    // Returns false if buffer details are not successfully returned.
    // Also returns device ID in deviceId - this will be an fb or dmaBuf depending on system.
    // On return, deviceId is valid only if bufferDeviceIdValid is true.
    // Pass bBlend true if the layer/format requires blending.
    // NOTE:
    //   This method can succesfully return buffer details but may still
    //   not return a valid deviceId; bufferDeviceIdValid must be checked.
    bool getBufferDetails( buffer_handle_t handle, bool bBlend,
                           buffer_details_t& bi,
                           uint64_t& deviceId, bool& bufferDeviceIdValid, ETilingFormat& tileFormat);

    void setBufferKeyFrame( buffer_handle_t handle, bool isKeyFrame );

    // Get media details for a buffer.
    // handle must be non-NULL.
    // Returns true if successful.
    // Returns false if not successful - in which case md will be cleared.
    bool getMediaDetails( buffer_handle_t handle, intel_ufo_buffer_media_details_t& md );

#if HAVE_GRALLOC_RC_API
    // Get GL resolve details for a buffer.
    // handle must be non-NULL.
    // Returns true if successful.
    // Returns false if not successful - in which case md will be cleared.
    bool getResolveDetails( buffer_handle_t handle, intel_ufo_buffer_resolve_details_t& rd );
#endif

#if INTEL_HWC_INTERNAL_BUILD
    // Asserts all handles are unique.
    void validateCache( bool bEndOfFrame = false );
#endif

    // Acquire the managed buffer for this handle, adding it to the managed set if necessary.
    // The fixed buffer state (info, bo, fb, dmaBuf) will be completed for the returned buffer.
    // Fb creation requires knowledge of the blending requirement - pbBlend must be provided to generate the fb.
    // The lock MUST NOT be held when calling this.
    // Returns the managed buffer if successful.
    // Returns NULL if not successful.
    sp<Buffer> acquireCompleteBuffer( buffer_handle_t handle, bool* pbBlend = NULL );

    // Add a new buffer to the set of managed buffers.
    // buffer_details_t can be specified in pBi if it is known.
    // The lock MUST be held when calling this.
    // Returns the managed buffer if successful.
    // Returns NULL if not successful.
    sp<Buffer> addBuffer( buffer_handle_t handle, const buffer_details_t* pBi = NULL );

    // Remove an existing buffer.
    // The lock MUST be held when calling this.
    void removeBuffer( buffer_handle_t handle );

    // Complete managed buffer details (info, bo, fb, dmaBuf).
    // Fb creation requires knowledge of the blending requirement - pbBlend must be provided to generate the fb.
    // The lock MUST NOT be held when calling this.
    void completeDetails( sp<Buffer> pBuffer, buffer_handle_t handle, bool* pbBlend );
#if INTEL_HWC_INTERNAL_BUILD
    // Asserts a complete buffer's details have not changed.
    void validateDetails( sp<Buffer> pBuffer, buffer_handle_t handle );
#endif

    // Update any buffer hints.
    void processBufferHints( );

    // Manipulate the per thread tiling allocation masks
    uint32_t getTilingMask();
    void setTilingMask(uint32_t mask);
    void resetTilingMask();

    Drm&            mDrm;                   // Drm cached in C'tor.
    GrallocClient&  mGralloc;               // Gralloc client cached in C'tor.
    GrallocCallbacks mGrallocCallbacks;     // Callback structure registered with Gralloc.
    Mutex           mLock;                  // Lock for public entry points - to protect managed set list.
    Mutex           mTrackerLock;           // Lock for tracker register/deregister/notifications.
    std::map<buffer_handle_t, sp<Buffer> > mManagedBuffers;   // Set of currently managed buffers (cached state).
    std::vector<Tracker*> mTrackers;        // Set of registered trackers.
    uint32_t        mFrameCounter;          // Incrementing counter used to timestamp accesses (frame).

    Option          mOptionFbLinear;        // Linear mode is supported for fb creations
    Option          mOptionFbXTile;         // X tiling is supported for fb creations
    Option          mOptionFbYTile;         // Y tiling is supported for fb creations
    Option          mOptionMaxYTileWidth;   // Maximum width of surface that we allow to be allocated as Y tiling
    Option          mOptionRenderCompress;  // Render Compression is supported for appropriate formats (Y tiled)

    Mutex                     mTLLock;      // Lock for thread-local override vector.
    std::map<pid_t, uint32_t> mTLTileMask;  // Thread-local override.
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_VPGBUFFERMANAGER_H
