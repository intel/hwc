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

#include "Hwc.h"
#include "PersistentRegistry.h"


namespace intel {
namespace ufo {
namespace hwc {

// *****************************************************************************
// Public methods
// *****************************************************************************

// Common asserts and errors checks.
#define INTEL_UFO_HWC_PERSISTENT_REGISTRY_FILEPATH "/cache/hwc.reg"
#define INTEL_UFO_HWC_ASSERT_KEY_VALID( K ) { ALOG_ASSERT( (K).length() > 0 ); ALOG_ASSERT( strchr( (K).string(), '=' ) == NULL ); }
#define INTEL_UFO_HWC_ASSERT_VALUE_VALID( V ) do { ; } while (0)
#define INTEL_UFO_HWC_LOGE_OPEN    ALOGE_IF( !mbOpen, "Persistent registry is not open" )

void PersistentRegistry::open( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    Mutex::Autolock _l( mLock );

    if ( mbOpen )
    {
        ALOGE( "Persistent registry is already open" );
        return;
    }

    // Load registry from disk.
    loadFromDisk();

    // One-time start of the async writer for auto-saves.
    if ( mpAsyncWriter == NULL )
    {
        mpAsyncWriter = new AsyncWriter( this );
        if ( mpAsyncWriter != NULL )
        {
            mpAsyncWriter->run( "PersistentRegistryWriter" );
        }
    }

    // Now open.
    mbOpen =true;
}

void PersistentRegistry::close( void )
{
    if ( !mbOpen )
    {
        ALOGE( "Persistent registry is not open" );
        return;
    }
    // Close and save to disk.
    mbOpen = false;
    saveToDisk();
}

void PersistentRegistry::write( const String8& key, const String8& value )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    INTEL_UFO_HWC_ASSERT_KEY_VALID( key );
    INTEL_UFO_HWC_ASSERT_VALUE_VALID( value );
    INTEL_UFO_HWC_LOGE_OPEN;

    Mutex::Autolock _l( mLock );

    if ( !mbOpen )
    {
        ALOGW_IF( PERSISTENT_REGISTRY_DEBUG, "Persistent registry skipped write - closed" );
        return;
    }

    mEntries[key] = value;

    // Write through - mark registry as dirty and signal worker (if auto-saving).
    mbDirty = true;
    if ( mpAsyncWriter != NULL )
    {
        mSignalDirty.signal();
    }
}

bool PersistentRegistry::read( const String8& key, String8& value ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    INTEL_UFO_HWC_ASSERT_KEY_VALID( key );
    INTEL_UFO_HWC_ASSERT_VALUE_VALID( value );
    INTEL_UFO_HWC_LOGE_OPEN;

    Mutex::Autolock _l( mLock );

    if ( !mbOpen )
    {
        ALOGW_IF( PERSISTENT_REGISTRY_DEBUG, "Persistent registry skipped read - closed" );
        return false;
    }

    auto search = mEntries.find( key );
    if ( search == mEntries.end() )
    {
        return false;
    }
    value = search->second;
    return true;
}

bool PersistentRegistry::read( const String8& key, char* pValue, uint32_t maxChars ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    INTEL_UFO_HWC_ASSERT_KEY_VALID( key );
    INTEL_UFO_HWC_LOGE_OPEN;

    String8 tmp;
    if ( !read( key, tmp ) )
    {
        return false;
    }

    uint32_t numChars = tmp.length();
    if ( numChars+1 > maxChars )
    {
        ALOGE( "Persistent registry read key %s returned %s, exhausted maxChars %u [%u]",
            key.string(), tmp.string(), maxChars, numChars );
        return false;
    }
    strncpy( pValue, tmp.string(), maxChars );
    pValue[ maxChars-1 ] = '\0';
    return true;
}

String8 PersistentRegistry::dump( void ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );
    INTEL_UFO_HWC_LOGE_OPEN;

    Mutex::Autolock _l( mLock );

    if ( !mbOpen )
    {
        ALOGW_IF( PERSISTENT_REGISTRY_DEBUG, "Persistent registry skipped dump - closed" );
        return String8( "closed" );
    }

    String8 str;
    str = String8::format( "saving:%d dirty:%d entries:%zu {", mbSaving, (bool)mbDirty, mEntries.size() );
    for ( auto e : mEntries )
    {
        str += e.first + "=" + e.second + " ";
    }
    str += String8( "}" );

    return str;
}

// *****************************************************************************
// Private methods
// *****************************************************************************

PersistentRegistry::PersistentRegistry() :
    mCacheFilepath( INTEL_UFO_HWC_PERSISTENT_REGISTRY_FILEPATH ),
    mbOpen( false ),
    mbSaving( false ),
    mbDirty( false )
{
    // Open registry on first use.
    open();
}

PersistentRegistry::~PersistentRegistry()
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );

    // Close/sync registry.
    close();

    Mutex::Autolock _l( mLock );

    if ( mpAsyncWriter != NULL )
    {
        // Signal writer and wait for it to exit.
        mbDirty = true;
        mSignalDirty.signal();
        mLock.unlock();
        mpAsyncWriter->requestExitAndWait( );
        mpAsyncWriter = NULL;
        mLock.lock();
    }

    // Release entries.
    mEntries.clear();
}

void PersistentRegistry::saveToDisk( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );

    // The registry is constructed first in memory so it can be written
    // without blocking the registry for other threads.
    char* pRegistry = NULL;
    uint32_t numChars = 0;

    {
        Mutex::Autolock _l( mLock );

        if ( !mbDirty )
        {
            ALOGD_IF( PERSISTENT_REGISTRY_DEBUG, "Persistent registry skipped save - no changes to save" );
            return;
        }

        // Wait for an ongoing save to complete first.
        const nsecs_t timeoutNS = 10000000000; // 10s.
        while ( mbSaving )
        {
            if ( mSignalSaveDone.waitRelative( mLock, timeoutNS ) != NO_ERROR )
            {
                if ( mbSaving )
                {
                    ALOGE( "Persistent registry wait for previous save timeout" );
                    return;
                }
            }
        }

        // Count required chars.
        for ( auto e : mEntries )
        {
            // "K=V\n"
            numChars += e.first.length() + e.second.length() + 2;
        }

        ALOGD_IF( PERSISTENT_REGISTRY_DEBUG, "Persistent registry saving %zu entries [total %u chars]", mEntries.size(), numChars );

        // Construct registry.
        pRegistry = new char[ numChars + 1 ];
        if ( pRegistry )
        {
            pRegistry[0] = '\0';
            char* pE = pRegistry;
            for ( auto e : mEntries )
            {
                strcat( pE, e.first.string() );
                strcat( pE, "=" );
                strcat( pE, e.second.string() );
                strcat( pE, "\n" );
            }

            ALOGD_IF( PERSISTENT_REGISTRY_DEBUG, "Persistent registry saving {" );
            ALOGD_IF( PERSISTENT_REGISTRY_DEBUG, " %s", pRegistry );
            ALOGD_IF( PERSISTENT_REGISTRY_DEBUG, "}" );
        }
        else
        {
            ALOGE( "Persistent registry failed to alloc %u chars", numChars );
            return;
        }

        // Reset dirty (assumes no error) and set saving.
        mbDirty = false;
        mbSaving = true;
    }

    // Open file for save.
    // Use a temporary new file and rename afterwards as an
    // atomic update to minimise corruption.
    bool bOK = false;
    String8 newFile = mCacheFilepath + ".new";
    FILE* fp = fopen( newFile, "w" );
    if ( fp )
    {
        // Write text.
        uint32_t w = fwrite( pRegistry, numChars, 1, fp );
        fclose( fp );
        if ( w == 1 )
        {
            // Move new file to live.
            if ( rename( newFile.string(), mCacheFilepath.string() ) == 0 )
            {
                ALOGD( "Persistent registry save %s x%u chars OK", mCacheFilepath.string(), numChars );
                bOK = true;
            }
            else
            {
                ALOGE( "Persistent registry rename %s -> %s failed %d/%s",
                    newFile.string(), mCacheFilepath.string(), errno, strerror(errno)  );
            }
        }
        else
        {
            ALOGE( "Persistent registry save %s x%u chars FAILED", newFile.string(), numChars );
        }
    }
    else
    {
        ALOGE( "Persistent registry save %s x%u chars failed to open file", newFile.string(), numChars );
    }

    // Release memory.
    delete [] pRegistry;

    {
        Mutex::Autolock _l( mLock );

        // Complete final state - update saving/dirty, signal save done.
        if ( !bOK )
        {
            // Re-raise dirty if the save failed.
            mbDirty = true;
        }
        mbSaving = false;
        mSignalSaveDone.broadcast( );
    }
}

void PersistentRegistry::loadFromDisk( void )
{
    INTEL_UFO_HWC_ASSERT_MUTEX_HELD( mLock );

    FILE* fp = fopen( mCacheFilepath.string(), "rb" );
    if ( !fp )
    {
        ALOGE( "Persistent registry load failed to open file %s", mCacheFilepath.string() );
        return;
    }

    ALOGD_IF( PERSISTENT_REGISTRY_DEBUG, "Persistent registry loading from %s", mCacheFilepath.string() );

    // "K=V\n\0"
    const uint32_t maxChars = mMaxKeyValueCharLength + 3;
    char entry[ maxChars ] = "";
    uint32_t line = 0;
    do
    {
        if ( fgets( entry, maxChars, fp ) == entry )
        {
            ++line;
            char* pchNL = strchr( entry, '\n' );
            if ( pchNL )
            {
                *pchNL = '\0';
            }
            char* pchKey = entry;
            char* pchValue = strchr( entry, '=' );
            if ( pchValue )
            {
                if ( pchKey && strlen(pchKey) )
                {
                    *pchValue = '\0';
                    ++pchValue;
                    if ( strlen( pchValue ) == 0 )
                    {
                        ALOGE( "Persistent registry [%s] malformed value at line %u", entry, line );
                    }
                    else
                    {
                        ALOGD_IF( PERSISTENT_REGISTRY_DEBUG, " Persistent registry %s=%s", pchKey, pchValue );
                        mEntries[String8(pchKey)] = pchValue;
                    }
                }
                else
                {
                    ALOGE( "Persistent registry [%s] malformed key at line %u", entry, line );
                }
            }
            else
            {
                ALOGE( "Persistent registry [%s] malformed entry at line %u", entry, line );
            }
        }
    } while ( !ferror( fp ) && !feof( fp ) );

    fclose( fp );

    ALOGD( "Persistent registry loaded %s created %zu entries from %u lines",
        mCacheFilepath.string(), mEntries.size(), line );
}

void PersistentRegistry::waitDirty( void ) const
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mLock );

    Mutex::Autolock _l( mLock );
    while ( !mbDirty )
    {
        mSignalDirty.wait( mLock );
    }
}

bool PersistentRegistry::AsyncWriter::threadLoop()
{
    // Wait for an update.
    mpRegistry->waitDirty();
    if ( !exitPending() )
    {
        // The async writer waits for a bit before saving.
        // This will allow multiple updates to be batched.
        // This limits disk activity.
        if ( mpRegistry->isOpen() )
        {
            sleep( 2 );
            if ( !exitPending() )
            {
                mpRegistry->saveToDisk();
            }
        }
    }
    return true;
}


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

