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

#ifndef INTEL_UFO_HWC_ABSTRACTDISPLAY_H
#define INTEL_UFO_HWC_ABSTRACTDISPLAY_H

#include "Content.h"
#include "Timing.h"
#include "HwcServiceApi.h"

namespace intel {
namespace ufo {
namespace hwc {

class DisplayCaps;
class DisplayState;


// Base class of all displays.
class AbstractDisplay
{
public:

    typedef EHwcsScalingMode EScalingMode;

    enum EAttribute
    {
        ATTRIB_UNKNOWN,
        ATTRIB_WIDTH,
        ATTRIB_HEIGHT,
        ATTRIB_VSYNC,
        ATTRIB_XDPI,
        ATTRIB_YDPI,
    };

    // Default timeout for flush.
    static const nsecs_t    mTimeoutForFlush = 5000000000;

                            AbstractDisplay() {}
    virtual                 ~AbstractDisplay() {}

    // This returns the name of the display. Mainly used in debug paths
    virtual const char*     getName( void ) const = 0;

    // Entrypoint for SurfaceFlinger/Hwc.
    // Get display config handles.
    // Returns config handles upto *pNumConfigs (as set on entry).
    // On return *pNumConfig will be total config count.
    // NOTE: The first config represent the 'current' config.
    // Returns OK (0) if successful, negative on error.
    virtual int             onGetDisplayConfigs( uint32_t* paConfigHandles, uint32_t* pNumConfigs ) const = 0;

    // Entrypoint for SurfaceFlinger/Hwc.
    // Get display attributes for a specific config previously returned by onGetDisplayConfigs.
    // NOTE: The first config represent the 'current' config.
    // Returns OK (0) if successful, negative on error.
    virtual int             onGetDisplayAttribute( uint32_t configHandle, EAttribute attribute, int32_t* pValue ) const = 0;

    // Entrypoint for SurfaceFlinger/Hwc.
    // Get the active display config.
    // This returns an index into the list of configs previously returned by onGetDisplayConfigs.
    // NOTE: The first config represent the 'current' config.
    // Returns OK (0) if successful, negative on error.
    virtual int             onGetActiveConfig( void ) const = 0;

    // Entrypoint for SurfaceFlinger/Hwc.
    // Set a display config using an index into the list of configs
    // previously returned by onGetDisplayConfigs.
    // NOTE: The first config represent the 'current' config.
    // Returns OK (0) if successful, negative on error.
    virtual int             onSetActiveConfig( uint32_t configIndex ) = 0;

    // Entrypoint for SurfaceFlinger/Hwc - Enable or disable vsync generation from this display.
    // Display should call Hwc::notifyPhysicalVSync() when a vsync is generated.
    // Returns OK (0) if successful, negative on error.
    virtual int             onVSyncEnable( bool bEnable ) = 0;

    // Called when the display should be blanked.
    // This call can be made from both main SF thread and via service calls.
    // To avoid stalling the main SF thread it must complete without blocking.
    // If the display blanking may take a while to complete then it should be deferred to a worker.
    // If blanking is requested by surfaceflinger then bIsSurfaceFlinger will be true.
    // Returns OK (0) if successful, negative on error.
    virtual int             onBlank( bool bEnable, bool bIsSurfaceFlinger = false ) = 0;

    // This will drop any set frames that have not yet reached the display (for displays that implement a queue).
    // This must be thread safe.
    virtual void            dropAllFrames( void ) = 0;

    // This will block until the specified frame has reached the display.
    // If frameIndex is zero, then it will block until all applied state has reached the display.
    // It will only flush work that queued before flush is called.
    // If timeoutNs is zero then this is blocking.
    virtual void            flush( uint32_t frameIndex = 0, nsecs_t timeoutNs = mTimeoutForFlush ) = 0;

    // Get display capabilities.
    virtual const           DisplayCaps& getDisplayCaps( void ) const = 0;

    // Get display default output format.
    virtual int32_t         getDefaultOutputFormat( void ) const = 0;

    // Get the 'current' display timing.
    // This should be based on the config for all subsequent frames.
    // Returns true if mode is available.
    // Returns false if a mode has not yet been established.
    virtual bool            getTiming( Timing& timing ) const = 0;

    // Get the 'current' display refresh in Hz.
    // This should be based on the config for all subsequent frames.
    virtual uint32_t        getRefresh( void ) const = 0;

    // Get the 'current' display horizontal size in pixels.
    // This should be based on the config for all subsequent frames.
    virtual uint32_t        getWidth( void ) const = 0;

    // Get the 'current' display vertical size in pixels.
    // This should be based on the config for all subsequent frames.
    virtual uint32_t        getHeight( void ) const = 0;

    // Get the 'current' display X-axis DPI.
    // This should be based on the config for all subsequent frames.
    virtual int32_t         getXdpi( void ) const = 0;

    // Get the 'current' display Y-axis DPI.
    // This should be based on the config for all subsequent frames.
    virtual int32_t         getYdpi( void ) const = 0;

    // Get display type.
    virtual EDisplayType    getDisplayType( void ) const = 0;

    // Set the display manager specific index
    virtual void            setDisplayManagerIndex( uint32_t dmIndex ) = 0;

    // Get the display manager specific index
    virtual uint32_t        getDisplayManagerIndex( void ) const = 0;

    // Get a copy of the native display timings.
    // Returns the set of native display timings - any existing timings are removed first.
    // NOTE: Timings and indices can change across plug events.
    virtual void            copyDisplayTimings( Vector<Timing>& timings ) const = 0;

    // Get a copy of the default native timing.
    // NOTE: Timings and indices can change across plug events.
    virtual void            copyDefaultDisplayTiming( Timing& timing ) const = 0;

    // Set a native display timing.
    // Optionally, synchronize to ensure the mode is applied.
    // NOTE: This shortcuts the Hwc Service Api (setUserDisplayMode) and SF Api (onSetActiveConfig).
    // If pResultantTiming is provided then the final timing is returned (which may differ from the requested).
    // Returns true if OK.
    virtual bool            setDisplayTiming( const Timing& timing, bool bSynchronize = true, Timing* pResultantTiming = NULL ) = 0;

    // Entrypoint for HwcService user-mode API.
    // Set an overscan in the range +/-HWCS_MAX_OVERSCAN inclusive.
    // -ve : zoom/crop the image  (increase display overscan).
    // +ve : shrink the image (decrease display overscan).
    // The actual effect (range) is a +/-HWCS_OVERSCAN_RANGE percent.
    virtual void            setUserOverscan( int32_t xoverscan, int32_t yoverscan ) = 0;

    // Entrypoint for HwcService user-mode API.
    // Get overscan.
    virtual void            getUserOverscan( int32_t& xoverscan, int32_t& yoverscan ) const = 0;

    // Entrypoint for HwcService user-mode API.
    // Set scaling mode.
    virtual void            setUserScalingMode( EScalingMode eScaling ) = 0;

    // Entrypoint for HwcService user-mode API.
    // Get scaling mode.
    virtual void            getUserScalingMode( EScalingMode& eScaling ) const = 0;

    // Entrypoint for HwcService user-mode API.
    // Request the specified timing (or nearest match).
    // Use getUserDisplayTiming() to discover the timing selected.
    // NOTE:
    //  If bSynchronize is true this function call will not return until the mode set is complete.
    //  Else the mode set may not complete immediately (so getTiming() and getUserDisplayTiming() can differ).
    // Returns true if succesful.
    virtual bool            setUserDisplayTiming( const Timing& timing, bool bSynchronize = true ) = 0;

    // Entrypoint for HwcService user-mode API.
    // Get the last timing requested due to a call through setUserDisplayTiming.
    // Returns true if succesful or false if no user timing has been succesfully requested.
    virtual bool            getUserDisplayTiming( Timing& timing ) const = 0;

    // Entrypoint for HwcService user-mode API.
    // Reset previous user specified timing (mode selection will revert to default/preferred.)
    virtual void            resetUserDisplayTiming( void ) = 0;

    // Get human-readable string of state.
    virtual String8         dump( void ) const = 0;
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_ABSTRACTDISPLAY_H
