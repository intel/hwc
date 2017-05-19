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

#include "Hwc.h"
#include "CompositionManager.h"
#include "Log.h"
#include "Utils.h"
#include "ufo/graphics.h"

namespace intel {
namespace ufo {
namespace hwc {

// Default composition buffer pool constraints.
// This is:
//   MAX_COMPOSITION_BUFFER_COUNT   : max number of buffers, or 0 if unbound.
//   MAX_COMPOSITION_BUFFER_ALLOC   : total allocation in MB, or 0 if unbound.
// The defaults can be overriden using options properties:
//   intel.hwc.cbcount
//   intel.hwc.cballoc
#define MAX_COMPOSITION_BUFFER_COUNT     20
#define MAX_COMPOSITION_BUFFER_ALLOC     0

// Align all allocated buffers to a multiple of 32 wide and 8 high. This gives us better buffer reuse
// when sizes change minimally. Do not align the height more than 8 - 1080p buffers must stay 1080p.
// Note, NV12 buffers need to be aligned to 2x2 and YUY2 aligned to 2x1.
static const uint32_t cBufferWidthAlignment = 32;
static const uint32_t cBufferHeightAlignment = 8;

// The number of milliseconds for which compositions are held pending imminent reuse
static const uint32_t cReuseCompositionMs = 100;

// This is an internal class which describes a composition. Initially, this is a simple 1:1 source to target.
// However, longer term we may want to augment this to support multiple targets at different resolutions
// which gives us the opportunity to scale an existing render target instead of generating it from source
// whenever this is cheaper.

// There are multiple uses for this class. If a composition has been requested twice on a single HWC update,
// this class allows us to return the same result as last time. Also if a composition from a previous frame
// contains identical state (including the handle) then again we may reuse it.
class CompositionManager::Composition : public AbstractComposition, public BufferQueue::BufferReference
{
public:

    const static int INVALID_COMPOSER = -1;
    const static int SURFACEFLINGER_COMPOSER = 0;

    Composition();
    virtual ~Composition();

    void            setRenderTargetBuffer( BufferQueue::BufferHandle handle );

    void            clear();

    const char*     getName() const { return mbEvaluationValid ? (mpComposer ? mpComposer->getName() : "Imposible Comp") : "Not Evaluated"; }
    const Layer&    getTarget()     { return mRenderTarget; }

    void            onUpdate(const Content::LayerStack& src);
    void            onUpdateFences(const Content::LayerStack& src);
    void            onUpdateOutputLayer(const Layer& target);
    void            onCompose();
    bool            onAcquire();
    void            onRelease();

    bool            isImpossible()  { return mpComposer == NULL; }
    float           getEvaluationCost() { return mEvaluationCost; }

    void            invalidate()    { mbTargetValid = false; }

    uint32_t        lock( void ) { ++mLocks; return mLocks; }
    uint32_t        unlock( void ) { ALOG_ASSERT(mLocks>0); --mLocks; return mLocks; }

    String8         dump(nsecs_t now, const char* pIdentifier = "") const;

    // Invalidate the composition's render target (used when buffer queue buffers are expired/modified).
    void            invalidateRenderTarget( void );

    // Implements BufferQueue::BufferReference.
    virtual         void referenceInvalidate( BufferQueue::BufferHandle handle );

private:
    friend class CompositionManager;

    bool match(const Content::LayerStack& src, uint32_t width, uint32_t height, uint32_t format, ECompressionType compression, bool* pbMatchedHandles = NULL, bool* pbContainsComposition = NULL) const;
    void onUpdateAll(const Content::LayerStack& src, uint32_t width, uint32_t height, uint32_t format, ECompressionType compression, nsecs_t timestamp);
    void onUpdateTimestamp(nsecs_t timestamp) { mTimestamp = timestamp; };
    void onUpdateBufferPavpSession();
    void onUpdateMediaTimestampFps();
    void expireBuffer( buffer_handle_t bufferHandle );

private:
    CompositionManager*                     mpCompositionManager;   // Pointer back to the manager
    AbstractComposer*                       mpComposer;             // Pointer to the composer for this composition. Null pointer means the composition is impossible

    uint32_t                                mRefCount;              // Number of times this composition has been acquired for use
    float                                   mEvaluationCost;        // Calculated cost of this evaluation

    // This layer stack contains the current frame's input state.
    Content::LayerStack                     mSourceStack;

    // This vector contains a copy of all the source layers for this composition. This will only be updated occasionally.
    // The geometry should match the composition, but the handles/framerates may not be accurate. Use mSourceStack for
    // accurate current data.
    std::vector<Layer>                      mSourceLayers;

    // Render target being used for this composition.
    BufferQueue::BufferHandle               mRenderTargetBuffer;    // Render target buffer.
    Layer                                   mRenderTarget;          // Render target layer.
    uint32_t                                mRenderTargetUsage;     // Usage flags required for the allocation of this buffer
    uint32_t                                mCompositionFormat;     // Composition output format (render target buffer format may differ).

    AbstractComposer::ResourceHandle        mComposerResource;          // The handle of any resources acquired for this composition.
    AbstractComposer::CompositionState*     mpComposerCompositionState; // Composer-composition state for this composition instance (if provided).

    nsecs_t                                 mTimestamp;             // Timestamp for when this was last valid
    uint32_t                                mLocks;                 // A count of locks on this composition (a lock will keep the composition 'live')..


    bool                                    mbEvaluationValid:1;    // The evaluation was performed and is valid.
    bool                                    mbTargetValid:1;        // This indicates that the target needs to be regenerated as something changed.
    bool                                    mbTargetProvided:1;     // The target buffer was allocated externally and provided already.
    bool                                    mbConsiderForReuse:1;   // Anything left as invalid at the end of a frame should be marked for reuse in the next frame.
};

CompositionManager::Composition::Composition() :
    mpCompositionManager(NULL),
    mRenderTargetBuffer(NULL),
    mpComposerCompositionState(NULL),
    mLocks(0)
    // Initialization should be in the clear function
{
    clear();
}

CompositionManager::Composition::~Composition()
{
}

void CompositionManager::Composition::setRenderTargetBuffer( BufferQueue::BufferHandle handle )
{
    if ( mRenderTargetBuffer == handle )
    {
        ALOGD_IF( COMPOSITION_DEBUG, "setRenderTargetBuffer %p no change", handle );
        return;
    }
    if ( mRenderTargetBuffer )
    {
        // Release this composition's existing reference.
        ALOGD_IF( COMPOSITION_DEBUG, "setRenderTargetBuffer remove old reference %p", mRenderTargetBuffer );
        mpCompositionManager->getBufferQueue().registerReference( mRenderTargetBuffer, NULL );
    }
    // Update RT.
    mRenderTargetBuffer = handle;
    if ( mRenderTargetBuffer )
    {
        // Register this composition's reference to this new buffer.
        mpCompositionManager->getBufferQueue().registerReference( mRenderTargetBuffer, this );
        Log::alogd( COMPOSITION_DEBUG, "CompositionManager composition %p registered reference to buffer queue record %p", this, mRenderTargetBuffer );
    }
}

void CompositionManager::Composition::invalidateRenderTarget( void )
{
    mRenderTarget.setHandle(0);
    mRenderTarget.setAcquireFenceReturn( Timeline::NullNativeFenceReference );
    mRenderTarget.setReleaseFenceReturn( Timeline::NullNativeFenceReference );
    setRenderTargetBuffer( NULL );
    mbTargetValid = false;
    mbTargetProvided = false;
}

void CompositionManager::Composition::referenceInvalidate( BufferQueue::BufferHandle handle )
{
    HWC_UNUSED( handle );
    Log::alogd( COMPOSITION_DEBUG, "CompositionManager composition %p invalidated from buffer queue with record %p", this, handle );
    // If we receive a reference callback then we MUST have a reference to it.
    ALOG_ASSERT( mRenderTargetBuffer != NULL );
    ALOG_ASSERT( mRenderTargetBuffer == handle );
    // We don't anticipate a reference callback if the target was provided.
    ALOG_ASSERT( !mbTargetProvided );
    // Invalidate this composition's render target (clear its reference to the buffer queue).
    invalidateRenderTarget();
}

void CompositionManager::Composition::clear()
{
    ALOG_ASSERT( !mLocks );
    delete mpComposerCompositionState;
    mpComposerCompositionState = NULL;
    mpComposer              = NULL;
    mRefCount               = 0;
    mEvaluationCost         = AbstractComposer::Eval_Cost_Max;
    mRenderTargetUsage      = GRALLOC_USAGE_HW_COMPOSER;
    mCompositionFormat      = 0;
    mComposerResource       = NULL;
    mLocks                  = 0;
    mbEvaluationValid       = false;
    mbTargetValid           = false;
    mbTargetProvided        = false;
    setRenderTargetBuffer( NULL );
    mRenderTarget.clear();
}

bool CompositionManager::Composition::match(const Content::LayerStack& src, uint32_t width, uint32_t height, uint32_t format,
                                            ECompressionType compression, bool* pbMatchedHandles, bool* pbContainsComposition) const
{
    ALOG_ASSERT( mRenderTarget.getComposition() == this );

    // Check the render target resolution
    if ( ( width  != mRenderTarget.getDstWidth() )
      || ( height != mRenderTarget.getDstHeight() )
      || ( format != mCompositionFormat )
      || ( compression != mRenderTarget.getBufferCompression() ) )
    {
        ALOGD_IF(COMPOSITION_DEBUG, "Mismatched width %d=%d , height %d=%d, format %s=%s or compression %u=%u",
            width, mRenderTarget.getDstWidth(), height, mRenderTarget.getDstHeight(),
            getHALFormatShortString(format), getHALFormatShortString(mCompositionFormat),
            compression, mRenderTarget.getBufferCompression());
        return false;
    }

    // Check the layer stacks match in size
    if (src.size() != mSourceLayers.size())
    {
        ALOGD_IF(COMPOSITION_DEBUG, "Mismatched src.size()=%d mSourceLayers.size()=%d", src.size(), uint32_t(mSourceLayers.size()));
        return false;
    }

    bool bMatchedHandles = true;
    for (size_t i = 0; i < mSourceLayers.size(); i++)
    {
        const Layer& ours = mSourceLayers[i];
        const Layer& theirs = src.getLayer(i);
        bool bThisLayerMatchesHandles;

        if ( !ours.matches( theirs, &bThisLayerMatchesHandles ) )
        {
            ALOGD_IF( COMPOSITION_DEBUG, "Mismatch" );
            ALOGD_IF( COMPOSITION_DEBUG, "Ours: %s", ours.dump().string() );
            ALOGD_IF( COMPOSITION_DEBUG, "Theirs: %s", theirs.dump().string() );
            return false;
        }

        if (pbContainsComposition && theirs.isComposition())
        {
            *pbContainsComposition = true;
        }

        bMatchedHandles &= bThisLayerMatchesHandles;
    }

    // If the user also needs info about the handles
    if (pbMatchedHandles)
        *pbMatchedHandles = bMatchedHandles;

    // Everything matched
    return true;
}

void CompositionManager::Composition::expireBuffer( buffer_handle_t bufferHandle )
{
    if ( mRenderTarget.getHandle() == bufferHandle )
    {
        Log::alogd( COMPOSITION_DEBUG, "CompositionManager expireBuffers composition %p uses %p in render target", this, bufferHandle );
        // Invalidate this composition's render target (clear its reference to the buffer queue).
        invalidateRenderTarget();
    }
    for (size_t i = 0; i < mSourceLayers.size(); i++)
    {
        if ( mSourceLayers[i].getHandle() == bufferHandle )
        {
            Log::alogd( COMPOSITION_DEBUG, "CompositionManager expireBuffers composition %p uses %p in source layer %u", this, bufferHandle, i );
            mSourceLayers[i].setHandle(0);
            mSourceLayers[i].setAcquireFenceReturn( Timeline::NullNativeFenceReference );
            mSourceLayers[i].setReleaseFenceReturn( Timeline::NullNativeFenceReference );

            // Invalidate the composition record
            mbTargetValid = false;
        }
    }
}

void CompositionManager::Composition::onUpdateAll(const Content::LayerStack& src, uint32_t width, uint32_t height, uint32_t format, ECompressionType compression, nsecs_t timestamp)
{
    // Update our source for this composition
    mSourceLayers.resize(src.size());

    // Run through the handles in the composition updating them
    uint32_t maxFramerate = 1;
    for (size_t ly = 0; ly < mSourceLayers.size(); ly++)
    {
        Layer& internalLayer = mSourceLayers[ly];
        internalLayer = src.getLayer(ly);

        uint32_t fps = internalLayer.getFps();
        if (maxFramerate < fps)
            maxFramerate = fps;

        ALOGD_IF( COMPOSITION_DEBUG, "%s", src.getLayer(ly).dump("CompositionManager::Composition::onUpdateAll S").string());
        ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onUpdateAll: S %s", src.getLayer(ly).getFrameRateTracker().dump().string());
        ALOGD_IF( COMPOSITION_DEBUG, "%s", internalLayer.dump("CompositionManager::Composition::onUpdateAll D").string());
        ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onUpdateAll: D %s", internalLayer.getFrameRateTracker().dump().string());
    }
    mSourceStack = Content::LayerStack(mSourceLayers.data(), mSourceLayers.size());
    mSourceStack.updateLayerFlags();

    ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onUpdateAll: maxFrameRate %d", maxFramerate);

    mRenderTarget.setHandle(0);
    setRenderTargetBuffer( NULL );

    // 1:1 mapping on the render target. Generate an appropriate target layer structure
    hwc_frect_t& s = mRenderTarget.editSrc();
    s.left = 0;
    s.top = 0;
    s.right = width;
    s.bottom = height;

    hwc_rect_t& d = mRenderTarget.editDst();
    d.left = 0;
    d.top = 0;
    d.right = width;
    d.bottom = height;

    // Since we permit BufferQueue to return an alpha equivalent format for non-alpha composition requests,
    // we must explicitly track the original requested format and set blending on/off as necessary.
    mCompositionFormat = format;
    if ( isAlpha( mCompositionFormat ) )
    {
        mRenderTarget.setBlending( EBlendMode::PREMULT );
        ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onUpdateAll: Enable blending for requested alpha format %d/%s",
            mCompositionFormat, getHALFormatShortString( mCompositionFormat ) );
    }
    else
    {
        mRenderTarget.setBlending( EBlendMode::NONE );
        ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onUpdateAll: Disable blending for requested opaque format %d/%s",
            mCompositionFormat, getHALFormatShortString( mCompositionFormat ) );
    }
    mRenderTarget.setPlaneAlpha(1.0f);
    mRenderTarget.setBufferFormat(format);
    mRenderTarget.setBufferCompression(compression);
    mRenderTarget.editVisibleRegions().resize(1);
    mRenderTarget.editVisibleRegions().editItemAt(0) = d;
    mRenderTarget.editFrameRateTracker().reset(mpCompositionManager->getTimestamp(), maxFramerate);
    mbTargetValid = false;
    mbConsiderForReuse = false;

    // TODO: Pipe render target flags through to the physical display.
    // An NV12 output format is expected to go to the encoder
    mRenderTargetUsage = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER;
    if (format == HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL)
        mRenderTargetUsage |= GRALLOC_USAGE_HW_VIDEO_ENCODER;

    mRenderTarget.onUpdateFlags();

    onUpdateTimestamp(timestamp);

    // Ensure the dequeue'd buffer encryption state matches the source stack state. Always need to do this
    // for protected buffers to manage instance counts correctly.
    onUpdateBufferPavpSession();
    // Propagate media timestamp to the render target if required.
    onUpdateMediaTimestampFps();

    ALOG_ASSERT( mRenderTarget.getComposition() == this );
}

void CompositionManager::Composition::onUpdate(const Content::LayerStack& src)
{
    if ( sbInternalBuild )
    {
        // Check the assumption that the composition in use matches the src layerstack
        bool bMatch = match(src, mRenderTarget.getDstWidth(), mRenderTarget.getDstHeight(), mCompositionFormat, mRenderTarget.getBufferCompression());
        if ( !bMatch )
        {
            ALOGE( "CompositionManager composition update mismatch" );
            ALOGE( "RT Layer   : %s", mRenderTarget.dump().string() );
            ALOGE( "SRC Stack  : %s", src.dump().string() );
            ALOG_ASSERT(0);
        }
    }

    // Update our source for this composition
    ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onUpdate: src %s", src.dump().string());

    // Run through the handles in the composition updating them if required
    for (uint32_t ly = 0; ly < mSourceLayers.size(); ly++)
    {
        Layer& internalCopy = mSourceLayers[ly];
        const Layer& inputLayer = src.getLayer(ly);

        if (internalCopy.getHandle() != inputLayer.getHandle() || inputLayer.isComposition())
        {
            // The previous composition remains valid unless a handle changes
            // TODO: if we have a subcomposition, we have to assume that it may not be valid at this point.
            // We may need a way to query the composition to see if its required. Currently only the SF composition
            // does this and it will always be required (due to it containing a skip layer)
            // There is also the question as to whether we need to forward on the update. SFC doesnt need it, but others
            // may.
            mbTargetValid = false;
        }
        internalCopy.onUpdateFrameState(inputLayer);
        ALOGD_IF( COMPOSITION_DEBUG, "%u %s", ly, internalCopy.dump().string());
    }

    mbConsiderForReuse = false;
    if (mbTargetValid)
        Log::add(mSourceStack, mRenderTarget, "Smart Composition Reuse: ");

    ALOG_ASSERT( mRenderTarget.getComposition() == this );
}

void CompositionManager::Composition::onUpdateFences(const Content::LayerStack& src)
{
    for (uint32_t ly = 0; ly < mSourceLayers.size(); ly++)
    {
        Layer& internalCopy = mSourceLayers[ly];
        const Layer& inputLayer = src.getLayer(ly);

        internalCopy.onUpdateFences(inputLayer);
    }
}

void CompositionManager::Composition::onUpdateOutputLayer(const Layer& target)
{
    ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onUpdateOutputLayer to: %s", target.dump().string());
    // Check the that the render target matches the source layer
    //ALOG_ASSERT(mRenderTarget.getDstWidth()     == target.getDstWidth()    );
    //ALOG_ASSERT(mRenderTarget.getDstHeight()    == target.getDstHeight()   );
    //ALOG_ASSERT(mRenderTarget.getBufferFormat() == target.getBufferFormat());

    mRenderTarget.onUpdateFrameState(target);
    // A Composition's render target layer composition should never change.
    mRenderTarget.setComposition( this );
    mbTargetProvided = true;
    mbTargetValid = false;

    ALOG_ASSERT( mRenderTarget.getComposition() == this );
}

void CompositionManager::Composition::onUpdateMediaTimestampFps()
{
   // Look for the backmost video layer
   for (uint32_t ly = 0; ly < mSourceStack.size(); ly++)
   {
       const Layer& layer = mSourceStack.getLayer(ly);
       if (layer.isVideo())
       {
           mRenderTarget.onUpdateMediaTimestampFps(layer.getMediaTimestamp(), layer.getMediaFps());
           return;
       }
   }
   // No video timestamp by default.
   mRenderTarget.onUpdateMediaTimestampFps(0,0);
   ALOG_ASSERT( mRenderTarget.getComposition() == this );
}

void CompositionManager::Composition::onUpdateBufferPavpSession()
{
    if (mSourceStack.isEncrypted())
    {
        // Need to find the session in the local stack
        for (uint32_t ly = 0; ly < mSourceStack.size(); ly++)
        {
            const Layer& layer = mSourceStack.getLayer(ly);
            if (layer.isEncrypted())
            {
                mRenderTarget.setBufferPavpSession(layer.getBufferPavpSessionID(), layer.getBufferPavpInstanceID(), true);
                // TODO: What happens if we have multiple Pavp sessions?
                break;
            }
        }
    }
    else
    {
        if (mRenderTarget.isEncrypted())
        {
            // Reset status
            mRenderTarget.setBufferPavpSession(0, 0, false);
        }
    }

    ALOG_ASSERT( mRenderTarget.getComposition() == this );
}


void CompositionManager::Composition::onCompose()
{
    ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onCompose to:");
    ALOGD_IF( COMPOSITION_DEBUG, "%s", mSourceStack.dump().string());
    ALOGD_IF( COMPOSITION_DEBUG, "%s", mRenderTarget.dump(" T").string());

    // Just in case no evaluation has been done yet, find an appropriate engine and forward
    if (!mbEvaluationValid)
    {
        ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onCompose: chooseBestCompositionEngine");
        mpCompositionManager->chooseBestCompositionEngine(*this, AbstractComposer::Power);
    }

    // If the composition isnt possible, fail
    if (isImpossible())
    {
        ALOGE("onCompose: Impossible composition requested");
        return;
    }

    if (!mbTargetValid)
    {
        // Make sure that any composition elements in the source have been triggered correctly
        mSourceStack.onCompose();

        if (!mbTargetProvided)
        {
            // We need to provide a buffer for the composer
            Timeline::Fence* pReleaseFence = NULL;

            uint32_t allocW = alignTo(mRenderTarget.getDstWidth(),  cBufferWidthAlignment);
            uint32_t allocH = alignTo(mRenderTarget.getDstHeight(), cBufferHeightAlignment);

            ALOGD_IF( COMPOSITION_DEBUG, "onCompose dequeuing new buffer [current mRenderTargetBuffer %p]", mRenderTargetBuffer );

            BufferQueue::BufferHandle handle = mpCompositionManager->getBufferQueue().dequeue(allocW, allocH, mRenderTarget.getBufferFormat(), mRenderTargetUsage, &pReleaseFence);
            sp<GraphicBuffer> pGB = mpCompositionManager->getBufferQueue().getGraphicBuffer( handle );
            if ( ( pGB == NULL ) || ( pGB->handle == NULL ) )
            {
                ALOGE( "onCompose: Failed to dequeue render target buffer" );
                return;
            }

            ALOGD_IF( COMPOSITION_DEBUG, "onCompose dequeued new buffer setting %p", handle );
            setRenderTargetBuffer( handle );
            mRenderTarget.setAcquireFenceReturn(Timeline::NullNativeFenceReference);
            mRenderTarget.setReleaseFenceReturn(pReleaseFence);

            // Update the handle of the render target
            mRenderTarget.onUpdateFrameState(pGB->handle, mpCompositionManager->getTimestamp());

            // Queue this immediately, the release fence will be filled in later.
            mpCompositionManager->getBufferQueue().queue();
        }

        // Ensure the dequeue'd buffer encryption state matches the source stack state.
        // Always need to do this for protected buffers to manage instance counts correctly.
        // Do this before the onCompose( ) in case the composer fails to propagate the
        // encryption state itself.
        onUpdateBufferPavpSession();
        // Propagate media timestamp to the render target if required.
        onUpdateMediaTimestampFps();

        ALOGD_IF( COMPOSITION_DEBUG, "mRenderTarget %s = %s", mbTargetProvided ? "provided" : "allocated", mRenderTarget.dump().string());

        // We must have a render target handle.
        ALOG_ASSERT( mRenderTarget.getHandle( ) );
        // Mark it used.
        mpCompositionManager->getBufferQueue().markUsed( mRenderTargetBuffer );

        AbstractBufferManager::get().requestCompression(mRenderTarget.getHandle(), mRenderTarget.getBufferCompression());
        mpComposer->onCompose(mSourceStack, mRenderTarget, mpComposerCompositionState);

        mbTargetProvided = false;
        mbTargetValid = true;
    }
    else
    {
        // Make sure all the source layers have any acquire fences closed
        for (uint32_t ly = 0; ly < mSourceStack.size(); ly++)
        {
            const Layer& layer = mSourceStack.getLayer(ly);

            if (layer.getAcquireFence() >= 0)
            {
                ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::onCompose: Closing unused fence %d", mSourceStack.getLayer(ly).getAcquireFence());
                mSourceStack.getLayer(ly).closeAcquireFence();
            }
        }

        // We must have a render target handle.
        ALOG_ASSERT( mRenderTarget.getHandle( ) );
        // Mark it used.
        mpCompositionManager->getBufferQueue().markUsed( mRenderTargetBuffer );
    }

    ALOG_ASSERT( mRenderTarget.getComposition() == this );
}

bool CompositionManager::Composition::onAcquire()
{
    mRefCount++;
    mComposerResource = mpComposer->onAcquire(mSourceStack, mRenderTarget);

    ALOG_ASSERT( mRenderTarget.getComposition() == this );

    return mComposerResource != NULL;
}

void CompositionManager::Composition::onRelease()
{
    mpComposer->onRelease(mComposerResource);
    mRefCount--;
}

CompositionManager::CompositionManager() :
    mPrimaryTid(0),
    mTimestamp(0)
{
    // This should always be the first composer in the array
    mpComposers.push_back(&mSurfaceFlingerComposer);
}

CompositionManager::~CompositionManager()
{
    for (auto c : mpComposers)
        delete c;
}

void CompositionManager::firstFrameInit( void )
{
    // Set primary tid and register tracker for buffer alloc/free.
    mPrimaryTid = gettid();
    AbstractBufferManager::get().registerTracker( *this );

    // Set buffer queue constraints from options.
    Option compBufferCount( "cbcount", MAX_COMPOSITION_BUFFER_COUNT, false );
    Option compBufferAlloc( "cballoc", MAX_COMPOSITION_BUFFER_ALLOC, false );

    mBufferQueue.setConstraints( compBufferCount, compBufferAlloc );
}

void CompositionManager::onPrepareBegin(size_t numDisplays, hwc_display_contents_1_t** displays, nsecs_t timestamp)
{

    if ( mPrimaryTid == 0 )
    {
        firstFrameInit();
    }
    else
    {
        ALOG_ASSERT( mPrimaryTid == gettid() );
    }

    mTimestamp = timestamp;

    // Process the stale buffer handle list at the top of the frame.
    expireBuffers();

    mSurfaceFlingerComposer.onPrepareBegin(numDisplays, displays, timestamp);
    mBufferQueue.onPrepareBegin();
    return;
}


void CompositionManager::onPrepareEnd()
{
    mSurfaceFlingerComposer.onPrepareEnd();
    mBufferQueue.onPrepareEnd();
    return;
}

void CompositionManager::invalidate(buffer_handle_t handle)
{
    for (uint32_t i = 0; i < mCompositions.size(); i++)
    {
        Composition& c = mCompositions[i];
        c.expireBuffer( handle );
    }
}

void CompositionManager::onAccept(const Content::Display& display, uint32_t d)
{
    // TODO: Get d from Content::Display as soon as its available


    // The main task here is to maintain the list of valid input handles. Compositions need to be
    // invalidated if any of their handles become invalid.
    const Content::LayerStack& layerStack = display.getLayerStack();
    if (display.isGeometryChanged())
    {
        // On a geometry change, order can change, layers can be added etc.
        // Hence, we need to do a full search of old handles and new handles.
        for (buffer_handle_t handle : mCurrentHandles[d])
        {
            bool bFound = false;
            for (uint32_t ly = 0 ; ly < layerStack.size(); ++ly)
            {
                const Layer& layer = layerStack.getLayer(ly);
                if (layer.getHandle() == handle)
                {
                    bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                // Mark this handle as unused. Invalidate any compositions containing the handle if no displays reference this now.
                mCurrentHandleUsage[handle] &= ~(1 << d);
                if (mCurrentHandleUsage[handle] == 0)
                    invalidate(handle);
            }
        }

        // Update handle list. Mark all handles as in use, remarking an already in use handle is harmless
        mCurrentHandles[d].resize(layerStack.size());
        for (uint32_t ly = 0 ; ly < layerStack.size(); ++ly)
        {
            const Layer& layer = layerStack.getLayer(ly);
            buffer_handle_t handle = layer.getHandle();
            mCurrentHandles[d][ly] = handle;
            mCurrentHandleUsage[handle] |= (1 << d);
        }
    }
    else
    {
        // This is the easy case, just check which handles changed, no reordering to consider.
        for (uint32_t ly = 0 ; ly < layerStack.size(); ++ly)
        {
            const Layer& layer = layerStack.getLayer(ly);
            if (mCurrentHandles[d][ly] != layer.getHandle())
            {
                buffer_handle_t handle = mCurrentHandles[d][ly];
                mCurrentHandleUsage[handle] &= ~(1 << d);
                if (mCurrentHandleUsage[handle] == 0)
                    invalidate(handle);

                invalidate(mCurrentHandles[d][ly]);
                mCurrentHandles[d][ly] = layer.getHandle();
            }
        }
    }
}

void CompositionManager::onSetBegin(size_t numDisplays, hwc_display_contents_1_t** ppDisplayContents)
{
    mSurfaceFlingerComposer.onSet(numDisplays, ppDisplayContents, mTimestamp);
    mBufferQueue.onSetBegin();

    // Update any SF compositions to have the right dst layer,
    for (uint32_t i = 0; i < mCompositions.size(); i++)
    {
        Composition& c = mCompositions[i];
        if ((c.mpComposer == &mSurfaceFlingerComposer) && c.mComposerResource)
        {
            ALOGD_IF( COMPOSITION_DEBUG, "UpdateOutputLayer on composition %d",i);
            c.onUpdateOutputLayer(mSurfaceFlingerComposer.getTarget(c.mComposerResource));
            ALOG_ASSERT( c.mRenderTarget.getComposition() == &c );
        }
    }

    return;
}

void CompositionManager::onEndOfFrame( uint32_t hwcFrameIndex )
{
    HWC_UNUSED( hwcFrameIndex );
    // Update the timestamp of any valid compositions.
    for (uint32_t i = 0; i < mCompositions.size(); i++)
    {
        Composition& c = mCompositions[i];
        if (c.mRefCount || c.mTimestamp + ms2ns( cReuseCompositionMs ) > mTimestamp)
        {
            // If there are references or this was used very recently, then do not reuse record
            c.mbConsiderForReuse = false;
        }
        else
        {
            c.mbConsiderForReuse = true;
        }
    }
    mBufferQueue.onSetEnd( );
}

AbstractComposition* CompositionManager::requestComposition(const Content::LayerStack& src, uint32_t width, uint32_t height, uint32_t format, ECompressionType compression, AbstractComposer::Cost type)
{
    ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::requestComposition: Looking for composition to %dx%d %s. compositions known:%d %p", width, height, getHALFormatShortString(format), mCompositions.size(), this);
    ALOGD_IF( COMPOSITION_DEBUG, "%s", src.dump().string());

    ALOGE_IF( src.isFrontBufferRendered(),
              "Composition request includes a front buffer rendered layer\n%s",
              src.dump().string() );

    // Default newEntrySlot to the first element after the end of the list. The iteration over the composition vector
    // will update this if there is something reusable.
    uint32_t newEntrySlot = mCompositions.size();
    nsecs_t  newEntryTimestamp = mTimestamp;

    for (uint32_t i = 0; i < mCompositions.size(); i++)
    {
        Composition& c = mCompositions[i];

        ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::requestComposition: Checking composition %d/%p", i, &c);
        ALOGD_IF( COMPOSITION_DEBUG, "%s", c.dump(mTimestamp).string());

        // If this composition is old, then skip it but remember its index, we will reuse the oldest record
        if (c.mRefCount == 0 && c.mbConsiderForReuse)
        {
            if (c.mLocks==0 && c.mTimestamp < newEntryTimestamp)
            {
                ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::requestComposition: Discarding old composition %d. May reuse index", i);
                newEntrySlot = i;
                newEntryTimestamp = c.mTimestamp;
            }
            continue;
        }

        bool bMatchedHandles = false;
        bool bContainsComposition = false;

        if ( c.match(src, width, height, format, compression, &bMatchedHandles, &bContainsComposition) )
        {
            // If the composition is impossible then we can just say so!
            if (c.isImpossible())
            {
                ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::requestComposition: composition matched as impossible");
                return NULL;
            }

            // If our handles all matched and this composition is either current or last frame, then
            // we can skip the handle lookup
            if (bMatchedHandles)
            {
                ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::requestComposition: composition matched current frame");
                // Update the counter to indicate that this composition is now current for this frame
                // No need to recompose, this is the smart composition case
                c.onUpdateFences(src);
                c.onUpdateTimestamp(mTimestamp);

                if (bContainsComposition)
                {
                    // Have to recompose this entry if there is a composition present
                    Log::add(src, c.getTarget(), "Smart Composition Invalidate: Contains Composition");
                    c.invalidate();
                }
                else
                {
                    Log::add(src, c.getTarget(), "Smart Composition Reuse: ");
                }
                return &c;
            }
            if (c.mTimestamp != mTimestamp)
            {
                ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::requestComposition: composition matched older frame - update handles");
                // This matches an older composition. Update the handles and reuse this entry.
                c.onUpdate(src);
                c.onUpdateTimestamp(mTimestamp);
                return &c;
            }

            // While we matched and its a current composition, the handles were different, which means that we cannot reuse this one.
        }
    }

    ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::requestComposition: No suitable previous composition found, adding composition at entry %d", newEntrySlot);

    // No match was found, need to recreate a new composition entry
    // TODO: We may want to limit the size of this table here by now looking for unused composition entries
    // However, there is also a fair chance of reuse of composition entries which will avoid re-evaluation
    if (newEntrySlot >= mCompositions.size())
    {
        mCompositions.grow(newEntrySlot+1);
    }

    Composition& ce = mCompositions[newEntrySlot];
    ce.clear();
    ce.mpCompositionManager = this;
    ce.mRenderTarget.setComposition(&ce);
    ce.onUpdateAll(src, width, height, format, compression, mTimestamp);

    // Now do a preliminary search for the best composition engine for this composition
    chooseBestCompositionEngine(ce, type);

    if (ce.isImpossible())
    {
        return NULL;
    }

    return &ce;
}

uint32_t CompositionManager::lockComposition( AbstractComposition* pComposition )
{
    for (uint32_t i = 0; i < mCompositions.size(); i++)
    {
        Composition& c = mCompositions[i];
        if ( &c == pComposition )
        {
            uint32_t newLocks = c.lock();
            ALOGD_IF( COMPOSITION_DEBUG,
                      "CompositionManager::lockComposition %p : entry %d, locks %u",
                      pComposition, i, newLocks );
            return newLocks;
        }
    }
    ALOGE( "CompositionManager::lockComposition %p : not found", pComposition );
    return 0;
}

uint32_t CompositionManager::unlockComposition( AbstractComposition* pComposition )
{
    for (uint32_t i = 0; i < mCompositions.size(); i++)
    {
        Composition& c = mCompositions[i];
        if ( &c == pComposition )
        {
            uint32_t newLocks = c.unlock();
            ALOGD_IF( COMPOSITION_DEBUG,
                      "CompositionManager::unlockComposition %p : entry %d, locks %u",
                      pComposition, i, newLocks );
            return newLocks;
        }
    }
    ALOGE( "CompositionManager::unlockComposition %p : not found", pComposition );
    return 0;
}

void CompositionManager::chooseBestCompositionEngine(Composition& c, AbstractComposer::Cost type)
{
    float               bestCost = AbstractComposer::Eval_Cost_Max;
    int32_t             bestComposer = Composition::INVALID_COMPOSER;
    AbstractComposer::CompositionState *bestState = NULL;
    for (int32_t i = mpComposers.size()-1; i >= 0; i--)
    {
        AbstractComposer& composer = *mpComposers[i];
        AbstractComposer::CompositionState *pState = NULL;
        float cost = composer.onEvaluate(c.mSourceStack, c.mRenderTarget, &pState, type);
        // If cost is negative, composer failed in some way.
        if (cost >= AbstractComposer::Eval_Cost_Min)
        {
            if (cost < bestCost)
            {
                ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::chooseBestCompositionEngine: %d %s evaluated a cost of %f: Best so far", i, composer.getName(), cost);
                // Track best composer/cost/state.
                bestComposer = i;
                bestCost = cost;
                delete bestState;
                bestState = pState;
            }
            else
            {
                delete pState;
                pState = NULL;
                ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::chooseBestCompositionEngine: %d %s evaluated a cost of %f: Already seen better", i, composer.getName(), cost);
            }
        }
        else
        {
            ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::chooseBestCompositionEngine: %d %s failed evaluation %f", i, composer.getName(), cost);
            ALOG_ASSERT( pState == NULL );
        }
    }

    ALOG_ASSERT( c.mpComposer == NULL );
    ALOG_ASSERT( c.mpComposerCompositionState == NULL );
    if (bestComposer == Composition::INVALID_COMPOSER)
    {
        c.mpComposer = NULL;
    }
    else
    {
        c.mpComposer = mpComposers[bestComposer];
        c.mpComposerCompositionState = bestState;
    }
    c.mbEvaluationValid = true;
    c.mEvaluationCost = bestCost;
    c.mbTargetValid = false;

    ALOG_ASSERT( c.mRenderTarget.getComposition() == &c );
}

void CompositionManager::notifyBufferAlloc( buffer_handle_t handle )
{
    HWC_UNUSED( handle );
}

void CompositionManager::notifyBufferFree( buffer_handle_t handle )
{
    ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::notifyBufferFree handle %p", handle );
    // Compositions will be expired as necessary at the start of the next frame
    // or immediately if this is the main thread.
    {
        // Put the freed handle into the stale buffer handle list.
        Mutex::Autolock _l( mStaleBufferMutex );
        mStaleBufferHandles.push_back( handle );
    }
    if ( gettid() == mPrimaryTid )
    {
        // Process immediately if this is the main thread.
        expireBuffers();
    }
}

void CompositionManager::expireBuffers( void )
{
    ALOG_ASSERT( gettid() == mPrimaryTid );
    Mutex::Autolock _l( mStaleBufferMutex );
    if ( !mStaleBufferHandles.empty() )
    {
        // Process compositions for buffer handles that are stale.
        for (buffer_handle_t handle : mStaleBufferHandles)
        {
            ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::expireBuffers buffer %p", handle );
            // Expire any compositions for which this buffer was a source.
            for (uint32_t i = 0; i < mCompositions.size(); i++)
            {
                Composition& c = mCompositions[i];
                ALOGD_IF( COMPOSITION_DEBUG, "CompositionManager::expireComposition: Checking composition %d/%p %s", i, &c, c.dump(mTimestamp).string() );
                c.expireBuffer( handle );
            }
        }
        mStaleBufferHandles.clear();
    }
}

bool CompositionManager::performComposition(const Content::LayerStack& src, const Layer& target)
{
    AbstractComposition* pComposition = requestComposition(src, target.getBufferWidth(), target.getBufferHeight(), target.getBufferFormat(), target.getBufferCompression());
    if (pComposition == NULL)
        return false;

    if (!pComposition->onAcquire())
        return false;

    pComposition->onUpdateOutputLayer(target);
    pComposition->onCompose();
    pComposition->onRelease();

    // This function does not require persistence for the LayerStack.
    static_cast<CompositionManager::Composition*>(pComposition)->invalidate();
    return true;
}

// Dump a little info about all the compositions
String8 CompositionManager::dump() const
{
    if (!sbInternalBuild)
        return String8();

    String8 output;
    output += mBufferQueue.dump();
    for (uint32_t i = 0; i < mCompositions.size(); i++)
    {
        output.appendFormat("Composition %d/%d ", i, mCompositions.size());
        output += mCompositions[i].dump(mTimestamp);
    }

    return output;
}

// Dump a little info about the composition
String8 CompositionManager::Composition::dump(nsecs_t now, const char* pIdentifier) const
{
    if (!sbInternalBuild)
        return String8();

    ALOG_ASSERT( ( mRenderTarget.getComposition() == NULL )
              || ( mRenderTarget.getComposition() == this ) );

    String8 output = String8(pIdentifier);

    output.appendFormat( "Name %s, RefCount:%u Locks:%u Timestamp:%" PRIu64 "(%d seconds ago) %s\n", getName(),
        mRefCount, mLocks, mTimestamp, uint32_t((now - mTimestamp)/1000000000), mbConsiderForReuse ? "Reuse" : "");
    for (uint32_t i = 0; i < mSourceLayers.size(); i++)
    {
        const Layer& src = mSourceLayers[i];
        output.appendFormat("   S %u %s\n", i, src.dump().string() );
    }
    output.appendFormat("     T %s %s BufferQueue:%p\n", mRenderTarget.dump().string(), mbTargetValid ? "Target:Valid": "Target:NotValid", mRenderTargetBuffer);
    return output;
}


}; // namespace hwc
}; // namespace ufo
}; // namespace intel
