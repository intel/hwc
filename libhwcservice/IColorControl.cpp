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

#include "IDisplayControl.h"

using namespace intel::ufo::hwc::services;


/**
 */
class BpColorControl : public BpInterface<IColorControl>
{
public:
    BpColorControl(const sp<IBinder>& impl)
        : BpInterface<IColorControl>(impl)
    {
    }

    enum {
        TRANSACT_RESTORE_DEFAULT = IBinder::FIRST_CALL_TRANSACTION,
        TRANSACT_GET_COLOR_PARAM,
        TRANSACT_SET_COLOR_PARAM,
    };

    virtual status_t restoreDefault()
    {
        Parcel data;
        Parcel reply;
        data.writeInterfaceToken(getInterfaceDescriptor());
        status_t ret = remote()->transact(TRANSACT_RESTORE_DEFAULT, data, &reply);
        if (ret != NO_ERROR) {
            ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
            return ret;
        }
        return reply.readInt32();
    }

    virtual status_t getColorParam(int32_t *value, int32_t *startvalue, int32_t *endvalue)
    {
        if (!value || !startvalue || !endvalue) {
            return android::BAD_VALUE;
        }
        Parcel data;
        Parcel reply;
        data.writeInterfaceToken(getInterfaceDescriptor());
        status_t ret = remote()->transact(TRANSACT_GET_COLOR_PARAM, data, &reply);
        if (ret != NO_ERROR) {
            ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
            return ret;
        }
        *value = reply.readInt32();
        *startvalue = reply.readInt32();
        *endvalue = reply.readInt32();
        return reply.readInt32();
    }

    virtual status_t setColorParam(int32_t value)
    {
        Parcel data;
        Parcel reply;
        data.writeInterfaceToken(getInterfaceDescriptor());
        data.writeInt32(value);
        status_t ret = remote()->transact(TRANSACT_SET_COLOR_PARAM, data, &reply);
        if (ret != NO_ERROR) {
            ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
            return ret;
        }
        return reply.readInt32();
    }
};


IMPLEMENT_META_INTERFACE(ColorControl, "intel.ufo.hwc.color.control");


status_t BnColorControl::onTransact(
        uint32_t code,
        const Parcel& data,
        Parcel* reply,
        uint32_t flags)
{
    //const int uid = IPCThreadState::self()->getCallingUid();
    //if (uid != AID_DIAG) {
    //    ALOGE("Permission denied! uid=%d", uid);
    //    return PERMISSION_DENIED;
    //}

    switch(code) {
    case BpColorControl::TRANSACT_RESTORE_DEFAULT:
        {
            CHECK_INTERFACE(IColorControl, data, reply);
            status_t ret = this->restoreDefault();
            reply->writeInt32(ret);
            return NO_ERROR;
        }
    case BpColorControl::TRANSACT_GET_COLOR_PARAM:
        {
            CHECK_INTERFACE(IColorControl, data, reply);
            int32_t value;
            int32_t startvalue;
            int32_t endvalue;
            status_t ret = this->getColorParam(&value, &startvalue, &endvalue);
            reply->writeInt32(value);
            reply->writeInt32(startvalue);
            reply->writeInt32(endvalue);
            reply->writeInt32(ret);
            return NO_ERROR;
        }
    case BpColorControl::TRANSACT_SET_COLOR_PARAM:
        {
            CHECK_INTERFACE(IColorControl, data, reply);
            int32_t value = data.readInt32();
            status_t ret = this->setColorParam(value);
            reply->writeInt32(ret);
            return NO_ERROR;
        }
    default:
        return BBinder::onTransact(code, data, reply, flags);
    }
}

