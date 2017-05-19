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

#include "McgBufferManager.h"

namespace intel {
namespace ufo {
namespace hwc {

AbstractBufferManager& AbstractBufferManager::get()
{
    return McgBufferManager::getInstance();
}

McgBufferManager::Buffer::Buffer( )
{
}

McgBufferManager::Buffer::~Buffer( )
{
}

McgBufferManager::McgBufferManager( )
{
}

void McgBufferManager::registerTracker( Tracker& tracker )
{
    HWC_UNUSED( tracker );
    // TODO:
    // MCG implementation required.
}

void McgBufferManager::unregisterTracker( Tracker& tracker )
{
    HWC_UNUSED( tracker );
    // TODO:
    // MCG implementation required.
}

void McgBufferManager::getLayerBufferDetails( Layer* pLayer, Layer::BufferDetails* pDetails )
{
    ALOG_ASSERT( pLayer );
    ALOG_ASSERT( pDetails );
    HWC_UNUSED( pLayer );
    HWC_UNUSED( pDetails );
    // TODO:
    // MCG implementation required.
    // Complete all fields in pDetails.
}


bool McgBufferManager::wait( buffer_handle_t handle, nsecs_t timeoutNs )
{
    ALOG_ASSERT( handle );
    HWC_UNUSED( handle );
    HWC_UNUSED( timeoutNs );
    // TODO:
    // MCG implementation required.
    return true;
}

void McgBufferManager::setPavpSession( buffer_handle_t handle, uint32_t session, uint32_t instance, uint32_t isEncrypted )
{
    ALOG_ASSERT( handle );
    HWC_UNUSED( handle );
    HWC_UNUSED( session );
    HWC_UNUSED( instance );
    HWC_UNUSED( isEncrypted );
    // TODO:
    // MCG implementation required.
}

void McgBufferManager::setBufferKeyFrame( buffer_handle_t handle, bool isKeyFrame )
{
    ALOG_ASSERT( handle );
    HWC_UNUSED( handle );
    HWC_UNUSED( isKeyFrame );
    // TODO:
    // MCG implementation required.
}

sp<AbstractBufferManager::Buffer> McgBufferManager::acquireBuffer( buffer_handle_t handle )
{
    ALOG_ASSERT( handle );
    HWC_UNUSED( handle );
    // TODO:
    // MCG implementation required.
    return NULL;
}

void McgBufferManager::requestCompression( buffer_handle_t, ECompressionType )
{
    // Implementation optional
}

void McgBufferManager::setBufferUsage( buffer_handle_t, BufferUsage )
{
    // Implementation optional
}

uint32_t McgBufferManager::getBufferSizeBytes( buffer_handle_t handle )
{
    ALOG_ASSERT( handle );
    HWC_UNUSED( handle );
    // TODO:
    // Implementation required.
    return 0;
}

void McgBufferManager::validate( sp<AbstractBufferManager::Buffer> pBuffer, buffer_handle_t handle, uint64_t deviceId )
{
    HWC_UNUSED( handle );
    HWC_UNUSED( deviceId );
#if INTEL_HWC_INTERNAL_BUILD
    ALOG_ASSERT( pBuffer->getStrongCount() );
    ALOG_ASSERT( pBuffer != NULL );
    // TODO:
    // MCG implementation required.
#else
    HWC_UNUSED( pBuffer );
#endif
}

void McgBufferManager::onEndOfFrame( void )
{
    // TODO:
    // MCG implementation required.
}

bool McgBufferManager::isCompressionSupportedByGL(ECompressionType compression)
{
    return compression == COMPRESSION_NONE;
}

const char* McgBufferManager::getCompressionName( ECompressionType compression )
{
    if (compression == COMPRESSION_NONE)
    {
        return "NONE";
    }
    return "UNKNOWN";
}

ECompressionType McgBufferManager::getSurfaceFlingerCompression()
{
    return COMPRESSION_NONE;
}

String8 McgBufferManager::dump( void )
{
    // TODO:
    // MCG implementation required.
    return String8("");
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
