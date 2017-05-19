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

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libhwcplatform$(INTEL_HWC_BUILD_EXTENSION)

LOCAL_ADDITIONAL_DEPENDENCIES += $(LOCAL_PATH)/Android.mk $(LOCAL_PATH)/../Android.common.mk

LOCAL_SRC_FILES :=              \
    McgDisplayCaps.cpp          \
    McgPlatform.cpp             \
    McgBufferManager.cpp

LOCAL_STATIC_LIBRARIES += \
    libhwccommon$(INTEL_HWC_BUILD_EXTENSION) libhwcdrm$(INTEL_HWC_BUILD_EXTENSION)

LOCAL_EXPORT_C_INCLUDE_DIRS += $(LOCAL_PATH)

include $(LOCAL_PATH)/../Android.common.mk
LOCAL_MULTILIB := $(INTEL_HWC_LIBRARY_MULTILIB)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_WHOLE_STATIC_LIBRARIES += libhwccommon$(INTEL_HWC_BUILD_EXTENSION) libhwcplatform$(INTEL_HWC_BUILD_EXTENSION) libhwcdrm$(INTEL_HWC_BUILD_EXTENSION)
LOCAL_SHARED_LIBRARIES +=
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_PLATFORM)$(INTEL_HWC_BUILD_EXTENSION)
include $(LOCAL_PATH)/../Android.common.mk
LOCAL_MULTILIB := $(INTEL_HWC_LIBRARY_MULTILIB)
include $(BUILD_SHARED_LIBRARY)

