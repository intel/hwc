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

LOCAL_MODULE := libhwcdrm$(INTEL_HWC_BUILD_EXTENSION)
include $(LOCAL_PATH)/../Android.common.mk
LOCAL_ADDITIONAL_DEPENDENCIES += $(LOCAL_PATH)/Android.mk $(LOCAL_PATH)/../Android.common.mk

LOCAL_SRC_FILES :=                      \
    Drm.cpp                             \
    DrmDisplay.cpp                      \
    DrmDisplayCaps.cpp                  \
    DrmEventThread.cpp                  \
    DrmUEventThread.cpp                 \
    DrmPageFlipHandler.cpp              \
    DrmLegacyPageFlipHandler.cpp        \
    DrmNuclearPageFlipHandler.cpp       \
    DrmSetDisplayPageFlipHandler.cpp

LOCAL_STATIC_LIBRARIES += \
    libhwccommon$(INTEL_HWC_BUILD_EXTENSION)

LOCAL_EXPORT_C_INCLUDE_DIRS += $(LOCAL_PATH)
LOCAL_MULTILIB := $(INTEL_HWC_LIBRARY_MULTILIB)
include $(BUILD_STATIC_LIBRARY)
