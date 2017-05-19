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

INTEL_HWC_ENABLE_BUILD := false
INTEL_HWC_EXPECTED_PATH := hardware/intel/hwc

# If this variable is set at all, then we check the value. If it matches the name of the
# directory that contains this Android.mk file then we build this version
ifneq ($(INTEL_HWC_ALWAYS_BUILD),)
    ifeq ($(INTEL_HWC_ALWAYS_BUILD), $(lastword $(subst /, ,$(LOCAL_PATH))))
        INTEL_HWC_ENABLE_BUILD := true
    endif
else
    ifneq ($(BOARD_GRAPHIC_IS_GEN), true)
        INTEL_HWC_ENABLE_BUILD := false
    else ifeq ($(INTEL_HWC_FROM_PREBUILTS), true)
        # Disable the source build if the config requests building from prebuilts.
        INTEL_HWC_ENABLE_BUILD := false
    else ifeq ($(LOCAL_PATH),$(INTEL_HWC_EXPECTED_PATH))
        # If we are building in the expected place in the repo, then enable
        INTEL_HWC_ENABLE_BUILD := true
    else ifeq ($(wildcard $(INTEL_HWC_EXPECTED_PATH)/Android.mk),)
        # If we are building elsewhere, and the expected place doesnt contain the HWC then enable
        # Eg in UFO/Source/Android/hwc when hardware/intel/hwc/Android.mk IS NOT present
        INTEL_HWC_ENABLE_BUILD := true
    else
        # If we are building elsewhere and the expected place does contain HWC, then skip this build.
        # Eg in UFO/Source/Android/hwc when hardware/intel/hwc/Android.mk IS present
        INTEL_HWC_ENABLE_BUILD := false
    endif
endif

ifeq ($(INTEL_HWC_ENABLE_BUILD),true)

    ifeq ($(strip $(INTEL_HWC_BUILD_PLATFORM)),)
        ifeq ($(TARGET_PRODUCT),saltbay)
            INTEL_HWC_BUILD_PLATFORM := mcg
        else
            INTEL_HWC_BUILD_PLATFORM := vpg
        endif
    endif

    # Select which platform to build
    HWC_BUILD_DIRS := \
        $(LOCAL_PATH)/common/Android.mk \
        $(LOCAL_PATH)/drm/Android.mk \
        $(LOCAL_PATH)/gen/Android.mk \
        $(LOCAL_PATH)/$(INTEL_HWC_BUILD_PLATFORM)/Android.mk \
        $(LOCAL_PATH)/libhwcservice/Android.mk

    # INTEL_HWC_BUILD_CONTROL_PANEL is set to decide if build HdmiSettings control panel.
    # It would be set from the BoardConfig most likely.
    ifeq ($(INTEL_HWC_BUILD_CONTROL_PANEL), true)
         HWC_BUILD_DIRS += $(LOCAL_PATH)/apk/HdmiSettings/Android.mk
    endif

    include $(HWC_BUILD_DIRS)

endif


