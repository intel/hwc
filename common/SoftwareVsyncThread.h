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

#ifndef INTEL_UFO_HWC_SOFTWAREVSYNCTHREAD_H
#define INTEL_UFO_HWC_SOFTWAREVSYNCTHREAD_H

#include <utils/Thread.h>
#include "PhysicalDisplay.h"

// Kernel sleep function - for some reason this isnt exported from bionic even
// though its implemented there. Used by standard Hwcomposer::SoftwareVsyncThread impl.
extern "C" int clock_nanosleep(clockid_t clock_id, int flags,
                           const struct timespec *request,
                           struct timespec *remain);

namespace intel {
namespace ufo {
namespace hwc {

//*****************************************************************************
//
// SoftwareVsyncThread class - responsible for generating vsyncs.
//
//*****************************************************************************

class SoftwareVsyncThread : public Thread {
public:
    // Construct a software vsync thread.
    SoftwareVsyncThread(Hwc& hwc, AbstractPhysicalDisplay* pPhysical, uint32_t refreshPeriod);
    // Enable generation of vsyncs.
    void enable(void);
    // Disable generation of vsyncs.
    void disable(bool bWait);
    // Terminate the software vsync thread.
    void terminate(void);
    // Change the period between vsyncs.
    bool updatePeriod( nsecs_t refreshPeriod );

private:
    enum EMode { eModeStopped = 0, eModeRunning, eModeStopping, eModeTerminating };

    virtual void onFirstRef();
    virtual bool threadLoop();

private:
    // Use terminate( ) to stop the thread prior to destruction.
    virtual void requestExit() { Thread::requestExit( ); };
    status_t     requestExitAndWait();
    status_t     join();

    Hwc&                        mHwc;
    PhysicalDisplayManager&     mPhysicalDisplayManager;
    mutable Mutex               mLock;
    EMode                       meMode;
    mutable nsecs_t             mNextFakeVSync;
    nsecs_t                     mRefreshPeriod;
    AbstractPhysicalDisplay*    mpPhysical;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_SOFTWAREVSYNCTHREAD_H
