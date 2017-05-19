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

#include "BufferManager.h"

namespace intel {
namespace ufo {
namespace hwc {

BufferManager::Buffer::Buffer( )
{
    mTag[0] = '\0';
}

void BufferManager::Buffer::setTag( const String8& tag )
{
    strncpy( mTag, tag.string(), MAX_TAG_CHARS-1 );
    mTag[ MAX_TAG_CHARS-1] ='\0';
}

String8 BufferManager::Buffer::getTag( void )
{
    return String8( mTag );
}

void BufferManager::setBufferTag( buffer_handle_t handle, const String8& tag )
{
    sp<AbstractBufferManager::Buffer> pAbstractBuffer = acquireBuffer( handle );
    if ( pAbstractBuffer == NULL )
    {
        return;
    }
    Buffer* pBuffer = static_cast< Buffer* >( pAbstractBuffer.get() );
    pBuffer->setTag( tag );
}

String8 BufferManager::getBufferTag( buffer_handle_t handle )
{
    sp<AbstractBufferManager::Buffer> pAbstractBuffer = acquireBuffer( handle );
    if ( pAbstractBuffer == NULL )
    {
        return String8( "UNKNOWN" );
    }
    Buffer* pBuffer = static_cast< Buffer* >( pAbstractBuffer.get() );
    return pBuffer->getTag( );
}

sp<GraphicBuffer> BufferManager::createGraphicBuffer( const char* pchTag,
                                                      uint32_t w, uint32_t h, int32_t format, uint32_t usage )
{
    ALOG_ASSERT( pchTag );
    ALOG_ASSERT( w );
    ALOG_ASSERT( h );
    ALOG_ASSERT( format );

    ALOGD_IF( BUFFER_MANAGER_DEBUG,
              "createGraphicBuffer %s allocate GraphicBuffer [%ux%u fmt:%u/%s usage:0x%x]",
              pchTag, w, h, format, getHALFormatShortString(format), usage );

    sp<GraphicBuffer> pGB = new GraphicBuffer( w, h, format, usage );
    if ( ( pGB == NULL ) || ( pGB->handle == NULL ) )
    {
        ALOGE( "createGraphicBuffer %s failed to allocate GraphicBuffer [%ux%u fmt:%u/%s usage:0x%x]",
            pchTag, w, h, format, getHALFormatShortString(format), usage );
        pGB = NULL;
    }
    else if ( sbInternalBuild )
    {
        setBufferTag( pGB->handle, String8::format( "%s", pchTag ) );
    }

    return pGB;
}

sp<GraphicBuffer> BufferManager::createGraphicBuffer( const char* pchTag,
                                                      uint32_t w, uint32_t h, int32_t format, uint32_t usage,
                                                      uint32_t stride, native_handle_t* handle, bool keepOwnership )
{
    ALOG_ASSERT( pchTag );
    ALOG_ASSERT( w );
    ALOG_ASSERT( h );
    ALOG_ASSERT( format );
    ALOG_ASSERT( stride );

    ALOGD_IF( BUFFER_MANAGER_DEBUG,
              "createGraphicBuffer %s allocate GraphicBuffer [%ux%u fmt:%u/%s usage:0x%x stride %u handle %p keep %d]",
              pchTag, w, h, format, getHALFormatShortString(format), usage, stride, handle, keepOwnership );

    sp<GraphicBuffer> pGB = new GraphicBuffer( w, h, format, usage, stride, handle, keepOwnership );
    if ( ( pGB == NULL ) || ( pGB->handle == NULL ) )
    {
        ALOGE( "createGraphicBuffer %s failed to allocate GraphicBuffer [%ux%u fmt:%u/%s usage:0x%x stride %u handle %p keep %d]",
            pchTag, w, h, format, getHALFormatShortString(format), usage, stride, handle, keepOwnership );
        pGB = NULL;
    }
    // Don't overwrite the original GRALLOC tag.

    return pGB;
}

void BufferManager::reallocateGraphicBuffer( sp<GraphicBuffer>& pGB,
                                             const char* pchTag,
                                             uint32_t w, uint32_t h, int32_t format, uint32_t usage )
{
    ALOG_ASSERT( pchTag );
    ALOG_ASSERT( w );
    ALOG_ASSERT( h );
    ALOG_ASSERT( format );
    if ( pGB == NULL )
    {
        return;
    }

    ALOGD_IF( BUFFER_MANAGER_DEBUG,
              "reallocateGraphicBuffer %s allocate GraphicBuffer [%ux%u fmt:%u/%s usage:0x%x]",
              pchTag, w, h, format, getHALFormatShortString(format), usage );

    pGB->reallocate( w, h, format, usage );
    if ( ( pGB == NULL ) || ( pGB->handle == NULL ) )
    {
        ALOGE( "reallocateGraphicBuffer %s failed to allocate GraphicBuffer [%ux%u fmt:%u/%s usage:0x%x]",
            pchTag, w, h, format, getHALFormatShortString(format), usage );
        pGB = NULL;
    }
    if ( sbInternalBuild )
    {
        setBufferTag( pGB->handle, String8::format( "%s", pchTag ) );
    }
}

sp<GraphicBuffer> BufferManager::createPurgedGraphicBuffer( const char* pchTag,
                                                            uint32_t w, uint32_t h, uint32_t format, uint32_t usage,
                                                            bool* pbIsPurged )
{
    sp<GraphicBuffer> pBuffer = createGraphicBuffer( pchTag, w, h, format, usage );
    bool bIsPurged = false;
    if ( pBuffer != NULL )
    {
        // Purge to release memory (maps all pages to single physical page).
        bIsPurged = ( purgeBuffer( pBuffer->handle ) > 0 );
    }
    if ( pbIsPurged )
    {
        *pbIsPurged = bIsPurged;
    }
    return pBuffer;
}

void BufferManager::setSurfaceFlingerRT( buffer_handle_t /*handle*/, uint32_t /*displayIndex*/ ) { };

void BufferManager::purgeSurfaceFlingerRenderTargets( uint32_t /*displayIndex*/ ) { };

void BufferManager::realizeSurfaceFlingerRenderTargets( uint32_t /*displayIndex*/ ) { };

uint32_t BufferManager::purgeBuffer( buffer_handle_t /*handle*/ ) { return 0; }

uint32_t BufferManager::realizeBuffer( buffer_handle_t /*handle*/ ) { return 0; }

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
