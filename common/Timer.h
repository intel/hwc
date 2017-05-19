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

#ifndef INTEL_UFO_HWC_TIMER_H
#define INTEL_UFO_HWC_TIMER_H

#include "Common.h"

namespace intel {
namespace ufo {
namespace hwc {

// A one-shot timer that calls the supplied 'Callback' on expiration.
class Timer : NonCopyable
{
public:
    struct Callback
    {
        virtual ~Callback() {}
        virtual void notify(Timer& timer) = 0;
    };

    Timer(Callback& callback) : mbHaveTimer(false), mCallback(callback) {}
    ~Timer() { timerDelete(); }

    status_t set(uint32_t timeoutMS);  // Set the timer.
    status_t clear();                  // Clear the timer.
private:
    void        timerDelete();
    status_t    setTimer(uint32_t timeoutMS);
    static void timeoutHandler(sigval_t value);

    bool        mbHaveTimer;
    timer_t     mTimer;
    Callback&   mCallback;
};


// A specialisation of 'Timer' that calls a member function pointer rather than
// requiring the user to supply a derrived callback.
// This is useful when you need multiple timers or want to avoid inheritance.
// Use: TimerMFn<ClassToCall, &ClassToCall::MemberFn> myTimer(classInstance);

template<class TCALLBACK, void (TCALLBACK::*CALLBACKMFN)()>
class TimerMFn : public Timer
{
public:
    TimerMFn(TCALLBACK &callbackClass)
        : Timer(mCallbackmFn),
          mCallbackmFn(callbackClass) {}

    struct CallbackMFn : public Callback
    {
        CallbackMFn(TCALLBACK &callbackClass) : mCallbackClass(callbackClass) {}
        virtual void notify(Timer& timer)
        {
            HWC_UNUSED(timer);
            (mCallbackClass.*CALLBACKMFN)();
        }
    private:
        TCALLBACK &mCallbackClass;
    };
private:
    CallbackMFn mCallbackmFn;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_TIMER_H
