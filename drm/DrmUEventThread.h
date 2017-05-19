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

#ifndef INTEL_UFO_HWC_HOTPLUGTHREAD_H
#define INTEL_UFO_HWC_HOTPLUGTHREAD_H

#include <utils/Thread.h>
#include "Drm.h"

namespace intel {
namespace ufo {
namespace hwc {

class Drm;

//*****************************************************************************
//
// DrmUEventThread class - responsible for handling HDMI uevents
//
//*****************************************************************************
class DrmUEventThread : public Thread
{
public:
    DrmUEventThread(Hwc& hwc, Drm& drm);
    virtual ~DrmUEventThread();

private:
    //Thread functions
    virtual void onFirstRef();
    virtual bool threadLoop();
    virtual status_t readyToRun();

    // Decode the most recent message and forward it to DRM for the appropriate displays.
    // Returns -1 if not decoded.
    int onUEvent( void );

    // Decodes the most recent message into an event.
    Drm::UEvent decodeUEvent( void );

private:
    Hwc&            mHwc;
    // TODO: change to HotPlugListener
    //Drm pointer
    Drm&            mDrm;
    uint32_t        mESDConnectorType;
    uint32_t        mESDConnectorID;

    // Maximum supported message size.
    static const int MSG_LEN = 256;
    // UEvent file handle (opened in readyToRun).
    int             mUeventFd;
    // Most recent read message.
    char            mUeventMsg[MSG_LEN];
    // Most recent read message size.
    size_t          mUeventMsgSize;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_HOTPLUGTHREAD_H
