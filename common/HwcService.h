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

#ifndef INTEL_UFO_HWC_HWCSERVICE_H
#define INTEL_UFO_HWC_HWCSERVICE_H

#include <list>

#include "Log.h"
#include "IService.h"
#include "IControls.h"
#include "IDisplayControl.h"
#include "IDiagnostic.h"
#include "IVideoControl.h"
#include "IMDSExtModeControl.h"
#include <binder/IInterface.h>
#include "Singleton.h"

namespace intel {
namespace ufo {
namespace hwc {

class Hwc;

using namespace intel::ufo::hwc::services;

class HwcService : public BnService, public Singleton<HwcService>
{
public:
    class Diagnostic : public BnDiagnostic
    {
    public:
        Diagnostic(Hwc& hwc) : mHwc(hwc) { HWC_UNUSED( mHwc ); }

        virtual status_t readLogParcel(Parcel* parcel);
        virtual void enableDisplay(uint32_t d);
        virtual void disableDisplay(uint32_t d, bool bBlank);
        virtual void maskLayer(uint32_t d, uint32_t layer, bool bHide);
        virtual void dumpFrames(uint32_t d, int32_t frames, bool bSync);
    private:
        Hwc&                mHwc;
    };

    bool start(Hwc& hwc);

    sp<IDiagnostic> getDiagnostic();
    sp<IControls>   getControls();

    // Dummys
    sp<IDisplayControl> getDisplayControl(uint32_t display);
    sp<IVideoControl>   getVideoControl();
    sp<IMDSExtModeControl> getMDSExtModeControl();

    android::String8 getHwcVersion();

    void     dumpOptions( void );
    status_t setOption( android::String8 option, android::String8 optionValue );
    status_t enableLogviewToLogcat( bool enable = true );

    class Controls : public BnControls
    {
    public:
        Controls(Hwc& hwc, HwcService& hwcService);
        virtual ~Controls();

        status_t displaySetOverscan(uint32_t display, int32_t xoverscan, int32_t yoverscan);
        status_t displayGetOverscan(uint32_t display, int32_t *xoverscan, int32_t *yoverscan);
        status_t displaySetScaling(uint32_t display, EHwcsScalingMode eScalingMode);
        status_t displayGetScaling(uint32_t display, EHwcsScalingMode *eScalingMode);
        status_t displayEnableBlank(uint32_t display, bool blank);
        status_t displayRestoreDefaultColorParam(uint32_t display, EHwcsColorControl color);
        status_t displayGetColorParam(uint32_t display, EHwcsColorControl color, float *value, float *startvalue, float *endvalue);
        status_t displaySetColorParam(uint32_t display, EHwcsColorControl color, float value);

        android::Vector<HwcsDisplayModeInfo> displayModeGetAvailableModes(uint32_t display);
        status_t displayModeGetMode(uint32_t display, HwcsDisplayModeInfo *pMode);
        status_t displayModeSetMode(uint32_t display, const HwcsDisplayModeInfo *pMode);

        status_t videoEnableEncryptedSession(uint32_t sessionID, uint32_t instanceID);
        status_t videoDisableEncryptedSession(uint32_t sessionID);
        status_t videoDisableAllEncryptedSessions();
        bool videoIsEncryptedSessionEnabled(uint32_t sessionID, uint32_t instanceID);
        bool needSetKeyFrameHint();
        status_t videoSetOptimizationMode(EHwcsOptimizationMode mode);
        status_t mdsUpdateVideoState(int64_t videoSessionID, bool isPrepared);
        status_t mdsUpdateVideoFPS(int64_t videoSessionID, int32_t fps);
        status_t mdsUpdateInputState(bool state);
        status_t widiGetSingleDisplay(bool *pEnabled);
        status_t widiSetSingleDisplay(bool enable);

    private:
        Hwc&                  mHwc;
        HwcService&           mHwcService;
        bool                  mbHaveSessionsEnabled;
        EHwcsOptimizationMode mCurrentOptimizationMode;
    };

    enum ENotification
    {
        eInvalidNofiy    = 0,
        eOptimizationMode,
        eMdsUpdateVideoState,
        eMdsUpdateInputState,
        eMdsUpdateVideoFps,
        ePavpEnableEncryptedSession,
        ePavpDisableEncryptedSession,
        ePavpDisableAllEncryptedSessions,
        ePavpIsEncryptedSessionEnabled,
        eWidiGetSingleDisplay,
        eWidiSetSingleDisplay,
        eNeedSetKeyFrameHint,
    };

    class NotifyCallback
    {
    public:
        virtual ~NotifyCallback() {}
        virtual void notify(ENotification notify, int32_t paraCnt, int64_t para[]) = 0;
    };

    void registerListener(ENotification notify, NotifyCallback* pCallback);
    void unregisterListener(ENotification notify, NotifyCallback* pCallback);
    void notify(ENotification notify, int32_t paraCnt, int64_t para[]);
private:
    friend class Singleton<HwcService>;

    HwcService();
    virtual ~HwcService();

    struct Notification
    {
        Notification() : mWhat(eInvalidNofiy), mpCallback(NULL) {}
        Notification(ENotification what, NotifyCallback* pCallback) :
            mWhat(what), mpCallback(pCallback) {}
        ENotification mWhat;
        NotifyCallback* mpCallback;
    };

    Mutex                           mLock;
    Hwc*                            mpHwc;

    sp<IDiagnostic>                 mpDiagnostic;

    std::vector<Notification>       mNotifications;
};

}   // hwc
}   // vpg
}   // intel
#endif // INTEL_UFO_HWC_HWC_H
