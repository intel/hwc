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


#ifndef INTEL_UFO_HWC_BXTDISPLAYCAPS_H
#define INTEL_UFO_HWC_BXTDISPLAYCAPS_H

#include "DisplayCaps.h"
#include "DisplayState.h"
#define BXT_PLATFORM_SCALAR_COUNT 2
#define GLV_PLATFORM_SCALAR_COUNT 1
// TODO:
//  Make DisplayState private to BxtDisplayCaps.
//  Register for notifications via a generic notification framework.

namespace intel {
namespace ufo {
namespace hwc {

class BxtDisplayCaps;

// ********************************************************************
// Display Capabilities for DRM Broxton/Skylake class devices.
// ********************************************************************
class BxtPlaneCaps : public DisplayCaps::PlaneCaps
{
public:
    BxtPlaneCaps() : DisplayCaps::PlaneCaps(), mpDisplayCaps(NULL), mbHaveCompression(false) {}
    bool isScaleFactorSupported(const Layer& layer) const;
    bool isSupported( const Layer& display ) const;
    ECompressionType getCompression( unsigned index, int32_t displayFormat ) const;

    void setHaveCompression(bool bHave)                 { mbHaveCompression = bHave; }
    void setDisplayCaps(const BxtDisplayCaps& caps)     { mpDisplayCaps = &caps; }

private:
    const BxtDisplayCaps*   mpDisplayCaps;
    bool                    mbHaveCompression;
};

class BxtDisplayCaps : public DisplayCaps
{
public:
    BxtDisplayCaps(uint32_t pipe, uint32_t scalarcount);
    void probe();
    static const unsigned int cPlaneCount = 4; // set to 4 temporary, to cover BXT planes
    DisplayCaps::PlaneCaps* createPlane(uint32_t planeIndex)
    {
        ALOG_ASSERT(planeIndex < cPlaneCount);
        return &mPlanes[planeIndex];
    }

    bool isSupported( const Content::Display& display, uint32_t zorder ) const;

    enum {
        enableDownscale = 1,
        enableUpscale   = 2,
    };

    bool isUpscaleEnabled() const       { return mOptionScale & enableDownscale; }
    bool isDownscaleEnabled() const     { return mOptionScale & enableUpscale; }

    virtual DisplayState* editState( void ) const { return &mDisplayState; }

    const DisplayState& getState( void ) const { return mDisplayState; }

private:
    Option                  mOptionScale;
    Option                  mOptionLatencyL0;
    BxtPlaneCaps            mPlanes[cPlaneCount];
    uint32_t                mPipe;
    uint32_t                mScalarCount;

    // TODO:
    //  Mutable so we can pass it out for editing through a const interface.
    //  This can be removed once we have a generic notification framework.
    mutable DisplayState    mDisplayState;

    // DBuf calculations.
    float calculateDownScale( const float sw, const float sh, const uint32_t dw, const uint32_t dh ) const;
    float calculatePipeDownScale( const Content::Display& display ) const;
    float calculateLayerDownScale( const Layer& ly ) const;
    uint32_t calculateMinimumYTileScanlines( const bool bTransposed, const uint32_t format, const uint32_t bpp ) const;
    uint32_t calculateAbsoluteMinimumYTileScanlines( const bool bTransposed, const uint32_t bpp ) const;
    uint32_t calculatePlaneBlocks( const uint32_t pipeHTotal,
                                   const uint32_t planeSourceWidth,
                                   const uint64_t adjustedPlanePixelRate,
                                   const uint32_t format,
                                   const uint32_t planeBpp,
                                   const bool bYTiled,
                                   const bool bTransposed,
                                   const bool bCompressed ) const;
    uint32_t calculateMinimumBlocks( const uint32_t pipeHTotal, const uint64_t adjustedPipePixelRate, const Layer& ly ) const;
    uint32_t calculateDBuf( const Content::Display&  display, const Timing& timing ) const;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_BXTDISPLAYCAPS_H
