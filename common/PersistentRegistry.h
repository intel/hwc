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

#ifndef INTEL_UFO_HWC_PERSISTENTREGISTRY_H
#define INTEL_UFO_HWC_PERSISTENTREGISTRY_H

#include "Common.h"
#include <utils/Mutex.h>
#include <utils/Thread.h>

#include <atomic>

namespace intel {
namespace ufo {
namespace hwc {

// Class PersistentRegistry provides a database of key-value pairs
// that will survive across device reboots.
// The database is loaded automatically on first access.
// Writes are saved automatically (batched and asynchronously).
// NOTES:
//   Keys must be >=1 characters and not contain '='.
//   Total length of KEY + length of VALUE must be <= mMaxKeyValueCharLength.
class PersistentRegistry
{
public:
    // Total length of KEY + length of VALUE must be <= mMaxKeyValueCharLength.
    static const uint32_t mMaxKeyValueCharLength = 512;

    // C'tor/D'tor.
    PersistentRegistry();
    ~PersistentRegistry();

    // Open the registry if it is closed.
    // This is usually not required because the registry will
    // be automatically opened on first access.
    void open( void );

    // Close the registry.
    // This will only return once outstanding saves have completed.
    // This can be used to sync prior to power off.
    void close( void );

    // Write an entry.
    // A write to the registry will trigger an automatic save.
    void write( const String8& key, const String8& value );

    // Read an entry.
    // Returns true and value on success.
    // Returns false if entry is not found.
    bool read( const String8& key, String8& value ) const;

    // Read an entry.
    // Value is returned upto maxChars including NULL termination.
    // Returns true and value on success.
    // Returns false if entry is not found or if maxChars is too small.
    bool read( const String8& key, char* pValue, const uint32_t maxChars ) const;

    // Accessors for status.
    uint32_t getEntries( void ) { return mEntries.size(); }
    bool isOpen( void ) { return mbOpen; }
    bool isDirty( void ) { return mbDirty; }
    bool isSaving( void ) { return mbSaving; }

    // Dump state.
    String8 dump( void ) const;

private:
    // Async writer used for async auto-save.
    class AsyncWriter : public Thread
    {
    public:
        AsyncWriter( PersistentRegistry* pRegistry ) : mpRegistry( pRegistry ) { }
        virtual bool threadLoop();
    protected:
        PersistentRegistry* mpRegistry;
    };

    friend class AsyncWriter;

    // Save the registry to disk.
    // Lock must be held.
    void saveToDisk( void );

    // Load the registry from disk.
    // Lock must be held.
    void loadFromDisk( void );

    // Block until the registry is dirtied.
    void waitDirty( void ) const;

    // Filename including full path of cache file.
    String8 mCacheFilepath;

    // Registry entries.
    std::map< String8, String8 > mEntries;

    // Async writer used for auto-save.
    sp<AsyncWriter> mpAsyncWriter;

    // Is the registry open?
    bool mbOpen:1;

    // Is there a thread currently saving?
    bool mbSaving:1;

    // Is the registry dirty (does it need saving)?
    std::atomic<bool> mbDirty;

    // Thread safe class.
    mutable Mutex mLock;

    // Signals to support async/batched saves.
    mutable Condition mSignalSaveDone;
    mutable Condition mSignalDirty;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_PERSISTENTREGISTRY_H
