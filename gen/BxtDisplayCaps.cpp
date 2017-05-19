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
#include "Format.h"
#include "BxtDisplayCaps.h"
#include "DisplayState.h"
#include "Transform.h"
#include "GenCompression.h"

namespace intel {
namespace ufo {
namespace hwc {

// Hardware minimum scale factors for the different formats
static const float      cMinScale               = 1.0 / 2.99;
static const float      cMinScaleNV12           = 1.0 / 1.99;
static const float      cMinScale2048           = 1.0 / 1.99;
static const uint32_t   cDisplayBufferBlocks    = 508;
static const uint32_t   cBlockBytes             = 512;;
static const uint32_t   cLimitLines             = 31;

#ifndef MAX
#define MAX(a,b) ((a)>=(b)?(a):(b))
#endif

BxtDisplayCaps::BxtDisplayCaps(uint32_t pipe, uint32_t scalarcount) :
    DisplayCaps(),
    mOptionScale("bxtscale", enableDownscale | enableUpscale),  // Enable both upscale and downscale by default
    mOptionLatencyL0("latencyl0", 20000 ),                      // 20.0us
    mPipe(pipe),
    mScalarCount(scalarcount),
    mDisplayState(*this)
{
}

void BxtDisplayCaps::probe()
{
    ALOGI_IF( sbLogViewerBuild, "DisplayCaps construct Broxton class caps for display pipe %d", mPipe);

    // All planes are common on a BXT
    for ( uint32_t s = 0; s < getNumPlanes(); ++s )
    {
        BxtPlaneCaps& caps = mPlanes[s];
        caps.setDisplayCaps( *this );

        // Enable sprite capabilities.
        caps.setBlendingMasks( static_cast<uint32_t>(EBlendMode::PREMULT) | static_cast<uint32_t>(EBlendMode::COVERAGE) );
        caps.enablePlaneAlpha( );
        caps.enableDisable( );
        caps.enableDecrypt( );
        caps.enableWindowing( );
        caps.enableSourceOffset( );
        caps.enableSourceCrop( );
        caps.enableScaling( );
        caps.setMaxSourceWidth( 8192 );
        caps.setMaxSourceHeight( 4096 );
        caps.setMaxSourcePitch( 32*1024 );

        caps.setTilingFormats(TILE_LINEAR | TILE_X | TILE_Y | TILE_Yf);

        Option enableBxtTransforms("bxttransforms",1);
        if (enableBxtTransforms)
        {
            // Note, 90 and 270 are only supported in conjunction with Y tiling and no render compression.
            // The caps arnt rich enough to express the limitiations, hence we have to have a custom callback
            // to validate the flag combinations are possible for BXT.
            static const ETransform transforms[] =
            {
                ETransform::NONE,
                ETransform::ROT_180,
                ETransform::ROT_90,
                ETransform::ROT_270,
            };
            caps.setTransforms( DISPLAY_CAPS_COUNT_OF( transforms ), transforms );
        }

        // Note that GL_RC is only supported with Y tiling on the first two
        // planes on pipes A & B with only 0 or 180 degree rotation.
        if ( (s < 2) && (mPipe < 2) && (s < cPlaneCount) )
        {
            caps.setHaveCompression(true);
        }
    }

    // Indicate that we need additional validation as not all combinations of caps can be used simultaneously.
    setComplexConstraints();

    // Note, this needs to be called after adding planes
    updateZOrderMasks();
}

// CDClk must exceed the pixel clock. However, the display should
// be running as low as possible in order to save power. Assume that the
// display is following this policy. The worst that can happen is we use
// the GPU for cases that the display may have been able to handle.
static uint32_t calcCdClk(int pixelClock)
{
    ALOG_ASSERT(pixelClock <= 624000);
    if      (pixelClock > 576000)       return 624000;
    else if (pixelClock > 384000)       return 576000;
    else if (pixelClock > 288000)       return 384000;
    else if (pixelClock > 144000)       return 288000;
    else                                return 144000;
}

bool BxtPlaneCaps::isScaleFactorSupported(const Layer& ly) const
{
    ALOG_ASSERT(mpDisplayCaps);

    float sw = ly.getSrcWidth();
    float sh = ly.getSrcHeight();

    if ((sw > 4096) || (sw < 8) || (sh < 8))
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isScaleFactorSupported() : Invalid source scalar dimensions %.2fx%.2f", sw, sh);
        return false;
    }
    else if (ly.isVideo() && ((sw < 16) || (sh < 16)))
    {
        // Technically BXT supports 8 high for YUV422, but its complex to know precisely whether its width or height when combined with
        // rotations. Hence, play it safe and keep both at a 16x16 minimum for all video formats
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isScaleFactorSupported() : Invalid NV12/YUV422 source scalar dimensions %.2fx%.2f", sw, sh);
        return false;
    }

    float w;
    float h;
    // 90/270 layers need to transpose width/height. Note, one flag checks for all rotation cases.
    if (isTranspose(ly.getTransform()))
    {
        w = ly.getDstHeight() / sw;
        h = ly.getDstWidth()  / sh;
    }
    else
    {
        w = ly.getDstWidth()  / sw;
        h = ly.getDstHeight() / sh;
    }

    if ((mpDisplayCaps->isUpscaleEnabled() == 0) && (w > 1.0f || h > 1.0f))
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isScaleFactorSupported() : Upscale disabled");
        return false;
    }
    else if ((mpDisplayCaps->isDownscaleEnabled() == 0) && (w < 1.0f || h < 1.0f))
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isScaleFactorSupported() : Downscale disabled");
        return false;
    }

    const DisplayState& state = mpDisplayCaps->getState();

    // In some cases during mode transitons, pixelclock is invalid and set to 0, hence disable any downscales
    if (state.getTiming().getPixelClock() <= 0 && (w < 1.0f || h < 1.0f))
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isScaleFactorSupported() : Invalid PixelClock %s", state.getTiming().dump().string());
        return false;
    }

    // The spec has these defined as downscale limits, but the HWC operates in terms of scale limits so the equations are inverted
    float pixelClk = state.getTiming().getPixelClock();
    float minClk = pixelClk / calcCdClk(pixelClk);
    float min = ((sw > 2048 || sh > 2048) ? cMinScale2048 : cMinScale);
    min = max(minClk, isNV12(ly.getBufferFormat()) ? cMinScaleNV12 : min);

    if (w < min || h < min || w * h < minClk)
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isScaleFactorSupported() : outside minimum scaling limit either (w(%f) or h(%f))<%f or w*h(%f)<%f %s", w, h, min, w*h, minClk, state.getTiming().dump().string());
        return false;
    }

    return true;
}


// In this function we need to exclude anything that the caps describe as possible, yet isnt
// actually possible on this hardware layer.
// For BXT, this is min/max scalar limitations, invalid combinations of rotations and tiling formats etc
bool BxtPlaneCaps::isSupported( const Layer& ly ) const
{
    // No transforms supported on RGB64 16:16:16:16 and CI8 if we ever add support for these

    // 90/270 rotations are only supported on TileY formats. Note single bit to check for all rotations
    if (isTranspose(ly.getTransform()))
    {
        if (ly.getBufferTilingFormat() != TILE_Y && ly.getBufferTilingFormat() != TILE_Yf && ly.getBufferTilingFormat() != TILE_Ys)
        {
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isSupported() : Invalid tile(%s) for rotation(%d)", getTilingFormatString(ly.getBufferTilingFormat()), ly.getTransform());
            return false;
        }

        // 90/270 is not supported on 565
        if (ly.getBufferFormat() == HAL_PIXEL_FORMAT_RGB_565)
        {
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isSupported() : Invalid format(%s) for rotation(%d)", getHALFormatString(ly.getBufferFormat()), ly.getTransform());
            return false;
        }

        // 90/270 is not supported with render compression
        if (ly.getBufferCompression() != COMPRESSION_NONE)
        {
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isSupported() : Invalid compression(%u) for rotation(%d)", ly.getBufferCompression(), ly.getTransform());
            return false;
        }
    }

    if (ly.getBufferCompression() != COMPRESSION_NONE)
    {
        // Compression is only supported on Y tiled buffers.
        if (ly.getBufferTilingFormat() != TILE_Y && ly.getBufferTilingFormat() != TILE_Yf && ly.getBufferTilingFormat() != TILE_Ys)
        {
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isSupported() : Invalid tile(%s) for compression(%u)", getTilingFormatString(ly.getBufferTilingFormat()), ly.getBufferCompression());
            return false;
        }

        // Compression is only supported on RGB8888
        if ( (ly.getBufferFormat() != HAL_PIXEL_FORMAT_RGBA_8888)
             && (ly.getBufferFormat() != HAL_PIXEL_FORMAT_RGBX_8888)
             && (ly.getBufferFormat() != HAL_PIXEL_FORMAT_BGRA_8888) )
        {
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isSupported() : Invalid format(%s) for compression(%u)", getHALFormatString(ly.getBufferFormat()), ly.getBufferCompression());
            return false;
        }
    }

    {
        // Working Source dimensions BXT hardware can only support whole source pixels.
        // For subsampled formats, it has to be whole pixels in the smaller plane.
        float x = ly.getSrcX();
        float y = ly.getSrcY();
        float w = ly.getSrcWidth();
        float h = ly.getSrcHeight();

        if (isNV12(ly.getBufferFormat()))
        {
            x /= 2;
            y /= 2;
            w /= 2;

            // 270 degree rotations need the PLANE_SIZE to be a multiple of 4.
            // Applying this to both 90/270 for consistency and because its not clear whether HWC 90 is display 270 rotation.
            h /= isTranspose(ly.getTransform()) ? 4 : 2;
        }
        else if (isYUV422(ly.getBufferFormat()))
        {
            if (isTranspose(ly.getTransform()))
            {
                y /= 2; // PLANE_OFFSET register has Y alignment restrictions for subsampled surfaces on 90/270
                h /= 2; // PLANE_SIZE register has height and width alignment restrictions for subsampled surfaces on 90/270
                w /= 2; // Bspec also says this needs to be even.
            }
            else
            {
                x /= 2; // PLANE_OFFSET register has X alignment restrictions for subsampled surfaces on 0/180
                w /= 2; // PLANE_SIZE register has width (but no height) alignment restrictions for subsampled surfaces on 0/180
            }
        }

        if (!isInteger(x) || !isInteger(y) || !isInteger(w) || !isInteger(h))
        {
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isSupported() : Invalid non integer source dimensions %f, %f %fx%f for format %s",
                ly.getSrcX(), ly.getSrcY(), ly.getSrcWidth(), ly.getSrcHeight(), getHALFormatString(ly.getBufferFormat()));
            return false;
        }
    }

    if ( !DisplayCaps::PlaneCaps::isSupported( ly ) )
    {
        return false;
    }

    ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtPlaneCaps::isSupported() : Yes : %s", ly.dump().string());
    return true;
}

ECompressionType BxtPlaneCaps::getCompression( unsigned index, int32_t displayFormat ) const
{
    if ((index == 0) && mbHaveCompression)
    {
        switch (displayFormat)
        {
            case HAL_PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_BGRA_8888:
            case HAL_PIXEL_FORMAT_RGBX_8888:
                return ECompressionType::GL_RC;
            default:
                break;
        }
    }
    return COMPRESSION_NONE;
}

// In this function we need to exclude anything that the caps describe as possible, yet isnt
// actually possible on this display.
// For BXT, this is exceeding the available scalars or the DBUF limit etc.
bool BxtDisplayCaps::isSupported( const Content::Display& display, uint32_t zorder ) const
{
    const Content::LayerStack& layers = display.getLayerStack();
    uint32_t numScalars = 0;

    // Pipe scaler consumes a scalar
    // TODO: if (isPipeScalar)
    //{
    //    numScalars = 1;
    //}

    const DisplayState& state = getState();
    const uint32_t numActiveDisplays = state.getNumActiveDisplays( );
    const uint32_t availDBuf = cDisplayBufferBlocks / ( ( numActiveDisplays > 0 ) ? numActiveDisplays : 1 );

    for (uint32_t i = 0; i < layers.size(); i++)
    {
        const Layer& ly = layers[i];

        if (ly.isDisabled())
            continue;

        // NV12 planes or scaling consumes a scalar
        if (isNV12(ly.getBufferFormat()) || ly.isScale())
        {
            numScalars++;
        }
    }
    // Check the Platform GLV or BXT
    // BXT : Two scalars, GLV: One Scalar
    if (numScalars > mScalarCount || (mPipe == 2 && numScalars > 1))
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtDisplayCaps::isSupported() : Too many scalars for pipe, %d required", numScalars );
        return false;
    }

    // DBuf.
    uint32_t reqDBuf = calculateDBuf( display, state.getTiming() );
    if ( reqDBuf > availDBuf )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "BxtDisplayCaps::isSupported() : Too many DBUF blocks required (%u v %u)",
                  reqDBuf, availDBuf );
        return false;
    }

    ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "BxtDisplayCaps::isSupported() : Yes : Z:%d Scalars:%d DBuf:%u/%u %s",
        zorder, numScalars, reqDBuf, availDBuf, display.dump().string() );
    return true;
}

float BxtDisplayCaps::calculateDownScale( const float sw, const float sh, const uint32_t dw, const uint32_t dh ) const
{
    if ( dw && dh )
    {
        const float hscale = sw/dw;
        const float vscale = sh/dh;
        const float hdown = MAX( 1.0f, hscale );
        const float vdown = MAX( 1.0f, vscale );
        const float totaldown = hdown * vdown;
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "  calculateDownScale %.2fx%.2f->%ux%u : hscale %.2f vscale %.2f hdown %.2f vdown %.2f totaldown %.2f",
                  sw, sh, dw, dh, hscale, vscale, hdown, vdown, totaldown );
        return totaldown;
    }
    ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
              "  calculateDownScale %.2fx%.2f->%ux%u : bad input totaldown 1.0",
              sw, sh, dw, dh );
    return 1.0f;
}

float BxtDisplayCaps::calculatePipeDownScale( const Content::Display& display ) const
{
    if ( !display.isOutputScaled() )
    {
        return 1.0f;
    }
    const float sw = (float)display.getWidth( );
    const float sh = (float)display.getHeight( );
    const hwc_rect_t& out = display.getOutputScaledDst( );
    const uint32_t dw = out.right - out.left;
    const uint32_t dh = out.bottom - out.top;
    return calculateDownScale( sw, sh, dw, dh );
}

float BxtDisplayCaps::calculateLayerDownScale( const Layer& ly ) const
{
    if ( !ly.isScale( ) )
    {
        return 1.0f;
    }
    const bool bTransposed = isTranspose( ly.getTransform() );
    const float sw = bTransposed ? ly.getSrcHeight() : ly.getSrcWidth();
    const float sh = bTransposed ? ly.getSrcWidth() : ly.getSrcHeight();
    const uint32_t dw = ly.getDstWidth();
    const uint32_t dh = ly.getDstHeight();
    return calculateDownScale( sw, sh, dw, dh );
}

uint32_t BxtDisplayCaps::calculateMinimumYTileScanlines( const bool bTransposed, const uint32_t format, const uint32_t bpp ) const
{
    if ( bTransposed )
    {
        if ( ( bpp == 1 ) || isNV12( format ) )
            return 16;
        else if ( ( bpp == 2 ) || ( format == HAL_PIXEL_FORMAT_YCbCr_422_I ) )
            return 8;
        else if ( bpp == 8 )
        {
            // Not supported.
            return 0;
        }
    }
    return 4;
}

uint32_t BxtDisplayCaps::calculateAbsoluteMinimumYTileScanlines( const bool bTransposed, const uint32_t bpp ) const
{
    // 0/180 Rotation => 8 scanlines.
    if ( !bTransposed )
        return 8;
    // 90/270 (transpose) => depends on plane Bpp : 1 Bpp:32, 2 Bpp:16, 4 Bpp:8, 8 Bpp:4
    // Conservatively rounding up to next defined Bpp.
    if ( bpp >= 5 )
        return 4;
    else if ( bpp >= 3 )
        return 8;
    else if ( bpp >= 2 )
        return 16;
    return 32;
}

uint32_t BxtDisplayCaps::calculatePlaneBlocks( const uint32_t pipeHTotal,
                                               const uint32_t planeSourceWidth,
                                               const uint64_t adjustedPlanePixelRate,
                                               const uint32_t format,
                                               const uint32_t planeBpp,
                                               const bool bYTiled,
                                               const bool bTransposed,
                                               const bool bCompressed ) const
{
    // Assume L0 latency.
    const float latencyUs = 0.001f * mOptionLatencyL0;

    ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
              "  calculatePlaneBlocks latency %.2f, pipeHTotal %u, planeW %u, adjPlanePixelRate %" PRIu64 " format %u, bpp %u, ytile %u, transposed %u, compressed %u",
              latencyUs, pipeHTotal, planeSourceWidth, adjustedPlanePixelRate, format, planeBpp, bYTiled, bTransposed, bCompressed );

    // Input sanity checks.
    ALOG_ASSERT( cBlockBytes );

    // Mode change transitions unknown timing by design.
    if ( !pipeHTotal || !planeSourceWidth || !planeBpp )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "Missing state pipeHTotal %u pipeSourceW %u planeBpp %u", pipeHTotal, planeSourceWidth, planeBpp );
        return ~0U;
    }

    // METHOD2.
    const uint32_t method2_planeBytesPerLine = planeSourceWidth * planeBpp;
    ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
              "  calculatePlaneBlocks  method2_planeBytesPerLine = %u x %u = %u",
              planeSourceWidth, planeBpp, method2_planeBytesPerLine );

    const uint32_t yTileMinLines = calculateMinimumYTileScanlines( bTransposed, format, planeBpp );
    if ( yTileMinLines == 0 )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "  calculatePlaneBlocks   yTileMinLines not supported\n" );
        return ~0U; // Not supported.
    }

    const float method2_planeBlocksPerLine = ( bYTiled ) ? ceilf( (float)yTileMinLines * (float)method2_planeBytesPerLine / (float)cBlockBytes )  / (float)yTileMinLines
                                                         : ceilf( (float)method2_planeBytesPerLine / (float)cBlockBytes );
    ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
              "  calculatePlaneBlocks  method2_planeBlocksPerLine = %.2f",
              method2_planeBlocksPerLine );
    ALOG_ASSERT( method2_planeBlocksPerLine > 0.0f );

    const uint32_t method2_lines = (uint32_t)ceilf( ( (1.0f/1000000.0f) * adjustedPlanePixelRate * latencyUs ) / pipeHTotal );
    ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
              "  calculatePlaneBlocks  method2_lines = ceil( ( %.2f * %.2f ) / %u ) = %u",
              1.0f/1000000.0f * adjustedPlanePixelRate, latencyUs, pipeHTotal, method2_lines );

    const uint32_t method2 = (uint32_t)ceilf( method2_lines * method2_planeBlocksPerLine );
    ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
              "  calculatePlaneBlocks  method2 = %u * %.2f = %u",
              method2_lines, method2_planeBlocksPerLine, method2 );

    uint32_t resultLines;
    uint32_t resultBlocks;

    // Linear or X-Tiled must allocate a minimum of 8 blocks.
    // NOTE: This is application of "basic" BSPEC "Display Buffer Programming".
    uint32_t absoluteMinimumBlocks = 8;

    if ( bYTiled )
    {
        // Override the absolute minimum block requirement for Y-Tile.
        // NOTE: This is application of "basic" BSPEC "Display Buffer Programming".
        const uint32_t minScanLinesSimple = calculateAbsoluteMinimumYTileScanlines( bTransposed, planeBpp );
        absoluteMinimumBlocks = ceilf( ( 4.0f * (float)planeSourceWidth * (float)planeBpp ) / 512.0f ) * minScanLinesSimple/4 + 3;

        const uint32_t yTileMin = (uint32_t)ceilf( yTileMinLines * method2_planeBlocksPerLine );

        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "  calculatePlaneBlocks   yTileMin = %u x %.2f = %u",
                  yTileMinLines, method2_planeBlocksPerLine, yTileMin );

        resultBlocks = MAX( method2, yTileMin );

        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "  calculatePlaneBlocks   resultBlocks = MAX( %u, %u ) = %u",
                  method2, yTileMin, resultBlocks );

        resultLines = ceilf( (1.0f / method2_planeBlocksPerLine) * resultBlocks );
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "  calculatePlaneBlocks   resultLines = ceil( %u / %.2f ) = %u\n",
                resultBlocks, method2_planeBlocksPerLine, resultLines );

        if ( bCompressed )
        {
            resultLines += yTileMinLines;
            resultBlocks += yTileMin;
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                "  calculatePlaneBlocks   (compressed) resultLines + %u = %u\n",
                yTileMinLines, resultLines );
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                "  calculatePlaneBlocks   (compressed) resultBlocks + %u = %u\n",
                yTileMin, resultBlocks );
        }
    }
    else
    {
        // METHOD1.
        const uint32_t method1 = (uint32_t)ceilf( ( (1.0f/1000000.0f) * adjustedPlanePixelRate * latencyUs * planeBpp ) / cBlockBytes );
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "  calculatePlaneBlocks   method1 = ceil( ( %.2f * %.2f * %u ) / %u ) = %u",
                  (1.0f/1000000.0f) * adjustedPlanePixelRate, latencyUs, planeBpp, cBlockBytes, method1 );

        resultBlocks = method1;

        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "  calculatePlaneBlocks   resultBlocks = sel[ %u, %u ] = %u",
                  method1, method2, resultBlocks );
        resultLines = ceilf( (1.0f / method2_planeBlocksPerLine) * resultBlocks );
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "  calculatePlaneBlocks   resultLines = ceil( %u / %.2f ) = %u\n",
                  resultBlocks, method2_planeBlocksPerLine, resultLines );
    }

    if ( resultLines > cLimitLines )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "  calculatePlaneBlocks   Out-of-lines %u v %u\n", resultLines, cLimitLines );
        return ~0U; // Not supported.
    }
    // The plane requirement is selected + 1.
    // Plus one more to account for block in flight.
    // Ref: GEN9+ Display Watermark 0.7 Revision note.
    return MAX( absoluteMinimumBlocks, resultBlocks + 2 );
}

uint32_t BxtDisplayCaps::calculateMinimumBlocks( const uint32_t pipeHTotal, const uint64_t adjustedPipePixelRate, const Layer& ly ) const
{
    ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "  calculateMinimumBlocks Layer:%s", ly.dump().string() );

    uint64_t adjustedPlanePixelRate = adjustedPipePixelRate;
    if ( ly.isScale() )
    {
        const float planeDownScaleAmount = calculateLayerDownScale( ly );
        adjustedPlanePixelRate = (uint64_t)ceilf( planeDownScaleAmount * adjustedPlanePixelRate );
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG,
                  "  calculateMinimumBlocks planeDownScaleAmount %.2f planePixelRate %" PRIu64,
                  planeDownScaleAmount, adjustedPlanePixelRate );
    }

    // Format and tileFormat.
    const ETilingFormat tilingFormat = ly.getBufferTilingFormat();

    const bool bYTiled = ( tilingFormat == TILE_Y );

    const uint32_t format = ly.getBufferFormat();

    const bool bCompressed = ( ly.getBufferCompression() != COMPRESSION_NONE );

    const bool bTransposed = isTranspose( ly.getTransform() );

    const uint32_t planeSourceWidth = bTransposed ? ly.getSrcHeight() : ly.getSrcWidth();

    if ( isYUV420Planar( format ) )
    {
        // Planar Y+UV.
        const uint32_t blocksY = calculatePlaneBlocks( pipeHTotal,
                                                       planeSourceWidth,
                                                       adjustedPlanePixelRate,
                                                       format,
                                                       1,
                                                       bYTiled,
                                                       bTransposed,
                                                       bCompressed );
        const uint32_t blocksUV = calculatePlaneBlocks( pipeHTotal,
                                                        planeSourceWidth/2,
                                                        adjustedPlanePixelRate,
                                                        format,
                                                        2,
                                                        bYTiled,
                                                        bTransposed,
                                                        bCompressed );
        return ( blocksY + blocksUV );
    }
    else
    {
        // Single packed plane.
        const uint32_t planeBpp = ( bitsPerPixelForFormat( format ) + 7 ) / 8;
        const uint32_t blocks = calculatePlaneBlocks( pipeHTotal,
                                                      planeSourceWidth,
                                                      adjustedPlanePixelRate,
                                                      format,
                                                      planeBpp,
                                                      bYTiled,
                                                      bTransposed,
                                                      bCompressed );
        return blocks;
    }
}

uint32_t BxtDisplayCaps::calculateDBuf( const Content::Display&  display, const Timing& timing ) const
{
    const Content::LayerStack& stack = display.getLayerStack( );

    const uint32_t pipeHTotal = timing.getHTotal( );
    uint64_t adjustedPipePixelRate = timing.getPixelClock()*1000;
    if ( display.isOutputScaled() )
    {
        const float pipeDownScaleAmount = calculatePipeDownScale( display );
        adjustedPipePixelRate = (uint64_t)ceilf( pipeDownScaleAmount * adjustedPipePixelRate );
    }

    const uint32_t layers = stack.size();

    uint32_t reqDBuf = 0;
    for ( uint32_t i = 0; i < layers; ++i )
    {
        const Layer& ly = stack.getLayer( i );
        if ( ly.isDisabled() )
            continue;

        const uint32_t planeDBuf = calculateMinimumBlocks( pipeHTotal, adjustedPipePixelRate, ly );
        reqDBuf += planeDBuf;
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, " DBUF : Plane %u  +%u DBUF Blocks (%u)", i, planeDBuf, reqDBuf );
    }
    return reqDBuf;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
