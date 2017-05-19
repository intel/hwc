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

#include "Common.h"
#include "DebugFilter.h"
#include "FilterManager.h"
#include "Log.h"

namespace intel {
namespace ufo {
namespace hwc {

DebugFilter::DebugFilter()
{
    // Add this filter to the front of the filter list
    FilterManager::getInstance().add(*this, FilterPosition::Debug);
}

DebugFilter::~DebugFilter()
{
    // remove this filter
    FilterManager::getInstance().remove(*this);
}

const Content& DebugFilter::onApply(const Content& ref)
{
    // Keep it simple, its a debug tool. If we have any elements in our debugdisplay vector
    // assume we may have something valid and do the work
    if (mDebugDisplay.size() == 0)
        return ref;

    // Copy the content
    mReference = ref;

    // Run through each display
    for (uint32_t d = 0; d < ref.size() && d < mDebugDisplay.size(); d++)
    {
        Content::Display& display = mReference.editDisplay(d);

        // If anything changed since last frame, propagate a geometry change through the stack
        if (mDebugDisplay[d].mbGeometryChange)
        {
            display.editLayerStack().setGeometryChanged(true);
            mDebugDisplay.editItemAt(d).mbGeometryChange = false;
        }

        uint32_t mask;
        if (mDebugDisplay[d].mbDisableDisplay)
        {
            display.setEnabled(false);
            mask = 0xffffffff;
        }
        else if (mDebugDisplay[d].mbBlankDisplay)
        {
            mask = 0xffffffff;
        }
        else
        {
            mask = mDebugDisplay[d].mMask;
        }

        // Run through backwards so that remove doesn't alter our dest index
        for (int32_t i = display.editLayerStack().size()-1; i >= 0; i--)
        {
            if (mask & (1<<i))
            {
                display.editLayerStack().removeLayer(i);
            }
        }

        // Update our layer flags as some of our layers may have gone.
        display.editLayerStack().updateLayerFlags();

        // Dump resultant display frame.
        if (mDebugDisplay[d].mDumpFrames != 0)
        {
            // Make adjustment to indices prior to dumping so they are consistent for following dumpHardwareFrame.
            ++mDebugDisplay.editItemAt(d).mDumpFrameIdx;
            if (mDebugDisplay[d].mDumpFrames > 0)
            {
                --mDebugDisplay.editItemAt(d).mDumpFrames;
            }

            String8 prefix = String8::format( "df_frame%u_d%u_i%u_in", display.getFrameIndex(), d, mDebugDisplay[d].mDumpFrameIdx );
            Log::alogd( true, "Dumping %s", prefix.string() );
            display.dumpContentToTGA( prefix );
            mDebugDisplay.editItemAt(d).mDumpHardwareFrame = display.getFrameIndex();
        }
    }
    return mReference;
}

void DebugFilter::enableDisplay(uint32_t d)
{
    if (mDebugDisplay.size() <= d)
        mDebugDisplay.insertAt(mDebugDisplay.size(), d + 1 - mDebugDisplay.size());

    DisplayDebug& dd = mDebugDisplay.editItemAt(d);
    dd.mbGeometryChange = true;
    dd.mbDisableDisplay = false;
    dd.mbBlankDisplay = false;
    dd.mMask = 0;
}

void DebugFilter::disableDisplay(uint32_t d, bool bBlank)
{
    if (mDebugDisplay.size() <= d)
        mDebugDisplay.insertAt(mDebugDisplay.size(), d + 1 - mDebugDisplay.size());

    DisplayDebug& dd = mDebugDisplay.editItemAt(d);
    dd.mbGeometryChange = true;
    if (bBlank)
        dd.mbBlankDisplay = true;
    else
        dd.mbDisableDisplay = true;

}

void DebugFilter::maskLayer(uint32_t d, uint32_t layer, bool bHide)
{
    if (mDebugDisplay.size() < d + 1)
        mDebugDisplay.insertAt(mDebugDisplay.size(), d + 1 - mDebugDisplay.size());

    DisplayDebug& dd = mDebugDisplay.editItemAt(d);
    dd.mbGeometryChange = true;
    if (bHide)
        dd.mMask |= 1 << layer;
    else
        dd.mMask &= ~(1 << layer);
}

void DebugFilter::dumpFrames(uint32_t d, int32_t frames)
{
    if (mDebugDisplay.size() < d + 1)
        mDebugDisplay.insertAt(mDebugDisplay.size(), d + 1 - mDebugDisplay.size());

    DisplayDebug& dd = mDebugDisplay.editItemAt(d);
    dd.mbGeometryChange = true;
    dd.mDumpFrames = frames;
    dd.mDumpFrameIdx = 0;
    dd.mDumpHardwareFrame = 0;
}

void DebugFilter::dumpHardwareFrame(uint32_t d, const Content::Display& out)
{
    if (mDebugDisplay.size() < d + 1)
        return;

    DisplayDebug& dd = mDebugDisplay.editItemAt(d);
    if ( dd.mDumpHardwareFrame == out.getFrameIndex() )
    {
        String8 prefix = String8::format( "df_frame%u_d%u_i%u_out", out.getFrameIndex(), d, dd.mDumpFrameIdx );
        Log::alogd( true, "Dumping %s", prefix.string() );
        out.dumpContentToTGA( prefix );
    }
}

String8 DebugFilter::dump()
{
    if (!sbInternalBuild)
        return String8();

    String8 output;

    for (uint32_t d = 0; d < mDebugDisplay.size(); d++)
    {
        const DisplayDebug& dd = mDebugDisplay[d];

        output.appendFormat("D%d 0x%x %s%s%s ", d, dd.mMask,
            dd.mbGeometryChange ? "Geom " : "", dd.mbDisableDisplay ? "Dis " : "", dd.mbBlankDisplay ? "Blank" : "");
    }
    return output;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
