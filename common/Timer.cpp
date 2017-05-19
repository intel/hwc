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
#include "Timer.h"

namespace intel {
namespace ufo {
namespace hwc {

status_t Timer::set(uint32_t timeoutMS)
{
    // If we haven't created the timer yet then do so.
    if (!mbHaveTimer)
    {
        struct sigevent timerEvent;
        memset(&timerEvent, 0, sizeof(timerEvent));
        timerEvent.sigev_notify = SIGEV_THREAD;
        timerEvent.sigev_notify_function = timeoutHandler;
        timerEvent.sigev_value.sival_ptr = this;

        if (0 != timer_create(CLOCK_MONOTONIC, &timerEvent, &mTimer))
        {
            ALOGE("Failed to create timer: %s", strerror(errno));
            return UNKNOWN_ERROR;
        }
        mbHaveTimer = true;
    }

    return setTimer(timeoutMS);
}

status_t Timer::clear()
{
    setTimer(0);
    return OK;
}

void Timer::timerDelete()
{
    if ( mbHaveTimer )
    {
        timer_delete(mTimer);
        mbHaveTimer = false;
    }
}

status_t Timer::setTimer(uint32_t timeoutMS)
{
    // Re-set the timer
    if ( !mbHaveTimer )
    {
        return NO_INIT;
    }

    struct itimerspec timerSpec;
    if (0 == timeoutMS)
    {
        timerSpec.it_value.tv_sec  = 0;
        timerSpec.it_value.tv_nsec = 0;
    }
    else
    {
        timerSpec.it_value.tv_sec  = timeoutMS / 1000;
        timerSpec.it_value.tv_nsec = (timeoutMS % 1000) * 1000000;
    }
    // This is a one-hit timer so no interval
    timerSpec.it_interval.tv_sec   = 0;
    timerSpec.it_interval.tv_nsec  = 0;

    if (0 != timer_settime(mTimer, 0, &timerSpec, NULL))
    {
        ALOGE("Failed to create set-timer: %s", strerror(errno));
        timerDelete();
        return UNKNOWN_ERROR;
    }
    return OK;
}

void Timer::timeoutHandler(sigval_t value)
{
    ALOG_ASSERT( value.sival_ptr );
    Timer *pTimer = static_cast<Timer*>(value.sival_ptr);
    pTimer->mCallback.notify(*pTimer);
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

