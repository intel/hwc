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

#ifndef INTEL_UFO_HWC_CONTENTREFERENCE_H
#define INTEL_UFO_HWC_CONTENTREFERENCE_H

#include "Common.h"
#include <vector>
#include <utils/Vector.h>
#include <utils/Timers.h>

namespace intel {
namespace ufo {
namespace hwc {

class Layer;

// This class defines a display state at any point in time as a list of pointers to layer
// objects.
// It should be as lightweight as possible as its expected to be copied by a number
// of the filters. This implies that it will generally use pointers to data owned by its
// creator rather than data that owns itself.

// While this class is initially expected to directly reference a content class, as filters
// get applied to it, its likely to diverge significantly
class Content
{
public:

    class LayerStack;
    class Display;



    Content();
    ~Content();

    // TODO: Remove this later. Temporary constructor
    Content(hwc_display_contents_1_t** pDisplay, uint32_t num);

    const Display&              getDisplay(uint32_t l) const            { return mDisplays[l]; }
    Display&                    editDisplay(uint32_t l)                 { return mDisplays.editItemAt(l); }

    uint32_t                    size() const                            { return mDisplays.size(); }
    void                        resize(uint32_t size)                   { mDisplays.resize(size); }

    void                        setGeometryChanged(bool geometry);

    // Do these Contents match other Contents.
    // Returns true if match (ignoring handles).
    // If pbMatchesHandles is provided, then on return it will be set true iff all layer handles also match.
    bool                        matches( const Content& other, bool* pbMatchesHandles = NULL ) const;

    // Copy a "snapshot" of another content with copiedLayers for each display.
    // This must be used when taking a copy of contents that will persist beyond the current frame.
    void                        snapshotOf( const Content& other, std::vector<Layer> copiedLayers[] );

    String8                     dump(const char* pIdentifier = "Content") const;

private:
    Vector<Display>             mDisplays;
};

// The Content class owns its display objects. This allows a filter to adjust a display
// without having to hold a copy of a display. A filter can also create additional displays if
// it needs, eg to split an android display over two screens or to provide a clone of another screen
class Content::LayerStack
{
public:
    LayerStack();
    LayerStack(const Layer* pLayers, uint32_t num);
    virtual ~LayerStack();

    const Layer&            getLayer(uint32_t ly) const                 { ALOG_ASSERT(mpLayers[ly]); return *(mpLayers[ly]); }
    const Layer* const*     getLayerArray() const                       { return mpLayers.array(); }
    void                    setLayer(uint32_t ly, const Layer* pL)      { mpLayers.editItemAt(ly) = pL; }
    const Layer&            operator[](uint32_t ly) const               { return *(mpLayers[ly]); }

    uint32_t                size() const                                { return mpLayers.size(); }
    void                    resize(uint32_t size)                       { mpLayers.resize(size); }
    uint32_t                getNumEnabledLayers() const;

    bool                    isGeometryChanged() const                   { return mbGeometry; }
    bool                    isEncrypted() const                         { return mbEncrypted; }
    bool                    isVideo() const                             { return mbVideo; }
    bool                    isFrontBufferRendered() const               { return mbFrontBufferRendered; }
    void                    setGeometryChanged(bool geometry)           { mbGeometry = geometry; }
    void                    updateLayerFlags();
    void                    updateLayerFlags(const LayerStack& layers);

    void                    removeLayer(uint32_t ly, bool bUpdateSource = true);
    void                    removeAllLayers(bool bUpdateSource = true);
    void                    setAllReleaseFences(int fence) const;

    // Does this LayerStack match another LayerStack.
    // Returns true if match (ignoring handles).
    // If pbMatchesHandles is provided, then on return it will be set true iff all layer handles also match.
    bool                    matches( const LayerStack& other, bool* pbMatchesHandles = NULL ) const;

    String8                 dumpHeader() const;
    String8                 dump(const char* pIdentifier = "") const;

    // Dump the contents of a layer stack - only useful in internal builds
    // Will dump to /data/hwc/<prefix>_l<N>.tga
    bool                    dumpContentToTGA(const String8& prefix) const;

    // Helper function to call the onCompose entrypoints for any component layers in a layerstack if needed
    void                    onCompose();

    // Helper function to copy subset of source layer stack to *this*
    void                    subset(const LayerStack source, uint32_t start, uint32_t size);


private:
    Vector<const Layer*>    mpLayers;                   // List of the layers that are currently on this stack
    bool                    mbGeometry:1;               // Geometry change with this stack
    bool                    mbEncrypted:1;              // At least one layer on this display is encrypted
    bool                    mbVideo:1;                  // At least one video plane is present
    bool                    mbFrontBufferRendered:1;    // At least one FBR plane is present
};

// A display is just a layer stack with some extra flags and display related info
class Content::Display
{
public:
    Display();
    virtual ~Display();

    uint32_t                getFrameIndex() const                       { return mFrameIndex; }
    nsecs_t                 getFrameReceivedTime() const                { return mFrameReceivedTime; }
    uint32_t                getWidth() const                            { return mWidth;    }
    uint32_t                getHeight() const                           { return mHeight;   }
    uint32_t                getRefresh() const                          { return mRefresh;  }
    uint32_t                getFormat() const                           { return mFormat;   }
    const hwc_rect_t&       getOutputScaledDst() const                  { return mOutputScaledDst; }
    EDisplayType            getDisplayType() const                      { return mDisplayType; }
    uint32_t                getDisplayManagerIndex() const              { return mDmIndex; }
    int*                    getRetireFenceReturn() const                { return mpSourceRetireFence; }
    uint32_t                getNumLayers() const                        { return mLayerStack.size(); }
    uint32_t                getNumEnabledLayers() const                 { return mLayerStack.getNumEnabledLayers(); }
    const Layer*            getOutputLayer() const                      { return mpOutputLayer; }
    int                     getRetireFence() const                      { return mpSourceRetireFence ? *mpSourceRetireFence : -1; }

    // Flag accessors
    bool                    isEnabled() const                           { return mbEnabled; }
    bool                    isBlanked() const                           { return mbBlanked; }
    bool                    isOutputScaled() const                      { return mbOutputScaled; }
    bool                    isVideo() const                             { return mLayerStack.isVideo();   }
    bool                    isEncrypted() const                         { return mLayerStack.isEncrypted(); }
    bool                    isFrontBufferRendered() const               { return mLayerStack.isFrontBufferRendered(); }
    bool                    isGeometryChanged() const                   { return mLayerStack.isGeometryChanged(); }

    void                    setGeometryChanged(bool geometry)           { mLayerStack.setGeometryChanged(geometry); }
    void                    setEnabled(bool enabled)                    { mbEnabled = enabled; }
    void                    setBlanked(bool blanked)                    { mbBlanked = blanked; }
    void                    setFrameIndex(uint32_t index)               { mFrameIndex = index; }
    void                    setFrameReceivedTime( nsecs_t rxTime )      { mFrameReceivedTime = rxTime; }
    void                    setWidth  (uint32_t width  )                { mWidth   = width  ;  }
    void                    setHeight (uint32_t height )                { mHeight  = height ;  }
    void                    setRefresh(uint32_t refresh)                { mRefresh = refresh;  }
    void                    setFormat (uint32_t format )                { mFormat  = format ;  }
    void                    setDisplayType (EDisplayType type)          { mDisplayType = type; }
    void                    setDisplayManagerIndex(uint32_t dmIndex)    { mDmIndex = dmIndex;  }
    void                    setRetireFenceReturn(int* pRetireFence)     { mpSourceRetireFence = pRetireFence; }
    void                    setOutputLayer(const Layer* pLayer)         { mpOutputLayer = pLayer; }
    void                    setOutputScaled(const hwc_rect_t& dst)      { mbOutputScaled = true; mOutputScaledDst = dst; }

    // Update all display state from the source except the layer stack
    void                    updateDisplayState(const Content::Display &source);

    // We must always have somewhere to put the source retire fence in any situation where we generate one.
    void                    returnCompositionRetireFence(int fence) const { ALOG_ASSERT(mpSourceRetireFence); *mpSourceRetireFence = fence; }

    // Disable this display
    void                    disable();

    // Close all acquire fences.
    void                    closeAcquireFences( void ) const;

    // Methods on the reference class
    const LayerStack&       getLayerStack() const                       { return mLayerStack; }
    LayerStack&             editLayerStack()                            { return mLayerStack; }

    // Does this Display match another Display.
    // Returns true if match (ignoring handles).
    // If pbMatchesHandles is provided, then on return it will be set true iff all layer handles also match.
    bool                    matches( const Display& other, bool* pbMatchesHandles = NULL ) const;

    String8                 dumpHeader() const;
    String8                 dump(const char* pIdentifier = "") const;

    // Dump the contents of a display - only useful in internal builds
    // Will dump to /data/hwc/<prefix>_l<N>.tga
    bool                    dumpContentToTGA(const String8& prefix) const;

private:
    LayerStack              mLayerStack;

    uint32_t                mFrameIndex;        // Frame Index.
    nsecs_t                 mFrameReceivedTime; // Time the frame was received (systemTime(SYSTEM_TIME_MONOTONIC))

    uint32_t                mWidth;             // Width of the display in pixels
    uint32_t                mHeight;            // Height of the display in pixels
    uint32_t                mRefresh;           // Refresh rate of the display in Hz
    uint32_t                mFormat;            // Preferred format of the display
    hwc_rect_t              mOutputScaledDst;   // Output scaled destination position/size.
    EDisplayType            mDisplayType;       // Type of display
    uint32_t                mDmIndex;           // Display manager index.

    // Various flags that control this display or indicate that the display is in some kind of state
    bool                    mbEnabled:1;        // display is currently enabled
    bool                    mbBlanked:1;        // display is currently blanked
    bool                    mbOutputScaled:1;   // output device is expected to apply some scaling

    int*                    mpSourceRetireFence;// This is the location of the source layers composition retire fence return value
    const Layer*            mpOutputLayer;      // The display provides its output buffer. Eg a Virtual Display.

};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_CONTENTREFERENCE_H
