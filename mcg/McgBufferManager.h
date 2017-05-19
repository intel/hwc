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

#ifndef INTEL_UFO_HWC_MCGBUFFERMANAGER_H
#define INTEL_UFO_HWC_MCGBUFFERMANAGER_H

#include "Common.h"
#include "BufferManager.h"
#include "Singleton.h"
#include "Layer.h"
#include <utils/KeyedVector.h>


namespace intel {
namespace ufo {
namespace hwc {

// McgBufferManager is a platform specific class to track buffer allocations.
class McgBufferManager : public BufferManager, public Singleton<McgBufferManager>
{
public:
    // Construct buffer manager for Mcg platform.
    McgBufferManager( );

    // Implements AbstractBufferManager.
    // Register a tracker to receive notifications of buffer allocations.
    // Use unregisterTracker to unregister.
    virtual void registerTracker( Tracker& tracker );

    // Implements AbstractBufferManager.
    // Unregister a previously registered tracker.
    virtual void unregisterTracker( Tracker& tracker );

    // Implements AbstractBufferManager.
    // Update layer details for its current buffer.
    virtual void getLayerBufferDetails( Layer* pLayer, Layer::BufferDetails* pDetails );

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
    // Set key frame flag for encoder use
    virtual void setBufferKeyFrame( buffer_handle_t handle, bool isKeyFrame );

    // Implements AbstractBufferManager.
    // Acquire a buffer, preventing it from being destroyed.
    virtual sp<AbstractBufferManager::Buffer> acquireBuffer( buffer_handle_t handle );

    virtual void requestCompression( buffer_handle_t handle, ECompressionType compression );

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
    // Dump info about the buffermanager.
    virtual String8 dump( void );

private:

   // Managed Buffer.
    class Buffer : public AbstractBufferManager::Buffer
    {
    public:
        Buffer( );
    private:
        ~Buffer( );
    private:
    };

};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_MCGBUFFERMANAGER_H
