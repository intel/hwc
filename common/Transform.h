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

#ifndef IVPG_TRANSFORM_H
#define IVPG_TRANSFORM_H

#include "Layer.h"

namespace intel {
namespace ufo   {
namespace hwc   {


//*****************************************************************************
//
// Miscellaneous helpers
//
//*****************************************************************************

// Given a layer with sourceCrop, arbitrary transform and displayFrame and a display size dispW x dispH.
// Adjust displayFrame and sourceCrop to ensure that displayFrame is entirely within the display.
void clipLayerToDisplay( Layer* pLayer, const uint32_t dispW, const uint32_t dispH );

// Given source crop co-ordinates, arbitrary transform, display frame co-ordinates and a display size dispW x dispH.
// Adjust display frame and source crop to ensure that display frame is entirely within the display.
// Returns false if the display frame is invalid, zero size or wholly outside the display
bool clipToDisplay( float* psx1, float* psy1,
                    float* psx2, float* psy2,
                    const ETransform transform,
                    int32_t* pdx1, int32_t* pdy1,
                    int32_t* pdx2, int32_t* pdy2,
                    const uint32_t dispW, const uint32_t dispH );

// Given a layer with sourceCrop, arbitrary transform and displayFrame and a display region.
// Adjust displayFrame and sourceCrop to ensure that displayFrame is entirely within the display region.
// Returns false if the Layer's display frame is invalid, zero size or wholly outside the display region.
bool clipLayerToDestRect( Layer* pLayer, const hwc_rect_t& destRect );

// Given source crop co-ordinates, arbitrary transform, display frame co-ordinates and a display region.
// Adjust display frame and source crop to ensure that display frame is entirely within the display region.
// Returns false if the display frame is invalid, zero size or wholly outside the display region
bool clipToDestRect( float* psx1, float* psy1,
                    float* psx2, float* psy2,
                    const ETransform transform,
                    int32_t* pdx1, int32_t* pdy1,
                    int32_t* pdx2, int32_t* pdy2,
                    const hwc_rect_t& destRect );


};  // namespace hwc.
};  // namespace ufo.
};  // namespace intel.

#endif // IVPG_TRANSFORM_H
