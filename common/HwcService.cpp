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
#include "OptionManager.h"
#include "LogicalDisplay.h"
#include "HwcService.h"
#include "AbstractPlatform.h"
#include "PlatformServices.h"

#include <binder/IInterface.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>

#if INTEL_HWC_INTERNAL_BUILD
#include "DebugFilter.h"
#endif

#define HWC_VERSION_STRING "VERSION: " HWC_VERSION_GIT_BRANCH " " HWC_VERSION_GIT_SHA " " __DATE__ " " __TIME__

// Log calls and statistics (timing) for calls to enableProtected/disableProtected/disableallProtected.
#define INTEL_UFO_HWC_WANT_PAVP_API_TIMING INTEL_HWC_INTERNAL_BUILD && 1

namespace intel {
namespace ufo {
namespace hwc {

// Conversion functions
Timing::EAspectRatio UintToEAspectRatio(uint32_t ar)
{
    Timing::EAspectRatio aspectratio = Timing::EAspectRatio::Any;
    switch(ar)
    {
        case HWCS_MODE_ASPECT_RATIO_ANY:
            aspectratio = Timing::EAspectRatio::Any;
            break;
        case HWCS_MODE_ASPECT_RATIO_4_3:
            aspectratio = Timing::EAspectRatio::R4_3;
            break;
        case HWCS_MODE_ASPECT_RATIO_16_9:
            aspectratio = Timing::EAspectRatio::R16_9;
            break;
        default:
            ALOGE("MODE Aspect Ration is not valid: %d - use AR:Any instead.", ar);
    }
    return aspectratio;
}

uint32_t EAspectRatioToUint(Timing::EAspectRatio ar)
{
    uint32_t aspectratio = HWCS_MODE_ASPECT_RATIO_ANY;
    switch(ar)
    {
        case Timing::EAspectRatio::Any:
            aspectratio = HWCS_MODE_ASPECT_RATIO_ANY;
            break;
        case Timing::EAspectRatio::R4_3:
            aspectratio = HWCS_MODE_ASPECT_RATIO_4_3;
            break;
        case Timing::EAspectRatio::R16_9:
            aspectratio = HWCS_MODE_ASPECT_RATIO_16_9;
            break;
        default:
            ALOGE("Timing AspectRatio is not valid: %s - use AR:Any instead.", Timing::dumpRatio(ar).string());
    }
    return aspectratio;
}

HwcService::HwcService() :
    mpHwc(NULL)
{
}

HwcService::~HwcService()
{
}

bool HwcService::start(Hwc& hwc)
{
    mpHwc = &hwc;
    sp<IServiceManager> sm(defaultServiceManager());
    if (sm->addService(String16(INTEL_HWC_SERVICE_NAME), this, false))
    {
        ALOGE("Failed to start %s service", INTEL_HWC_SERVICE_NAME);
        return false;
    }
    return true;
}

String8 HwcService::getHwcVersion()
{
    return String8(HWC_VERSION_STRING);
}

status_t HwcService::setOption( String8 option, String8 value )
{
    Option* pOption = OptionManager::find(option);
    if ( pOption )
    {
        if (pOption->isStringProperty())
        {
            // Use string API
            pOption->set(value);
        }
        else
        {
            // Use integer API
            pOption->set(atoi(value));
        }
        return OK;
    }
    return BAD_VALUE;
}

void HwcService::dumpOptions( void )
{
    ALOGD("%s", OptionManager::getInstance().dump().string());
}

status_t HwcService::enableLogviewToLogcat( bool enable )
{
    if (sbInternalBuild || sbLogViewerBuild)
    {
        Mutex::Autolock _l(mLock);
        Log::enableLogviewToLogcat( enable );
    }
    return BAD_VALUE;
}

sp<IDiagnostic> HwcService::getDiagnostic()
{
    if (sbInternalBuild || sbLogViewerBuild)
    {
        Mutex::Autolock _l(mLock);
        ALOG_ASSERT( mpHwc );
        if (mpDiagnostic == NULL)
            mpDiagnostic = new Diagnostic(*mpHwc);
    }
    return mpDiagnostic;
}

sp<IControls> HwcService::getControls()
{
    Mutex::Autolock _l(mLock);
    ALOG_ASSERT( mpHwc );
    return new Controls(*mpHwc, *this);
}

sp<IDisplayControl> HwcService::getDisplayControl(uint32_t)
{
    // Dummy.
    return NULL;
}
sp<IVideoControl>   HwcService::getVideoControl()
{
    // Dummy.
    return NULL;
}

sp<IMDSExtModeControl> HwcService::getMDSExtModeControl()
{
    // Dummy.
    return NULL;
}

status_t HwcService::Diagnostic::readLogParcel(Parcel* parcel)
{
    if (sbLogViewerBuild)
        return Log::readLogParcel(parcel);
    else
        return INVALID_OPERATION;
}

#if INTEL_HWC_INTERNAL_BUILD
void HwcService::Diagnostic::enableDisplay(uint32_t d)
{
    if (sbInternalBuild)
        DebugFilter::get().enableDisplay(d);
}

void HwcService::Diagnostic::disableDisplay(uint32_t d, bool bBlank)
{
    if (sbInternalBuild)
        DebugFilter::get().disableDisplay(d, bBlank);
}

void HwcService::Diagnostic::maskLayer(uint32_t d, uint32_t layer, bool bHide)
{
    if (sbInternalBuild)
        DebugFilter::get().maskLayer(d, layer, bHide);
}

void HwcService::Diagnostic::dumpFrames(uint32_t d, int32_t frames, bool bSync)
{
    if (sbInternalBuild)
    {
        DebugFilter::get().dumpFrames(d, frames);
        if ( bSync )
        {
            mHwc.synchronize();
        }
    }
}

#else
void HwcService::Diagnostic::enableDisplay(uint32_t)                { /* nothing */ }
void HwcService::Diagnostic::disableDisplay(uint32_t, bool)         { /* nothing */ }
void HwcService::Diagnostic::maskLayer(uint32_t, uint32_t, bool)    { /* nothing */ }
void HwcService::Diagnostic::dumpFrames(uint32_t, int32_t, bool)    { /* nothing */ }
#endif

HwcService::Controls::Controls(Hwc &hwc, HwcService& hwcService) :
    mHwc(hwc),
    mHwcService(hwcService),
    mbHaveSessionsEnabled(false),
    mCurrentOptimizationMode(HWCS_OPTIMIZE_NORMAL)
{
}

HwcService::Controls::~Controls()
{
    if (mbHaveSessionsEnabled)
    {
        videoDisableAllEncryptedSessions();
    }

    if (mCurrentOptimizationMode != HWCS_OPTIMIZE_NORMAL)
    {
        // Reset mode back to normal if needed
        videoSetOptimizationMode(HWCS_OPTIMIZE_NORMAL);
    }
}

#define HWCS_ENTRY_FMT(fname, fmt, ...) \
    const char* ___HWCS_FUNCTION = fname; \
    Log::add(fname " " fmt " -->", __VA_ARGS__)

#define HWCS_ENTRY(fname)  \
    const char* ___HWCS_FUNCTION = fname; \
    Log::add(fname " -->")

#define HWCS_ERROR(code) \
    Log::add("%s ERROR %d <--", ___HWCS_FUNCTION, code)

#define HWCS_EXIT_ERROR(code) do { \
    int ___code = code; \
    HWCS_ERROR(___code); \
    return ___code; } while(0)

#define HWCS_OK_FMT(fmt, ...) \
    Log::add("%s OK " fmt " <--", ___HWCS_FUNCTION, __VA_ARGS__);

#define HWCS_EXIT_OK_FMT(fmt, ...) do { \
    HWCS_OK_FMT(fmt, __VA_ARGS__); \
    return OK; } while(0)

#define HWCS_EXIT_OK() do { \
    Log::add("%s OK <--", ___HWCS_FUNCTION); \
    return OK; } while(0)

#define HWCS_EXIT_VAR(code) do { \
    int ____code = code; \
    if (____code == OK) HWCS_EXIT_OK(); \
    HWCS_EXIT_ERROR(____code); \
    } while(0)

#define HWCS_EXIT_VAR_FMT(code, fmt, ...) do { \
    int ____code = code; \
    if (____code == OK) HWCS_EXIT_OK_FMT(fmt, __VA_ARGS__); \
    HWCS_EXIT_ERROR(____code); \
    } while(0)

status_t HwcService::Controls::displaySetOverscan(uint32_t display, int32_t xoverscan, int32_t yoverscan)
{
    HWCS_ENTRY_FMT("HwcService_Display_SetOverscan", "D%u %dx%d", display, xoverscan, yoverscan);

    LogicalDisplay* pDisp = mHwc.getSurfaceFlingerDisplay(display);
    if (pDisp == NULL)
        HWCS_EXIT_ERROR(BAD_VALUE);

    // valid range:[-HWCS_MAX_OVERSCAN, HWCS_MAX_OVERSCAN]
    if ( (xoverscan > HWCS_MAX_OVERSCAN) || (xoverscan < -HWCS_MAX_OVERSCAN) ||
         (yoverscan > HWCS_MAX_OVERSCAN) || (yoverscan < -HWCS_MAX_OVERSCAN) )
         HWCS_EXIT_ERROR(BAD_VALUE);

    pDisp->setUserOverscan(xoverscan, yoverscan);
    HWCS_EXIT_OK();
}

status_t HwcService::Controls::displayGetOverscan(uint32_t display, int32_t *xoverscan, int32_t *yoverscan)
{
    HWCS_ENTRY_FMT("HwcService_Display_GetOverscan", "D%u", display);

    // Implement via scaling filter.
    if ( ( xoverscan == NULL ) ||
         ( yoverscan == NULL ) )
        HWCS_EXIT_ERROR(BAD_VALUE);

    LogicalDisplay* pDisp = mHwc.getSurfaceFlingerDisplay(display);
    if (pDisp == NULL)
        HWCS_EXIT_ERROR(BAD_VALUE);

    pDisp->getUserOverscan(*xoverscan, *yoverscan);
    HWCS_EXIT_OK_FMT("%ux%u", *xoverscan, *yoverscan);
}

status_t HwcService::Controls::displaySetScaling(uint32_t display, EHwcsScalingMode eScalingMode)
{
    HWCS_ENTRY_FMT("HwcService_Display_SetScaling", "D%u %u", display, (unsigned)eScalingMode);

    // Implement via scaling filter.
    LogicalDisplay* pDisp = mHwc.getSurfaceFlingerDisplay(display);
    if (pDisp == NULL || eScalingMode >= HWCS_SCALE_MAX_ENUM)
        HWCS_EXIT_ERROR(BAD_VALUE);
    pDisp->setUserScalingMode(eScalingMode);
    HWCS_EXIT_OK();
}

status_t HwcService::Controls::displayGetScaling(uint32_t display, EHwcsScalingMode *peScalingMode)
{
    HWCS_ENTRY_FMT("HwcService_Display_GetScaling", "D%u", display);

    // Implement via scaling filter.
    LogicalDisplay* pDisp = mHwc.getSurfaceFlingerDisplay(display);
    if ((peScalingMode == NULL) || (pDisp == NULL))
        HWCS_EXIT_ERROR(BAD_VALUE);

    pDisp->getUserScalingMode(*peScalingMode);
    HWCS_EXIT_OK_FMT("%u", (unsigned)*peScalingMode);
}

status_t HwcService::Controls::displayEnableBlank(uint32_t display, bool blank)
{
    HWCS_ENTRY_FMT("HwcService_Display_EnableBlank", "D%u %u", display, (unsigned)blank);
    HWCS_EXIT_VAR(mHwc.onBlank(display, blank != 0, PhysicalDisplayManager::BLANK_HWCSERVICE));
}

status_t HwcService::Controls::displayRestoreDefaultColorParam(uint32_t display, EHwcsColorControl color)
{
    HWCS_ENTRY_FMT("HwcService_Display_RestoreDefaultColorParam", "D%u C:%u", display, (unsigned)color);
    // TODO Need to implement
    ALOGE("%s not Implemented", __PRETTY_FUNCTION__);
    HWCS_EXIT_ERROR(INVALID_OPERATION);
}

status_t HwcService::Controls::displayGetColorParam(uint32_t display, EHwcsColorControl color, float *, float *, float *)
{
    HWCS_ENTRY_FMT("HwcService_Display_GetColorParam", "D%u C:%u", display, (unsigned)color);
    // TODO Need to implement
    ALOGE("%s not Implemented", __PRETTY_FUNCTION__);
    HWCS_EXIT_ERROR(INVALID_OPERATION);
}

status_t HwcService::Controls::displaySetColorParam(uint32_t display, EHwcsColorControl color, float value)
{
    HWCS_ENTRY_FMT("HwcService_Display_SetColorParam", "D%u C:%u %f", display, (unsigned)color, value);
    // TODO Need to implement
    ALOGE("%s not Implemented", __PRETTY_FUNCTION__);
    HWCS_EXIT_ERROR(INVALID_OPERATION);
}

Vector<HwcsDisplayModeInfo> HwcService::Controls::displayModeGetAvailableModes(uint32_t display)
{
    HWCS_ENTRY_FMT("HwcService_DisplayMode_GetAvailableModes", "D%u", display);
    Vector<HwcsDisplayModeInfo> modes;

    LogicalDisplay* pDisp = mHwc.getSurfaceFlingerDisplay(display);
    if (pDisp == NULL)
    {
        HWCS_ERROR(BAD_VALUE);
        return modes;
    }

    Vector<Timing> timings;
    pDisp->copyDisplayTimings( timings );

    for (Timing& t : timings)
    {
        HwcsDisplayModeInfo info;
        info.width = t.getWidth();
        info.height = t.getHeight();
        info.refresh = t.getRefresh();
        info.ratio = EAspectRatioToUint(t.getRatio());
        info.flags = 0;

        if (t.getFlags() & Timing::Flag_Preferred)
            info.flags |= HWCS_MODE_FLAG_PREFERRED;

        if (t.getFlags() & Timing::Flag_Interlaced)
            info.flags |= HWCS_MODE_FLAG_INTERLACED;

        // Remove any identical duplicates. Note, any duplicates of the preferred mode that arnt
        // preferred can be removed.
        bool bNeedToAdd = true;
        for (HwcsDisplayModeInfo& m : modes)
        {
            if (m.width   == info.width &&
                m.height  == info.height &&
                m.refresh == info.refresh &&
                m.ratio == info.ratio &&
                (m.flags & ~Timing::Flag_Preferred) == (info.flags & ~Timing::Flag_Preferred))
            {
                bNeedToAdd = false;
                break;
            }
        }
        if (bNeedToAdd)
        {
            modes.push_back(info);
        }
    }
    if (Log::wantLog())
    {
        String8 outLog;
        for (HwcsDisplayModeInfo& m : modes)
        {
            outLog += String8::format("%ux%u@%u F:%u, A:%u ", m.width, m.height, m.refresh, m.flags, m.ratio);
        }
        HWCS_OK_FMT("%s", outLog.string());
    }
    return modes;
}

status_t HwcService::Controls::displayModeGetMode(uint32_t display, HwcsDisplayModeInfo *pMode)
{
    HWCS_ENTRY_FMT("HwcService_DisplayMode_GetMode", "D%u", display);
    LogicalDisplay* pDisp = mHwc.getSurfaceFlingerDisplay(display);
    if (pDisp == NULL)
        HWCS_EXIT_ERROR(BAD_VALUE);

    // Maybe use timing is better
    Timing timing;
    if ( !pDisp->getUserDisplayTiming( timing ) )
    {
        return BAD_VALUE;
    }
    pMode->width = timing.getWidth( );
    pMode->height = timing.getHeight( );
    pMode->refresh = timing.getRefresh( );
    pMode->flags = timing.getFlags( );
    pMode->ratio = EAspectRatioToUint(timing.getRatio( ));
    HWCS_EXIT_OK_FMT("{%u, %u, %u, %u, %u}", pMode->width, pMode->height, pMode->refresh, pMode->flags, pMode->ratio);
}

status_t HwcService::Controls::displayModeSetMode(uint32_t display, const HwcsDisplayModeInfo *pMode)
{
    HWCS_ENTRY_FMT("HwcService_DisplayMode_SetMode", "D%u %ux%u@%u, F:%u, A:%u", display, pMode->width, pMode->height, pMode->refresh, pMode->flags, pMode->ratio);
    LogicalDisplay* pDisp = mHwc.getSurfaceFlingerDisplay(display);
    if (pDisp == NULL)
        HWCS_EXIT_ERROR(BAD_VALUE);

    uint32_t outFlags = 0;
    if (pMode->flags & HWCS_MODE_FLAG_INTERLACED)
        outFlags |= Timing::Flag_Interlaced;
    // Note - we do not exit if AR value in pMode->ratio is not valid - we just use AR:Any in this case.
    Timing::EAspectRatio aspectratio = UintToEAspectRatio(pMode->ratio);
    Timing timing(pMode->width, pMode->height, pMode->refresh, /*pixelclock*/ 0,
                  /*hTotal*/0, /*hTotal*/0, aspectratio, outFlags);
    pDisp->setUserDisplayTiming(timing);
    HWCS_EXIT_OK();
}

status_t HwcService::Controls::videoEnableEncryptedSession( uint32_t sessionID, uint32_t instanceID )
{
    HWCS_ENTRY_FMT("HwcService_Video_EnableEncryptedSession", "sessionID:%u instanceID:%u", sessionID, instanceID);

    Log::add( "Hwc service enable protected sessionID:%u instanceID:%u", sessionID, instanceID );

#if INTEL_UFO_HWC_WANT_PAVP_API_TIMING
    ALOGI( "Enabling protected sessionID:%u instanceID:%u", sessionID, instanceID );
    static uint32_t minms = ~0U;
    static uint32_t maxms = 0;
    static uint32_t totms = 0;
    static uint32_t count = 0;
    static uint32_t avgms = 0;
    nsecs_t time1 = systemTime(SYSTEM_TIME_MONOTONIC);
#endif

    mbHaveSessionsEnabled = true;

    int64_t p[2];
    p[0] = sessionID;
    p[1] = instanceID;
    mHwcService.notify(HwcService::ePavpEnableEncryptedSession, 2, p);

#if INTEL_UFO_HWC_WANT_PAVP_API_TIMING
    nsecs_t time2 = systemTime(SYSTEM_TIME_MONOTONIC);
    uint64_t ela = (uint64_t)(int64_t)(time2 - time1);
    uint32_t elams = ela/1000000;
    if ( elams )
    {
        if ( elams > maxms )
            maxms = elams;
        if ( elams < minms )
            minms = elams;
        totms += elams;
        count++;
        avgms = (totms / count );
    }
    ALOGI( "Enabled protected sessionID:%u instanceID:%u :: elapsed: %u [MIN %u, MAX %u, AVG %u]",
        sessionID, instanceID, elams, minms, maxms, avgms );
    ALOGI( "*******************************************************************");
#endif

    HWCS_EXIT_OK();
}

status_t HwcService::Controls::videoDisableEncryptedSession( uint32_t sessionID )
{
    HWCS_ENTRY_FMT("HwcService_Video_DisableEncryptedSession","sessionID:%u", sessionID);

    Log::add( "Hwc service disable protected sessionID:%u", sessionID );

#if INTEL_UFO_HWC_WANT_PAVP_API_TIMING
    ALOGI( "Disabling protected sessionID:%u", sessionID );
    static uint32_t minms = ~0U;
    static uint32_t maxms = 0;
    static uint32_t totms = 0;
    static uint32_t count = 0;
    static uint32_t avgms = 0;
    nsecs_t time1 = systemTime(SYSTEM_TIME_MONOTONIC);
#endif

    int64_t p = sessionID;
    mHwcService.notify(HwcService::ePavpDisableEncryptedSession, 1, &p);

#if INTEL_UFO_HWC_WANT_PAVP_API_TIMING
    nsecs_t time2 = systemTime(SYSTEM_TIME_MONOTONIC);
    uint64_t ela = (uint64_t)(int64_t)(time2 - time1);
    uint32_t elams = ela/1000000;
    if ( elams )
    {

        if ( elams > maxms )
            maxms = elams;
        if ( elams < minms )
            minms = elams;
        totms += elams;
        count++;
        avgms = totms / count;
    }
    ALOGI( "Disabled protected sessionID:%u :: elapsed: %u [MIN %u, MAX %u, AVG %u]",
        sessionID, elams, minms, maxms, avgms );
    ALOGI( "*******************************************************************");
#endif

    HWCS_EXIT_OK();
}

status_t HwcService::Controls::videoDisableAllEncryptedSessions( )
{
    HWCS_ENTRY("HwcService_Video_DisableAllEncryptedSessions");

    Log::add( "Hwc service disable all protected sessions" );

#if INTEL_UFO_HWC_WANT_PAVP_API_TIMING
    ALOGI( "Disabling all protected sessions" );
    static uint32_t minms = ~0U;
    static uint32_t maxms = 0;
    static uint32_t totms = 0;
    static uint32_t count = 0;
    static uint32_t avgms = 0;
    nsecs_t time1 = systemTime(SYSTEM_TIME_MONOTONIC);
#endif

    int64_t p;
    mHwcService.notify(HwcService::ePavpDisableAllEncryptedSessions, 0, &p);


#if INTEL_UFO_HWC_WANT_PAVP_API_TIMING
    nsecs_t time2 = systemTime(SYSTEM_TIME_MONOTONIC);
    uint64_t ela = (uint64_t)(int64_t)(time2 - time1);
    uint32_t elams = ela/1000000;
    if ( elams )
    {

        if ( elams > maxms )
            maxms = elams;
        if ( elams < minms )
            minms = elams;
        totms += elams;
        count++;
        avgms = totms / count;
    }
    ALOGI( "Disabled all protected :: elapsed: %u [MIN %u, MAX %u, AVG %u]",
        elams, minms, maxms, avgms );
    ALOGI( "*******************************************************************");
#endif

    HWCS_EXIT_OK();
}

bool HwcService::Controls::videoIsEncryptedSessionEnabled( uint32_t sessionID, uint32_t instanceID )
{
    HWCS_ENTRY_FMT("HwcService_Video_IsEncryptedSessionEnabled","sessionID:%u instanceID:%u", sessionID, instanceID);
    bool result = false;
    int64_t p[3];
    p[0] = sessionID;
    p[1] = instanceID;
    p[2] = result;

    mHwcService.notify(HwcService::ePavpIsEncryptedSessionEnabled, 3, p);
    HWCS_EXIT_OK_FMT("%d", (unsigned)p[2]);
}

bool HwcService::Controls::needSetKeyFrameHint()
{
    HWCS_ENTRY("HwcService_needSetKeyFrameHint");
    int64_t p;
    mHwcService.notify(HwcService::eNeedSetKeyFrameHint, 1, &p);
    HWCS_EXIT_OK_FMT("%d", (unsigned)p);
}

status_t HwcService::Controls::videoSetOptimizationMode( EHwcsOptimizationMode mode )
{
    HWCS_ENTRY_FMT("HwcService_Video_SetOptimizationMode", "%u", mode);

    if (mode < HWCS_OPTIMIZE_NORMAL || mode > HWCS_OPTIMIZE_CAMERA)
        HWCS_EXIT_ERROR(BAD_VALUE);

    // Reset back to eNormal if we were previously optimized just in case
    // an implementation is refcounting these.
    if (mCurrentOptimizationMode != HWCS_OPTIMIZE_NORMAL)
    {
        Log::add( "HwcService::Controls::videoSetOptimizationMode %d->HWCS_OPTIMIZE_NORMAL", mCurrentOptimizationMode);
        int64_t p = HWCS_OPTIMIZE_NORMAL;
        mHwcService.notify(HwcService::eOptimizationMode, 1, &p);
        mCurrentOptimizationMode = HWCS_OPTIMIZE_NORMAL;
    }

    // Set it to the desired mode if we are no longer normal
    if (mode != HWCS_OPTIMIZE_NORMAL)
    {
        Log::add( "HwcService::Controls::videoSetOptimizationMode HWCS_OPTIMIZE_NORMAL->%d", mode);
        int64_t p = mode;
        mHwcService.notify(HwcService::eOptimizationMode, 1, &p);
        mCurrentOptimizationMode = mode;
    }
    HWCS_EXIT_OK();
}

status_t HwcService::Controls::mdsUpdateVideoState(int64_t videoSessionID, bool isPrepared)
{
    HWCS_ENTRY_FMT("HwcService_MDS_UpdateVideoState", "session:%" PRId64 ", prepared:%u", videoSessionID, (unsigned)isPrepared);
    int64_t p[2];
    p[0] = videoSessionID;
    p[1] = isPrepared;
    ALOGD_IF( MDS_DEBUG, "HwcService -- Set video state to %d for session %" PRId64 "", isPrepared, videoSessionID);
    mHwcService.notify(eMdsUpdateVideoState, 2, p);
    HWCS_EXIT_OK();
}

status_t HwcService::Controls::mdsUpdateVideoFPS(int64_t videoSessionID, int32_t fps)
{
    HWCS_ENTRY_FMT("HwcService_MDS_UpdateVideoFPS", "session:%" PRId64 ", fps:%" PRId32 "", videoSessionID, fps);
    int64_t p[2];
    p[0] = videoSessionID;
    p[1] = fps;
    ALOGD_IF( MDS_DEBUG, "HwcService -- Set FPS to %d for session %" PRId64 "", fps, videoSessionID);
    mHwcService.notify(eMdsUpdateVideoFps, 2, p);
    HWCS_EXIT_OK();
}

status_t HwcService::Controls::mdsUpdateInputState(bool state)
{
    HWCS_ENTRY_FMT("HwcService_MDS_UpdateInputState", "%u", (unsigned)state);
    int64_t p = state;
    ALOGD_IF( MDS_DEBUG, "HwcService -- Set input state to %d", state);
    mHwcService.notify(eMdsUpdateInputState, 1, &p);
    HWCS_EXIT_OK();
}

status_t HwcService::Controls::widiGetSingleDisplay(bool *pEnabled)
{
    HWCS_ENTRY("HwcService_Widi_GetSingleDisplay");
    int64_t p = 0;
    ALOGD_IF(WIDI_DEBUG, "HwcService -- Get Widi Single Display");
    mHwcService.notify(eWidiGetSingleDisplay, 1, &p);
    *pEnabled = p ? true : false;
    HWCS_EXIT_OK_FMT("%u", (unsigned)(pEnabled && *pEnabled));
}

status_t HwcService::Controls::widiSetSingleDisplay(bool enable)
{
    HWCS_ENTRY_FMT("HwcService_Widi_SetSingleDisplay", "%u", (unsigned)enable);
    int64_t p = (int64_t)enable;
    ALOGD_IF(WIDI_DEBUG, "HwcService -- Set Widi Single Display: %s", enable ? "true": "false");
    mHwcService.notify(eWidiSetSingleDisplay, 1, &p);
    HWCS_EXIT_VAR(p);
}

void HwcService::registerListener(ENotification notify, NotifyCallback* pCallback)
{
    Mutex::Autolock _l(mLock);
    Notification n(notify, pCallback);
    mNotifications.push_back(n);
}

void HwcService::unregisterListener(ENotification notify, NotifyCallback* pCallback)
{
    mNotifications.erase(std::remove_if(mNotifications.begin(), mNotifications.end(),
                                        [=](const Notification &n)
                                        { return n.mWhat == notify && n.mpCallback == pCallback; }),
                         mNotifications.end());
}

void HwcService::notify(ENotification notify, int32_t paraCnt, int64_t para[])
{
    Mutex::Autolock _l(mLock);
    for (Notification& n : mNotifications)
    {
        if (n.mWhat == notify)
        {
            n.mpCallback->notify(notify, paraCnt, para);
        }
    }
}


}   // hwc
}   // ufo
}   // intel
