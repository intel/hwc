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

#ifndef INTEL_UFO_HWC_TRANSPARENCYFILTER_H
#define INTEL_UFO_HWC_TRANSPARENCYFILTER_H

#include "AbstractFilter.h"
#include "AbstractBufferManager.h"


namespace intel {
namespace ufo {
namespace hwc {

#define MAX_DETECT_LAYERS 4

class TransparencyFilter : public AbstractFilter
{
public:
    TransparencyFilter();
    virtual ~TransparencyFilter();

    const char* getName() const { return "TransparencyFilter"; }
    const Content& onApply(const Content& ref);
    String8 dump();

private:
    class DetectionThread;
    class DetectionItem
    {
        friend TransparencyFilter;
    public:
        DetectionItem();
        virtual ~DetectionItem();
        void reset();
        void updateRepeatCounts(const Layer& ly);
        void initiateDetection(const Layer& layer, hwc_frect_t videoRect);
        void filterLayers(Content& ref);
        void garbageCollect(void);
        String8 dump();
    private:
        AbstractBufferManager&  mBM;
        buffer_handle_t         mCurrentHandle;     // Handle of the currently repeating frame
        hwc_rect                mBlackMask;
        uint32_t                mRepeatCount;
        bool                    mbEnabled;
        sp<GraphicBuffer>       mpLinearBuffer;
        uint32_t                mFramesBeforeCheck;
        sp<DetectionThread>     mpDetectionThread;
        bool                    mbFirstEnabledFrame;
        bool                    mbFirstDisabledFrame;
    };

    void skipFilter(void);

    DetectionItem       mDetection[MAX_DETECT_LAYERS];
    uint32_t            mDetectionNum;

    Content             mReference;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_TRANSPARENCYFILTER_H
