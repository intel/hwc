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
#include "SoftwareVsyncThread.h"

// Kernel sleep function - for some reason this isnt exported from bionic even
// though its implemented there. Used by standard Hwcomposer::SoftwareVsyncThread impl.
extern "C" int clock_nanosleep(clockid_t clock_id, int flags,
                           const struct timespec *request,
                           struct timespec *remain);

namespace intel {
namespace ufo {
namespace hwc {

SoftwareVsyncThread::SoftwareVsyncThread(Hwc& hwc, AbstractPhysicalDisplay* pPhysical, uint32_t refreshPeriod)
    : mHwc(hwc),
      mPhysicalDisplayManager( hwc.getPhysicalDisplayManager() ),
      meMode(eModeStopped),
      mNextFakeVSync(0),
      mRefreshPeriod(refreshPeriod),
      mpPhysical(pPhysical)
{
    ALOG_ASSERT( mRefreshPeriod > 0 );
    ALOG_ASSERT( pPhysical != NULL );
}

void SoftwareVsyncThread::enable(void) {
    Mutex::Autolock _l(mLock);
    ALOGD_IF( VSYNC_DEBUG, "Display P%u enable SW vsync", mpPhysical->getDisplayManagerIndex() );
    if (meMode != eModeRunning)
    {
        ATRACE_INT_IF( VSYNC_DEBUG, String8::format( "HWC:P%u SW VSYNC", mpPhysical->getDisplayManagerIndex() ).string(), 1 );
        meMode = eModeRunning;
    }
}

void SoftwareVsyncThread::disable(bool /*bWait*/) {
    Mutex::Autolock _l(mLock);
    ALOGD_IF( VSYNC_DEBUG, "Display P%u disable SW vsync", mpPhysical->getDisplayManagerIndex() );
    if (meMode == eModeRunning)
    {
        ATRACE_INT_IF( VSYNC_DEBUG, String8::format( "HWC:P%u SW VSYNC", mpPhysical->getDisplayManagerIndex() ).string(), 0 );
        meMode = eModeStopping;
    }
}

void SoftwareVsyncThread::terminate(void) {
    {
        Mutex::Autolock _l(mLock);
        meMode = eModeTerminating;
    }
    Thread::requestExitAndWait();
}

bool SoftwareVsyncThread::updatePeriod( nsecs_t refreshPeriod )
{
    ALOG_ASSERT( refreshPeriod > 0 );
    if ( mRefreshPeriod != refreshPeriod )
    {
        mRefreshPeriod = refreshPeriod;
        return true;
    }
    return false;
}

void SoftwareVsyncThread::onFirstRef() {
    run("SoftwareVsyncThread", PRIORITY_URGENT_DISPLAY + PRIORITY_MORE_FAVORABLE);
}

bool SoftwareVsyncThread::threadLoop() {
    { // scope for lock
        Mutex::Autolock _l(mLock);
        if ( meMode == eModeTerminating )
        {
            return false;
        }
    }

    const nsecs_t period = mRefreshPeriod;
    const nsecs_t now = systemTime(CLOCK_MONOTONIC);
    nsecs_t next_vsync = mNextFakeVSync;
    nsecs_t sleep = next_vsync - now;
    if (sleep < 0) {
        // we missed, find where the next vsync should be
        sleep = (period - ((now - next_vsync) % period));
        next_vsync = now + sleep;
    }
    mNextFakeVSync = next_vsync + period;

    struct timespec spec;
    spec.tv_sec  = next_vsync / 1000000000;
    spec.tv_nsec = next_vsync % 1000000000;

    int err;
    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);

    if (err == 0) {
        // Only send vsync in running state
        if ( meMode == eModeRunning )
        {
            mPhysicalDisplayManager.notifyPhysicalVSync( mpPhysical, next_vsync );
        }

        // Still call postSoftwareVSync even if in stop state
        mpPhysical->postSoftwareVSync();
    }

    return true;
}


}; // namespace hwc
}; // namespace ufo
}; // namespace intel
