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

#ifndef INTEL_UFO_HWC_LAYER_H
#define INTEL_UFO_HWC_LAYER_H

#include "AbstractComposition.h"
#include "Timeline.h"
#include "Format.h"
#include "Utils.h"
#include <utils/Vector.h>

namespace intel {
namespace ufo {
namespace hwc {

class Layer
{
public:
    // This class's job is to track the rate at which the handles are changing in a layer
    class FramerateTracker
    {
    public:
        FramerateTracker();
        void        reset(nsecs_t timestamp, uint32_t fps = INTEL_HWC_DEFAULT_REFRESH_RATE);
        void        update(nsecs_t now, bool bHandleChanged);
        uint32_t    getFps() const          { return mFps;  }
        void        setFps(uint32_t fps)    { mFps = fps;   }
        String8     dump() const;

    private:
        // Note, keep this a multiple of 2 to allow for simple integer calculations
        static const int cFramesToTrackShift = 3;
        static const int cFramesToTrack = 1 << cFramesToTrackShift;
        nsecs_t         mLastTimestamp;
        nsecs_t         mPeriod;
        uint32_t        mFps;
    };

    // Buffer details are those state completed on request by the BufferManager.
    // Only guaranteed to be valid between the start of prepare and the end of set
    class BufferDetails
    {
    public:

        // Set accessors.
        void setDeviceId( uint64_t id, bool valid )         { mDeviceId = id; mbDeviceIdValid = valid; }
        void setWidth( uint16_t w )                         { mWidth = w; }
        void setHeight( uint16_t h )                        { mHeight =h; }
        void setFormat( uint32_t format )                   { mFormat = format; }
        void setUsage( uint32_t usage )                     { mUsage = usage; }
        void setPitch( uint32_t pitch )                     { mPitch = pitch; }
        void setSize( uint32_t size )                       { mSize = size; }
        void setAllocWidth( uint16_t aw )                   { mAllocWidth = aw; }
        void setAllocHeight( uint16_t ah )                  { mAllocHeight = ah; }
        void setColorRange( EDataSpaceRange colorRange )    { mColorRange = colorRange; }
        void setPavpSessionID( uint16_t pavpSessionID )     { mPavpSessionID = pavpSessionID; }
        void setPavpInstanceID( uint16_t pavpInstanceID )   { mPavpInstanceID = pavpInstanceID; }
        void setCompression( ECompressionType compression ) { mCompression = compression; }
        void setMediaTimestampFps( uint64_t ts, uint32_t fps){ mMediaTimestamp = ts; mMediaFps = fps; }
        void setEncrypted( bool bEncrypted )                { mbEncrypted = bEncrypted; }
        void setKeyFrame( bool bKeyFrame )                  { mbKeyFrame = bKeyFrame; }
        void setInterlaced( bool bInterlaced )              { mbInterlaced = bInterlaced; }
        void setTilingFormat(ETilingFormat format)          { mTilingFormat = format; }
        void setBufferModeFlags( uint32_t bufferModeFlags ) { mBufferModeFlags = bufferModeFlags; }

        // Get accessors.
        uint64_t         getDeviceId( void )        const   { return mDeviceId; }
        bool             isDeviceIdValid( void )    const   { return mbDeviceIdValid; }
        uint16_t         getWidth( void )           const   { return mWidth; }
        uint16_t         getHeight( void )          const   { return mHeight; }
        uint32_t         getFormat( void )          const   { return mFormat; }
        uint32_t         getUsage( void )           const   { return mUsage; }
        uint32_t         getPitch( void )           const   { return mPitch; }
        uint32_t         getSize( void )            const   { return mSize; }
        uint16_t         getAllocWidth( void )      const   { return mAllocWidth; }
        uint16_t         getAllocHeight( void )     const   { return mAllocHeight; }
        EDataSpaceRange  getColorRange( void )      const   { return mColorRange; }
        uint16_t         getPavpSessionID( void )   const   { return mPavpSessionID; }
        uint16_t         getPavpInstanceID( void )  const   { return mPavpInstanceID; }
        ECompressionType getCompression( void   )   const   { return mCompression; }
        uint64_t         getMediaTimestamp( void )  const   { return mMediaTimestamp; }
        uint32_t         getMediaFps( void )        const   { return mMediaFps; }
        bool             getEncrypted( void )       const   { return mbEncrypted; }
        bool             getKeyFrame( void )        const   { return mbKeyFrame; }
        bool             getInterlaced( void )      const   { return mbInterlaced; }
        ETilingFormat    getTilingFormat()          const   { return mTilingFormat; }
        uint32_t getBufferModeFlags( void )   const         { return mBufferModeFlags; }

        // Clear state.
        void clear( void )
        {
            mWidth = 0;
            mHeight = 0;
            mFormat = 0;
            mUsage = 0;
            mPitch = 0;
            mSize = 0;
            mAllocWidth = 0;
            mAllocHeight = 0;
            mDeviceId = 0;
            mColorRange = EDataSpaceRange::Limited;
            mPavpSessionID = 0;
            mPavpInstanceID = 0;
            mCompression = COMPRESSION_NONE;
            mbEncrypted = false;
            mbKeyFrame = false;
            mbInterlaced = false;
            mbDeviceIdValid = false;
            mTilingFormat = TILE_UNKNOWN;
            mMediaTimestamp = 0;
            mMediaFps = 0;
            mBufferModeFlags = 0;
        }

    protected:
        uint64_t         mDeviceId;        // Device specific buffer handle. May be a pointer, but is expected to be a file descriptor
        uint16_t         mWidth;           // Width in pixels.
        uint16_t         mHeight;          // Height in pixels.
        uint32_t         mFormat;          // Format.
        uint32_t         mUsage;           // Android usage flags.
        uint32_t         mPitch;           // Pitch in bytes.
        uint32_t         mSize;            // Size in bytes.
        uint16_t         mAllocWidth;      // Actual allocation width (used for oversize allocations optimisation).
        uint16_t         mAllocHeight;     // Actual allocation height (used for oversize allocations optimisation).
        EDataSpaceRange  mColorRange;      // Buffer color space. Currently only correct for video layers
        uint16_t         mPavpSessionID;   // PAVP session.
        uint16_t         mPavpInstanceID;  // PAVP instance.
        ETilingFormat    mTilingFormat;    // Tiling format
        uint64_t         mMediaTimestamp;  // Media timestamp.
        uint32_t         mMediaFps;        // Media fps reported by decoder.
        uint32_t         mBufferModeFlags; // Buffer mode flags.
        ECompressionType mCompression;     // Is the buffer compressed.
        bool             mbEncrypted:1;    // Is the buffer encrypted
        bool             mbKeyFrame:1;     // Is the buffer a keyframe
        bool             mbInterlaced:1;   // Is the buffer interlaced
        bool             mbDeviceIdValid:1;// Is mDeviceId valid.
    };

    Layer();
    Layer(hwc_layer_1_t& hwc_layer);
    Layer(buffer_handle_t handle);

    // Reset layer to constructed state
    void clear();

    // Accessor functions
    uint32_t            getBufferWidth() const              { return getBufferDetails().getWidth();               }
    uint32_t            getBufferHeight() const             { return getBufferDetails().getHeight();              }
    uint32_t            getBufferFormat() const             { return getBufferDetails().getFormat();              }
    uint32_t            getBufferUsage() const              { return getBufferDetails().getUsage();               }
    uint64_t            getBufferDeviceId() const           { return getBufferDetails().getDeviceId();            }
    bool                isBufferDeviceIdValid() const       { return getBufferDetails().isDeviceIdValid();        }
    uint32_t            getBufferPitch() const              { return getBufferDetails().getPitch();               }
    uint32_t            getBufferSize() const               { return getBufferDetails().getSize();                }
    uint32_t            getBufferAllocWidth() const         { return getBufferDetails().getAllocWidth();          }
    uint32_t            getBufferAllocHeight() const        { return getBufferDetails().getAllocHeight();         }
    uint32_t            getBufferPavpSessionID() const      { return getBufferDetails().getPavpSessionID();       }
    uint32_t            getBufferPavpInstanceID() const     { return getBufferDetails().getPavpInstanceID();      }
    ETilingFormat       getBufferTilingFormat() const       { return getBufferDetails().getTilingFormat();        }
    uint64_t            getMediaTimestamp() const           { return getBufferDetails().getMediaTimestamp();      }
    uint32_t            getBufferModeFlags() const          { return getBufferDetails().getBufferModeFlags();     }
    uint32_t            getMediaFps() const                 { return getBufferDetails().getMediaFps();            }
    ECompressionType    getBufferCompression() const        { return getBufferDetails().getCompression();         }

    uint32_t            getHints() const                    { return mHints;                            }
    uint32_t            getFlags() const                    { return mFlags;                            }
    buffer_handle_t     getHandle() const
    {
        const Layer & target = mpComposition ? mpComposition->getTarget() : *this;
        return (&target == this) ? mHandle : target.getHandle();
    }
    ETransform          getTransform() const                { return mTransform;                        }
    EBlendMode          getBlending() const                 { return mBlending;                         }
    DataSpace           getDataSpace() const                { return mDataSpace;                        }
    const hwc_frect_t&  getSrc() const                      { return mSrc;                              }
    hwc_frect_t&        editSrc()                           { return mSrc;                              }
    const hwc_rect_t&   getDst() const                      { return mDst;                              }
    hwc_rect_t&         editDst()                           { return mDst;                              }
    float               getPlaneAlpha() const               { return mPlaneAlpha;                       }
    uint32_t            getFps() const                      { return mFrameRate.getFps();               }
    const FramerateTracker& getFrameRateTracker() const     { return mFrameRate;                        }
    FramerateTracker&   editFrameRateTracker()              { return mFrameRate;                        }
    AbstractComposition* getComposition() const             { return mpComposition;                     }

    int32_t             getDstX() const                     { return getDst().left;                     }
    int32_t             getDstY() const                     { return getDst().top;                      }
    uint32_t            getDstWidth() const                 { return getDst().right - getDst().left;    }
    uint32_t            getDstHeight() const                { return getDst().bottom - getDst().top;    }
    float               getSrcX() const                     { return getSrc().left;                     }
    float               getSrcY() const                     { return getSrc().top;                      }
    float               getSrcWidth() const                 { return getSrc().right - getSrc().left;    }
    float               getSrcHeight() const                { return getSrc().bottom - getSrc().top;    }
    float               getWidthScaleFactor() const         { return mWidthScaleFactor;                 }
    float               getHeightScaleFactor() const        { return mHeightScaleFactor;                }

    bool                isEnabled() const                   { return getHandle() != 0 ||  isComposition(); }
    bool                isDisabled() const                  { return getHandle() == 0 && !isComposition(); }
    bool                isEncrypted() const                 { return mBufferDetails.getEncrypted();           }
    bool                isVideo() const                     { return mbVideo;                           }
    bool                isAlpha() const                     { return mbAlpha;                           }
    bool                isPlaneAlpha() const                { return mPlaneAlpha != 1.0f;               }
    bool                isComposition() const               { return mpComposition != NULL;             }
    bool                isScale() const                     { return mbScale;                           }
    bool                isOversized() const                 { return mbOversized;                       }
    bool                isBlend() const                     { return mbBlend;                           }
    bool                isOpaque() const                    { return !mbBlend;                          }
    bool                isSrcOffset() const                 { return mbSrcOffset;                       }
    bool                isSrcCropped() const                { return mbSrcCropped;                      }
    bool                isFrontBufferRendered() const       { return mbFrontBufferRendered;             }
    bool                isFullScreenVideo(uint32_t outWidth, uint32_t outHeight) const;

    const Vector<hwc_rect_t>& getVisibleRegions() const     { return mVisibleRegions;                   }
    Vector<hwc_rect_t>& editVisibleRegions()                { return mVisibleRegions;                   }

    // Set various state.  NOTE: you *MUST* call 'onUpdateFlags()' following any of these.
    void setBufferFormat(int32_t format)                    { mBufferDetails.setFormat( format );
                                                              if ( formatToTiling( format ) != TILE_UNKNOWN )
                                                                mBufferDetails.setTilingFormat( formatToTiling( format ) ); }
    void setBufferCompression(ECompressionType compression) { mBufferDetails.setCompression( compression );   }
    void setBufferTilingFormat(ETilingFormat tileFormat)    { if ( formatToTiling( getBufferFormat() ) == TILE_UNKNOWN )
                                                                mBufferDetails.setTilingFormat( tileFormat ); }
    void setHints(uint32_t hints)                           { mHints = hints;                           }
    void setFlags(uint32_t flags)                           { mFlags = flags;                           }
    void setHandle(buffer_handle_t handle)                  { mHandle = handle;                         }
    void setTransform(ETransform transform)                 { mTransform = transform;                   }
    void setBlending(EBlendMode blending)                   { mBlending = blending;                     }
    void setDataSpace(DataSpace dataSpace)                  { mDataSpace = dataSpace;                   }
    void setSrc(hwc_rect_t src)                             { mSrc.left   = src.left;
                                                              mSrc.right  = src.right;
                                                              mSrc.top    = src.top;
                                                              mSrc.bottom = src.bottom;                 }
    void setSrc(const hwc_frect_t src)                      { mSrc = src;                               }
    void setDst(const hwc_rect_t dst)                       { mDst = dst;                               }
    void setPlaneAlpha(float planeAlpha)                    { mPlaneAlpha = planeAlpha;                 }
    void setVisibleRegions(const Vector<hwc_rect_t>& vr)    { mVisibleRegions = vr;                     }
    void setFps(uint32_t fps)                               { mFrameRate.setFps(fps);                   }
    void setComposition(AbstractComposition *pComposition)  { mpComposition = pComposition;             }
    void setBufferPavpSession(uint32_t session, uint32_t instance, uint32_t isEncrypted);

    int  getAcquireFence() const                            { return mSourceAcquireFence.get(); }
    const Timeline::FenceReference& getAcquireFenceReturn() const { return mSourceAcquireFence; }
    void setAcquireFenceReturn(int* pFence)                 { mSourceAcquireFence.setLocation( pFence ); }
    void returnAcquireFence(int32_t fence) const            { mSourceAcquireFence.set( fence ); }

    int  getReleaseFence() const                            { return mSourceReleaseFence.get(); }
    const Timeline::FenceReference& getReleaseFenceReturn() const { return mSourceReleaseFence; }
    void setReleaseFenceReturn(int* pFence)                 { mSourceReleaseFence.setLocation( pFence ); }
    void setReleaseFenceReturn(Timeline::Fence* pFence)     { mSourceReleaseFence.setLocation( pFence ); }
    void returnReleaseFence(int fence) const                { mSourceReleaseFence.merge( &fence ); }
    void cancelReleaseFence(void)                           { mSourceReleaseFence.cancel(); }

    bool waitAcquireFence(nsecs_t timeoutNs = 60000000000) const { return doWaitAcquireFence( timeoutNs ); }
    void closeAcquireFence() const                          { mSourceAcquireFence.close(); }

    // Waits for rendering to the layer's buffer to be complete.
    // Will wait for up to timeoutNs nanoseconds.
    // If timeoutNs is 0 then this is a polling test.
    // Returns false if the layer's buffer still has work pending.
    bool waitRendering(nsecs_t timeoutNs) const;

    // Update every field in the layer.
    // Note, accurate frame rate tracking requires the timestamp for the composition to be provided
    void onUpdateAll(hwc_layer_1& layer, nsecs_t frameTime = 0, bool bForceOpaque = false);
    void onUpdateAll(buffer_handle_t handle, bool bForceOpaque = false);

    // Update just the changing frame specific data. This should only be used when no geometry change has happened since the last onUpdateAll()
    // Note, accurate frame rate tracking requires the timestamp for the composition to be provided
    void onUpdateFrameState(const Layer& layer);
    void onUpdateFrameState(hwc_layer_1& layer, nsecs_t frameTime = 0);
    void onUpdateFrameState(buffer_handle_t handle, nsecs_t frameTime = 0);

    // Update the fence pointers only
    void onUpdateFences(const Layer& layer);

    // Update the internal flags.  Required after various 'set' calls.
    void onUpdateFlags();

    // Query the buffer state from the buffer manager
    void onUpdateBufferState();

    // Indication that an update Handle is pending (generally during the onPrepare call). Used to track FPS
    void onUpdatePending(nsecs_t frameTime);

    // Media timestamp access
    void onUpdateMediaTimestampFps(nsecs_t n, uint32_t fps) { mBufferDetails.setMediaTimestampFps(n, fps); }

    static const Layer& Empty();

    bool isEqual(const hwc_layer_1& layer) const;

    // Does this Layer match another Layer.
    // Returns true if match (ignoring handles).
    // If pbMatchesHandle is provided, then on return it will be set true iff handles also match.
    bool matches( const Layer& other, bool* pbMatchesHandle = NULL ) const;

    // Copy a "snapshot" of another layer.
    // This will copy the layer while also removing any indirection (e.g. to composition targets).
    // This must be used when taking a copy of a layer that will persist beyond the current frame.
    void snapshotOf( const Layer& other );

    // Dump layer to a string
    String8 dump(const char* pPrefix = NULL) const;

    // Dump the contents of a layer - only useful in internal builds
    // Will dump to /data/hwc/<name>.tga
    bool dumpContentToTGA(const String8& name) const;

private:
    bool doWaitAcquireFence(nsecs_t timeoutNs) const;

    const BufferDetails & getBufferDetails() const
    {
        const Layer & target = mpComposition ? mpComposition->getTarget() : *this;
        return (&target == this) ? mBufferDetails : target.getBufferDetails();
    }

private:

    // This class tracks the frame rate that this layers handle is changing at
    FramerateTracker            mFrameRate;

    // Fence references.
    Timeline::FenceReference mSourceAcquireFence;    // This is the location of the source layers acquire fence return value
    Timeline::FenceReference mSourceReleaseFence;    // This is the location of the source layers release fence return value

    AbstractComposition*        mpComposition;          // Pointer to the engine required to compose this layer. Null if its a uncomposed allocation.

    // Buffer details for this layers handle.
    // Only guaranteed to be valid between the start of prepare and the end of set
    BufferDetails               mBufferDetails;

    // Copy of the input layer state. This can be modified by the HWC at need
    buffer_handle_t             mHandle;
    hwc_frect_t                 mSrc;
    hwc_rect_t                  mDst;
    Vector<hwc_rect_t>          mVisibleRegions;
    uint32_t                    mHints;
    uint32_t                    mFlags;
    EBlendMode                  mBlending;
    ETransform                  mTransform;
    float                       mPlaneAlpha;
    DataSpace                   mDataSpace;

    // Store layer scale factor, maybe used to do some optimization.
    float                       mWidthScaleFactor;
    float                       mHeightScaleFactor;

    // State flags for the layer used in a variety of places
    bool                        mbVideo:1;                  // Is this a video buffer
    bool                        mbAlpha:1;                  // Does the buffer have an alpha channel
    bool                        mbScale:1;                  // Scaling is required.
    bool                        mbOversized:1;              // Buffer allocation is bigger than the allocated size.
    bool                        mbBlend:1;                  // Layer/buffer state indicate that blending MUST be enabled.
    bool                        mbSrcOffset:1;              // Layer is presenting an offset subrect of the source buffer.
    bool                        mbSrcCropped:1;             // Layer is presenting a cropped subrect of the source buffer.
    bool                        mbFrontBufferRendered:1;    // Rendering may occur after the buffer is presented.
};

inline bool operator==(const hwc_layer_1 &hwcLayer, const Layer &layer)
{
    return layer.isEqual(hwcLayer);
}

inline bool operator==(const Layer &layer, const hwc_layer_1 &hwcLayer)
{
    return layer.isEqual(hwcLayer);
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_LAYER_H
