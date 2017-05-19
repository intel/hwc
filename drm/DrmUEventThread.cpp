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

// #define LOG_TAG "ivpg-hwc.hotplug"

#include "Hwc.h"
#include "DrmUEventThread.h"
#include "Drm.h"
#include "DrmDisplay.h"
#include "AbstractPlatform.h"
#include "HwcService.h"

#include <sys/socket.h>
#include <linux/netlink.h>
#include <poll.h>

namespace intel {
namespace ufo {
namespace hwc {

#if !defined(DRM_MODE_CONNECTOR_DSI)
// Currently needed for GMIN builds where libdrm doesnt define this
#define DRM_MODE_CONNECTOR_DSI 16
#endif

// common UEvent paths relating to display.
#define UEVENT_NAME                 "change@/devices/pci0000:00/0000:00:02.0/drm/card0"
#define UEVENT_ACTION               "ACTION=change"
#define UEVENT_DEVPATH              "DEVPATH=/devices/pci0000:00/0000:00:02.0/drm/card0"
#define UEVENT_SUBSYSTEM            "SUBSYSTEM=drm"

// uevent plug event types
#define UEVENT_HOTPLUG0             "HOTPLUG=0"         // Seen on some customer kernel builds.
#define UEVENT_HOTPLUG1             "HOTPLUG=1"         // Common hotplug request
#define UEVENT_HOTPLUG_IMMINENT     "IMMINENT_HOTPLUG"  // Common hotplug request
#define UEVENT_MONITOR_CHANGE       "HDMI-Change"       // Hotplug happened during suspend

// uevent ESD event types
#define UEVENT_I915_DISPLAY_RESET_DSI   "I915_DISPLAY_RESET=DSI"
#define UEVENT_I915_DISPLAY_RESET_EDP   "I915_DISPLAY_RESET=EDP"
#define UEVENT_I915_CONNECTOR_ID        "CONNECTOR_ID="


DrmUEventThread::DrmUEventThread(Hwc& hwc, Drm& drm) :
    mHwc(hwc),
    mDrm(drm),
    mESDConnectorType(-1),
    mESDConnectorID(-1),
    mUeventFd(-1),
    mUeventMsgSize(0)
{
    memset(mUeventMsg, 0, sizeof(mUeventMsg));
}


DrmUEventThread::~DrmUEventThread()
{
}


void DrmUEventThread::onFirstRef()
{
    run("hwc.uevent", PRIORITY_NORMAL);
}


status_t DrmUEventThread::readyToRun()
{
    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid =  pthread_self() | getpid();
    addr.nl_groups = 0xffffffff;

    mUeventFd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (mUeventFd < 0) {
        ALOGE("failed to create uevent socket, %d", errno);
        return TIMED_OUT;
    }

    int sz = 256;
    int err = setsockopt(mUeventFd, SOL_SOCKET, SO_RCVBUFFORCE, &sz, sizeof(sz));
    // TODO: It seems that we dont care about this error from setsockopt (email form Pallavi)
    ALOGW_IF(HPLUG_DEBUG && err, "setsockopt failed: %d", err);

    if (bind(mUeventFd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        ALOGE("failed to bind uevent socket, %d", errno);
        close(mUeventFd);
        return TIMED_OUT;
    }

    return NO_ERROR;
}


bool DrmUEventThread::threadLoop()
{
    struct pollfd fds;
    fds.fd = mUeventFd;
    fds.events = POLLIN;
    fds.revents = 0;
    int nr = poll(&fds, 1, -1);

    if (nr > 0 && (fds.revents & POLLIN)) {
        mUeventMsgSize = recv(mUeventFd, mUeventMsg, sizeof(mUeventMsg)-1, 0);
        if (mUeventMsgSize > 0) {
            mUeventMsg[ mUeventMsgSize ] = '\0';
            onUEvent();
        }
        else
        {
            ALOGE("error recv from uevent socket, %zd, exiting hotplug thread", mUeventMsgSize);
            return false;
        }
    }
    return true;
}


Drm::UEvent DrmUEventThread::decodeUEvent( void )
{
#if HPLUG_DEBUG
    {
        const char* pchEnd = mUeventMsg + mUeventMsgSize;
        char* pchLine = mUeventMsg;
        while ( pchLine < pchEnd )
        {
            const uint32_t lineChars = strlen( pchLine );
            if ( !lineChars )
                break;
            ALOGD( "decodeHotPlugEvent [%s]", pchLine );
            pchLine += lineChars + 1;
        }
    }
#endif

    // This is the set of fields that we require for a "hotplug" event.
    static const char* requiredFields[] =
    {
        UEVENT_NAME,
        UEVENT_ACTION,
        UEVENT_DEVPATH,
        UEVENT_SUBSYSTEM
    };

    const char* pchEnd = mUeventMsg + mUeventMsgSize;
    char* pchLine = mUeventMsg;
    const uint32_t requiredFieldCount = sizeof( requiredFields )/sizeof( requiredFields[0] );
    uint8_t fieldsFound = 0;

    // Check each line.
    while ( fieldsFound < requiredFieldCount && pchLine < pchEnd )
    {
        const uint32_t lineChars = strlen(requiredFields[fieldsFound]);
        if (strncmp(pchLine, requiredFields[fieldsFound], lineChars) != 0)
        {
            ALOGD_IF( HPLUG_DEBUG, "decodeHotPlugEvent return UEVENT_UNRECOGNISED %s != %s", pchLine, requiredFields[fieldsFound]);
            return Drm::UEvent::UNRECOGNISED;
        }
        fieldsFound++;
        pchLine += lineChars + 1;
    }

    if ( fieldsFound == requiredFieldCount && pchLine < pchEnd )
    {
        // HOTPLUG uevents
        if (strncmp(pchLine, UEVENT_HOTPLUG1, sizeof(UEVENT_HOTPLUG1)) == 0 ||
            strncmp(pchLine, UEVENT_HOTPLUG0, sizeof(UEVENT_HOTPLUG0)) == 0 ||
            strncmp(pchLine, UEVENT_MONITOR_CHANGE, sizeof(UEVENT_MONITOR_CHANGE)) == 0)
        {
            ALOGD_IF( HPLUG_DEBUG, "decodeUEvent return HOTPLUG_CHANGED - %s", pchLine );
            return Drm::UEvent::HOTPLUG_CHANGED;
        }
        else if (strncmp(pchLine, UEVENT_HOTPLUG_IMMINENT, sizeof(UEVENT_HOTPLUG_IMMINENT)) == 0)
        {
            ALOGD_IF( HPLUG_DEBUG, "decodeUEvent return HOTPLUG_IMMINENT - %s", pchLine );
            return Drm::UEvent::HOTPLUG_IMMINENT;
        }
        // ESD uevent - Display Reset DSI
        else if( strncmp(pchLine, UEVENT_I915_DISPLAY_RESET_DSI, strlen(UEVENT_I915_DISPLAY_RESET_DSI)) == 0 )
        {
            ALOGD_IF( HPLUG_DEBUG, "decodeUEvent return UEVENT_ESD_RECOVERY - %s", pchLine );
            pchLine += sizeof(UEVENT_I915_DISPLAY_RESET_DSI) + strlen(UEVENT_I915_CONNECTOR_ID);
            ALOGD_IF( HPLUG_DEBUG, "  From connectorID: %s", pchLine );
            mESDConnectorID = atoi(pchLine);
            mESDConnectorType = DRM_MODE_CONNECTOR_DSI;
            return Drm::UEvent::ESD_RECOVERY;
        }
        // ESD uevent - Display Reset eDP
        else if( strncmp(pchLine, UEVENT_I915_DISPLAY_RESET_EDP, strlen(UEVENT_I915_DISPLAY_RESET_EDP)) == 0 )
        {
            ALOGD_IF( HPLUG_DEBUG, "decodeUEvent return UEVENT_ESD_RECOVERY - %s", pchLine );
            pchLine += sizeof(UEVENT_I915_DISPLAY_RESET_EDP) + strlen(UEVENT_I915_CONNECTOR_ID);
            ALOGD_IF( HPLUG_DEBUG, "  From connectorID: %s", pchLine );
            mESDConnectorID = atoi(pchLine);
            mESDConnectorType = DRM_MODE_CONNECTOR_eDP;;
            return Drm::UEvent::ESD_RECOVERY;
        }
    }

    ALOGD_IF( HPLUG_DEBUG, "decodeUEvent return UEVENT_UNRECOGNISED - %s", pchLine);
    return Drm::UEvent::UNRECOGNISED;
}

status_t DrmUEventThread::onUEvent( void )
{
    ATRACE_CALL_IF(DISPLAY_TRACE);

    Drm::UEvent eUE = decodeUEvent( );

    if (eUE == Drm::UEvent::UNRECOGNISED)
        return OK;

    // Block uevent processing while surfaceflinger is not ready.
    // This is to ensure any plug/unplug occurs *after* SF processing has commenced.
    mHwc.waitForSurfaceFlingerReady();

    // Forward the event to Drm.
    switch( eUE )
    {
        // Forward hotplug events to Drm.
        case Drm::UEvent::HOTPLUG_CHANGED:
        case Drm::UEvent::HOTPLUG_CONNECTED:
        case Drm::UEvent::HOTPLUG_DISCONNECTED:
        case Drm::UEvent::HOTPLUG_RECONNECT:
            {
                mDrm.onHotPlugEvent( eUE );
            }
            break;
         // Forward ESD event to Drm.
        case Drm::UEvent::ESD_RECOVERY:
            {
                mDrm.onESDEvent( eUE, mESDConnectorID, mESDConnectorType );
            }
            break;

        case Drm::UEvent::HOTPLUG_IMMINENT:
            {
            int64_t p;
            HwcService& hwcService = HwcService::getInstance();
            hwcService.notify(HwcService::ePavpDisableAllEncryptedSessions, 0, &p);
            }
            break;
        default:
            break;
    }

    return OK;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
