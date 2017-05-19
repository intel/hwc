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

#ifndef INTEL_UFO_HWC_TIMING_H
#define INTEL_UFO_HWC_TIMING_H

namespace intel {
namespace ufo {
namespace hwc {

// Display timing.
class Timing
{
public:

    enum ETimingFlag
    {
        Flag_Preferred      = 0x0001,
        Flag_Interlaced     = 0x0002,
    };

    enum class EAspectRatio : uint32_t
    {
        Any      = 0x00000000,
        R4_3     = 0x00040003,
        R16_9    = 0x00100009,
    };

    Timing() :
        mWidth(0), mHeight(0), mRefresh(0), mMinRefresh(0), mRatio(EAspectRatio::Any), mFlags(0), mPixelClock(0), mHTotal(0), mVTotal(0) {}
    Timing(uint32_t width, uint32_t height, uint32_t refresh, uint32_t pixelclock = 0, uint32_t hTotal = 0, uint32_t vTotal = 0, EAspectRatio ratio = EAspectRatio::Any,
        uint32_t flags = 0, uint32_t minRefresh = 0) :
        mWidth(width), mHeight(height), mRefresh(refresh), mMinRefresh(minRefresh ? minRefresh : refresh),
        mRatio(ratio), mFlags(flags), mPixelClock(pixelclock), mHTotal(hTotal), mVTotal(vTotal) {}

    uint32_t        getWidth() const        { return mWidth; }
    uint32_t        getHeight() const       { return mHeight; }
    uint32_t        getRefresh() const      { return mRefresh; }
    uint32_t        getMinRefresh() const   { return mMinRefresh; }
    EAspectRatio    getRatio() const        { return mRatio; }
    uint32_t        getFlags() const        { return mFlags; }
    uint32_t        getPixelClock() const   { return mPixelClock; }
    uint32_t        getHTotal() const       { return mHTotal; }
    uint32_t        getVTotal() const       { return mVTotal; }

    bool            isPreferred() const     { return mFlags & Flag_Preferred; }
    bool            isInterlaced() const    { return mFlags & Flag_Interlaced; }

    String8         dump() const;
static   String8    dumpRatio(EAspectRatio t);
private:
    uint32_t        mWidth;
    uint32_t        mHeight;
    uint32_t        mRefresh;
    uint32_t        mMinRefresh;
    EAspectRatio    mRatio;
    uint32_t        mFlags;
    uint32_t        mPixelClock;
    uint32_t        mHTotal;
    uint32_t        mVTotal;
};


}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_TIMING_H
