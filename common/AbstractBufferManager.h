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

#ifndef INTEL_UFO_HWC_ABSTRACTBUFFERMANAGER_H
#define INTEL_UFO_HWC_ABSTRACTBUFFERMANAGER_H

#include "Layer.h"
#include <utils/RefBase.h>
#include <ui/GraphicBuffer.h>

namespace intel {
namespace ufo {
namespace hwc {

// An abstract interface representing a buffer manager providing functions
// like retrieving buffer details and waiting on buffers.
class AbstractBufferManager
{
public:

    // A component may derive from this class and implement the pure virtual methods
    // to receive notifications of buffer creation and destruction.  The component
    // should register its tracker using registerTracker.
    // NOTE:
    //  The callbacks will be synchronous; components must be careful to avoid risk of deadlock.
    class Tracker
    {
    public:
        virtual ~Tracker() {};
        // This method is called immediately after a new buffer has been allocated.
        virtual void notifyBufferAlloc( buffer_handle_t handle ) = 0;
        // This method is called immediately before an existing buffer is freed.
        virtual void notifyBufferFree( buffer_handle_t handle ) = 0;
    };

    // Opaque Buffer object.
    // Implementation is platform specific.
    // Buffers are reference counted.
    // A buffer may be acquired using AbstractBufferManager::acquireBuffer( ).
    class Buffer : public RefBase
    {
    };

    // Singleton type accessor for device specific buffermanager class
    static AbstractBufferManager& get();

    AbstractBufferManager() {}
    virtual ~AbstractBufferManager() {}

    // Register a tracker to receive notifications of buffer allocations.
    // Use unregisterTracker to unregister.
    virtual void registerTracker( Tracker& tracker ) = 0;

    // Unregister a previously registered tracker.
    virtual void unregisterTracker( Tracker& tracker ) = 0;

    // Get details for a layer's current buffer.
    virtual void getLayerBufferDetails( Layer* pLayer, Layer::BufferDetails* pDetails ) = 0;

    // Set a buffer's PAVP status.
    virtual void setPavpSession( buffer_handle_t handle, uint32_t session, uint32_t instance, uint32_t isEncrypted ) = 0;

    // Set key frame flag for encoder use
    virtual void setBufferKeyFrame( buffer_handle_t handle, bool isKeyFrame ) = 0;

    // Wait for any writes to the buffer to complete.
    // handle must be non-NULL.
    // This will wait for up to timeoutNs nanoseconds.
    // If timeoutNs is 0 then this is a polling test.
    // Returns false if the buffer still has work pending.
    virtual bool wait( buffer_handle_t handle, nsecs_t timeoutNs ) = 0;

    // Acquire a buffer, preventing it from being destroyed.
    virtual sp<Buffer> acquireBuffer( buffer_handle_t handle ) = 0;

    // Buffer usage flags.  Values should be consecutive so they can be used as indices.
    enum BufferUsage {
        eBufferUsage_Display = 0,
        eBufferUsage_GL,

        // Implemenations can extend this enum starting from the following value.
        eBufferUsage_PLATFORM_START,
    };

    // Specify any buffer usage.
    virtual void setBufferUsage( buffer_handle_t handle, BufferUsage usage ) = 0;

    // Get buffer size in bytes.
    virtual uint32_t getBufferSizeBytes( buffer_handle_t handle ) = 0;

    virtual void requestCompression( buffer_handle_t handle, ECompressionType compression ) = 0;

    // Assert that an acquired buffer matches expected handle and deviceId.
    virtual void validate( sp<Buffer> pBuffer, buffer_handle_t handle, uint64_t deviceId ) = 0;

    // Post onSet entry point.
    // This is called at the end of each onSet.
    // It may be used to update/validate internal state.
    virtual void onEndOfFrame( void ) = 0;

    virtual bool isCompressionSupportedByGL(ECompressionType compression) = 0;

    // Get a string describing a given buffer compression.
    virtual const char* getCompressionName( ECompressionType compression ) = 0;

    // Get the compression type used in SurfaceFlinger output.
    virtual ECompressionType getSurfaceFlingerCompression() = 0;

    // Wrapper to create a graphic buffer.
    // If sucessful, returns succesfully allocated buffer, else returns NULL.
    virtual sp<GraphicBuffer> createGraphicBuffer( const char* pchTag,
                                                   uint32_t w, uint32_t h, int32_t format, uint32_t usage ) = 0;

    // Wrapper to create a graphic buffer.
    // If sucessful, returns succesfully allocated buffer, else returns NULL.
    virtual sp<GraphicBuffer> createGraphicBuffer( const char* pchTag,
                                                   uint32_t w, uint32_t h, int32_t format, uint32_t usage,
                                                   uint32_t stride, native_handle_t* handle, bool keepOwnership ) = 0;

    // Wrapper to reallocate a graphic buffer.
    // If sucessful, on return pGB will be the succesfully reallocated buffer, else pGB will be NULL.
    virtual void reallocateGraphicBuffer( sp<GraphicBuffer>& pGB,
                                          const char* pchTag,
                                          uint32_t w, uint32_t h, int32_t format, uint32_t usage ) = 0;

    // Wrapper to create a graphic buffer with minimal backing store (e.g. for "empty" buffers).
    // If sucessful, returns succesfully allocated buffer, else returns NULL.
    virtual sp<GraphicBuffer> createPurgedGraphicBuffer( const char* pchTag,
                                                         uint32_t w, uint32_t h, uint32_t format, uint32_t usage,
                                                         bool* pbIsPurged = NULL ) = 0;

    // Specify this buffer as a SurfaceFlinger RenderTarget for a display.
    // handle must be non-NULL.
    // OPTIONAL.
    virtual void setSurfaceFlingerRT( buffer_handle_t handle, uint32_t displayIndex ) = 0;

    // This will be called before leaving onPrepare to inform the buffer manager
    // that SurfaceFlinger compositions will not be used on a display.
    // OPTIONAL.
    virtual void purgeSurfaceFlingerRenderTargets( uint32_t displayIndex ) = 0;

    // This will be called before leaving onPrepare to inform the buffer manager
    // that SurfaceFlinger compositions will be used on a display.
    // If the buffermanager implementation purges unused SF buffers then it MUST
    // implement this to ensure SF buffers are ready in time.
    // OPTIONAL.
    virtual void realizeSurfaceFlingerRenderTargets( uint32_t displayIndex ) = 0;

    // Purge the backing for this buffer.
    // Returns the full buffer size in bytes if succesful.
    // Returns zero if the call fails or is not implemented.
    // OPTIONAL.
    virtual uint32_t purgeBuffer( buffer_handle_t handle ) = 0;

    // Realize the backing for this buffer.
    // Returns the full buffer size in bytes if succesful.
    // Returns zero if the call fails or is not implemented.
    // OPTIONAL.
    virtual uint32_t realizeBuffer( buffer_handle_t handle ) = 0;

    // Dump info about the buffermanager.
    virtual String8 dump( void ) = 0;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_ABSTRACTBUFFERMANAGER_H
