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

LOCAL_MODULE := libhwccommon$(INTEL_HWC_BUILD_EXTENSION)
include $(LOCAL_PATH)/../Android.common.mk
LOCAL_ADDITIONAL_DEPENDENCIES += $(LOCAL_PATH)/Android.mk $(LOCAL_PATH)/../Android.common.mk

LOCAL_SRC_FILES :=                      \
    BufferManager.cpp                   \
    BufferQueue.cpp                     \
    CompositionManager.cpp              \
    Content.cpp                         \
    Debug.cpp                           \
    DisplayCaps.cpp                     \
    SinglePlaneDisplayCaps.cpp          \
    DisplayQueue.cpp                    \
    EmptyFilter.cpp                     \
    FakeDisplay.cpp                     \
    FilterManager.cpp                   \
    GlCellComposer.cpp                  \
    GlobalScalingFilter.cpp             \
    Hwc.cpp                             \
    HwcService.cpp                      \
    InputAnalyzer.cpp                   \
    Layer.cpp                           \
    LayerBlanker.cpp                    \
    LogicalDisplay.cpp                  \
    LogicalDisplayManager.cpp           \
    Option.cpp                          \
    OptionManager.cpp                   \
    PartitionedComposer.cpp             \
    PassthroughDisplay.cpp              \
    PersistentRegistry.cpp              \
    PhysicalDisplay.cpp                 \
    PhysicalDisplayManager.cpp          \
    PlaneAllocatorJB.cpp                \
    PlaneComposition.cpp                \
    Rotate180Filter.cpp                 \
    SoftwareVsyncThread.cpp             \
    SurfaceFlingerComposer.cpp          \
    SurfaceFlingerProcs.cpp             \
    Timeline.cpp                        \
    Timer.cpp                           \
    Transform.cpp                       \
    TransparencyFilter.cpp              \
    VideoModeDetectionFilter.cpp        \
    VirtualDisplay.cpp                  \
    VisibleRectFilter.cpp

# Compile in debug support if this is an engineering build
ifeq ($(strip $(INTEL_HWC_INTERNAL_BUILD)),true)
    LOCAL_SRC_FILES += DebugFilter.cpp
endif
ifeq ($(strip $(INTEL_HWC_LOGVIEWER_BUILD)),true)
    LOCAL_SRC_FILES += Log.cpp
endif

ifeq ($(TARGET_FORCE_HWC_FOR_VIRTUAL_DISPLAYS),true)
    LOCAL_CFLAGS += -DFORCE_HWC_COPY_FOR_VIRTUAL_DISPLAYS=1
endif

LOCAL_EXPORT_C_INCLUDE_DIRS += $(LOCAL_PATH)
LOCAL_MULTILIB := $(INTEL_HWC_LIBRARY_MULTILIB)
include $(BUILD_STATIC_LIBRARY)
