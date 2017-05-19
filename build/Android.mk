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

# Ensure all debug is enabled in any HWC userdebug builds.
BUILD_TYPE := release-internal

# Want dev asserts for these builds.
INTEL_HWC_WANT_DEV_ASSERTS := true


vpg_modules := $(call all-named-subdir-makefiles,../common ../drm ../gen ../vpg)
mcg_modules := $(call all-named-subdir-makefiles,../common ../drm ../mcg)
common_modules := $(call all-named-subdir-makefiles,../libhwcservice ../utils ../lib)
ifneq ($(JENKINS_URL)$(INTEL_HWC_ALWAYS_BUILD_UFO),)
    ufo_modules := $(wildcard $(patsubst %,hardware/intel/ufo/ufo/Source/Android/%/Android.mk,include gralloc iVP core/coreu))
endif


include $(common_modules) $(vpg_modules) $(ufo_modules)

# Disable dev asserts for other build configs.
INTEL_HWC_WANT_DEV_ASSERTS := false

# Build these with a user config to ensure no build errors
INTEL_HWC_SIMULATE_USER_BUILD := true
INTEL_HWC_BUILD_EXTENSION := .user
include $(vpg_modules) $(widi_modules)
INTEL_HWC_SIMULATE_USER_BUILD := false

# Build these with a mcg config to ensure no build errors
INTEL_HWC_BUILD_EXTENSION := .mcg
INTEL_HWC_BUILD_PLATFORM := mcg
include $(mcg_modules)
INTEL_HWC_BUILD_PLATFORM :=

