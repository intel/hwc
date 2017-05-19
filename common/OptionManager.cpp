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
#include "Option.h"
#include "OptionManager.h"
#include "Log.h"
#include <cutils/properties.h>

namespace intel {
namespace ufo {
namespace hwc {

#if INTEL_HWC_INTERNAL_BUILD
#define WANT_PARTIAL_MATCH              1
#else
#define WANT_PARTIAL_MATCH              0
#endif


void OptionManager::add(Option* pOption)
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mOptionsMutex );
    Mutex::Autolock _l(mOptionsMutex);
    mOptions.push_back(pOption);
}

void OptionManager::remove(Option* pOption)
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mOptionsMutex );
    Mutex::Autolock _l(mOptionsMutex);
    for (int32_t i = mOptions.size()-1; i >= 0; i--)
    {
        if (mOptions[i] == pOption)
        {
            mOptions.removeAt(i);
        }
    }
}

android::String8 OptionManager::dump()
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mOptionsMutex );
    Mutex::Autolock _l(mOptionsMutex);
    android::String8 output("Option Values:");

    for (uint32_t i = 0; i < mOptions.size(); i++)
    {
        if (mOptions[i]->isInitialized())
        {
            output += "\n";
            output += mOptions[i]->dump();
        }
    }

    return output;
}

Option* OptionManager::find(const char* optionName, bool bExact)
{
    return OptionManager::getInstance().findInternal(optionName, bExact);
}

Option* OptionManager::findInternal(const char* optionName, bool bExact)
{
    INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( mOptionsMutex );

    String8 sOption(optionName);

    // Compare in lower case only.
    sOption.toLower( );

    // Compare exactly to both supplied and prefixed.
    String8 sPrefixedOption( Option::getPropertyRoot() );
    sPrefixedOption += sOption;

    // Find candidates and exact matches.
    uint32_t exact = mOptions.size();
    uint32_t exactAlternate = mOptions.size();

#if WANT_PARTIAL_MATCH
    uint32_t matches = 0;
    String8 matchString;
    uint32_t candidate = mOptions.size();
    uint32_t candidateAlternate = mOptions.size();
    uint32_t matchesAlternate = 0;
    String8 matchStringAlternate;
#else
    HWC_UNUSED(bExact);
#endif

    Mutex::Autolock _l(mOptionsMutex);

    bool bEmpty = ( sOption.length( ) == 0 );

    for ( uint32_t opt = 0; opt < mOptions.size(); ++opt )
    {
        const Option& option = *mOptions[ opt ];
        if ( option.getPropertyString().length( ) == 0 )
            continue;

        // Exactly matched masters.
        if ( !bEmpty
          && ( ( option.getPropertyString() == sOption )
            || ( option.getPropertyString() == sPrefixedOption ) ) )
        {
            if ( exact < mOptions.size() )
            {
                ALOGE( "Option '%s' exactly matches %s and %s",
                    sOption.string( ),
                    mOptions[ exact ]->getPropertyString().string( ),
                    option.getPropertyString().string( ) );
            }
            else
            {
                exact = opt;
            }
        }

        // Exactly matched alternates.
        if ( !bEmpty
          && ( ( option.getPropertyStringAlternate() == sOption )
            || ( option.getPropertyStringAlternate() == sPrefixedOption ) ) )
        {
            if ( exactAlternate < mOptions.size() )
            {
                ALOGE( "Option '%s' exactly matches alternate %s and %s",
                    sOption.string( ),
                    mOptions[ exactAlternate ]->getPropertyStringAlternate().string( ),
                    option.getPropertyStringAlternate().string( ) );
            }
            else
            {
                exactAlternate = opt;
            }
        }
#if WANT_PARTIAL_MATCH
        // Partially matched master candidates.
        else if (!bExact)
        {
            if ( bEmpty
                   || ( option.getPropertyString().find( sOption ) != -1 ) )
            {
                matchString += String8( "\n  " ) + option.getPropertyString();
                candidate = opt;
                ++matches;
            }
            else if ( option.getPropertyStringAlternate().find( sOption ) != -1 )
            {
                matchStringAlternate += String8( "\n  " ) + option.getPropertyStringAlternate();
                candidateAlternate = opt;
                ++matchesAlternate;
            }
        }
#endif
    }

    // Prioritize matches:
    // 1/ exact matches.
    if ( exact < mOptions.size() )
    {
        ALOGI( "Matching option %s", mOptions[ exact ]->getPropertyString().string( ) );
        if ( mOptions[ exact ]->isPermitChange() )
        {
            return mOptions.editItemAt(exact);
        }
        ALOGE( "Matching option %s immutable", mOptions[ exact ]->getPropertyString().string( ) );
        return NULL;
    }
    // 2/ exact matches on alternate strings.
    else if ( exactAlternate < mOptions.size() )
    {
        ALOGI( "Matching option %s  (from alternate:%s)",
            mOptions[ exactAlternate ]->getPropertyString().string( ),
            mOptions[ exactAlternate ]->getPropertyStringAlternate().string( ) );
        if ( mOptions[ exactAlternate ]->isPermitChange() )
        {
            return mOptions.editItemAt(exactAlternate);
        }
        ALOGE( "Matching option %s immutable", mOptions[ exactAlternate ]->getPropertyString().string( ) );
        return NULL;
    }
#if WANT_PARTIAL_MATCH
    // 3/ partial matches on strings.
    else if ( matches > 1 )
    {
        ALOGE( "Option '%s' matches %u options: %s", sOption.string( ), matches, matchString.string( ) );
        return NULL;
    }
    else if ( candidate < mOptions.size() )
    {
        ALOGI( "Matching option %s", mOptions[ candidate ]->getPropertyString().string( ) );
        if ( mOptions[ candidate ]->isPermitChange() )
        {
            return mOptions.editItemAt(candidate);
        }
        ALOGE( "Matching option %s immutable", mOptions[ candidate ]->getPropertyString().string( ) );
        return NULL;
    }
    // 4/ partial matches on alternate strings.
    else if ( matchesAlternate > 1 )
    {
        ALOGE( "Option '%s' matches %u alternate options: %s",
            sOption.string( ), matchesAlternate, matchStringAlternate.string( ) );
        return NULL;
    }
    else if ( candidateAlternate < mOptions.size() )
    {
        ALOGI( "Matching option %s (from alternate:%s)",
            mOptions[ candidateAlternate ]->getPropertyString().string( ),
            mOptions[ candidateAlternate ]->getPropertyStringAlternate().string( ) );
        if ( mOptions[ candidateAlternate ]->isPermitChange() )
        {
            return mOptions.editItemAt(candidateAlternate);
        }
        ALOGE( "Matching option %s immutable", mOptions[ candidateAlternate ]->getPropertyString().string( ) );
        return NULL;
    }
#endif
    ALOGE( "Option '%s' not recognised.", sOption.string( ) );

    return NULL;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
