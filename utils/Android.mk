# Copyright (c) 2017 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= Surface.cpp
LOCAL_MODULE:= hwcsurface
LOCAL_SHARED_LIBRARIES := libgui
LOCAL_SHARED_LIBRARIES += libgrallocclient
include $(LOCAL_PATH)/../Android.common.mk
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE:= hwclogviewer
LOCAL_SRC_FILES:= LogView.cpp
include $(LOCAL_PATH)/../Android.common.mk
ifeq ($(strip $(INTEL_HWC_LOGVIEWER_BUILD)),true)
    include $(BUILD_EXECUTABLE)
endif

include $(CLEAR_VARS)
LOCAL_MODULE:= hwcdiag
LOCAL_SRC_FILES:= Diag.cpp
include $(LOCAL_PATH)/../Android.common.mk
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE:= hwcmode
LOCAL_SRC_FILES:= Mode.cpp
include $(LOCAL_PATH)/../Android.common.mk
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE:= hwcblank
LOCAL_SRC_FILES:= Blank.cpp
LOCAL_SHARED_LIBRARIES := libgui
include $(LOCAL_PATH)/../Android.common.mk
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE:= hwcprotect
LOCAL_SRC_FILES:= Protect.cpp
include $(LOCAL_PATH)/../Android.common.mk
include $(BUILD_EXECUTABLE)
