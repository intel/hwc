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
#include "Log.h"
#include "Layer.h"
#include "AbstractLog.h"
#include "AbstractCompositionChecker.h"
#include <utils/Mutex.h>
#include "Option.h"
#include "OptionManager.h"


namespace intel {
namespace ufo {
namespace hwc {;

#define ENTRY_LOST_MASK 0x8000

// This is primarily a debug logging class expected to generate data thats expected
// to be used by the validation team to check that the HWC is operating correctly.
class BasicLog : public AbstractLogRead, public AbstractLogWrite, public NonCopyable
{
public:
    BasicLog(uint32_t maxLogSize = 32 * 1024);
    ~BasicLog();

    virtual char*       read(uint32_t& size, bool& lost);
    Mutex&              getLock();
    void                setLogviewToLogcat(bool enable);

protected:
    virtual char*       reserve(uint32_t maxSize);
    virtual void        log(char* endPtr);

private:
    Option                  mOptionLogSizeK;
    char*                   mLogBuf;
    char*                   mFront;
    char*                   mBack;
    bool                    mbLogviewToLogcat;
    uint32_t                mAllocatedSize;
    Mutex                   mLock;
};

BasicLog::BasicLog(uint32_t maxLogSize) :
    mOptionLogSizeK("debuglogbufk", 64),
    mbLogviewToLogcat(false)
{
    int32_t logSizeK = mOptionLogSizeK;
    if (logSizeK < 16) logSizeK = 16;
    if (logSizeK > 1024) logSizeK = 1024;
    maxLogSize = logSizeK * 1024;

    mLogBuf = new char[maxLogSize + sizeof(uint16_t)];
    ALOGD_IF(HWCLOG_DEBUG, "Log: Allocated HWC Log buffer %d bytes @%p", maxLogSize, mLogBuf);

    ALOG_ASSERT(mLogBuf != 0);
    mFront = mBack = mLogBuf;
    mAllocatedSize = maxLogSize;
}

BasicLog::~BasicLog()
{
    Mutex::Autolock _l(mLock);
    delete [] mLogBuf;
}

char* BasicLog::reserve(uint32_t maxSize)
{
    mLock.lock();
    ALOGD_IF(HWCLOG_DEBUG, "Log: OpenNewLogEntry mBack=%p", mBack);
    char* possibleEntryEnd = mBack + maxSize;
    if (possibleEntryEnd > mLogBuf + mAllocatedSize)
    {
        if (mFront > mBack)
        {
            ALOGD_IF(HWCLOG_DEBUG, "Log: Discarding remaining entries at %p and base entry at %p", mFront, mLogBuf);
            mFront = mLogBuf + ((*((uint16_t*)mLogBuf)) & ~ENTRY_LOST_MASK);
            *((uint16_t*)mFront) |= ENTRY_LOST_MASK;
        }
        // Mark the rest of the buffer is not used
        *((uint16_t*)mBack) = 0;
        mBack = mLogBuf;
    }

    // Discard any entries we MIGHT overflow on to
    while ((mBack + maxSize > mFront) && (mBack < mFront))
    {
        uint16_t entrySize = *((uint16_t*)mFront) & ~ENTRY_LOST_MASK;

        if (entrySize == 0)
        {
            mFront = mLogBuf;
        }
        else
        {
            ALOGD_IF(HWCLOG_DEBUG, "Log: Discarding %d byte entry at %p",entrySize,mFront);
            mFront += entrySize;
        }

        *((uint16_t*)mFront) |= ENTRY_LOST_MASK;
        ALOGV_IF(HWCLOG_DEBUG, "Log: Now front=%p",mFront);
    }
    ALOGD_IF(HWCLOG_DEBUG, "Log: OpenNewLogEntry returning mBack=%p", mBack);
    return mBack + sizeof(uint16_t);
}

void BasicLog::log(char* endPtr)
{
    ALOGD_IF(HWCLOG_DEBUG, "Log: %zd byte entry written @ %p", endPtr - mBack, mBack);
    if ((mBack < mFront) && (endPtr > mFront))
    {
        ALOGE("Log error : entry @ %p too big (%zd bytes) - resetting log", mBack, endPtr - mBack);
        if (endPtr > mLogBuf + mAllocatedSize)
        {
            ALOGE("WARNING: Buffer at [%p - %p], entry ends @ %p, possible memory corruption", mLogBuf, mLogBuf + mAllocatedSize, endPtr);
        }
        mFront = mBack = mLogBuf;
    }
    else
    {
        *((uint16_t*)mBack) = (uint16_t)(endPtr - mBack);

        if (mbLogviewToLogcat)
        {
            logToLogcat(mBack + sizeof(uint16_t));
        }

        mBack = endPtr;
    }

    mLock.unlock();
}

char* BasicLog::read(uint32_t& size, bool& lost)
{
    // Caller must place a lock on mLock
    if (mFront == mBack)
    {
        // Log empty
        return 0;
    }
    else
    {
        uint16_t entryHeader = (*((uint16_t*) mFront));
        if (entryHeader == 0)
        {
            mFront = mLogBuf;
            entryHeader = (*((uint16_t*) mFront));
        }

        char* entry = mFront + sizeof(uint16_t);
        bool entryLost = (entryHeader & ENTRY_LOST_MASK) != 0;
        uint32_t entrySize = entryHeader & ~ENTRY_LOST_MASK;

        if (entryLost)
        {
            ALOGD_IF(HWCLOG_DEBUG, "Log: Entry/ies lost");
        }

        if (entrySize < sizeof(uint16_t))
        {
            ALOGE("Log error : Entry length %d at %p - resetting log", entrySize, mFront);
            mFront = mBack = mLogBuf;
            return 0;
        }

        ALOGD_IF(HWCLOG_DEBUG, "Log: %d byte entry read at %p", entrySize, mFront);
        mFront += entrySize;

        size = entrySize - sizeof(uint16_t);
        lost = entryLost;
        return entry;
    }
}

void BasicLog::setLogviewToLogcat(bool enable)
{
    mbLogviewToLogcat = enable;
}

intel::ufo::hwc::Mutex& BasicLog::getLock()
{
    return mLock;
}

Log* Log::spLog = NULL;

Log::Log() :
    mpLogWrite(0),
    mpCheckComposition(NULL)
{
    mLog = new BasicLog;
    mpLogWrite = mLog;
}

Log::~Log()
{
    mpLogWrite = NULL;
    delete mLog;
}

void Log::enable()
{
    if (spLog == NULL)
    {
        spLog = new Log();
    }
}

void Log::disable()
{
    if (spLog)
    {
        Log *pLog = spLog;
        spLog = NULL;
        delete pLog;
    }
}

static const char* compositionTypeString(uint32_t type)
{
    switch (type)
    {
    case HWC_FRAMEBUFFER:
        return "FB";
    case HWC_BACKGROUND:
        return "BG";
    case HWC_OVERLAY:
        return "OV";
    case HWC_FRAMEBUFFER_TARGET:
        return "TG";
    default:
        return "  ";
    }
}

const char* Log::addInternal(const char* fmt, va_list& args)
{
    return mpLogWrite->addV(fmt, args);
}

void Log::addInternal(uint32_t numDisplays, hwc_display_contents_1_t** pDisplays, uint32_t frameIndex, const char* description, va_list& args)
{
    for (uint32_t d = 0; d < numDisplays; d++)
    {
        hwc_display_contents_1_t* pDisp = pDisplays[d];
        if (pDisp == NULL)
            continue;

        String8 output = String8::format("SF%u %s frame:%u Fd:%d outBuf:%p outFd:%d flags:%x",
                  d, description, frameIndex,
                  pDisp->retireFenceFd, pDisp->outbuf,
                  pDisp->outbufAcquireFenceFd, pDisp->flags);

        for (uint32_t ly = 0; ly < pDisp->numHwLayers; ly++)
        {
            Layer layer;
            layer.onUpdateAll(pDisp->hwLayers[ly]);
            output.appendFormat("\n  %d %s %s", ly, compositionTypeString(pDisp->hwLayers[ly].compositionType), layer.dump().string());
        }
        mpLogWrite->addV(output.string(), args);
    }
}

void Log::addInternal(const Content::LayerStack& layers, const char* description, va_list& args)
{
    String8 output = String8::format("%s", description) + layers.dumpHeader();
    for (uint32_t ly = 0; ly < layers.size(); ly++)
    {
        const Layer& layer = layers.getLayer(ly);
        output.appendFormat("\n  %d    %s", ly, layer.dump().string());
    }
    mpLogWrite->addV(output.string(), args);
    return;
}

void Log::addInternal(const Content::LayerStack& layers, const Layer& target, const char* description, va_list& args)
{
    // Validation callback
    if (mpCheckComposition)
    {
        validate(layers, target, description);
    }

    String8 output = String8::format("%s", description) + layers.dumpHeader() ;
    for (uint32_t ly = 0; ly < layers.size(); ly++)
    {
        const Layer& layer = layers.getLayer(ly);
        output.appendFormat("\n  %d    %s", ly, layer.dump().string());
    }
    output.appendFormat("\n  %d RT %s", layers.size(), target.dump().string());
    mpLogWrite->addV(output.string(), args);
}

void Log::addInternal(const Content::Display& display, const char* description, va_list& args)
{
    addInternal(display.getLayerStack(),
                String8::format("%s %s", description, display.dumpHeader().string()).string(),
                args );
}


void Log::addInternal(const  Content& content, const char* description, va_list& args)
{
    for (size_t d = 0; d < content.size(); d++)
    {
        Content::Display display = content.getDisplay(d);
        if (display.isEnabled())
        {
            addInternal(display,  String8::format("%s%zd", description, d).string(), args);
        }
    }
}

status_t Log::readLogParcel(Parcel* reply)
{
    // If we are receiving a request to read a log, then the user wants logging enabled.
    // Note, first call will always return not enabled
    enable();

    if (spLog)
    {
        Mutex::Autolock _l(spLog->mLog->getLock());
        uint32_t size = 0;
        bool lost;
        const char* entry = spLog->mLog->read(size, lost);
        uint32_t totalSize = size + 8;

        int i=0;

        if (entry)
        {
            while (entry)
            {
                if (lost)
                {
                    ALOGD_IF(HWCLOG_DEBUG, "Log: Lost entries: status=eLogTruncated");
                    reply->writeInt32(IDiagnostic::eLogTruncated);
                }
                else
                {
                    reply->writeInt32(NO_ERROR);
                }

                reply->writeInt32(size);
                ALOGD_IF(HWCLOG_DEBUG, "Writing log entry(%d) @ [%p:%d] to parcel %p total size will be %d", i, entry, size, reply,totalSize);
                reply->write(entry, size);

                if (++i > 100)
                {
                    break;
                }

                entry = spLog->mLog->read(size, lost);
                totalSize += size + 8;
            }
        }
        reply->writeInt32(NOT_ENOUGH_DATA);
    }
    return 0;
}

void Log::enableLogviewToLogcat( bool en )
{
    if( en == true )
    {
        enable();
        if (spLog)
        {
            spLog->mLog->setLogviewToLogcat(true);
        }
    }
    else
    {
        if (spLog)
        {
            spLog->mLog->setLogviewToLogcat(false);
        }
        disable();
    }
}

static uint32_t convertToHwc1Transform(ETransform transform)
{
    uint32_t t;
    switch (transform)
    {
        case ETransform::NONE      : t = 0                            ; break;
        case ETransform::FLIP_H    : t = HWC_TRANSFORM_FLIP_H         ; break;
        case ETransform::FLIP_V    : t = HWC_TRANSFORM_FLIP_V         ; break;
        case ETransform::ROT_90    : t = HWC_TRANSFORM_ROT_90         ; break;
        case ETransform::ROT_180   : t = HWC_TRANSFORM_ROT_180        ; break;
        case ETransform::ROT_270   : t = HWC_TRANSFORM_ROT_270        ; break;
        case ETransform::FLIP_H_90 : t = HWC_TRANSFORM_FLIP_H | HWC_TRANSFORM_ROT_90  ; break;
        case ETransform::FLIP_V_90 : t = HWC_TRANSFORM_FLIP_V | HWC_TRANSFORM_ROT_90  ; break;
    }
    return t;
}

static uint32_t convertToHwc1Blending(EBlendMode blend)
{
    uint32_t b;
    switch (blend)
    {
        case EBlendMode::NONE       : b = HWC_BLENDING_NONE            ; break;
        case EBlendMode::PREMULT    : b = HWC_BLENDING_PREMULT         ; break;
        case EBlendMode::COVERAGE   : b = HWC_BLENDING_COVERAGE        ; break;
    }
    return b;
}

class InternalValLayer : public validation::AbstractCompositionChecker::ValLayer
{
public:
    // Construct a temporary layer for passing to validation
    InternalValLayer(const Layer& layer);
};

// Construct a temporary layer for passing to validation
// Caller must declare rects with enough space for layer.getVisibleRegions().size() entries
InternalValLayer::InternalValLayer(const Layer& layer)
{
    handle = layer.getHandle();
    transform = convertToHwc1Transform(layer.getTransform());
    blending = convertToHwc1Blending(layer.getBlending());
    sourceCropf = layer.getSrc();
    displayFrame = layer.getDst();
    visibleRegionScreen.numRects = layer.getVisibleRegions().size();
    visibleRegionScreen.rects = layer.getVisibleRegions().array();

    compositionType = Log::HWC_IRRELEVANT_COMPOSITION_TYPE;
    acquireFenceFd = layer.getAcquireFence();
    releaseFenceFd = layer.getReleaseFence();
    planeAlpha = layer.getPlaneAlpha()*255; // Val expects alpha in the 0-255 range
}

void Log::validate(const Content::LayerStack& layers, const Layer& target, const char* composer)
{
    if (sbLogViewerBuild && mpCheckComposition)
    {
        void* ctx = mpCheckComposition->CreateContext(composer);

        if (ctx)
        {
            for (uint32_t i=0; i<layers.size(); ++i)
            {
                const Layer& layer = layers.getLayer(i);
                InternalValLayer valLayer(layer);
                mpCheckComposition->AddSource(ctx, valLayer, composer);
            }

            InternalValLayer valLayer(target);
            mpCheckComposition->CheckComposition(ctx, valLayer, composer);
        }
    }
}

// Override the default log writer with the one passed, returning the original.
// And set the composition checker object too.
AbstractLogWrite* Log::setLogVal(AbstractLogWrite* logVal,
                               validation::AbstractCompositionChecker* checkComposition,
                               uint32_t& versionSupportMask)
{
    // Enable HWC logging when this validation call is issued
    enable();

    if (spLog)
    {
        spLog->mpCheckComposition = checkComposition;

        // Give HWCVAL the pointer to the real logger
        AbstractLogWrite* ret = spLog->mLog;

        // All HWC logging redirected to validation
        spLog->mpLogWrite = logVal;

        // Dump the options to the hwclog, since some will already have been logged by this point.
        Log::add(OptionManager::getInstance().dump().string());

        versionSupportMask = ABSTRACTCOMPOSITIONCHECKER_VERSION_SUPPORT_MASK;
        return ret;
    }
    else
    {
        return 0;
    }
}

extern "C" ANDROID_API AbstractLogWrite* hwcSetLogVal(AbstractLogWrite* logVal,
                                                    validation::AbstractCompositionChecker* checkComposition,
                                                    uint32_t& versionSupportMask)
{
    return Log::setLogVal(logVal, checkComposition, versionSupportMask);
}

class InitLog {
public:
    // Tiny helper class to cause logging to start from the constructors if intel.hwc.initlog is set to 1
    InitLog()
    {
        Option initlog("initlog", 0, false);
        if (initlog)
            Log::enable();
    }
} gInitLog;

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
