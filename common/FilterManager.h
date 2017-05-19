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

#ifndef INTEL_UFO_HWC_FILTERMANAGER_H
#define INTEL_UFO_HWC_FILTERMANAGER_H

#include "Content.h"
#include "Layer.h"
#include "AbstractFilter.h"
#include "FilterPosition.h"
#include "Singleton.h"
#include <utils/SortedVector.h>
#include <utils/Mutex.h>

namespace intel {
namespace ufo {
namespace hwc {

class Hwc;

class FilterManager : public Singleton<FilterManager>
{
public:

    // Method responsible for calling any registered filters on the display and returning
    // the final content to compose
    const Content&          onPrepare(const Content& ref)               { return onApply(ref, FilterPosition::Min, FilterPosition::Max); }

    const Content&          onApply(const Content& ref, FilterPosition first, FilterPosition last);

    // Add a new filter to the filter list
    void                    add(AbstractFilter& filter, FilterPosition position);

    // Remove this filter from the list
    void                    remove(AbstractFilter& filter);

    // Called once displays are ready but before first frame(s).
    // This provides each filter with the context (Hwc) if it is required and
    // also gives the filter opportunity to run one-time initialization.
    void                    onOpen( Hwc& hwc );

    // Dump a little info about all the filter
    String8                 dump();

private:
    friend class Singleton<FilterManager>;

    Mutex                   mLock;

    class Entry
    {
    public:
        Entry() : mpFilter(NULL), mPosition(FilterPosition::Invalid) {}
        Entry(AbstractFilter& filter, FilterPosition position) : mpFilter(&filter), mPosition(position) {}
        AbstractFilter* mpFilter;
        FilterPosition  mPosition;
    };
    Vector<Entry> mFilters;

    // Sorting comparison function.
    static int compareFilterPositions( const FilterManager::Entry* lhs, const FilterManager::Entry* rhs );

#if INTEL_HWC_INTERNAL_BUILD
    // Previous frame content.
    Content                     mOldContent;
    std::vector<Layer>          mOldContentLayers[ cMaxSupportedPhysicalDisplays ];

    // Compares new content and previous (old) output content.
    // Will assert *REQUIRED* geometry change transitions (with error to logcat/logviewer).
    // Will warn to logviewer for possibly unnecessary geometry change transitions.
    // On return oldContent is updated with newContent.
    // (a "snapshot" of the new input is taken with copiedLayers for each display).
    // Returns true if OK.
    // This is an internal build self-validation check only.
    bool    validateGeometryChange( const char* pchPrefix,
                                    const Content& newContent,
                                    Content& oldContent,
                                    std::vector<Layer> copiedLayers[] );
#endif
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_FILTERMANAGER_H
