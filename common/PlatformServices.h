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

#ifndef INTEL_UFO_HWC_PLATFORMSERVICES_H
#define INTEL_UFO_HWC_PLATFORMSERVICES_H

namespace intel {
namespace ufo {
namespace hwc {

class Hwc;

// Optional platform specific services.
// All functions have a default no-op implementation, returning appropriate
// errors where possible.
class PlatformServices
{
public:
    PlatformServices() {}
    virtual ~PlatformServices() {}

    // Enable the display of encrypted buffers with the specified sessionID and instanceID.
    // Returns true if succesful.
    virtual void enableEncryptedSession( uint32_t sessionID, uint32_t instanceID )
    {
        HWC_UNUSED(sessionID);
        HWC_UNUSED(instanceID);
        ALOGW("No implementation of PlatformServices::enableEncryptedSession");
    }

    // Disable specific encrypted session.
    // Returns true if succesful.
    virtual void disableEncryptedSession( uint32_t sessionID )
    {
        HWC_UNUSED(sessionID);
        ALOGW("No implementation of PlatformServices::disableEncryptedSession");
    }

    // Disable all encrypted sessions.
    // Returns true if succesful.
    virtual void disableAllEncryptedSessions( )
    {
        ALOGW("No implementation of PlatformServices::disableAllEncryptedSessions");
    }

    // Get session status.
    virtual bool isEncryptedSessionEnabled( uint32_t sessionID, uint32_t instanceID )
    {
        HWC_UNUSED(sessionID);
        HWC_UNUSED(instanceID);
        ALOGW("No implementation of PlatformServices::isEncryptedSessionEnabled");
        return false;
    }
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_PLATFORMSERVICES_H
