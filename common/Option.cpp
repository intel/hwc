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
#include "PersistentRegistry.h"
#include <cutils/properties.h>

namespace intel {
namespace ufo {
namespace hwc {

// Private persistent registry instance
static PersistentRegistry& getPersistentRegistry()
{
    static PersistentRegistry registry;
    return registry;
}

Option::Option() :
    mValue( 0 ),
    mSourceType( SOURCE_TYPE_DEFAULT ),
    mPropertyString( "" ),
    mbInitialized( false ),
    mbForceGeometryChange( true ),
    mbPermitChange( true ),
    mbChanged( false ),
    mbStringProperty( false ),
    mbPersistent( false )
{
    OptionManager::getInstance().add(this);
}

Option::Option( const char* propertyString, int32_t defaultValue, bool bForceGeometryChange) :
    mValue( 0 ),
    mSourceType( SOURCE_TYPE_DEFAULT ),
    mbInitialized( false ),
    mbForceGeometryChange( bForceGeometryChange ),
    mbPermitChange( true ),
    mbChanged( false ),
    mbStringProperty( false ),
    mbPersistent( false )
{
    initialize(propertyString, defaultValue);
    OptionManager::getInstance().add(this);
}

Option::Option( const char* propertyString, const char* defaultValue, bool bForceGeometryChange) :
    mValue( 0 ),
    mSourceType( SOURCE_TYPE_DEFAULT ),
    mbInitialized( false ),
    mbForceGeometryChange( bForceGeometryChange ),
    mbPermitChange( true ),
    mbChanged( false ),
    mbStringProperty( false ),
    mbPersistent( false )
{
    initialize(propertyString, defaultValue);
    OptionManager::getInstance().add(this);
}


Option::~Option()
{
    OptionManager::getInstance().remove(this);
}

void Option::initialize(const char* propertyString, int32_t defaultValue)
{
    mbStringProperty = false;
    initializeInternal(propertyString, String8::format("%d", defaultValue));
}

void Option::initialize(const char* propertyString, const char* defaultValue)
{
    mbStringProperty = true;

    initializeInternal(propertyString, defaultValue);
}

void Option::initializeInternal(const char* propertyString, const char* defaultValue)
{
    mPropertyString = propertyString ?: "";
    mPropertyString.toLower( );
    mPropertyStringAlternate.toLower( );

    String8 propertyName = String8( getPropertyRoot() ) + mPropertyString;
    String8 persistentRegistryName = String8( getPersistRoot() ) + mPropertyString;

    ALOG_ASSERT( propertyString && mPropertyString.length( ) > 0,
                 "Property names cannot be NULL or Empty [err:%p : %s = %zu]",
                 propertyString, mPropertyString.string( ), mPropertyString.length( ));
    // The specified property string must not exceed our expected size.
    ALOG_ASSERT( mPropertyString.length( ) <= cNameLength,
                 "Property names must not exceed %u characters [err:%s = %zu]",
                 cNameLength, mPropertyString.string( ), mPropertyString.length( ));
    // The specified property name must not exceed property key max length.
    ALOG_ASSERT( propertyName.length( ) <= PROPERTY_KEY_MAX,
                 "Property names must not exceed %u characters [err:%s = %zu]",
                 PROPERTY_KEY_MAX, propertyName.string( ), propertyName.length( ));
    // The specified property name should not contain spaces.
    ALOG_ASSERT( propertyName.find( " " ) == -1,
                 "Property names must not contain spaces [err:%s]",
                 propertyName.string( ) );

    // Get/set initial setting.
    char value[PROPERTY_VALUE_MAX];
    if ( property_get( propertyName, value, NULL) )
    {
        // Property.
        mValueString = String8( value );
        mSourceType  = SOURCE_TYPE_PROPERTY;
        Log::add( "Option Forced %s (property:%s)", dump().string(), propertyName.string( ) );
        ALOGI_IF(sbInternalBuild, "Option Forced %s (property:%s)", dump().string(), propertyName.string( ) );
    }
    else if ( property_get( mPropertyStringAlternate.string( ), value, NULL) )
    {
        // Alternate property.
        mValueString = String8( value );
        mSourceType  = SOURCE_TYPE_ALTERNATE_PROPERTY;
        Log::add( "Option Forced  %s (alternate property:%s)", dump().string(), mPropertyStringAlternate.string( ) );
        ALOGI_IF(sbInternalBuild, "Option Forced  %s (alternate property:%s)", dump().string(), mPropertyStringAlternate.string( ) );
    }
    else if ( getPersistentRegistry().read( persistentRegistryName, mValueString ) )
    {
        // Persistent registry.
        mSourceType  = SOURCE_TYPE_PERSISTENT_REGISTRY;
        Log::add( "Option Forced  %s (persistent registry:%s)", dump().string(), persistentRegistryName.string( ) );
        ALOGI_IF(sbInternalBuild, "Option Forced  %s (persistent registry:%s)", dump().string(), persistentRegistryName.string( ) );
    }
    else
    {
        // Build-time default.
        mValueString = defaultValue;
        mSourceType  = SOURCE_TYPE_DEFAULT;
        Log::add( "Option Default %s (HWC default)", dump().string());
        ALOGI_IF(sbInternalBuild, "Option Default %s (HWC default)", dump().string());
    }

    mValue = atoi( mValueString );
    mbInitialized = true;
}

void Option::set(int32_t value)
{
    ALOG_ASSERT(!mbStringProperty, "set integer on a string property %s", mPropertyString.string());
    setInternal(String8::format("%d", value));
}

void Option::set(const char* value)
{
    ALOG_ASSERT(mbStringProperty, "set string on a integer property %s", mPropertyString.string());
    setInternal(value);
}

void Option::setInternal(const char* value)
{
    ALOG_ASSERT(mbInitialized, "Uninitialised access of %s", mPropertyString.string());
    Log::alogi( "Changed option %s %s -> %s", mPropertyString.string( ), mValueString.string(), value );
    mValueString = value;
    mbChanged = true;

    // If persist flag is set, save it
    if ( mbPersistent == true )
    {
        // Persistent registry name.
        String8 persistentRegistryName = String8( getPersistRoot() ) + mPropertyString;
        getPersistentRegistry().write( persistentRegistryName, mValueString );
        Log::alogi(" Save persistent registry: %s = %s", persistentRegistryName.string(), mValueString.string());
    }

    mValue = atoi( mValueString );
    mSourceType = SOURCE_TYPE_RUNTIME_SET;

    // force a geometry change after update as this stalls until complete
    if ( mbForceGeometryChange )
        OptionManager::getInstance().forceGeometryChange();
}

String8 Option::dump() const
{
    String8 output = String8::format("%*s : ", cNameLength, mPropertyString.string());

        output.appendFormat( "%-16s(%6d) ", mValueString.string(), mValue);

    if (mbStringProperty)       output += "Str ";
    else                        output += "Int ";
    if (mbChanged)              output += "Changed ";
    if (mbPermitChange)         output += "Changable ";
    if (mbForceGeometryChange)  output += "Force ";
    if (mbPersistent)           output += "Persistent ";

    return output;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
