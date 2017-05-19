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

#ifndef INTEL_UFO_HWC_ABSTRACTPHYSICALDISPLAY_H
#define INTEL_UFO_HWC_ABSTRACTPHYSICALDISPLAY_H

#include "Content.h"
#include "AbstractDisplay.h"

namespace intel {
namespace ufo {
namespace hwc {

class DisplayCaps;

// Base class of all displays to be enumerated by the PhysicalDisplayManager.
class AbstractPhysicalDisplay : public AbstractDisplay
{
public:
    // Flags to adjust findDisplayMode behaviour.
    enum
    {
        FIND_MODE_FLAG_FALLBACK_TO_DEFAULT      = (1<<0),   //< If a match is not found then fallback to default.
        FIND_MODE_FLAG_CLOSEST_REFRESH_MULTIPLE = (1<<1),   //< Match refresh rate to the closest whole multiple.
    };

    // Is this display only enabled as the backend of the primary proxy?
    virtual void        setProxyOnly( bool bProxyOnly ) = 0;

    // Is this display only enabled as the backend of the primary proxy?
    virtual bool        getProxyOnly( void ) const = 0;

    // Set all modes.
    virtual void        setDisplayTimings( Vector<Timing>& timings ) = 0;

    // This must be called if display timings are modified.
    virtual void        notifyTimingsModified( void ) = 0;

    // Get the 'current' display timing index.
    // This should be the timing for all subsequent frames.
    // NOTE: Timings and indices can change across plug events.
    virtual uint32_t    getTimingIndex( void ) const = 0;

    // Get a copy of a specific timing.
    // NOTE: Timings and indices can change across plug events.
    virtual bool        copyDisplayTiming( uint32_t timingIndex, Timing& timing ) const = 0;

    // Pixels per inch in X given a specific timing mode.
    virtual int32_t     getXdpiForTiming( const Timing& t ) const = 0;

    // Pixels per inch in Y given a specific timing mode.
    virtual int32_t     getYdpiForTiming( const Timing& t ) const = 0;

    // Get default timing index.
    // Returns -1 if the mode could not be established.
    // NOTE: Timings and indices can change across plug events.
    virtual int32_t     getDefaultDisplayTiming( void ) const = 0;

    // Look up a timing.
    // Returns a timing index or -1 if a timing is not found.
    // NOTE: Timings and indices can change across plug events.
    virtual int32_t     findDisplayTiming( const Timing& timing,
                                           uint32_t findFlags = FIND_MODE_FLAG_FALLBACK_TO_DEFAULT ) const = 0;

    // Set specific timing by index.
    // Optionally, synchronize to ensure the mode is applied.
    // Returns true if OK.
    // NOTE: Timings and indices can change across plug events.
    virtual bool        setSpecificDisplayTiming( uint32_t timingIndex, bool bSynchronize = true ) = 0;

    // Acquire and configure global scaling.
    // Specify the effective source size and the display destination window position.
    // A derived display class must implement this if it supports global scaling.
    // The implementation must return true only if the scaling is supported.
    // Returns false if the settings are not valid or if the global scaling can not be acquired.
    virtual bool        acquireGlobalScaling( uint32_t srcW, uint32_t srcH,
                                              int32_t dstX, int32_t dstY,
                                              uint32_t dstW, uint32_t dstH ) = 0;

    // Release the global scaling previously acquired with acquireGlobalScaling( ).
    // Returns true if global scaling is released.
    // A derived display class must implement this if it supports global scaling.
    virtual bool        releaseGlobalScaling( void ) = 0;

    // Some displays may need to adapt (their capabilities) to the display output format.
    virtual void        updateOutputFormat( int32_t format ) = 0;

    // This requests a set of layers to be sent to the screen. One layer per sprite plane.
    // All non-Virtual displays SHOULD return a retire fence (even if the frame is dropped).
    // NOTE:
    //   Following onSet the PhysicalDisplayManager will close all acquire fences automatically.
    //   If the layer acquire fences are used but not processed synchronously then the display must dup them.
    virtual void        onSet( const Content::Display& display, uint32_t zorder, int* pRetireFenceFd ) = 0;


    // This is called by SW vsync thread when a software vsync event is generated.
    virtual void        postSoftwareVSync( void ) = 0;

    // Reconnect hotplugable device.
    virtual void        reconnect( void ) = 0;

    // Called before a display is added or after a display is removed.
    // This updates the number of active hardware displays.
    // Returns true if this display acknowledges the change,
    //  in which case some synchronization will be required.
    virtual bool        notifyNumActiveDisplays( uint32_t active ) = 0;
};

// Callback class to receive notification of a change.
// The PhysicalDisplayManager forwards notifications of display changes to a receiver.
// The Hwc itself can be the receiver, in which case it will just complete plug to SurfaceFlinger.
// Or, a logical LogicalDisplayManager can be inserted between the PhysicalDisplayManager and Hwc to marshall displays.
class PhysicalDisplayNotificationReceiver
{
public:
    virtual         ~PhysicalDisplayNotificationReceiver( )  { }

    // This must be called when a display becomes available.
    // If all slots are already taken, or, if this display is available but should not be
    // plugged to SurfaceFlinger, then sfIndex can be INVALID_DISPLAY_ID.
    // The display mmay end up proxied (plugged as primary).
    // If bPrimaryProxyOnly is true then the display will be considered for primary proxy only.
    // The receiver must call display manager plugSurfaceFlingerDisplay( ) to finalize plug
    //  if it is plugged to SurfaceFlinger.
    virtual void    notifyDisplayAvailable(AbstractPhysicalDisplay* pDisplay) = 0;

    // This must be called when a display is no longer available.
    // The receiver must call display manager unplugSurfaceFlingerDisplay( ) to finalize unplug
    //  if it is unplugged from SurfaceFlinger.
    virtual void    notifyDisplayUnavailable( AbstractPhysicalDisplay* pDisplay ) = 0;

    // This must be called when a display wants to change its size.
    virtual void    notifyDisplayChangeSize( AbstractPhysicalDisplay* pDisplay ) = 0;

    // This must be called when a display generates a VSync event.
    virtual void    notifyDisplayVSync( AbstractPhysicalDisplay* pDisplay, nsecs_t timeStampNs ) = 0;
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_ABSTRACTPHYSICALDISPLAY_H
