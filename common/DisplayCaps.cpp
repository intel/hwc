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
#include "DisplayCaps.h"
#include "Layer.h"
#include "Log.h"

namespace intel {
namespace ufo {
namespace hwc {

// Adjust precedence of YUV CSC/composition format.
//  prioritizenv12y : 0 => {NV12X,YUY2,NV12Y}
//  prioritizenv12y : 1 => {NV12Y,NV12X,YUY2}
Option DisplayCaps::mPrioritizeNV12Y( "prioritizenv12y", 0, false );

DisplayCaps::PlaneCaps::PlaneCaps( ) :
    mbCapFlagOpaqueControl( true ),
    mbCapFlagBlending( false ),
    mbCapFlagPlaneAlpha( false ),
    mbCapFlagScaling( false ),
    mbCapFlagDecrypt( false ),
    mbCapFlagWindowing( false ),
    mbCapFlagSourceOffset( false ),
    mbCapFlagSourceCrop( false ),
    mbCapFlagDisable( false ),
    mBlendingModeMask( 0 ),
    mZOrderPreMask( 0 ),
    mZOrderPostMask ( 0 ),
    mMaxSourceWidth( 4096 ),
    mMaxSourceHeight( 4096 ),
    mMinSourceWidth( 1 ),
    mMinSourceHeight( 1 ),
    mMaxSourcePitch( 16*1024 ),
    mTilingFormats(TILE_LINEAR | TILE_X)
{
    mName[0] = 0;
    updateCSCFormats();
}

DisplayCaps::PlaneCaps::~PlaneCaps( )
{
}

void DisplayCaps::PlaneCaps::setTransforms( uint32_t numTransforms, const ETransform* pTransforms )
{
    mTransformLUT.clear( );
    ALOG_ASSERT( ( ( numTransforms == 0 ) && ( pTransforms == NULL ) )
              || ( ( numTransforms != 0 ) && ( pTransforms != NULL ) ) );
    for ( uint32_t t = 0; t < numTransforms; ++t )
    {
        mTransformLUT.push( pTransforms[t] );
    }
}

void DisplayCaps::PlaneCaps::setDisplayFormats( uint32_t numDisplayFormats, const int32_t* pDisplayFormats )
{
    mDisplayFormatLUT.clear( );
    ALOG_ASSERT( ( ( numDisplayFormats == 0 ) && ( pDisplayFormats == NULL ) )
              || ( ( numDisplayFormats != 0 ) && ( pDisplayFormats != NULL ) ) );
    for ( uint32_t f = 0; f < numDisplayFormats; ++f )
    {
        mDisplayFormatLUT.push( pDisplayFormats[f] );
    }
    updateCSCFormats();
}

void DisplayCaps::PlaneCaps::setDisplayFormats( const Vector<int32_t>& formats )
{
    mDisplayFormatLUT = formats;
    updateCSCFormats();
}

bool DisplayCaps::PlaneCaps::isTransformSupported( ETransform transform ) const
{
    uint32_t numTransforms = mTransformLUT.size( );
    if ( numTransforms )
    {
        const ETransform* pTransforms = mTransformLUT.array( );
        ALOG_ASSERT( pTransforms );
        for ( uint32_t t = 0; t < numTransforms; ++t )
        {
            if ( pTransforms[t] == transform )
            {
                return true;
            }
        }
    }
    return false;
}

bool DisplayCaps::PlaneCaps::isDisplayFormatSupported( int32_t displayFormat ) const
{
    uint32_t numDisplayFormats = mDisplayFormatLUT.size( );
    if ( numDisplayFormats )
    {
        const int32_t* pDisplayFormats = mDisplayFormatLUT.array( );
        ALOG_ASSERT( pDisplayFormats );
        for ( uint32_t f = 0; f < numDisplayFormats; ++f )
        {
            if ( pDisplayFormats[f] == displayFormat )
            {
                return true;
            }
        }
    }
    return false;
}

bool DisplayCaps::PlaneCaps::isCompressionSupported( ECompressionType compression, int32_t displayFormat ) const
{
    bool supported = false;
    for (unsigned i = 0;; ++i)
    {
        ECompressionType comp = getCompression( i, displayFormat );
        supported = (comp == compression);
        if (supported || (comp == COMPRESSION_NONE))
        {
            break;
        }
    }
    return supported;
}

void DisplayCaps::PlaneCaps::updateCSCFormats( void )
{
    // Set default CSC formats, using the first specified display format if available.
    if ( mDisplayFormatLUT.size() > 0 )
    {
        mCSCFormat[ CSC_CLASS_RGBX ] = mDisplayFormatLUT[ 0 ];
        mCSCFormat[ CSC_CLASS_RGBA ] = mDisplayFormatLUT[ 0 ];
    }
    else
    {
        mCSCFormat[ CSC_CLASS_RGBX ] = HAL_PIXEL_FORMAT_RGBX_8888;
        mCSCFormat[ CSC_CLASS_RGBA ] = INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT;
    }

    // Set default YUV CSC format from RGBX.
    mCSCFormat[ CSC_CLASS_YUV8 ] = mCSCFormat[ CSC_CLASS_RGBX ];

    // Override RGBX CSC to a preferred format if it is supported.
    if ( isDisplayFormatSupported( HAL_PIXEL_FORMAT_RGBX_8888 ) )
    {
        mCSCFormat[ CSC_CLASS_RGBX ] = HAL_PIXEL_FORMAT_RGBX_8888;
    }

    // Override RGBA CSC to a preferred format if it is supported.
    if ( isDisplayFormatSupported( INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT ) )
    {
        mCSCFormat[ CSC_CLASS_RGBA ] = INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT;
    }

    // Override YUV CSC to a preferred format if it is supported.
    if ( mPrioritizeNV12Y && isDisplayFormatSupported( HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL ) )
    {
        mCSCFormat[ CSC_CLASS_YUV8 ] = HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL;
    }
    else if ( isDisplayFormatSupported( HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL ) )
    {
        mCSCFormat[ CSC_CLASS_YUV8 ] = HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL;
    }
    else if ( isDisplayFormatSupported( HAL_PIXEL_FORMAT_YCbCr_422_I ) )
    {
        mCSCFormat[ CSC_CLASS_YUV8 ] = HAL_PIXEL_FORMAT_YCbCr_422_I;
    }
    else if ( isDisplayFormatSupported( HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL ) )
    {
        mCSCFormat[ CSC_CLASS_YUV8 ] = HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL;
    }

    // Default the high bitdepth CSC format to either 1010102 if supported and
    // its a high bitdepth panel or to YUY8 if not.
    if (/* TODO: is10bppPanel() && */ isDisplayFormatSupported( HAL_PIXEL_FORMAT_A2R10G10B10_INTEL ) )
    {
        mCSCFormat[ CSC_CLASS_YUV16 ] = HAL_PIXEL_FORMAT_A2R10G10B10_INTEL;
    }
    else if ( isDisplayFormatSupported( HAL_PIXEL_FORMAT_A2B10G10R10_INTEL ) )
    {
        mCSCFormat[ CSC_CLASS_YUV16 ] = HAL_PIXEL_FORMAT_A2B10G10R10_INTEL;
    }
    else
    {
        mCSCFormat[ CSC_CLASS_YUV16 ] = mCSCFormat[ CSC_CLASS_YUV8 ];
    }

}

String8 DisplayCaps::PlaneCaps::capsString( void ) const
{
    String8 str( "" );
#define CHECK_LOG_CAP( C ) if ( mbCapFlag##C ) { \
        if ( str.length( ) )                     \
            str += "|";                          \
        str += #C; }
    CHECK_LOG_CAP( OpaqueControl )
    CHECK_LOG_CAP( Blending )
    CHECK_LOG_CAP( Scaling )
#if INTEL_HWC_INTERNAL_BUILD
    CHECK_LOG_CAP( Decrypt )
#endif
    CHECK_LOG_CAP( Windowing )
    CHECK_LOG_CAP( SourceOffset )
    CHECK_LOG_CAP( SourceCrop )
    CHECK_LOG_CAP( Disable )
#undef CHECK_LOG_CAP
    return str;
}

String8 DisplayCaps::PlaneCaps::transformLUTString( void ) const
{
    String8 str( "" );
    uint32_t numTransforms = mTransformLUT.size( );
    if ( numTransforms )
    {
        const ETransform* pTransforms = mTransformLUT.array( );
        ALOG_ASSERT( pTransforms );
        for ( uint32_t t = 0; t < numTransforms; ++t )
        {
            if ( t )
                str += "|";
            if ( pTransforms[t] == ETransform::NONE )
                str += "NONE";
            else if ( pTransforms[t] == ETransform::FLIP_H )
                str += "FLIPH";
            else if ( pTransforms[t] == ETransform::FLIP_V )
                str += "FLIPV";
            else if ( pTransforms[t] == ETransform::ROT_90 )
                str += "ROT90";
            else if ( pTransforms[t] == ETransform::ROT_180 )
                str += "ROT180";
            else if ( pTransforms[t] == ETransform::FLIP_H_90 )
                str += "FLIPH90";
            else if ( pTransforms[t] == ETransform::FLIP_V_90 )
                str += "FLIPV90";
            else if ( pTransforms[t] == ETransform::ROT_270 )
                str += "ROT270";
        }
    }
    else
    {
        str += "N/A";
    }
    return str;
}

String8 DisplayCaps::PlaneCaps::displayFormatLUTString( void ) const
{
    String8 str( "" );
    uint32_t numDisplayFormats = mDisplayFormatLUT.size( );
    if ( numDisplayFormats )
    {
        const int32_t* pDisplayFormats = mDisplayFormatLUT.array( );
        ALOG_ASSERT( pDisplayFormats );

        for ( uint32_t f = 0; f < numDisplayFormats; ++f )
        {
            if ( f )
                str += "|";
            str += getHALFormatString( pDisplayFormats[ f ] );
        }
    }
    str += "  Tiling:";
    if (mTilingFormats == TILE_UNKNOWN)
    {
        str += "? ";
    }
    else
    {
        if (mTilingFormats & TILE_LINEAR) str += "L ";
        if (mTilingFormats & TILE_X) str += "X ";
        if (mTilingFormats & TILE_Y) str += "Y ";
        if (mTilingFormats & TILE_Yf) str += "Yf ";
        if (mTilingFormats & TILE_Ys) str += "Ys ";
    }
    return str;
}

String8 DisplayCaps::PlaneCaps::cscFormatLUTString( void ) const
{
    String8 output;
    output.appendFormat("RGBX:%s ",  getHALFormatString( mCSCFormat[ CSC_CLASS_RGBX ] ) );
    output.appendFormat("RGBA:%s ",  getHALFormatString( mCSCFormat[ CSC_CLASS_RGBA ] ) );
    output.appendFormat("YUY8:%s ",  getHALFormatString( mCSCFormat[ CSC_CLASS_YUV8 ] ) );
    output.appendFormat("YUY16:%s ", getHALFormatString( mCSCFormat[ CSC_CLASS_YUV16 ] ) );
    return output;
}

bool DisplayCaps::PlaneCaps::isSupported( const Layer& ly ) const
{
    if ( !isTilingFormatSupported( ly.getBufferTilingFormat() ) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "PlaneCaps::isSupported() : Invalid tile(%s)", getTilingFormatString(ly.getBufferTilingFormat()) );
        return false;
    }
    return true;
}


String8 DisplayCaps::GlobalScalingCaps::capsString() const
{
    if ( !( mFlags & GLOBAL_SCALING_CAP_SUPPORTED ) )
    {
        return String8( "NOT SUPPORTED" );
    }
    String8 str( "" );
    str += "Flags:";
    str += getFlagsString();
    str.appendFormat( "|MinScale:%f", mMinScale );
    str.appendFormat( "|MaxScale:%f", mMaxScale );
    str.appendFormat( "|MinSourceWidth:%d", mMinSourceWidth );
    str.appendFormat( "|MinSourceHeight:%d", mMinSourceHeight );
    str.appendFormat( "|MaxSourceWidth:%d", mMaxSourceWidth );
    str.appendFormat( "|MaxSourceHeight:%d", mMaxSourceHeight );

    return str;
}

String8 DisplayCaps::GlobalScalingCaps::getFlagsString() const
{
    String8 str( "" );
#define CAPS_FLAGS_STR( C ) if ( mFlags & GLOBAL_SCALING_CAP_##C ) { \
        if ( str.length( ) )                                          \
            str += "|";                                               \
        str += #C; }

    CAPS_FLAGS_STR( SUPPORTED );
    CAPS_FLAGS_STR( OVERSCAN );
    CAPS_FLAGS_STR( UNDERSCAN );
    CAPS_FLAGS_STR( PILLARBOX );
    CAPS_FLAGS_STR( LETTERBOX );
    CAPS_FLAGS_STR( WINDOW );
    CAPS_FLAGS_STR( SEAMLESS );

    if ( str.length() == 0 )
        str += "null";

#undef CAPS_FLAGS_STR

    return str;
}

DisplayCaps::DisplayCaps( ) :
    mDefaultOutputFormat(INTEL_HWC_DEFAULT_HAL_PIXEL_FORMAT),
    mBitsPerChannel(INTEL_HWC_DEFAULT_BITS_PER_CHANNEL),
    mbSeamlessRateChange(false),
    mbNativeBuffersReq(true),
    mbComplexConstraints(false)
{
    mName[0] = 0;
    updateZOrderMasks();
}

DisplayCaps::~DisplayCaps( )
{
}

DisplayCaps::PlaneCaps* DisplayCaps::createPlane(uint32_t /*planeIndex*/)
{
    // Baseclass implementation doesnt support chip specific planes.
    return NULL;
}

void DisplayCaps::setOutputBitsPerChannel(uint32_t bpc)
{
    mBitsPerChannel = bpc;
}

void DisplayCaps::updateZOrderMasks( void )
{
    // For each overlay, establish which overlays can precede it and which overlays can follow it in Z order.
    // Do this by parsing the mpZOrderLUT.
    ALOG_ASSERT( mpPlaneCaps.size( ) <= MAX_OVERLAYS );

    uint32_t preMaskDefault = 0;
    uint32_t postMaskDefault = (1 << mpPlaneCaps.size( ))-1;
    for ( uint32_t ly = 0; ly < mpPlaneCaps.size( ); ++ly )
    {
        postMaskDefault &= ~( 1<<ly );

        char ovchar = 'A' + ly;
        uint32_t preMask = 0;
        uint32_t postMask = 0;

        for (uint32_t le = 0; le < mZOrderLUT.size(); le++)
        {
            const char* pchOv = strchr( mZOrderLUT[le].getHWCZOrder( ), ovchar );
            ALOGD_IF( !pchOv, "%s Missing overlay char [%c] in ZOrderLUT entry %u [==%s]", __FUNCTION__, ovchar, le, mZOrderLUT[le].getHWCZOrder( ) );
            if ( pchOv )
            {
                const char* pchTmp;
                pchTmp = mZOrderLUT[le].getHWCZOrder( );
                while ( pchTmp < pchOv )
                    preMask |= ( 1<<(*pchTmp++ - 'A') );
                pchTmp = pchOv+1;
                while ( *pchTmp != '\0' )
                    postMask |= ( 1<<(*pchTmp++ - 'A') );
            }
        }

        // Set the supported ZOrder so this information can be used to constrain the allocator.
        // Use the ordering established from the LUT,
        // else only permit default ordering (which is the overlay order).
        if ( mZOrderLUT.size() )
            mpPlaneCaps[ly]->setZOrderMasks( preMask, postMask );
        else
            mpPlaneCaps[ly]->setZOrderMasks( preMaskDefault, postMaskDefault );

        preMaskDefault |= ( 1<<ly );
    }
}


DisplayCaps::ECSCClass DisplayCaps::halFormatToCSCClass( int32_t halFmt, bool forceOpaque )
{
    switch (halFmt)
    {
        // RGBX class:
        case HAL_PIXEL_FORMAT_RGBX_8888     :
        case HAL_PIXEL_FORMAT_RGB_888       :
        case HAL_PIXEL_FORMAT_RGB_565       :
            return CSC_CLASS_RGBX;

        // RGBA class:
        case HAL_PIXEL_FORMAT_RGBA_8888     :
        case HAL_PIXEL_FORMAT_BGRA_8888     :
        case HAL_PIXEL_FORMAT_A2R10G10B10_INTEL:
        case HAL_PIXEL_FORMAT_A2B10G10R10_INTEL:
            if (forceOpaque)
                return CSC_CLASS_RGBX;
            else
                return CSC_CLASS_RGBA;

        // YUV class:
        case HAL_PIXEL_FORMAT_YV12          :
        case HAL_PIXEL_FORMAT_YCbCr_422_SP  :
        case HAL_PIXEL_FORMAT_YCrCb_420_SP  :
        case HAL_PIXEL_FORMAT_YCbCr_422_I   :
        case HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL  :
        case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL  :
        case HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL   :
        case HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL  :
        case HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL  :
        case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_Tiled_INTEL :
        case HAL_PIXEL_FORMAT_YUV420PackedSemiPlanar_INTEL       :
            return CSC_CLASS_YUV8;

        case HAL_PIXEL_FORMAT_P010_INTEL:
            return CSC_CLASS_YUV16;

        // Shouldn't try to handle these next ones.
        // Returns CSC_CLASS_MAX as 'unsupported'.
        case HAL_PIXEL_FORMAT_BLOB:
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
            return CSC_CLASS_NOT_SUPPORTED;

        // Else, default to RGBX.
        default:
            return CSC_CLASS_RGBX;
    }
}

String8 DisplayCaps::zOrdersString( void ) const
{
    String8 str( "" );
    uint32_t numZOrders = getNumZOrders( );
    if ( numZOrders )
    {
        for ( uint32_t z = 0; z < numZOrders; ++z )
        {
            if ( z )
                str += "|";
            str.appendFormat( "%s[%s]", mZOrderLUT[ z ].getHWCZOrder( ), mZOrderLUT[ z ].getDisplayString( ) );
        }
    }
    else
    {
        str += "N/A";
    }
    return str;
}

String8 DisplayCaps::displayCapsString( void ) const
{
    String8 str( "N/A" );
    return str;
}

void DisplayCaps::dump( void ) const
{
    LOG_DISPLAY_CAPS( ( "HWC Display %s Capabilities", mName ) );
    LOG_DISPLAY_CAPS( ( " Caps                         : %s", displayCapsString( ).string( ) ) );
    LOG_DISPLAY_CAPS( ( " ZOrders                      : %s", zOrdersString( ).string( ) ) );
    LOG_DISPLAY_CAPS( ( " GlobalScaling (panel fitter) : %s", globalScalingCapsString( ).string( ) ) );
    LOG_DISPLAY_CAPS( ( " DefaultOutput                : %s", getHALFormatString( mDefaultOutputFormat ) ) );
    LOG_DISPLAY_CAPS( ( " BitsPerChannel               : %d", mBitsPerChannel ) );
    LOG_DISPLAY_CAPS( ( " SeamlessRateChange           : %d", mbSeamlessRateChange ) );
    LOG_DISPLAY_CAPS( ( " NativeBufferReq              : %d", mbNativeBuffersReq ) );
    for ( uint32_t p = 0; p < mpPlaneCaps.size( ); ++p )
    {
        LOG_DISPLAY_CAPS( ( " Plane %u %s", p , getPlaneCaps(p).getName() ) );
        LOG_DISPLAY_CAPS( ( "  Caps       : %s", planeCapsString( p ).string( ) ) );
        LOG_DISPLAY_CAPS( ( "  Transforms : %s", planeTransformLUTString( p ).string( ) ) );
        LOG_DISPLAY_CAPS( ( "  Formats    : %s", planeDisplayFormatLUTString( p ).string( ) ) );
        LOG_DISPLAY_CAPS( ( "  CSC        : %s", planeCscFormatLUTString( p ).string( ) ) );
    }
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
