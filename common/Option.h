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

#ifndef INTEL_UFO_HWC_HWCOPTION_H
#define INTEL_UFO_HWC_HWCOPTION_H

namespace intel {
namespace ufo {
namespace hwc {

// Setting for each option.
class Option
{
public:
    // Default constructor, option must be initialised manually in the code
    Option();
    ~Option();

    // Initialise at construction time. Can be used immediately.
    Option( const char* propertyString, int32_t defaultValue, bool bForceGeometryChange = true );
    Option( const char* propertyString, const char* defaultValue, bool bForceGeometryChange = true);

    enum EOptionSourceType
    {
        SOURCE_TYPE_PROPERTY            = 0,
        SOURCE_TYPE_ALTERNATE_PROPERTY  = 1,
        SOURCE_TYPE_PERSISTENT_REGISTRY = 2,
        SOURCE_TYPE_RUNTIME_SET         = 50,
        SOURCE_TYPE_DEFAULT             = 99
    };

    // Maximum name length for variables given the required persistent property tag
    static const uint32_t cNameLength = 15;

    // Common root prefix for property/option names.
    // e.g. myproperty -> intel.hwc.myproperty
    static const char* getPropertyRoot()                                { return "intel.hwc."; }

    // Prefix for persistent registry names.
    // These are stored in the PersistentRegistry database.
    // e.g. myproperty -> option.myproperty
    static const char* getPersistRoot()                                 { return "option."; }

    // Initialise an option setting from a property. This can be executed at any time
    // and will cause the existing state of the option to be updated to whatever is
    // specified in the system properties or the defaultValue if not
    void initialize(const char* propertyString, int32_t defaultValue);
    void initialize(const char* propertyString, const char* defaultValue);

    void setPropertyString(const char* propertyString)                  { mPropertyString = propertyString; }
    void setPropertyStringAlternate(const char* propertyString)         { mPropertyStringAlternate = propertyString; }
    void setForceGeometryChange(bool bForceGeometryChange)              { mbForceGeometryChange = bForceGeometryChange; }
    void setPermitChange(bool bPermitChange)                            { mbPermitChange = bPermitChange; }
    void setChanged(bool bChanged)                                      { mbChanged = bChanged; }
    void setStringProperty(bool bStringProperty)                        { mbStringProperty = bStringProperty; }
    void setPersistent(bool bPersistent)                                { mbPersistent = bPersistent; }

    const String8& getPropertyString() const                            { return mPropertyString; }
    const String8& getPropertyStringAlternate() const                   { return mPropertyStringAlternate; }
    bool isInitialized() const                                          { return mbInitialized; }
    bool isForceGeometryChange() const                                  { return mbForceGeometryChange; }
    bool isPermitChange() const                                         { return mbPermitChange; }
    bool isChanged() const                                              { return mbChanged; }
    bool isStringProperty() const                                       { return mbStringProperty; }
    bool isPersistent() const                                           { return mbPersistent; }

    // Accessors on the internal value
    operator int32_t() const                                            { ALOG_ASSERT(mbInitialized, "Uninitialised access of %s", mPropertyString.string());
                                                                          ALOG_ASSERT(!mbStringProperty, "Integer access of string property %s", mPropertyString.string());
                                                                          return mValue; }
    operator const char*() const                                        { ALOG_ASSERT(mbInitialized, "Uninitialised access of %s", mPropertyString.string());
                                                                          ALOG_ASSERT(mbStringProperty, "String access of integer property %s", mPropertyString.string());
                                                                          return mValueString.string(); }

    int32_t get() const                                                 { return *this; }
    const char* getString() const                                       { return *this; }
    EOptionSourceType getSourceType() const                             { return mSourceType; }

    void set(int32_t value);
    void set(const char* value);

    String8 dump() const;                                               // return current option state

private:
    void initializeInternal(const char* propertyString, const char* defaultValue); // Internal helper for initialize
    void setInternal(const char* defaultValue);                         // Internal helper for initialize

    int32_t             mValue;
    EOptionSourceType   mSourceType;
    String8             mValueString;
    String8             mPropertyString;
    String8             mPropertyStringAlternate;
    bool                mbInitialized:1;                                // Has been initialized?
    bool                mbForceGeometryChange:1;                        // Does changing the option require a Geometry Change?
    bool                mbPermitChange:1;                               // Is this option allowed to change after init?
    bool                mbChanged:1;                                    // Has this option been changed since the last init?
    bool                mbStringProperty:1;                             // Is this option a string value?
    bool                mbPersistent:1;                                 // Is this option saved in the PersistentRegistry?
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
#endif // INTEL_UFO_HWC_HWCOPTION_H
