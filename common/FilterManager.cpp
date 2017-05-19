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
#include "FilterManager.h"
#include "Log.h"


namespace intel {
namespace ufo {
namespace hwc {

const Content& FilterManager::onApply(const Content& ref, FilterPosition first, FilterPosition last)
{
#if INTEL_HWC_INTERNAL_BUILD
    validateGeometryChange( "FilterManager Entry SF", ref, mOldContent, mOldContentLayers );
#endif

    Mutex::Autolock _l(mLock);

    ALOGD_IF(FILTER_DEBUG, "%s", ref.dump("FilterManager::onApply").string());
    // Apply all the filters to the input
    const Content* pRef = &ref;
    for (uint32_t f = 0; f < mFilters.size(); f++)
    {
        // Skip any filters outside the first to last range
        if (mFilters[f].mPosition < first)
            continue;
        if (last < mFilters[f].mPosition)
            break;

        AbstractFilter* pFilter = mFilters[f].mpFilter;
        const Content* pNewRef = &pFilter->onApply(*pRef);

#if INTEL_HWC_INTERNAL_BUILD
        validateGeometryChange( String8::format( "F%d %s%s",
                                                  f,
                                                  pFilter->getName(),
                                                  pFilter->outputsPhysicalDisplays() ? "P" : "SF" ).string(),
                                *pNewRef, pFilter->mOldOutput, pFilter->mOldLayers );
#endif

        if (pNewRef != pRef)
        {
            // If the reference changed, then log the change
            Log::add(*pNewRef, String8::format( "%s %s",
                                                pFilter->getName(),
                                                pFilter->outputsPhysicalDisplays() ? "P" : "SF" ).string() );
            ALOGD_IF(FILTER_DEBUG, "Filter:%s", pNewRef->dump(pFilter->getName()).string());
            pRef = pNewRef;
        }

    }

    return *pRef;
}

int FilterManager::compareFilterPositions( const FilterManager::Entry* lhs, const FilterManager::Entry* rhs )
{
    return static_cast<uint32_t>(lhs->mPosition) - static_cast<uint32_t>(rhs->mPosition);
}

void FilterManager::add(AbstractFilter& filter, FilterPosition position)
{
    Mutex::Autolock _l(mLock);
    Entry e(filter, position);
    LOG_FATAL_IF( ( position < FilterPosition::DisplayManager ) && filter.outputsPhysicalDisplays( ),
                  "Filters < FilterPosition::DisplayManager must be in SF display space [POS:%u PHY:%d v GS:%u]",
                  position, filter.outputsPhysicalDisplays( ), FilterPosition::DisplayManager );
    LOG_FATAL_IF( ( position >= FilterPosition::DisplayManager ) && !filter.outputsPhysicalDisplays( ),
                  "Filters >= FilterPosition::DisplayManager must be in PHY display space [POS:%u PHY:%d v GS:%u]",
                  position, filter.outputsPhysicalDisplays( ), FilterPosition::DisplayManager );
    mFilters.add(e);
    mFilters.sort( compareFilterPositions );
}

void FilterManager::remove(AbstractFilter& filter)
{
    Mutex::Autolock _l(mLock);
    ALOGD_IF(FILTER_DEBUG, "Remove Filter: %s(%p)", filter.getName(), &filter);

    for (uint32_t f = 0; f < mFilters.size(); f++)
    {
        if (mFilters[f].mpFilter == &filter)
        {
            ALOGD_IF(FILTER_DEBUG, "Filter:%d %s(%p) Removing", f, mFilters[f].mpFilter->getName(), &mFilters[f].mpFilter);
            mFilters.removeAt(f);
            break;
        }
    }
}

void FilterManager::onOpen( Hwc& hwc )
{
    Mutex::Autolock _l(mLock);
    for (uint32_t f = 0; f < mFilters.size(); f++)
    {
        mFilters[f].mpFilter->onOpen( hwc );
    }
}


String8 FilterManager::dump()
{
    if (!sbInternalBuild)
        return String8();

    String8 output;
    Mutex::Autolock _l(mLock);
    for (const Entry& f : mFilters)
    {
        ALOGD_IF(FILTER_DEBUG, "dumping filter %s", f.mpFilter->getName());
        String8 fdump = f.mpFilter->dump();
        if ( fdump.size() )
        {
            output += f.mpFilter->getName();
            output.append(": ");
            output += fdump;
            output.append("\n");
        }
    }

    return output;
}

#if INTEL_HWC_INTERNAL_BUILD

bool FilterManager::validateGeometryChange( const char* pchPrefix,
                                            const Content& newContent,
                                            Content& oldContent,
                                            std::vector<Layer> copiedLayers[] )
{
    bool bError = false;
    bool bWarning = false;

    // We expect and require cMaxSupportedPhysicalDisplays is always >= cMaxSupportedSFDisplays.
    ALOG_ASSERT( cMaxSupportedPhysicalDisplays >= cMaxSupportedSFDisplays );

    // Compare the new content with the old content and warn/error if the geometry flag is not correct.
    // Only makes sense to do this if both new and old are enabled and represent subsequent frames.
    for ( uint32_t d = 0; d < newContent.size(); ++d )
    {
        if ( d < oldContent.size() )
        {
            const Content::Display& newDisplay = newContent.getDisplay(d);
            const Content::Display& oldDisplay = oldContent.getDisplay(d);
            if ( !oldDisplay.isEnabled() || !newDisplay.isEnabled() )
            {
                continue;
            }
            if ( newDisplay.getFrameIndex() != ( oldDisplay.getFrameIndex() + 1 ) )
            {
                continue;
            }
            if ( oldDisplay.matches( newDisplay ) )
            {
                if ( newDisplay.isGeometryChanged() )
                {
                    Log::alogd( FILTER_DEBUG, "%s%u has an unnecessary geometry change", pchPrefix, d );
                    bWarning = true;
                }
            }
            else
            {
                if ( !newDisplay.isGeometryChanged() )
                {

                    Log::aloge( true, "%s%u is missing a required geometry change **ERROR**", pchPrefix, d );
                    Log::aloge( true, "Old : %s", oldDisplay.dump().string() );
                    Log::aloge( true, "New : %s", newDisplay.dump().string() );
                    bError = true;
                }
            }
        }
    }

    // Stop hard on errors.
    if ( bError )
    {
        ALOG_ASSERT( 0 );
    }

    // Snapshot the new input so we can validate the next input.
    oldContent.snapshotOf( newContent, copiedLayers );

    return ( !bError && !bWarning );
}

#endif

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
