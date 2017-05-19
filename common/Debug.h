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

#ifndef INTEL_UFO_HWC_DEBUG_H
#define INTEL_UFO_HWC_DEBUG_H

#define BUFFER_MANAGER_DEBUG            0 // Debug from buffer manager.
#define BUFFERQUEUE_DEBUG               0 // Debug from the BufferQueue class
#define COMPOSITION_DEBUG               0 // Debug related to the Hardware classes
#define COMPOSER_DEBUG                  0 // Debug from the GPU composer subsystems
#define CONTENT_DEBUG                   0 // Debug related to the Content classes
#define DISPLAY_QUEUE_DEBUG             0 // Debug from display queue.
#define DRM_DEBUG                       0 // Debug from DRM (general).
#define DRM_DISPLAY_DEBUG               0 // Debug from DRM display.
#define DRM_STATE_DEBUG                 0 // Debug from DRM state update.
#define DRM_SUSPEND_DEBUG               0 // Debug from DRM suspend/resume.
#define DRM_BLANKING_DEBUG              0 // Debug from DRM blanking.
#define DRM_PAGEFLIP_DEBUG              0 // Debug from DRM pageflip handler.
#define ESD_DEBUG                       0 // Debug from DRM ESD processing.
#define FILTER_DEBUG                    0 // Debugging from filters
#define GLOBAL_SCALING_DEBUG            0 // Debug from global scaling processing (includes panel fitter for DRM displays).
#define HWC_DEBUG                       0 // Dump HWC entrypoints
#define HWC_SYNC_DEBUG                  0 // Debug from HWC synchronization methods.
#define HWCLOG_DEBUG                    0 // Debug HWC logger
#define HPLUG_DEBUG                     0 // Debug from DRM hotplug processing.
#define LOGDISP_DEBUG                   0 // Debug related to the LogicalDisplay classes
#define LOWLOSS_COMPOSER_DEBUG          0 // Debug Lowloss composer
#define MDS_DEBUG                       0 // Debug from MDS (multidisplay server) and related.
#define MODE_DEBUG                      0 // Dump debug about mode enumeration/update.
#define MUTEX_CONDITION_DEBUG           0 // Debug mutex/conditions.
#define PAVP_DEBUG                      0 // Debug from PAVP.
#define PHYDISP_DEBUG                   0 // Debug related to the PhysicalDisplay classes
#define PARTITION_DEBUG                 0 // Partitioning info from the PartitionedComposer
#define PERSISTENT_REGISTRY_DEBUG       0 // Debug persistent registry.
#define PLANEALLOC_OPT_DEBUG            0 // Debug from plane allocator optimizer (detailed).
#define PLANEALLOC_CAPS_DEBUG           0 // Debug from plane allocator plane caps pre-check.
#define PLANEALLOC_SUMMARY_DEBUG        0 // Summary debug from plane allocator module.
#define PRIMARYDISPLAYPROXY_DEBUG       0 // Debug for Proxy Display
#define SYNC_FENCE_DEBUG                0 // Debug relating to sync fences
#define VIRTUALDISPLAY_DEBUG            0 // Debug from the Virtual Display subsystem
#define VISIBLERECTFILTER_DEBUG         0 // Debug VisibleRect Filter
#define VSYNC_DEBUG                     0 // Debug about vsync.
#define VSYNC_RATE_DEBUG                0 // Debug about vsync issue rate.
#define WIDI_DEBUG                      0 // Dump debug from WIDI display

// Mode related debug combo.
#define DRMDISPLAY_MODE_DEBUG ( DRM_DEBUG || MODE_DEBUG || HPLUG_DEBUG || DRM_SUSPEND_DEBUG || DRM_BLANKING_DEBUG )

// Dump HWC input state on prepare
#define PREPARE_INFO_DEBUG      (0 || DRM_DEBUG)

// Dump HWC input state on set
#define SET_INFO_DEBUG          (0 || DRM_DEBUG)

// Trace enabling tags
#define DISPLAY_TRACE                   sbInternalBuild
#define DRM_CALL_TRACE                  sbInternalBuild
#define HWC_TRACE                       sbInternalBuild
#define RENDER_TRACE                    sbInternalBuild
#define BUFFER_WAIT_TRACE               sbInternalBuild
#define TRACKER_TRACE                   sbInternalBuild

#include <cutils/log.h>

#include <hardware/hwcomposer.h>
#include <utils/String8.h>

// Trace support
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include <utils/Trace.h>

namespace android
{
    class Mutex;
    class Condition;
};

namespace intel {
namespace ufo {
namespace hwc {

using namespace android;
class Layer;

// Utility function - returns human-readable string from a HAL format number.
const char* getHALFormatString( int32_t halFormat );
const char* getHALFormatShortString( int32_t halFormat );
// Utility function - returns human-readable string from a DRM format number.
const char* getDRMFormatString( int32_t drmFormat );

// This ScopedTrace function compiles away properly when disabled. Android's one doesnt, it
// leaves strings and atrace calls in the code.
class HwcScopedTrace
{
public:
    inline HwcScopedTrace(bool bEnable, const char* name)
        : mbEnable(bEnable)
    {
        if (mbEnable)
            atrace_begin(ATRACE_TAG_GRAPHICS,name);
    }

    inline ~HwcScopedTrace()
    {
        if (mbEnable)
            atrace_end(ATRACE_TAG_GRAPHICS);
    }
private:
    bool mbEnable;
};

// Conditional variants of the macros in utils/Trace.h
#define ATRACE_CALL_IF(enable)              HwcScopedTrace ___tracer(enable, __FUNCTION__)
#define ATRACE_NAME_IF(enable, name)        HwcScopedTrace ___tracer(enable, name)
#define ATRACE_INT_IF(enable, name, value)  do { if ( enable ) { ATRACE_INT( name, value ); } } while (0)
#define ATRACE_EVENT_IF(enable, name)       do { ATRACE_INT_IF( enable, name, 1 ); ATRACE_INT_IF( enable, name, 0 ); } while (0)

extern String8 printLayer(hwc_layer_1_t& layer);
extern void dumpDisplayContents(const char *pIdentifier, hwc_display_contents_1_t* pDisp, uint32_t frameIndex);
extern void dumpDisplaysContents(const char *pIdentifier, size_t numDisplays, hwc_display_contents_1_t** displays, uint32_t frameIndex);

}; // namespace hwc
}; // namespace ufo
}; // namespace intel


#if INTEL_HWC_INTERNAL_BUILD
#if INTEL_HWC_DEV_ASSERTS_BUILD
// Developer assert as real ALOG_ASSERT.
#define INTEL_HWC_DEV_ASSERT( C, ... ) LOG_FATAL_IF( !(C), "ASSERT: !(" #C ") " __VA_ARGS__ )
#else
// Developer assert relaxed to a LOGE.
#define INTEL_HWC_DEV_ASSERT( C, ... ) ALOGE_IF( !(C), "ASSERT: !(" #C ") " __VA_ARGS__ )
#endif
#else
// Developer asserts removed entirely for non-internal builds.
#define INTEL_HWC_DEV_ASSERT( C, ... ) ((void)0)
#endif

#define VPG_HAVE_DEBUG_MUTEX            (INTEL_HWC_INTERNAL_BUILD)

#if VPG_HAVE_DEBUG_MUTEX


#include <utils/Mutex.h>
#include <utils/Condition.h>

namespace intel {
namespace ufo {
namespace hwc {


// Wrapper Mutex and Condition classes that add some debug and trap deadlocks.
class Mutex
{
    public:
        static const uint64_t mLongTime = 1000000000;  //< 1 second.
        static const uint32_t mSpinWait = 1000;        //< 1 millisecond.
        Mutex( );
        ~Mutex( );
        int lock( );
        int unlock( );
        bool isHeld( void );
        void incWaiter( void );
        void decWaiter( void );
        uint32_t getWaiters( void );
        class Autolock
        {
            public:
                Autolock( Mutex& m );
                ~Autolock( );
            private:
                Mutex& mMutex;
        };
    private:
        friend class Condition;
        bool mbInit:1;
        android::Mutex mMutex;
        pid_t   mTid;
        nsecs_t mAcqTime;
        uint32_t mWaiters;
};

class Condition
{
    public:
        Condition( );
        ~Condition( );
        int waitRelative( Mutex& mutex, nsecs_t timeout );
        int wait( Mutex& mutex );
        void signal( );
        void broadcast( );
    private:
        bool mbInit:1;
        uint32_t mWaiters;
        android::Condition mCondition;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#define INTEL_UFO_HWC_ASSERT_MUTEX_HELD( M ) ALOG_ASSERT( M.isHeld() );
#define INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( M ) ALOG_ASSERT( !M.isHeld() );

#else

#define INTEL_UFO_HWC_ASSERT_MUTEX_HELD( M ) do { ; } while (0)
#define INTEL_UFO_HWC_ASSERT_MUTEX_NOT_HELD( M ) do { ; } while (0)

#endif // VPG_HAVE_DEBUG_MUTEX

#endif // INTEL_UFO_HWC_DEBUG_H
