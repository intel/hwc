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

#ifndef INTEL_UFO_HWC_LOG_H
#define INTEL_UFO_HWC_LOG_H

#include "Common.h"
#include "IDiagnostic.h"
#include "Option.h"
#include "Content.h"
#include "AbstractLog.h"

#include <utils/Mutex.h>

class Hwc;

namespace intel {
namespace ufo {
namespace hwc {

using intel::ufo::hwc::services::IDiagnostic;

namespace validation
{
    class AbstractCompositionChecker;
}

class BasicLog;

// This is primarily a debug logging class expected to generate data thats expected
// to be used by the validation team to check that the HWC is operating correctly.
class Log : NonCopyable
{
public:
    Log();
    ~Log();

    // Log entries generated from layer stacks have no specific composition type, so they get marked them with this.
    static const int HWC_IRRELEVANT_COMPOSITION_TYPE = -1;

    // Basic logging function, logs a description and a number of layers
    static void add(const char* fmt, ...)
    {
        if (sbLogViewerBuild && spLog)
        {
            va_list args;
            va_start(args, fmt);
            spLog->addInternal(fmt, args);
            va_end(args);
        }
    }

    // Basic logging function, logs a description and a number of layers
    static void add(const Content::LayerStack& layers, const char* fmt, ...)
    {
        if (sbLogViewerBuild && spLog)
        {
            va_list args;
            va_start(args, fmt);
            spLog->addInternal(layers, fmt, args);
            va_end(args);
        }
    }

    // Logging function that logs a content display reference
    static void add(const Content::Display& display, const char* fmt, ...)
    {
        if (sbLogViewerBuild && spLog)
        {
            va_list args;
            va_start(args, fmt);
            spLog->addInternal(display, fmt, args);
            va_end(args);
        }
    }

    // Logging function that logs a content reference
    static void add(const Content& content, const char* fmt, ...)
    {
        if (sbLogViewerBuild && spLog)
        {
            va_list args;
            va_start(args, fmt);
            spLog->addInternal(content, fmt, args);
            va_end(args);
        }
    }

    // Basic logging function, logs a description and a number of layers
    static void add(const Content::LayerStack& layers, const Layer& target, const char* fmt, ...)
    {
        if (sbLogViewerBuild && spLog)
        {
            va_list args;
            va_start(args, fmt);
            spLog->addInternal(layers, target, fmt, args);
            va_end(args);
        }
    }

    // Logging function that logs a display array
    static void add(hwc_display_contents_1_t** pDisplay, uint32_t num, uint32_t frameIndex, const char* fmt, ...)
    {
        if (sbLogViewerBuild && spLog)
        {
            va_list args;
            va_start(args, fmt);

            spLog->addInternal(num, pDisplay, frameIndex, fmt, args);

            va_end(args);
        }
    }

    // Always log to HWC log; conditionally log to Android log
    static void alogd(bool enableDebug, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);

        if (sbLogViewerBuild && spLog)
        {
            const char* str = spLog->addInternal(fmt, args);

            if (enableDebug)
            {
                nsecs_t timestamp = systemTime(CLOCK_MONOTONIC);
                ALOGD( INTEL_UFO_HWC_TIMESTAMP_STR " %s", INTEL_UFO_HWC_TIMESTAMP_PARAM( timestamp ), str);
            }
        }
        else if (enableDebug)
        {
            LOG_PRI_VA(ANDROID_LOG_DEBUG, LOG_TAG, fmt, args);
        }

        va_end(args);
    }

    // Always log to both HWC log and Android log
    static void alogi(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);

        if (sbLogViewerBuild && spLog)
        {
            const char* str = spLog->addInternal(fmt, args);
            nsecs_t timestamp = systemTime(CLOCK_MONOTONIC);
            ALOGI( INTEL_UFO_HWC_TIMESTAMP_STR " %s", INTEL_UFO_HWC_TIMESTAMP_PARAM( timestamp ), str);
        }
        va_end(args);
    }

    // Conditionally log error to both HWC log and Android log
    static void aloge(bool enable, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);

        if (enable)
        {
            if (sbLogViewerBuild && spLog)
            {
                const char* str = spLog->addInternal(fmt, args);
                nsecs_t timestamp = systemTime(CLOCK_MONOTONIC);
                ALOGE( INTEL_UFO_HWC_TIMESTAMP_STR " %s", INTEL_UFO_HWC_TIMESTAMP_PARAM( timestamp ), str);
            }
            else
            {
                LOG_PRI_VA(ANDROID_LOG_ERROR, LOG_TAG, fmt, args);
            }
        }

        va_end(args);
    }

    // Test if logging would generate output.
    static bool wantLog(bool enable)
    {
        return (enable || (sbInternalBuild && spLog));
    }

    // Test if logging would generate output.
    static bool wantLog(void)
    {
        return (sbInternalBuild && spLog);
    }

    static android::status_t readLogParcel(Parcel* parcel);

    static void enable();
    static void disable();
    static Log* get()               { return spLog; }

    static void enableLogviewToLogcat( bool enable = true );

    static AbstractLogWrite* setLogVal(AbstractLogWrite* logVal,
                               validation::AbstractCompositionChecker* checkComposition,
                               uint32_t& versionSupportMask);

private:

    void        addInternal(uint32_t numDisplays, hwc_display_contents_1_t** pDisplays, uint32_t frameIndex, const char* description, va_list& args);
    void        addInternal(const Content::LayerStack& layers, const Layer& target, const char* description, va_list& args);
    void        addInternal(const Content::LayerStack& layers, const char* description, va_list& args);
    void        addInternal(const Content::Display& display, const char* description, va_list& args);
    void        addInternal(const Content& content, const char* description, va_list& args);

    const char* addInternal(const char* description, va_list& args);

    // Logger instance
    static Log*                 spLog;
    BasicLog*                   mLog;
    AbstractLogWrite*           mpLogWrite;  // In validation mode this may point to a different object.

    // String8 input parameters don't compile to nothing. Hence, making this private will cause an error on
    // any merges that havnt been updated to use char* strings.
    static void add(String8 fmt, ...);

// Composition validation support
private:
    void validate(const Content::LayerStack& layers, const Layer& target, const char* composer);

    // Interface for composition validation
    validation::AbstractCompositionChecker* mpCheckComposition;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_LOG_H
