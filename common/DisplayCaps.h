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

#ifndef INTEL_UFO_HWC_DISPLAY_CAPS_H
#define INTEL_UFO_HWC_DISPLAY_CAPS_H

#include <utils/Vector.h>
#include "Format.h"
#include "Content.h"
#include "Option.h"

namespace intel {
namespace ufo {
namespace hwc {

// Set whether to dump device caps to logcat (as well as hwclogviewer).
#if INTEL_HWC_INTERNAL_BUILD
#define LOG_DISPLAY_CAPS( A ) { ALOGD A; Log::add A; }
#else
#define LOG_DISPLAY_CAPS( A ) Log::add A
#endif

// Count entries in a fixed size array.
#define DISPLAY_CAPS_COUNT_OF( A ) sizeof( A )/sizeof( A[0] )

class DisplayState;

// ********************************************************************
// Display Capabilities Class.
// ********************************************************************
class DisplayCaps
{
public:
    static const uint32_t NAME_LENGTH = 8;

    //   The ZOrderLUT is limited by the lower of:
    //      i) The use of chars A-Z to represent zorder permutations (26).
    //     ii) The use of 32bit masks to represent overlays (32).
    static const uint32_t MAX_OVERLAYS = 26;

    // Create chip specific capabilities for the specific display pipe
    static DisplayCaps* create(uint32_t hardwarePipe, uint32_t deviceId);

    // Colour-space general formats.
    // Each HAL format is mapped to a format class.
    // The format class determines the buffer format used for intermediate buffers
    // when pre-processing is applied.
    enum ECSCClass
    {
        CSC_CLASS_RGBX = 0,         //< Surface format class is opaque RGB.
        CSC_CLASS_RGBA,             //< Surface format class is blendable RGB.
        CSC_CLASS_YUV8,             //< Surface format class is 8 bit YUV (opaque).
        CSC_CLASS_YUV16,            //< Surface format class is 16 bit YUV (opaque).
        CSC_CLASS_MAX,              //< Count of format classes.
        CSC_CLASS_NOT_SUPPORTED = CSC_CLASS_MAX
    };

    // A display specific look-up-table is used to
    // a) Define the permitted plane order permutations.
    // b) map each permitted permutation to a display code.
    class ZOrderLUTEntry
    {
        public:
            ZOrderLUTEntry( ) :
                mpchHWCZOrder( NULL ),
                mpchDisplayStr( NULL ),
                mDisplayEnum( 0 )
            {
            }
            ZOrderLUTEntry( const char* pchHWCZOrder, int32_t displayEnum, const char* pchDisplayStr ) :
                mpchHWCZOrder( pchHWCZOrder ),
                mpchDisplayStr( pchDisplayStr ),
                mDisplayEnum( displayEnum )
            {
            }
            const char* getHWCZOrder( void ) const { return mpchHWCZOrder; }
            int32_t getDisplayEnum( void ) const { return mDisplayEnum; }
            const char* getDisplayString( void ) const { return mpchDisplayStr; }
        protected:
            const char* mpchHWCZOrder;      // HWC ZOrder (generic string).
            const char* mpchDisplayStr;     // Equivalent display-specific human-readable string.
            int32_t mDisplayEnum;           // Equivalent display-specific enum value.
    };

    // Plane Capabilities Class.
    class PlaneCaps
    {
        public:
            // Constructor.
            PlaneCaps( );
            virtual ~PlaneCaps( );

            // Enable capabilities.
            void enableOpaqueControl( bool enable = true )      { mbCapFlagOpaqueControl = enable; }
            void enablePlaneAlpha( bool enable = true )         { mbCapFlagPlaneAlpha = enable; }
            void enableScaling( bool enable = true )            { mbCapFlagScaling = enable; }
            void enableDecrypt( bool enable = true )            { mbCapFlagDecrypt = enable; }
            void enableWindowing( bool enable = true )          { mbCapFlagWindowing = enable; }
            void enableSourceOffset( bool enable = true )       { mbCapFlagSourceOffset = enable; }
            void enableSourceCrop( bool enable = true )         { mbCapFlagSourceCrop = enable; }
            void enableDisable( bool enable = true )            { mbCapFlagDisable = enable; }
            void setTransforms( uint32_t numTransforms, const ETransform* pTransforms );
            void setDisplayFormats( const Vector<int32_t>& formats );
            void setDisplayFormats( uint32_t numDisplayFormats, const int32_t* pDisplayFormats );
            void setZOrderMasks(uint32_t pre, uint32_t post)    { mZOrderPreMask = pre; mZOrderPostMask = post; }
            void setName(const char* name)                      { strncpy(mName, name, NAME_LENGTH-1); mName[NAME_LENGTH-1] = 0; }
            void setBlendingMasks( uint32_t blend )             { mBlendingModeMask = (blend & BLENDING_MASK); mbCapFlagBlending = mBlendingModeMask ? true : false;}
            void setMaxSourceWidth( uint32_t maxWidth )         { mMaxSourceWidth = maxWidth; }
            void setMaxSourceHeight( uint32_t maxHeight )       { mMaxSourceHeight = maxHeight; }
            void setMinSourceWidth( uint32_t minWidth )         { mMinSourceWidth = minWidth; }
            void setMinSourceHeight( uint32_t minHeight )       { mMinSourceHeight = minHeight; }
            void setMaxSourcePitch(uint32_t maxPitch)           { mMaxSourcePitch = maxPitch; }
            void setTilingFormats( uint32_t formats )           { mTilingFormats = formats; }

            // Test specific capabilities.
            bool isOpaqueControlSupported( void ) const         { return mbCapFlagOpaqueControl; }
            bool isPlaneAlphaSupported( void ) const            { return mbCapFlagPlaneAlpha; }
            bool isBlendingSupported( void ) const              { return mbCapFlagBlending; }
            bool isBlendingModeSupported( EBlendMode blend ) const
            {
                bool bSupported = false;
                switch( blend )
                {
                    case EBlendMode::NONE:
                        bSupported = true;
                        break;
                    case EBlendMode::PREMULT:
                        bSupported = (mBlendingModeMask & static_cast<uint32_t>(EBlendMode::PREMULT)) ? true : false;
                        break;
                    case EBlendMode::COVERAGE:
                        bSupported = (mBlendingModeMask & static_cast<uint32_t>(EBlendMode::COVERAGE)) ? true : false;
                        break;
                }
                return bSupported;
            }
            bool isScalingSupported( void ) const               { return mbCapFlagScaling; }
            bool isDecryptSupported( void ) const               { return mbCapFlagDecrypt; }
            bool isWindowingSupported( void ) const             { return mbCapFlagWindowing; }
            bool isSourceOffsetSupported( void ) const          { return mbCapFlagSourceOffset; }
            bool isSourceCropSupported( void )const             { return mbCapFlagSourceCrop; }
            bool isDisableSupported( void ) const               { return mbCapFlagDisable; }
            bool isTransformSupported( ETransform transform ) const;
            bool isDisplayFormatSupported( int32_t displayFormat ) const;
            bool isTilingFormatSupported( ETilingFormat format ) const { return (mTilingFormats & format) != 0; }
            bool isSourceSizeSupported( uint32_t width, uint32_t height, uint32_t pitch ) const
            {
                return ( ( width <= mMaxSourceWidth ) && ( height <= mMaxSourceHeight )
                      && ( width >= mMinSourceWidth ) && ( height >= mMinSourceHeight )
                      && ( pitch <= mMaxSourcePitch ) );
            }
            bool isCompressionSupported( ECompressionType compression, int32_t displayFormat ) const;

            uint32_t    getZOrderPreMask( void ) const          { return mZOrderPreMask; }
            uint32_t    getZOrderPostMask( void ) const         { return mZOrderPostMask; }
            int32_t     getCSCFormat(ECSCClass format) const    { ALOG_ASSERT(format < CSC_CLASS_MAX); return mCSCFormat[format]; }
            const char* getName() const                         { return mName; }

            // This defaults to false, but devices may override it and check the scale factor for a layer
            virtual bool isScaleFactorSupported(const Layer&) const { return false; }

            // Get plane capabilities as human-readable string.
            String8 capsString( void ) const;

            // Get transform look-up-table as human-readable string.
            String8 transformLUTString( void ) const;

            // Get display-format look-up-table as human-readable string.
            String8 displayFormatLUTString( void ) const;

            // Get csc format look-up-table as human-readable string.
            String8 cscFormatLUTString( void ) const;

            // If the plane has complex constraints beyond the capabilities of the caps, then this can be implemented.
            // It will return true only if the layer is supported on this plane.
            // The base class checks tiling support.
            // If this is overridden then the base class must also be called.
            virtual bool isSupported( const Layer& ) const;

            // Return the compressions formats supported for a given display format.
            // Should be called with increasing value of index until COMPRESSION_NONE
            // is returned.  Compression formats should be returned in order of
            // best to worst.
            virtual ECompressionType getCompression( unsigned /*index*/, int32_t /*displayFormat*/ ) const { return COMPRESSION_NONE; }

        protected:
            // This should be called to recalculate the intermediate format table after a display format change
            void updateCSCFormats();

        protected:
            // Generic Plane capabilities.
            bool mbCapFlagOpaqueControl:1;          //< Forced opaque (disable of blending)
            bool mbCapFlagBlending:1;               //< Blending.
            bool mbCapFlagPlaneAlpha:1;             //< Plane alpha.
            bool mbCapFlagScaling:1;                //< Scaling.
            bool mbCapFlagDecrypt:1;                //< Decryption of protected buffers.
            bool mbCapFlagWindowing:1;              //< Windowing.
            bool mbCapFlagSourceOffset:1;           //< Offset source subrect.
            bool mbCapFlagSourceCrop:1;             //< Cropped source subrect.
            bool mbCapFlagDisable:1;                //< Can be disabled.
            uint32_t mBlendingModeMask;             //< Blending mode support.
            uint32_t mZOrderPreMask;                //< Which overlays can be order before this overlay.
            uint32_t mZOrderPostMask;               //< Which overlays can be order after this overlay.
            Vector<ETransform> mTransformLUT;       //< Supported transforms.
            Vector<int32_t> mDisplayFormatLUT;      //< Supported display formats.
            int32_t mCSCFormat[ CSC_CLASS_MAX + 1]; //< Format to use for intermediate buffers (for each class).
            char mName[NAME_LENGTH];                //< Short name of this plane

            // BYT/CHT/BXT/SKL: plane source size register is 12bits for width/height.
            // Default value is 4096.
            // More details in BSpec, such as PIPESRCA-Pipe A Source Image Size
            uint32_t mMaxSourceWidth;               // < Supported max source width
            uint32_t mMaxSourceHeight;              // < Supported max source height

            // Set default min width/height: 1
            uint32_t mMinSourceWidth;               // < Supported min source width
            uint32_t mMinSourceHeight;              // < Supported min source height

            // VALLEYVIEW: tiled buffer max pitch 16K, linear buffer max pitch 32K
            // Higher platforms: max pitch 32K
            // More details in BSpec, such as: DSPASTRIDE-Display A Stride Register
            uint32_t mMaxSourcePitch;               // < Supported max source pitch
            uint32_t mTilingFormats;                // Bitmask of ETilingFormat bits describing the tiling formats this display supports
    };

    // Global scaling capabilities.
    // These 'CAPS' describe the displays supported features and limitations.
    // These are used to limit application of global scaling for this specific display.
    class GlobalScalingCaps
    {
        public:
            enum EFlags
            {
                GLOBAL_SCALING_CAP_SUPPORTED    = (1<<0),           // Display implements global scaling.
                GLOBAL_SCALING_CAP_OVERSCAN     = (1<<1),           // Display can scale to fit to destination inside display bounds.
                GLOBAL_SCALING_CAP_UNDERSCAN    = (1<<2),           // Display can scale to fit to destination outside display bounds.
                GLOBAL_SCALING_CAP_PILLARBOX    = (1<<3),           // Display supports fit to pillarbox with AR preservation.
                GLOBAL_SCALING_CAP_LETTERBOX    = (1<<4),           // Display supports fit to letterbox with AR preservation.
                GLOBAL_SCALING_CAP_WINDOW       = (1<<5),           // Display supports fit to arbitrary subwindow.
                GLOBAL_SCALING_CAP_SEAMLESS     = (1<<6),           // Display supports seamless transitions (between scaling/non-scaling).
            };

            // Constructor.
            GlobalScalingCaps( ) : mFlags( 0 ), mMinScale( 0.0f ), mMaxScale( 0.0f ),
                                   mMinSourceWidth( 0 ), mMinSourceHeight( 0 ),
                                   mMaxSourceWidth( 0 ), mMaxSourceHeight( 0 ) { };
            virtual ~GlobalScalingCaps( ) { };

            uint32_t getFlags( void ) const            { return mFlags; }
            float    getMinScale( void ) const         { return mMinScale; }
            float    getMaxScale( void ) const         { return mMaxScale; }
            uint32_t getMinSourceWidth( void ) const   { return mMinSourceWidth; }
            uint32_t getMinSourceHeight( void ) const  { return mMinSourceHeight; }
            uint32_t getMaxSourceWidth( void ) const   { return mMaxSourceWidth; }
            uint32_t getMaxSourceHeight( void ) const  { return mMaxSourceHeight; }

            void     setFlags( uint32_t flags )        { mFlags = flags; }
            void     setMinScale( float minScale )     { mMinScale = minScale; }
            void     setMaxScale( float maxScale )     { mMaxScale = maxScale; }
            void     setMinSourceWidth( uint32_t minSourceWidth )   { mMinSourceWidth = minSourceWidth; }
            void     setMinSourceHeight( uint32_t minSourceHeight ) { mMinSourceHeight = minSourceHeight; }
            void     setMaxSourceWidth( uint32_t maxSourceWidth )   { mMaxSourceWidth = maxSourceWidth; }
            void     setMaxSourceHeight( uint32_t maxSourceHeight ) { mMaxSourceHeight = maxSourceHeight; }

            // Get global scaling capabilities as human-readable string.
            String8 capsString( void ) const;
        protected:
            // Get Flags as human-readable string.
            String8 getFlagsString() const;
            uint32_t mFlags;                                        // Flags indicating level of support (if any).
            float    mMinScale;                                     // Minimum scale (0 if not constrained).
            float    mMaxScale;                                     // Maximum scale (0 if not constrained).
            uint32_t mMinSourceWidth;                               // Minimum source width (0 if not constrained).
            uint32_t mMinSourceHeight;                              // Minmum source height (0 if not constrained).
            uint32_t mMaxSourceWidth;                               // Maximum source width (0 if not constrained).
            uint32_t mMaxSourceHeight;                              // Maximum source height (0 if not constrained).
    };

    // Destructor.
    virtual ~DisplayCaps( );

    // Complete initialisation. The transport mechanism (eg DRM/ADF) will call this probe call to complete initialisation
    // of the chip capabilities after it has populated planes from the transports own enumeration mechanisms
    virtual void probe() = 0;

    // Create chip specific plane capabilities. May be null if device specific planes not supported
    // The expectation is that device specific initialisation of this object will happen in the probe() call.
    virtual PlaneCaps* createPlane(uint32_t planeIndex);

    // Get number of planes supported.
    uint32_t getNumPlanes( void ) const                     { return mpPlaneCaps.size( ); }

    // Get plane cap object.
    const PlaneCaps& getPlaneCaps( uint32_t plane ) const   { return *mpPlaneCaps[ plane ]; }

    // Accessor for non-const plane caps.
    PlaneCaps& editPlaneCaps( uint32_t plane )              { return *mpPlaneCaps[ plane ]; }

    // Add a plane to the caps
    void add(PlaneCaps* pPlane)                             { mpPlaneCaps.push(pPlane); }

    // Get GlobalScaling cap object.
    const GlobalScalingCaps& getGlobalScalingCaps( void ) const   { return mGlobalScalingCaps; }
    // Accessor for non-const GlobalScaling caps.
    GlobalScalingCaps& editGlobalScalingCaps( void )              { return mGlobalScalingCaps; }

    // Get ZOrder look-up-table.
    const ZOrderLUTEntry* getZOrderLUT( void ) const        { return mZOrderLUT.array( ); }

    // Get count of entries in the ZOrder look-up-table.
    uint32_t getNumZOrders( void ) const                    { return mZOrderLUT.size( ); }

    // set the zorder LUT
    void setZOrderLUT(Vector<ZOrderLUTEntry>& lut)
    {
        mZOrderLUT = lut;
        updateZOrderMasks();
    }

    // The default output format for generated render targets if needed
    int32_t  getDefaultOutputFormat() const                 { return mDefaultOutputFormat; }
    void setDefaultOutputFormat(int32_t format)             { mDefaultOutputFormat = format; }

    // Panel quality if known (Defaults to high quality : 16 bits per channel)
    uint32_t getOutputBitsPerChannel() const                { return mBitsPerChannel; }
    void setOutputBitsPerChannel(uint32_t bpc);

    // Seemless refresh rate change
    bool isSeemlessRateChangeSupported() const              { return mbSeamlessRateChange; }
    void setSeemlessRateChangeSupported(bool supported)     { mbSeamlessRateChange = supported; }

    // Does the display *require* device native buffers?
    // Default is true.
    bool areDeviceNativeBuffersRequired() const              { return mbNativeBuffersReq; }
    void setDeviceNativeBuffersRequred(bool bNative)         { mbNativeBuffersReq = bNative; }

    // Does the display have complex constraints that require additional validation?
    // (see isSupported())
    bool hasComplexConstraints( void ) const                { return mbComplexConstraints; }
    void setComplexConstraints( void )                      { mbComplexConstraints = true; }

    // Get a short name for this display
    const char* getName() const                             { return mName; }
    void setName(const char* name)                          { strncpy(mName, name, NAME_LENGTH-1); mName[NAME_LENGTH-1] = 0; }

    // Test specific plane capabilities.
    bool isOpaqueControlSupported( uint32_t plane ) const                           { return mpPlaneCaps[ plane ]->isOpaqueControlSupported( ); }
    bool isBlendingSupported( uint32_t plane ) const                                { return mpPlaneCaps[ plane ]->isBlendingSupported( ); }
    bool isScalingSupported( uint32_t plane ) const                                 { return mpPlaneCaps[ plane ]->isScalingSupported( ); }
    bool isDecryptSupported( uint32_t plane ) const                                 { return mpPlaneCaps[ plane ]->isDecryptSupported( ); }
    bool isWindowingSupported( uint32_t plane ) const                               { return mpPlaneCaps[ plane ]->isWindowingSupported( ); }
    bool isSourceOffsetSupported( uint32_t plane ) const                            { return mpPlaneCaps[ plane ]->isSourceOffsetSupported( ); }
    bool isSourceCropSupported( uint32_t plane )const                               { return mpPlaneCaps[ plane ]->isSourceCropSupported( ); }
    bool isDisableSupported( uint32_t plane ) const                                 { return mpPlaneCaps[ plane ]->isDisableSupported( ); }
    bool isTransformSupported( uint32_t plane, ETransform transform ) const         { return mpPlaneCaps[ plane ]->isTransformSupported( transform ); }
    bool isDisplayFormatSupported( uint32_t plane, int32_t displayFormat ) const    { return mpPlaneCaps[ plane ]->isDisplayFormatSupported( displayFormat ); }
    uint32_t getZOrderPreMask( uint32_t plane ) const                               { return mpPlaneCaps[ plane ]-> getZOrderPreMask(); }
    uint32_t getZOrderPostMask( uint32_t plane ) const                              { return mpPlaneCaps[ plane ]-> getZOrderPostMask(); }
    bool isSourceSizeSupported( uint32_t plane, uint32_t width, uint32_t height, uint32_t pitch ) const
    {
        return mpPlaneCaps[ plane ]->isSourceSizeSupported( width, height, pitch );
    }

    // Test whole display content capabilities.
    // If the display has complex constraints then this *MUST* be implemented.
    // It must return true only if the suggested displayContent is supported.
    virtual bool isSupported( const Content::Display& /*displayContent*/, uint32_t /*ZOrder*/ ) const { return true; }


    // TODO:
    //  This API is temporary - for updating variable state that is required
    //   when determining current capabilities. These will be removed later; to be
    //   re-implementated via a generic notification framework.
    virtual DisplayState* editState( void ) const { return NULL; }

    // Dump device capabilities to HWC log.
    void dump( void ) const;

    // Get plane capabilities as human-readable string.
    String8 planeCapsString( uint32_t plane ) const
        { return mpPlaneCaps[ plane ]->capsString( ); }

    // Get plane transform look-up-table as human-readable string.
    String8 planeTransformLUTString( uint32_t plane ) const
        { return mpPlaneCaps[ plane ]->transformLUTString( ); }

    // Get plane display-format look-up-table as human-readable string.
    String8 planeDisplayFormatLUTString( uint32_t plane ) const
        { return mpPlaneCaps[ plane ]->displayFormatLUTString( ); }

    // Get plane color space conversion format look-up-table as human-readable string.
    String8 planeCscFormatLUTString( uint32_t plane ) const
        { return mpPlaneCaps[ plane ]->cscFormatLUTString( ); }

    // Get zorders as human-readable string.
    String8 zOrdersString( void ) const;

    // Get display capabilities as human-readable string.
    virtual String8 displayCapsString( void ) const;

    static ECSCClass halFormatToCSCClass( int32_t halFmt, bool bForceOpaque = false );

    String8 globalScalingCapsString( ) const
        { return mGlobalScalingCaps.capsString( ); }

protected:
    // Constructor.
    DisplayCaps( void );

    // This should be called to recalculate the pre/post masks for each plane after a Z order LUT change
    void updateZOrderMasks();

    static Option           mPrioritizeNV12Y;       // Adjust peferences for YUV CSC/composition format.

    char                    mName[NAME_LENGTH];     // Short name of this display

    // Generic Display capabilities.
    Vector<PlaneCaps*>      mpPlaneCaps;            // Plane capabilities.
    GlobalScalingCaps       mGlobalScalingCaps;     // Display capabilities for global scaling.
    Vector<ZOrderLUTEntry>  mZOrderLUT;             // The ZOrder look-up-table.
    int32_t                 mDefaultOutputFormat;   // Ideal format for the output on this surface
    uint32_t                mBitsPerChannel;        // Output quality of the display (Defaults to high quality)
    bool                    mbSeamlessRateChange:1; // Supports DRRS
    bool                    mbNativeBuffersReq:1;   // Native buffers are required.
    bool                    mbComplexConstraints:1; // Does ths display require whole display content check.
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DISPLAY_CAPS_H
