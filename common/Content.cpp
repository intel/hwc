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
#include "Content.h"
#include "Layer.h"
#include "Log.h"
#include "Timeline.h"

namespace intel {
namespace ufo {
namespace hwc {

Content::Content()
{
}

Content::~Content()
{
}

void Content::setGeometryChanged(bool geometry)
{
    for (size_t d = 0; d < size(); d++)
    {
        editDisplay(d).setGeometryChanged(geometry);
    }
}


Content::Display::Display() :
    mFrameIndex(0),
    mFrameReceivedTime(0),
    mWidth(0),
    mHeight(0),
    mRefresh(INTEL_HWC_DEFAULT_REFRESH_RATE),
    mFormat(INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT),
    mOutputScaledDst(),
    mDisplayType(eDTUnspecified),
    mDmIndex(INVALID_DISPLAY_ID),
    mbEnabled(false),
    mbBlanked(false),
    mbOutputScaled(false),
    mpSourceRetireFence(NULL),
    mpOutputLayer(NULL)
{
}

Content::Display::~Display()
{
}

void Content::Display::disable()
{
    mLayerStack.resize(0);
    mpSourceRetireFence = NULL;
    mbOutputScaled = false;
    mbEnabled = false;
}

void Content::Display::closeAcquireFences( void ) const
{
    for ( uint32_t ly = 0; ly < mLayerStack.size( ); ly++ )
    {
        const Layer& layer = mLayerStack.getLayer(ly);
        layer.closeAcquireFence( );
    }
}

void Content::Display::updateDisplayState(const Content::Display &source)
{
    mFrameIndex         = source.mFrameIndex;
    mFrameReceivedTime  = source.mFrameReceivedTime;
    mWidth              = source.mWidth;
    mHeight             = source.mHeight;
    mRefresh            = source.mRefresh;
    mFormat             = source.mFormat;
    mOutputScaledDst    = source.mOutputScaledDst;
    mDisplayType        = source.mDisplayType;
    mDmIndex            = source.mDmIndex;
    mbEnabled           = source.mbEnabled;
    mbBlanked           = source.mbBlanked;
    mbOutputScaled      = source.mbOutputScaled;
    mpSourceRetireFence = source.mpSourceRetireFence;
    mpOutputLayer       = source.mpOutputLayer;
}

Content::LayerStack::LayerStack() :
    mbGeometry(false),
    mbEncrypted(false),
    mbVideo(false),
    mbFrontBufferRendered(false)
{
}

Content::LayerStack::LayerStack(const Layer* pLayers, uint32_t num) :
    mbGeometry(false),
    mbEncrypted(false),
    mbVideo(false),
    mbFrontBufferRendered(false)
{
    for (uint32_t i = 0; i < num; i++)
    {
        mpLayers.push_back(&pLayers[i]);
    }
}

Content::LayerStack::~LayerStack()
{
}

uint32_t Content::LayerStack::getNumEnabledLayers() const
{
    uint32_t numEnabled = 0;
    for (uint32_t ly = 0; ly < mpLayers.size(); ly++)
    {
        const Layer& layer = *(mpLayers[ly]);
        if (layer.isEnabled())
        {
            ++numEnabled;
        }
    }
    return numEnabled;
}

// Run through the layers caching some useful general flags
void Content::LayerStack::updateLayerFlags()
{
    mbEncrypted = false;
    mbVideo = false;
    mbFrontBufferRendered = false;

    for ( uint32_t ly = 0; ly < mpLayers.size(); ly++ )
    {
        const Layer& layer = *(mpLayers[ly]);
        mbEncrypted             |= layer.isEncrypted();
        mbVideo                 |= layer.isVideo();
        mbFrontBufferRendered   |= layer.isFrontBufferRendered();
    }
}

void Content::LayerStack::updateLayerFlags(const Content::LayerStack& stack)
{
    // Update the appropriate flags from the source layer. Check the layer flags directly
    mbGeometry = stack.mbGeometry;
    updateLayerFlags();
}


void Content::LayerStack::removeLayer(uint32_t ly, bool bUpdateSource)
{
    if (bUpdateSource)
    {
        // If we are removing a layer from the stack, then we need to ensure its acquire fence is closed and
        // its release fence is set to -1.

        const Layer& layer = *mpLayers[ly];
        layer.closeAcquireFence();
        layer.returnReleaseFence(-1);
    }
    mpLayers.removeAt(ly);
}

void Content::LayerStack::removeAllLayers(bool bUpdateSource)
{
    if (bUpdateSource)
    {
        // If we are removing a layer from the stack, then we need to ensure its acquire fence is closed and
        // its release fence is set to -1.

        for (unsigned ly = 0; ly < mpLayers.size(); ++ly)
        {
            const Layer& layer = *mpLayers[ly];
            layer.closeAcquireFence();
            layer.returnReleaseFence(-1);
        }
    }
    mpLayers.clear();
}

void Content::LayerStack::setAllReleaseFences(int fence) const
{
    String8 dupList;
    const bool bWantLog = Log::wantLog( );
    if ( fence != -1 )
    {
        for(uint32_t ly = 0; ly < size(); ly++)
        {
            const Layer& layer = getLayer(ly);
            if (layer.isEnabled())
            {
                int32_t dupFence = Timeline::dupFence( &fence );
                layer.returnReleaseFence( dupFence );
                if ( bWantLog )
                {
                    dupList += String8::format( " fd:%d", layer.getReleaseFence() );
                }
            }
        }
    }
    else
    {
        for(uint32_t ly = 0; ly < size(); ly++)
        {
            const Layer& layer = getLayer(ly);
            if (layer.isEnabled())
            {
                layer.returnReleaseFence( -1 );
                if ( bWantLog )
                {
                    dupList += String8::format( " fd:-1" );
                }
            }
        }
    }
    if ( bWantLog )
    {
        Log::add( "Fence: Stack replicated fence %d to all layers {%s }", fence, dupList.string() );
    }
}

void Content::LayerStack::onCompose()
{
    for (uint32_t ly = 0; ly < mpLayers.size(); ly++)
    {
        const Layer& layer = *mpLayers[ly];
        if (layer.isComposition())
        {
            layer.getComposition()->onCompose();
        }
    }
}

void Content::LayerStack::subset(const LayerStack source, uint32_t start, uint32_t size)
{
    ALOG_ASSERT( start + size <= source.size() );
    this->resize(size);
    for (uint32_t ly = 0; ly < size; ly++)
    {
        this->setLayer(ly, &source.getLayer(start + ly));
    }
}

bool Content::LayerStack::matches( const LayerStack& other, bool* pbMatchesHandles ) const
{
    if ( size() != other.size() )
    {
        ALOGD_IF( CONTENT_DEBUG, "Content::LayerStack mismatch layer size %u v %u", size(), other.size() );
        return false;
    }
    bool bMatchesHandles = true;
    for ( uint32_t ly = 0; ly < size(); ++ly )
    {
        const Layer& ours = getLayer(ly);
        const Layer& theirs = other.getLayer(ly);
        bool bThisLayerMatchesHandle;
        if ( !ours.matches( theirs, &bThisLayerMatchesHandle ) )
        {
            ALOGD_IF( CONTENT_DEBUG, "Content::LayerStack mismatch on layer %u", ly );
            return false;
        }
        bMatchesHandles &= bThisLayerMatchesHandle;
    }
    if ( pbMatchesHandles )
    {
        *pbMatchesHandles = bMatchesHandles;
    }
    return true;
}

String8 Content::LayerStack::dumpHeader() const
{
    return String8::format("%s%s%s",
                               isGeometryChanged() ? "Geometry " : "",
                               isVideo()           ? "Video " : "",
                               isEncrypted()       ? "Encrypted " : "");
}


String8 Content::LayerStack::dump(const char* pIdentifier) const
{
    if (!sbInternalBuild)
        return String8();

    String8 output = dumpHeader() + "\n";

    for (uint32_t ly = 0; ly < mpLayers.size(); ly++)
    {
        output += mpLayers[ly]->dump(String8::format("%s %d", pIdentifier, ly)) + "\n";
    }

    return output;
}

bool Content::LayerStack::dumpContentToTGA(const String8& prefix) const
{
    if (!sbInternalBuild)
        return false;

    if ( !mpLayers.size() )
        return false;

    bool bOK = true;

    for (uint32_t ly = 0; ly < mpLayers.size(); ly++)
    {
        String8 lyPrefix = String8::format( "%s_l%u", prefix.string(), ly );
        if ( !mpLayers[ly]->dumpContentToTGA( lyPrefix ) )
            bOK = false;
    }

    return bOK;
}

bool Content::Display::matches( const Display& other, bool* pbMatchesHandles ) const
{
    if ( ( mWidth == other.mWidth )
      && ( mHeight == other.mHeight )
      && ( mFormat == other.mFormat )
      && ( mDisplayType == other.mDisplayType )
      && ( mbEnabled == other.mbEnabled )
      && ( mbBlanked == other.mbBlanked )
      && ( mbOutputScaled == other.mbOutputScaled )
      && ( mOutputScaledDst.left == other.mOutputScaledDst.left )
      && ( mOutputScaledDst.right == other.mOutputScaledDst.right )
      && ( mOutputScaledDst.top == other.mOutputScaledDst.top )
      && ( mOutputScaledDst.bottom == other.mOutputScaledDst.bottom ) )
    {
        const LayerStack& ours = getLayerStack();
        const LayerStack& theirs = other.getLayerStack();
        if ( ours.matches( theirs, pbMatchesHandles ) )
        {
            return true;
        }
    }
    ALOGD_IF( CONTENT_DEBUG, "Display mismatch\n%s\n v \n%s", dump().string(), other.dump().string() );
    return false;
}

String8 Content::Display::dumpHeader() const
{
    return String8::format("Frame:%d %" PRIi64 "s %03" PRIi64 "ms Fd:%p/%d %dx%d %dHz %s %s %s%s%s",
                           mFrameIndex,
                           mFrameReceivedTime/1000000000, (mFrameReceivedTime%1000000000)/1000000,
                           getRetireFenceReturn(), getRetireFence(),
                           mWidth, mHeight, mRefresh, getHALFormatShortString(mFormat),
                           mDmIndex == INVALID_DISPLAY_ID ? "Dm:invalid" : String8::format( "Dm:%u", mDmIndex ).string(),
                           isOutputScaled()   ? String8::format( "OutputScaled [%d,%d,%d,%d] ",
                                                mOutputScaledDst.left, mOutputScaledDst.top,
                                                mOutputScaledDst.right, mOutputScaledDst.bottom ).string() : "",
                           isEnabled()        ? "Enabled " : "",
                           isBlanked()        ? "Blanked " : "");
}


String8 Content::Display::dump(const char* pIdentifier) const
{
    if (!sbInternalBuild || !isEnabled())
        return String8();

    String8 output = String8::format("%s %s %s\n",
                                pIdentifier,
                                dumpHeader().string(),
                                mLayerStack.dump("").string());

    if (mpOutputLayer)
        output += mpOutputLayer->dump(" T") + "\n";

    return output;
}

bool Content::Display::dumpContentToTGA(const String8& prefix) const
{
    if (!sbInternalBuild)
        return false;

    String8 filename = String8::format( "/data/hwc/%s.log", prefix.string() );
    FILE* fp;
    if ( ( fp = fopen( filename.string(), "wt" ) ) != NULL )
    {
        fprintf( fp, "%s\n", prefix.string() );
        fprintf( fp, "%s\n", dump().string() );
        fclose(fp);
    }
    else
    {
        ALOGE( "Failed to open %s", filename.string() );
    }

    const Content::LayerStack& stack = getLayerStack();
    return stack.dumpContentToTGA( prefix );
}

bool Content::matches( const Content& other, bool* pbMatchesHandles ) const
{
    if ( size() == other.size() )
    {
        ALOGD_IF( CONTENT_DEBUG, "Content mismatch display size %u v %u", size(), other.size() );
        return false;
    }
    bool bMatchesHandles = true;
    for ( uint32_t d = 0; d < size(); ++d )
    {
        const Display& ours = getDisplay(d);
        const Display& theirs = other.getDisplay(d);
        bool bThisDisplayMatchesHandles;
        if ( !ours.matches( theirs, &bThisDisplayMatchesHandles ) )
        {
            ALOGD_IF( CONTENT_DEBUG, "Content mismatch on display %u", d );
            return false;
        }
        bMatchesHandles &= bThisDisplayMatchesHandles;
    }
    if ( pbMatchesHandles )
    {
        *pbMatchesHandles = bMatchesHandles;
    }
    return true;
}

void Content::snapshotOf( const Content& from, std::vector<Layer> copiedLayers[] )
{
    *this = from;
    for ( uint32_t d = 0; d < size(); ++d )
    {
        Content::LayerStack& layerStack = editDisplay(d).editLayerStack();
        const uint32_t numLayers = layerStack.size();
        copiedLayers[d].resize( numLayers );
        for ( uint32_t ly = 0 ; ly < numLayers; ++ly )
        {
            // Snapshot layer into our layer copy.
            copiedLayers[d][ly].snapshotOf( layerStack.getLayer( ly ) );
            // Clear down fence references.
            copiedLayers[d][ly].setAcquireFenceReturn( (int*)NULL );
            copiedLayers[d][ly].setReleaseFenceReturn( (int*)NULL );
            // Replace layer with copied layer,
            layerStack.setLayer( ly, &copiedLayers[d][ly] );
        }
    }
}

String8 Content::dump(const char* pIdentifier) const
{
    if (!sbInternalBuild)
        return String8();

    String8 output;
    for (size_t d = 0; d < size(); d++)
    {
        output += getDisplay(d).dump(String8::format("%s Display:%zd", pIdentifier, d));
    }

    return output;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
