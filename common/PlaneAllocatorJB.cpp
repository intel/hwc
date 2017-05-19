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
#include "Option.h"
#include "PlaneAllocatorJB.h"
#include "Utils.h"
#include "Log.h"

#include <ui/GraphicBuffer.h>
#include <ufo/graphics.h>
#include <cutils/properties.h>

namespace intel {
namespace ufo {
namespace hwc {

// Helper class to hold the options related to compositions. The PlaneAllocator is
// created on the stack at need, so its inefficient to keep on reading property
// values from their source each time a Geometry change arrives.
class PlaneAllocatorJB::Options
{
public:

    Options() :
        mOverlay     ("overlay", 1),
        mOverlayRGB  ("overlayrgb", 1),
        mOverlayYUV  ("overlayyuv", 1),
        mPlaneZOrder ("overlayzorder", 1),
        mCollapse    ("collapse", 1)
    {
    }

    Option mOverlay;        //< Support upper planes (overlays)?
    Option mOverlayRGB;     //< Can put RGB layers on upper planes (overlays)?
    Option mOverlayYUV;     //< Can put YUV layers on upper planes (overlays)?
    Option mPlaneZOrder;    //< Plane ZOrder re-ordering enabled?
    Option mCollapse;       //< Allow collapse of layers to fit planes.
};

PlaneAllocatorJB::Options* PlaneAllocatorJB::spOptions = NULL;

// **************************************************************************************************************
//
// Overview:
// The allocator algorithm iterates all permutations of planes allocated to layers (with no repeats).
// Constraints are applied to reject invalid permutations.
// Scores are generated for each valid permutation to determine the best permutation to use.
//
// Terminology:
// Plane   - A plane is a hardware surface with which a layer can be presented to the display.
// Set     - A set is a contiguous grouping of layers (contiguous in layer order).
// Layer   - A layer represents content - a single discrete surface - that is to be presented to the display.
//           Layers are arranged in Z-order (depth order).
//           A layer may be allocated an plane (i.e. it is made part of a handled set), else it is part of
//           an unhandled set.
//
// Each unhandled layer set (group of layers that are not allocated an plane) must be "collapsed"
// by rendering them down to an intermediate target. Each resultant target will need its own plane with
// which to present that set to the display.
//
// Various constraints exist:
// - Which planes support which layers?
// - Are any planes absolutely required?
// - Are there any ordering restrictions?
// - How many handled sets are supported?
// - How many unhandled sets are supported?
//
// **************************************************************************************************************
class PlaneAllocator : NonCopyable
{
public:

    // Constants.
    static const uint32_t MAX_PLANES    = 8;
    static const uint32_t INVALID_PLANE = 0xFFFF;
    static const int64_t MIN_SCORE      = -0xFFFFFFFFFFFFLL;
    static const int64_t MAX_SCORE      = +0xFFFFFFFFFFFFLL;


    // Dummy composition (we don't expect this to be called into).
    class ProposedComposition : public AbstractComposition
    {
    public:
        ProposedComposition() {};
        virtual ~ProposedComposition() {};
        virtual const char* getName() const { return "Proposed"; }
        virtual const Layer& getTarget() { return *mpTarget; }
        virtual void onUpdate(const Content::LayerStack&) { ALOG_ASSERT( false ); }
        virtual void onUpdateOutputLayer(const Layer&) { ALOG_ASSERT( false ); }
        virtual void onCompose() { ALOG_ASSERT( false ); }
        virtual bool onAcquire() { ALOG_ASSERT( false ); return false; }
        virtual void onRelease() { ALOG_ASSERT( false ); }
        virtual float getEvaluationCost() { ALOG_ASSERT( false ); return 0; }
        virtual String8 dump(const char*) const { ALOG_ASSERT( false ); return String8(""); }
        Layer* mpTarget = NULL;
    };

    // The allocator returns its solution in this structure.
    class Solution : NonCopyable
    {
    public:

        // This describes the mapping to a display plane.
        class Plane
        {
            public:
            void reset(void)
            {
                mFirst = 0;
                mLast = 0;
                mTarget = Layer();
                mbUsed = false;
                mbCollapsed = false;
                mbPreProcess = false;
            };

            String8 dump( void ) const
            {
                if ( !mbUsed )
                    return String8::format( "-Disabled-" );
                else if ( mbCollapsed )
                    return String8::format( "<-- Collapse L%2u-L%2u <-- %s", mFirst, mLast, mTarget.dump().string());
                else if ( mbPreProcess )
                    return String8::format( "<-- PreProcess L%2u <-- %s",mFirst, mTarget.dump().string());
                else
                    return String8::format( "<-- Direct L%2u", mFirst );
            }

            uint32_t            mFirst;            //< Index of first contributing input layer.
            uint32_t            mLast;             //< Index of last contributing input layer.
            Layer               mTarget;           //< Target Layer (if CSC or collapse)
            ProposedComposition mComposition;
            bool                mbUsed:1;          //< Used?
            bool                mbCollapsed:1;     //< Used for a collapsed set of layers.
            bool                mbPreProcess:1;    //< Used for a pre-processed layer.
        };

        // Default constructor.
        Solution( ) :
            mNumPlanes( 0 ),
            maPlanes( NULL ),
            mZOrder( 0 ),
            mCompositions( 0 )
        {
        }

        // Destructor.
        ~Solution( )
        {
            delete [] maPlanes;
        }

        // Init output (set fixed number of planes for the display output).
        // Returns true if successful.
        bool init( uint32_t numPlanes )
        {
            if ( mNumPlanes == numPlanes )
                return true;
            delete [] maPlanes;
            mNumPlanes = 0;
            maPlanes = new Solution::Plane [ numPlanes ];
            if ( maPlanes == NULL )
                return false;
            mNumPlanes = numPlanes;
            return true;
        }

        // Reset mappings/counts.
        void reset( void )
        {
            for ( uint32_t pl = 0; pl < mNumPlanes; ++pl )
            {
                maPlanes[ pl ].reset();
            }
            mZOrder = 0;
            mCompositions = 0;
        }

        String8 dump( void ) const
        {
            String8 str;
            for ( uint32_t pl = 0; pl < mNumPlanes; ++pl )
            {
                str += String8::format( "P%u ", pl ) + maPlanes[ pl ].dump() + String8( "\n" );
            }
            str += String8::format( " (ZOrder %u/%s)", mZOrder, mZOrderStr.string() );
            return str;
        }

        // Number of output planes (an plane may be unused).
        uint32_t        mNumPlanes;
        // State for each output layer
        Plane*          maPlanes;
        // Display ZOrder.
        uint32_t        mZOrder;
        String8         mZOrderStr;
        // Number of compositions required.
        uint32_t        mCompositions;
    };

    // Evaluation.
    class Eval
    {
    public:
        // Flags.
        enum
        {
            FLAG_PREPROCESS = (1<<0),   //< Plane requires layer surface is pre-processed.
            FLAG_OVERSIZE   = (1<<1),   //< Plane oversize adjustment required.
        };
        Eval( ) :
            mbValid( false ),
            mScore( 0 ),
            mFlags( 0 )
        { }
        bool        mbValid;                //< Is the assignment valid.
        int64_t     mScore;                 //< The relative scoring (+ve => more preferred).
        uint32_t    mFlags;                 //< If the assignment is valid, then mFlags indicates any special info.

        Layer               mTarget;        //< Target Layer (if CSC or collapse)
        ProposedComposition mComposition;   //< Dummy Composition record

    };

    // Cached plane caps.
    class CachedPlaneCaps
    {
    public:
        enum
        {
            // Capability flags.
            FLAG_CAP_COLLAPSE   = (1<<0),    //< The plane supports collapsed layer-sets.
            FLAG_CAP_BLEND      = (1<<1),    //< The plane supports blending.
            FLAG_CAP_DECRYPT    = (1<<2),    //< The plane supports decrypt.
            // Behaviour/hint flags.
            FLAG_HINT_REQUIRED  = (1<<16)    //< The plane is required (can not be disabled).
        };
        CachedPlaneCaps( ) :
            mSupportedZOrderPreMask( ~0U ),
            mSupportedZOrderPostMask( ~0U ),
            mFlags( 0 )
        { }
        uint32_t    mSupportedZOrderPreMask;    //< The set of planes that may be placed before this plane.
        uint32_t    mSupportedZOrderPostMask;   //< The set of planes that may be placed after this plane.
        uint32_t    mFlags;                     //< Flags indicating behaviour/capabilities.
    };

    // Layer configuration.
    class LayerConfig
    {
    public:
        enum
        {
            UNSPECIFIED_LAYER_INDEX = ~0U
        };
        LayerConfig( ) :
            mIndex( 0 ),
            mbOptional( false ),
            mbEncrypted( false )
        { }
        // The index should correspond to the SF layer index.
        // i.e. The backmost layer should have index 0.
        uint32_t    mIndex;
        // Can this layer be omitted entirely.
        bool        mbOptional;
        // Is this layer encrypted.
        bool        mbEncrypted;
        // An evaluation of using each plane to handle this layer.
        Eval        mHandledEval[MAX_PLANES];
        // An evaluation of not using dedicated plane to handle this layer.
        Eval        mUnhandledEval;
    };

    // Default constructor.
    PlaneAllocator( const Content::Display& display, const DisplayCaps& caps ) :
        mDisplayInput( display ),
        mDisplayCaps( caps ),
        mNumPlanes( 0 ),
        mNumLayers( 0 ),
        maLayerConfig( NULL),
        mMaxHandledSets( MAX_PLANES ),
        mMaxUnhandledSets( MAX_PLANES )
    {
    }

    // Destructor.
    ~PlaneAllocator( )
    {
        delete [] maLayerConfig;
    }

    // Initialize.
    // This allocates layers/planes.
    // By default all planes are enabled and maximum unhandled sets is bound only by number of planes.
    // Returns true if successful.
    bool init( uint32_t enabledPlanes = 0, uint32_t maxUnhandledSets = 0 );

    // Returns number initialized planes.
    uint32_t getNumPlanes( void ) const { return mNumPlanes; }

    // Returns number initialized layers.
    uint32_t getNumLayers( void ) const { return mNumLayers; }

    // Access a layer config.
    LayerConfig& getLayerConfig( uint32_t ly ) { ALOG_ASSERT( ly < mNumLayers ); return maLayerConfig[ ly ]; }

    // Pre-evaluate inputs, setting up scores/weights/capabilites.
    // Call this before using findOptimalSolution().
    void preEvaluate( PlaneAllocatorJB::Options* pOptions, bool bOptimizeIdleDisplay );

    // Run allocator to find an optimal solution.
    // On success, returns a pointer to the best solution.
    const Solution* findOptimalSolution( void );

private:

    // CachedOptions class describes options passed to the isLayerSupportedOnPlane() method.
    class CachedOptions
    {
    public:
        // Construct options with defaults.
        CachedOptions( int32_t yuv, int32_t rgb,
                       bool optimizeIdleDisplay = true,
                       uint32_t permittedPreProcessCSCMask =
                        (1<<DisplayCaps::CSC_CLASS_YUV8)| (1<<DisplayCaps::CSC_CLASS_YUV16) ) :
            mbOverlayRGB( rgb ),
            mbOverlayYUV( yuv ),
            mbOptimizeIdleDisplay( optimizeIdleDisplay ),
            mPermittedPreProcessCSCMask( permittedPreProcessCSCMask )
        {
        }
        // Allow RGB sources on (non-main) planes.
        bool mbOverlayRGB : 1;
        // Allow YUV sources on (non-main) planes.
        bool mbOverlayYUV : 1;
        // Optimize for idle display.
        bool mbOptimizeIdleDisplay : 1;
        // This describes which color-spaces are candidates for pre-processing.
        uint32_t  mPermittedPreProcessCSCMask;
    };

    // References to display content and display caps.
    const Content::Display& mDisplayInput;
    const DisplayCaps& mDisplayCaps;

    // Cached plane caps info.
    CachedPlaneCaps maCachedPlaneCaps[MAX_PLANES];

    // Number of planes in pool.
    uint32_t mNumPlanes;

    // Number of layers to process.
    uint32_t mNumLayers;

    // Layer config.
    LayerConfig* maLayerConfig;

    // The maximum number of contiguous handled sets.
    // If planes are totatally independent then we are effectively unlimited for handled sets.
    // Setting zero here will disable all plane usage (except for the main plane).
    uint32_t mMaxHandledSets;

    // The maximum number of contiguous unhandled sets.
    // Unhandled sets must be rendered down.
    // Setting zero here will enforce complete plane usage.
    uint32_t mMaxUnhandledSets;


    // Internal solutions.
    Solution mSolution[2];

    // For displays requiring complex validation.
    Content::Display mDisplayOutput;

    // Helper to preEvaluate a layer.
    // Returns true if this plane MIGHT display this layer.
    // Returns false if this plane can definitely NOT display this layer.
    // If suported then on exit,
    //   - score is relative score to be used as a weighting.
    //   - flags will be updated with special info (e.g. FLAG_PREPROCESS).
    bool isLayerSupportedOnPlane( uint32_t ly, uint32_t pl, const CachedOptions& options, Eval& eval );

    // Helper used internally to above call
    bool isLayerSupportedOnPlane( uint32_t pl, const Layer& layer, const DisplayCaps::PlaneCaps& planeCaps, const CachedOptions& options, DisplayCaps::ECSCClass formatCSCClass, bool& bConsiderPreProcess);


    // Find display ZOrder enum given ZOrder in form 'ABCD'.
    uint32_t findBestZOrder( const char *pchZOrderStr );

    // Validate proposed solution for any complex constraints.
    // Solution is only valid if this returns true.
    bool validateSolution( const Solution& solution );

    // Helper to accumulate score.
    // Issues LOGE error and clamps if an overflow has occurred.
    static int64_t& AccumulateScore( int64_t& score, int64_t acc )
    {
        if ( acc < 0 )
        {
            if ( ( LLONG_MIN - acc ) > score )
            {
                ALOGE( "Accumulate Score %" PRIi64 " + %" PRIi64 " overflow, clamped LLONG_MIN (%lld)", score, acc, LLONG_MIN );
                score = LLONG_MIN;
            }
            else
            {
                score += acc;
            }
        }
        else
        {
            if ( ( LLONG_MAX - acc ) < score )
            {
                ALOGE( "Accumulate Score %" PRIi64 " + %" PRIi64 " overflow, clamped LLONG_MAX (%lld)", score, acc, LLONG_MAX );
                score = LLONG_MAX;
            }
            else
            {
                score += acc;
            }
        }
        return score;
    }

    // Internal processing data.
    class Scratch
    {
    public:
        Scratch( ) :
            assignedPlane(INVALID_PLANE),
            sharedPlane(INVALID_PLANE),
            nextPlane(0),
            runHandled(0),
            runUnhandled(0)
        { }
        // Current allocator state.
        uint32_t assignedPlane;        //< The assigned plane (if set to mNumPlanes => not using dedicated plane).
        uint32_t sharedPlane;          //< The shared plane (collapsed layers).
        // Recursion state.
        uint32_t nextPlane;            //< The next plane.
        uint32_t runHandled;        //< Accumulated run of handled layers (prior to this layer).
        uint32_t runUnhandled;      //< Accumulated run of unhandled layers (prior to this layer).
    };
}; // PlaneAllocator

bool PlaneAllocator::init( uint32_t enabledPlanes, uint32_t maxUnhandledSets )
{
    if ( enabledPlanes )
    {
        ALOG_ASSERT( enabledPlanes <= mDisplayCaps.getNumPlanes() );
        mNumPlanes = enabledPlanes;
    }
    else
    {
        mNumPlanes = mDisplayCaps.getNumPlanes();
    }
    mMaxHandledSets = mNumPlanes;
    mMaxUnhandledSets = maxUnhandledSets ? maxUnhandledSets : mNumPlanes;
    const uint32_t numLayers = mDisplayInput.getNumLayers();
    delete [] maLayerConfig;
    mNumLayers = 0;
    maLayerConfig = new LayerConfig[ numLayers ];
    if ( maLayerConfig == NULL )
    {
        return false;
    }
    mNumLayers = numLayers;
    // Cache some plane caps.
    for ( uint32_t pl = 0; pl < mNumPlanes; ++pl )
    {
        // ZOrders.
        maCachedPlaneCaps[ pl ].mSupportedZOrderPreMask = mDisplayCaps.getZOrderPreMask(pl);
        maCachedPlaneCaps[ pl ].mSupportedZOrderPostMask = mDisplayCaps.getZOrderPostMask(pl);
        maCachedPlaneCaps[ pl ].mFlags = 0;
        // For now, we can assume that any sprite planes have at least the same level of capability
        // as the main plane. If this changes for any future chips, we will need to adjust this. However,
        // take care of the HSW where we need to put a non supported RGBA Target on this plane
        maCachedPlaneCaps[ pl ].mFlags |= PlaneAllocator::CachedPlaneCaps::FLAG_CAP_COLLAPSE;
        // If upper layers end up collapsed and presented over lower layers then blending is required.
        // Let the allocator know whether this plane supports opaque/blended collapsed layer-sets.
        if ( mDisplayCaps.isBlendingSupported( pl ) )
            maCachedPlaneCaps[ pl ].mFlags |= PlaneAllocator::CachedPlaneCaps::FLAG_CAP_BLEND;
        // Can the plane be disabled?
        // If not, then we flag it as required.
        if ( !mDisplayCaps.isDisableSupported( pl ) )
            maCachedPlaneCaps[ pl ].mFlags |= PlaneAllocator::CachedPlaneCaps::FLAG_HINT_REQUIRED;
        // Can the plane present protected content?
        if ( mDisplayCaps.isDecryptSupported( pl ) )
            maCachedPlaneCaps[ pl ].mFlags |= PlaneAllocator::CachedPlaneCaps::FLAG_CAP_DECRYPT;
    }
    return true;
}

bool PlaneAllocator::isLayerSupportedOnPlane(uint32_t pl, const Layer& layer, const DisplayCaps::PlaneCaps& planeCaps, const CachedOptions& options, DisplayCaps::ECSCClass formatCSCClass, bool& bConsiderPreProcess)
{

    // Display size.
    const uint32_t displayWidth = mDisplayInput.getWidth();
    const uint32_t displayHeight = mDisplayInput.getHeight();

    // Initial refusal checks. These are absolute, they cannot be resolved by preprocessing.
    bConsiderPreProcess = false;
    if ( options.mbOptimizeIdleDisplay && !layer.isFrontBufferRendered() &&
         ( ( formatCSCClass == DisplayCaps::CSC_CLASS_RGBX ) || ( formatCSCClass == DisplayCaps::CSC_CLASS_RGBA ) ) )
    {
        // Do not permit RGB dedicated to any plane *including* the 'main plane  regardless of theoretical support.
        ALOGD_IF(  PLANEALLOC_CAPS_DEBUG, "%s %s : No [RGB not permitted on idle]", planeCaps.getName(), layer.dump().string() );
        return false;
    }
    else if ( ( pl > 0 )
            && ( ( formatCSCClass == DisplayCaps::CSC_CLASS_RGBX ) || ( formatCSCClass == DisplayCaps::CSC_CLASS_RGBA ) )
            && !options.mbOverlayRGB )
    {
        // Do not permit RGB dedicated to any plane *except* pl 0 regardless of theoretical support.
        ALOGD_IF(  PLANEALLOC_CAPS_DEBUG, "%s %s : No [RGB not permitted]", planeCaps.getName(), layer.dump().string() );
        return false;
    }
    else if ( ( pl > 0 )
           && ( formatCSCClass == DisplayCaps::CSC_CLASS_YUV8 || formatCSCClass == DisplayCaps::CSC_CLASS_YUV16 )
           && !options.mbOverlayYUV )
    {
        // Do not permit YUV dedicated to any plane *except* the pl 0 regardless of theoretical support.
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [YUV not permitted]", planeCaps.getName(), layer.dump().string() );
        return false;
    }
    else if ( ( layer.getDstX() != 0 || layer.getDstY() != 0 || layer.getDstWidth() != displayWidth || layer.getDstHeight() != displayHeight )
           && ( !( planeCaps.isWindowingSupported() ) ))
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Windowed]", planeCaps.getName(), layer.dump().string() );
        return false;
    }
    else if ( layer.isOpaque() && layer.isAlpha() && !( planeCaps.isOpaqueControlSupported()) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Opaque Control]", planeCaps.getName(), layer.dump().string() );
        return false;
    }
    else if ( layer.isBlend() && ( !( planeCaps.isBlendingModeSupported( layer.getBlending() ) ) ) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Blend]", planeCaps.getName(), layer.dump().string() );
        return false;
    }
    else if ( layer.isEncrypted() && !( planeCaps.isDecryptSupported() ) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Protected]", planeCaps.getName(), layer.dump().string() );
        return false;
    }
    else if ( layer.isPlaneAlpha() && !planeCaps.isPlaneAlphaSupported() )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Plane Alpha]", planeCaps.getName(), layer.dump().string() );
        return false;
    }



    // Check remaining state (crop, transform, format, scaling).
    // We can consider managing these using pre-processing.
    if ( layer.isSrcOffset() && !planeCaps.isSourceOffsetSupported() )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Source offset]", planeCaps.getName(), layer.dump().string() );
        bConsiderPreProcess = true;
        return false;
    }
    else if ( layer.isSrcCropped() && !planeCaps.isSourceCropSupported() )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Source crop]", planeCaps.getName(), layer.dump().string() );
        bConsiderPreProcess = true;
        return false;
    }
    else if ( ( layer.getTransform() != ETransform::NONE ) && !( planeCaps.isTransformSupported( layer.getTransform() ) ) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [transfrom != 0]", planeCaps.getName(), layer.dump().string() );
        bConsiderPreProcess = true;
        return false;
    }
    else if ( !planeCaps.isDisplayFormatSupported( layer.getBufferFormat()) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [format not supported]", planeCaps.getName(), layer.dump().string() );
        bConsiderPreProcess = true;
        return false;
    }
    else if ( (layer.isScale()) && !( planeCaps.isScalingSupported() && planeCaps.isScaleFactorSupported(layer) ) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Scaled (s%fx%f->d%dx%d)]",
            planeCaps.getName(), layer.dump().string(),
            layer.getSrcWidth(), layer.getSrcHeight(),
            layer.getDstWidth(), layer.getDstHeight());
        bConsiderPreProcess = true;
        return false;
    }
    else if ( mDisplayCaps.areDeviceNativeBuffersRequired() && !layer.isComposition() && !layer.isBufferDeviceIdValid() )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [device id is invalid]",
            planeCaps.getName(), layer.dump().string() );
        bConsiderPreProcess = true;
        return false;
    }
    else if ( !(planeCaps.isSourceSizeSupported( ceil(layer.getSrcWidth()), ceil(layer.getSrcHeight()), layer.getBufferPitch() ) ) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [Src Size]", planeCaps.getName(), layer.dump().string() );
        bConsiderPreProcess = true;
        return false;
    }
    else if ( (layer.getBufferCompression() != COMPRESSION_NONE) && !planeCaps.isCompressionSupported(layer.getBufferCompression(), layer.getBufferFormat()) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [buffer compression]",
            planeCaps.getName(), layer.dump().string() );
        bConsiderPreProcess = true;
        return false;
    }
    else if ( !planeCaps.isSupported(layer) )
    {
        ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [caps isSupported]", planeCaps.getName(), layer.dump().string() );
        bConsiderPreProcess = true;
        return false;
    }

    // If we got here, everything is suitable for direct plane flipping
    return true;
}


bool PlaneAllocator::isLayerSupportedOnPlane( uint32_t ly, uint32_t pl, const CachedOptions& options, Eval& eval )
{
    // Display input layers.
    const Content::LayerStack layers = mDisplayInput.getLayerStack();
    const Layer& layer = layers[ ly ];


    // Get the class of format for this layer source.
    DisplayCaps::ECSCClass formatCSCClass = DisplayCaps::halFormatToCSCClass( layer.getBufferFormat());
    ALOG_ASSERT(formatCSCClass < DisplayCaps::CSC_CLASS_MAX);

    // Caps for this plane.
    const DisplayCaps::PlaneCaps& planeCaps = mDisplayCaps.getPlaneCaps( pl );

    bool bConsiderPreProcess = false;

    // Set relative score of using this plane for each mode:
    //  'PassThru'      : The layer source is presented directly with no pre-processing.
    //  'preProcessYUV' : Pre-process to intermediate YUV.
    //  'preProcessRGB' : Pre-process to intermediate RGB.
    // If pre-processing is required, then YUY2 HAL format is preferred
    // (over RGBX) since it minimises bandwidth usage - both to convert
    // the layer source to the intermediate buffer and during scanout by the
    // display hardware.
    // TODO:
    //   Make this a genuine measure of (relative) bandwidth cost.
    const uint32_t passThruScore        = 15;
    const uint32_t preProcessVideoScore = 10;
    const uint32_t preProcessRGBScore   = 5;

    // Adjustment to favour backmost or frontmost layers going to plane.
    uint32_t levelWeighting = 0;
    if ( ly == 0 )
            levelWeighting = +1;
    else if ( ly == ( mDisplayInput.getNumLayers() - 1) )
            levelWeighting = +1;

    // Default the score assuming we can support this layer without needing pre-processing.
    eval.mScore = passThruScore + levelWeighting;

    bool bOK = isLayerSupportedOnPlane(pl, layer, planeCaps, options, formatCSCClass, bConsiderPreProcess);

    if ( bConsiderPreProcess )
    {
        bool bPermitPP = ( ( options.mPermittedPreProcessCSCMask & (1<<formatCSCClass) ) != 0 );
        if ( bPermitPP )
        {

            // Check that the plane can handle the preprocessed target
            Layer& ppLayer = eval.mTarget;
            const bool bOpaque = ( pl == 0 );

            eval.mComposition.mpTarget = &ppLayer;
            ppLayer.setBufferTilingFormat( TILE_X );
            ppLayer.setBlending( bOpaque ? EBlendMode::NONE : EBlendMode::PREMULT );
            ppLayer.setComposition( &eval.mComposition );

            // Establish pre-process composition target.
            DisplayCaps::ECSCClass formatClass = mDisplayCaps.halFormatToCSCClass( layer.getBufferFormat(), bOpaque );
            hwc_frect_t src = { 0, 0, (float)layer.getDstWidth(), (float)layer.getDstHeight() };
            hwc_rect_t dst = layer.getDst();
            ppLayer.setSrc( src );
            ppLayer.setDst( dst );
            ppLayer.setBufferFormat( mDisplayCaps.getPlaneCaps( pl ).getCSCFormat( formatClass ) );
            ppLayer.onUpdateFlags();

            // Validate that this layer is actually supported on the plane
            CachedOptions options( true, true, false );
            options.mPermittedPreProcessCSCMask = 0;
            bOK = isLayerSupportedOnPlane(pl, ppLayer, mDisplayCaps.getPlaneCaps( pl ), options, formatClass, bConsiderPreProcess);
            if (bOK)
            {
                // We can handle this layer but only via pre-processing.
                // Pass this requirement info out in flags.
                eval.mFlags |= Eval::FLAG_PREPROCESS;

                // Adjust the returned score to indicates that pre-processing will be used.
                if ( isVideo( planeCaps.getCSCFormat( formatCSCClass) ) )
                    eval.mScore = preProcessVideoScore + levelWeighting;
                else
                    eval.mScore = preProcessRGBScore + levelWeighting;
            }
            else
            {
                ALOGD_IF( 1, "%s No [Preprocessed target not supported] ", ppLayer.dump().string());
            }
        }
        else
        {
            ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : No [PreProcess not permitted]", planeCaps.getName(), layer.dump().string() );
            bOK = false;
        }
    }

    if (!bOK )
    {
        // Something went wrong during the allocation
        eval.mScore = 0;
        return false;
    }

    // Success
    ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s %s : Yes Score : %" PRIi64 " %s",
        planeCaps.getName(), layer.dump().string(), eval.mScore,
        (eval.mFlags & Eval::FLAG_PREPROCESS) ? " (PreProcess)" : "" );
    return true;
}

void PlaneAllocator::preEvaluate( PlaneAllocatorJB::Options* pOptions, bool bOptimizeIdleDisplay )
{
    const Content::LayerStack inputStack = mDisplayInput.getLayerStack();
    ALOG_ASSERT( mNumLayers == inputStack.size() );
    // TODO:
    //   Add more "smarts" here when setting scores.
    //   Currently we just set relative costs of using blanking v plane v RT.
    //   In reality, the cost should be calculated as a function of multiple factors:
    //     1/ Size of image source/destination
    //     2/ Buffer formats
    //     3/ Framerate
    //     4/ Cost of processing
    //     5/ Opportunity cost (using a slow idle engine to process a buffer may be better
    //          if it releases time on another fast engine that can be used for other purposes).
    for ( uint32_t ly = 0; ly < inputStack.size(); ++ly )
    {
        const Layer& layer = inputStack[ ly ];

        // Set index to the real layer index.
        maLayerConfig[ ly ].mIndex = ly;
        // Real layers are never optional.
        maLayerConfig[ ly ].mbOptional = false;
        // Real layers may be presenting protected buffers.
        // Note: We pre-check which planes can handle this layer via the call to isLayerSupportedOnPlane()
        // below. But if this layer is NOT handled with a dedicated plane then the allocator still needs
        // to know if it should limit the final collapsed layer set to an plane that supports decrypt.
        maLayerConfig[ ly ].mbEncrypted = layer.isEncrypted();

        // *****************************************************************************************************
        // Set up inputs for the optimizer: Per-layer composition constraints and scoring.
        // Set up the validity and scoring for the case of this layer being collapsed via a composer.
        // TODO:
        //  At some point we may call out to a notional 'composer' directly from the allocator
        //  to ask it to provide validity and scoring.
        // *****************************************************************************************************
        PlaneAllocator::Eval& unhandledEval = maLayerConfig[ ly ].mUnhandledEval;

        if ( !pOptions->mCollapse )
        {
            // All layers need their own plane.
            unhandledEval.mScore = PlaneAllocator::MIN_SCORE;
            unhandledEval.mbValid = false;
        }
        else if ( layer.isFrontBufferRendered() )
        {
            // Strongly prefer that we handle FBR layers using a dedicated plane.
            unhandledEval.mScore = PlaneAllocator::MIN_SCORE;
            unhandledEval.mbValid = true;
        }
        else if (layer.isEncrypted() || layer.isVideo())
        {
            // Strongly prefer that we handle ENCRYPTED and VIDEO layers using a dedicated plane.
            // Some composers (e.g. VPP) can support encrypted content.
            // So we now permit encrypted content to be unhandled.
            unhandledEval.mScore = PlaneAllocator::MIN_SCORE;
            unhandledEval.mbValid = true;
        }
        else
        {
            // Don't really want collapse/composition (favour plane).
            unhandledEval.mScore = -2;
            unhandledEval.mbValid = true;
        }

        // *****************************************************************************************************
        // Set up inputs for the optimizer: Per-layer dedicated plane constraints and scoring.
        // Set up the validity and scoring for the case of this layer being handled by each plane.
        // *****************************************************************************************************
        for ( uint32_t pl = 0; pl < mNumPlanes; ++pl )
        {
            Eval& handledEval = maLayerConfig[ ly ].mHandledEval[ pl ];
            CachedOptions options( pOptions->mOverlayYUV,
                                   pOptions->mOverlayRGB,
                                   bOptimizeIdleDisplay );
            if ( !unhandledEval.mbValid )
            {
                // When we have no valid fallback we *must* support the layer(s)
                // via plane(s) so we enable all options and remove all restrictions.
                // NOTE:
                //   This is usually to clone YUV sources or RGB compositions from other displays.
                //   Or, for front buffer rendered layers.
                if ( layer.isFrontBufferRendered() )
                {
                    // Disable PreProcess for FBR.
                    options.mPermittedPreProcessCSCMask = 0;
                }
                else
                {
                    options.mPermittedPreProcessCSCMask = ~0U;
                }
                options.mbOverlayYUV      = true;
                options.mbOverlayRGB      = true;
            }
            handledEval.mbValid = isLayerSupportedOnPlane( ly, pl, options, handledEval );
        }
    }
}

const PlaneAllocator::Solution* PlaneAllocator::findOptimalSolution( void )
{
    // We use uint32_t bit fields to track assigment - this limits us to no more than 32 planes.
    ALOG_ASSERT(MAX_PLANES <= 32);

    if ( maLayerConfig == NULL )
    {
        ALOGE( "Missing maLayerConfig!" );
        return NULL;
    }

    if ( mNumLayers == 0 )
    {
        ALOGE( "mNumLayers out of range [%u]", mNumLayers );
        return NULL;
    }

    if ( ( mNumPlanes == 0 ) || ( mNumPlanes > MAX_PLANES ) )
    {
        ALOGE( "mNumPlanes out of range [%u v %u]", mNumPlanes, MAX_PLANES );
        return NULL;
    }

    // Log inputs/pre-evaluation.
    if ( PLANEALLOC_SUMMARY_DEBUG || PLANEALLOC_OPT_DEBUG )
    {
        const Content::LayerStack& inputStack = mDisplayInput.getLayerStack();
        ALOGD( "PlaneAllocator::optimizeSolution %s\n--INPUT--", mDisplayCaps.getName() );
        // Dump evaluation status.
        for ( uint32_t ly = 0; ly < mNumLayers; ++ly )
        {
            LayerConfig& layerConfig = getLayerConfig( ly );
            const Layer& inputLayer = inputStack[ ly ];

            ALOGD("Layer %u %s, Optional==%s, Unhandled Supported==%s, Score==%" PRIi64,
                ly, inputLayer.dump().string(),
                layerConfig.mbOptional ? "yes" : "no",
                layerConfig.mUnhandledEval.mbValid ? "yes" : " no",
                layerConfig.mUnhandledEval.mScore );

            for ( uint32_t pl = 0; pl < mNumPlanes; ++pl )
            {
                ALOGD( "  Plane %02u : Supported==%s, Score==%" PRIi64 "%s",
                     pl,
                    layerConfig.mHandledEval[ pl ].mbValid ? "yes" : " no",
                    layerConfig.mHandledEval[ pl ].mScore,
                    layerConfig.mHandledEval[ pl ].mbValid &&
                     (layerConfig.mHandledEval[ pl ].mFlags & Eval::FLAG_PREPROCESS ) ? " (PreProcess)" : "" );
            }
        }
        for ( uint32_t pl = 0; pl < mNumPlanes; ++pl )
        {
            PlaneAllocator::CachedPlaneCaps& cpc = maCachedPlaneCaps[ pl ];
            ALOGD( "CachedPlaneCaps P%02u : Flags==%s|%s|%s|%s(0x%08x), ZOrderPreMask==0x%08x, ZOrderPostMask==0x%08x.",
                pl,
                cpc.mFlags & PlaneAllocator::CachedPlaneCaps::FLAG_CAP_COLLAPSE  ? "Coll" : "----",
                cpc.mFlags & PlaneAllocator::CachedPlaneCaps::FLAG_CAP_BLEND     ? "Blnd" : "----",
                cpc.mFlags & PlaneAllocator::CachedPlaneCaps::FLAG_CAP_DECRYPT   ? "Dcrp" : "----",
                cpc.mFlags & PlaneAllocator::CachedPlaneCaps::FLAG_HINT_REQUIRED ? "Reqd" : "----",
                cpc.mFlags,
                cpc.mSupportedZOrderPreMask,
                cpc.mSupportedZOrderPostMask );
        }
    }

    // Internal output work.
    uint32_t permutations = 0;
    int64_t bestScore = INT64_MIN;
    bool bValidSolution = false;
    uint32_t solutionIndex = 0;

    if ( !mSolution[0].init( mNumPlanes ) ||
         !mSolution[1].init( mNumPlanes ) )
    {
        ALOGE( "Failed to initialize allocator solution space" );
        return NULL;
    }

    const uint32_t lastLayer           = mNumLayers-1;

    // Layers can be:
    // Allocated to a specific dedicated plane,
    // Or unhandled (put into a collapsed layer),
    // Or disabled,
    // Else we have exhausted the options.
    const uint32_t unhandledPlaneIdx = mNumPlanes;
    const uint32_t disabledPlaneIdx  = mNumPlanes+1;
    const uint32_t exhaustedPlaneIdx = mNumPlanes+2;

    // Set up mask of planes that must be assigned.
    uint32_t maskRequiredPlanes = 0;
    for (  uint32_t pl = 0; pl < mNumPlanes; ++pl )
        if ( maCachedPlaneCaps[ pl ].mFlags & CachedPlaneCaps::FLAG_HINT_REQUIRED )
            maskRequiredPlanes |= (1<<pl);

    // Allocate mNumPlanes to mNumLayers.
    // Each plane can be allocated once, to any one or none of the layers.
    uint32_t maskAssigned = 0;     //< The set of assigned planes (bit0=>p0)
    uint32_t assigned = 0;      //< The count of assigned planes.
    uint32_t layer = 0;         //< The layer 'level'.
    uint32_t handledSets = 0;   //< Count of handled sets.
    uint32_t unhandledSets = 0; //< Count of unhandled sets.

    Scratch *scratch = new Scratch[ mNumLayers ];
    if( !scratch )
    {
        ALOGE("Failed to allocate memory for scratch!!!");
        return NULL;
    }
    Scratch* pSL = &scratch[layer];

    // This iterates all permutations of planes allocated to layers (no repeats).
    for (;;)
    {
        ALOGD_IF( PLANEALLOC_OPT_DEBUG, "layer: %u/%p, assignedPlane %u, nextPlane %u, runHandled %u, runUnhandled %u",
            layer, pSL, pSL->assignedPlane, pSL->nextPlane, pSL->runHandled, pSL->runUnhandled );
        ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  1/ maskAssigned 0x%08x, assigned %u, handledSets %u, unhandledSets %u",
            maskAssigned, assigned, handledSets, unhandledSets );

        // Release plane currently assigned (if any).
        // Decrement handled/unhandled count.
        if ( pSL->assignedPlane == unhandledPlaneIdx )
        {
            if ( ( pSL->runHandled ) && handledSets )
                --handledSets;
        }
        else if ( pSL->assignedPlane < unhandledPlaneIdx )
        {
            ALOGE_IF( assigned == 0, ( "assigned==0" ) );
            maskAssigned &= ~(1<<pSL->assignedPlane);
            --assigned;
            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Unassigned %u", pSL->assignedPlane );
            pSL->assignedPlane = unhandledPlaneIdx;
            if ( ( pSL->runUnhandled ) && unhandledSets )
                --unhandledSets;
        }
        // Set assignedPlane to invalid.
        pSL->assignedPlane = INVALID_PLANE;

        ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  2/ maskAssigned 0x%08x, assigned %u, handledSets %u, unhandledSets %u",
            maskAssigned, assigned, handledSets, unhandledSets );

        // Find next valid plane to give to this layer, or none.
        // (pl==numPlane => none).
        uint32_t pl = pSL->nextPlane;

        if ( pl < unhandledPlaneIdx )
        {
            // Early out if all planes are already assigned.
            if ( assigned >= mNumPlanes )
            {
                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  All %u assigned", assigned );
                pl = unhandledPlaneIdx;
            }
            else
            {
                while ( pl < mNumPlanes )
                {
                    // Is this plane valid for this layer?
                    // Is it free to use for this layer?
                    // Does this plane support ordering w.r.t. the planes preceding it?
                    if ( maLayerConfig[layer].mHandledEval[ pl ].mbValid
                     && ( !( maskAssigned & (1<<pl) ) )
                     && ( ( maCachedPlaneCaps[ pl ].mSupportedZOrderPreMask & maskAssigned ) == maskAssigned ) )
                    {
                        maskAssigned |= (1<<pl);
                        ++assigned;
                        ALOGE_IF( assigned > mNumPlanes, ( "assigned > mNumPlanes" ) );
                        break;
                    }
                    ++pl;
                }
            }
        }

        // Step over unhandledPlaneIdx if that is not valid.
        if (( pl == unhandledPlaneIdx ) && ( !maLayerConfig[layer].mUnhandledEval.mbValid ))
            pl = disabledPlaneIdx;

        // Step over disabledPlaneIdx if that is not valid.
        if (( pl == disabledPlaneIdx ) && ( !maLayerConfig[layer].mbOptional ))
            pl = exhaustedPlaneIdx;

        ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Advanced pl to %u", pl );

        if ( pl >= exhaustedPlaneIdx )
        {
            // Exhausted options for this layer.
            // Unwind.
            if ( layer )
            {
                if ( pSL->assignedPlane < mNumPlanes )
                    ALOGE( "Unexpected assigned == %u", pSL->assignedPlane );
                pSL->nextPlane = 0;
                --layer;
                pSL = &scratch[layer];
                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  <-- Exhausted options, unwound to layer %u/%p", layer, pSL );
            }
            else
            {
                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Exhausted all options, breaking" );
                break;
            }
        }
        else
        {
            pSL->assignedPlane = pl;
            pSL->nextPlane = pl+1;

            uint32_t runHandled = 0;
            uint32_t runUnhandled = 0;

            if ( pl == disabledPlaneIdx )
            {
                // Skip layer - propogate handled and unhandled counts.
                runUnhandled = pSL->runUnhandled;
                runHandled = pSL->runHandled;
            }
            else if ( pl == unhandledPlaneIdx )
            {
                // This  layer is unhandled.
                // Accumulate run count and check for handled run break.
                runUnhandled = pSL->runUnhandled + 1;
                if ( pSL->runHandled )
                    ++handledSets;
            }
            else
            {
                // This  layer is handled.
                // Accumulate run count and check for unhandled run break.
                runHandled = pSL->runHandled + 1;
                if ( pSL->runUnhandled )
                    ++unhandledSets;
            }

            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  AssignedPlane %u, nextPlane %u, runHandled %u, runUnhandled %u",
                pSL->assignedPlane, pSL->nextPlane, runHandled, runUnhandled );

            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  3/ maskAssigned 0x%08x, assigned %u, handledSets %u, unhandledSets %u",
                maskAssigned, assigned, handledSets, unhandledSets );

            uint32_t finalHandledSets = handledSets + ( runHandled ? 1: 0 );
            uint32_t finalUnhandledSets = unhandledSets + ( runUnhandled ? 1: 0 );
            uint32_t planesRequired = assigned + finalUnhandledSets;

            // ****************************************************************
            // Filter constraints.
            // ****************************************************************
            if (( finalHandledSets > mMaxHandledSets )
             || ( finalUnhandledSets > mMaxUnhandledSets ))
            {
                // Do not continue here:
                // This permutation already exceeds the simple constraints imposed for handled/unhandled sets.
                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Invalid: finalHandledSets %u, finalUnhandledSets %u : Terminate search (maxHandledSets %u, maxUnhandledSets %u)",
                    finalHandledSets, finalUnhandledSets, mMaxHandledSets, mMaxUnhandledSets );
            }
            else if ( planesRequired > mNumPlanes )
            {
                // Do not continue here:
                // total required planes == sum of assigned + unhandled sets
                // and this already exceeds available planes.
                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Invalid: planesRequired %u (assigned %u + finalUnhandledSets %u) > numPlanes %u",
                    planesRequired, assigned, finalUnhandledSets, mNumPlanes );
            }
            else if ( layer == lastLayer )
            {
                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "***************************************************." );
                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "Eval permutation %u.", permutations );

                bool bValid = true;
                int32_t firstUnhandledLayer = -1;
                uint32_t lastPlane = ~0U;
                uint32_t maskEffectiveAssigned = maskAssigned;
                uint32_t prePlanes = 0;

                for ( uint32_t ly = 0; bValid && (ly <= mNumLayers); ++ly )
                {
                    // If we have processed all layers, or, if the assigned plane changes then process the 'run of layers'.
                    // NOTE:
                    //  Array access limits are made explicit below to satisfy
                    //   Coverity code analysis Jira OAM-11711.
                    bool bProcessRun = false;
                    if ( ly >= mNumLayers )
                    {
                        bProcessRun = true;
                    }
                    else if ( ly < mNumLayers )
                    {
                        bProcessRun = ( scratch[ ly ].assignedPlane < unhandledPlaneIdx );
                    }

                    if ( bProcessRun )
                    {
                        if ( firstUnhandledLayer != -1 )
                        {
                            // Process end of unhandled run.

                            // First, look to see if we have any protected layers in our unhandled list.
                            // If ANY layer is protected then we anticipate a protected RT for the collapsed
                            // set and we must present that RT to an plane that supports decrypt.
                            bool bProtectedCollapsedLayer = false;
                            for ( uint32_t shly = firstUnhandledLayer; (shly < ly) && (shly < mNumLayers); ++shly )
                            {
                                if ( scratch[shly].assignedPlane != disabledPlaneIdx )
                                {
                                    if ( maLayerConfig[shly].mbEncrypted )
                                    {
                                        bProtectedCollapsedLayer = true;
                                        break;
                                    }
                                }
                            }

                            // TODO:
                            //   For exhaustive search we should iterate permutations of unassigned planes
                            //   to unhandled sets here... For now, just assign unused planes in low->high order.
                            //   If constraints are applied on plane zordering then this should be completed.
                            //   Likewise, if we have a mix of plane support for collapsed layers.
                            uint32_t freePlane = 0;
                            for ( freePlane = 0; freePlane < mNumPlanes; ++freePlane )
                            {
                                // Skip planes already assigned.
                                if ( maskEffectiveAssigned & ( 1<<freePlane ) )
                                {
                                    ALOGD_IF( PLANEALLOC_OPT_DEBUG,
                                        "  Plane %u already assigned",
                                        freePlane );
                                    continue;
                                }
                                // Skip planes that have no collapsed layer-set capabilities.
                                if ( !( maCachedPlaneCaps[freePlane].mFlags & CachedPlaneCaps::FLAG_CAP_COLLAPSE ) )
                                {
                                    ALOGD_IF( PLANEALLOC_OPT_DEBUG,
                                        "  Plane %u can not be used for collapsed layer sets [No collapse]",
                                        freePlane );
                                    continue;
                                }
                                // Skip planes that don't support blending if the layer-set is not back-most.
                                if ( !( maCachedPlaneCaps[freePlane].mFlags & CachedPlaneCaps::FLAG_CAP_BLEND )
                                   && ( maLayerConfig[ firstUnhandledLayer ].mIndex > 0 ) )
                                {
                                    ALOGD_IF( PLANEALLOC_OPT_DEBUG,
                                        "  Plane %u can not be used for upper collapsed layer sets [No blend]",
                                        freePlane );
                                   continue;
                                }
                                // Skip planes that don't support decrypt if the layer-set will be protected.
                                if ( bProtectedCollapsedLayer
                                   && !( maCachedPlaneCaps[freePlane].mFlags & CachedPlaneCaps::FLAG_CAP_DECRYPT ) )
                                {
                                    ALOGD_IF( PLANEALLOC_OPT_DEBUG,
                                        "  Plane %u can not be used for protected collapsed layer sets [No decrypt]",
                                        freePlane );
                                   continue;
                                }
                                // Finally, check permitted ZOrder.
                                // The planes that will be used after this one are the full set
                                // less those preceding it and less this plane itself.
                                uint32_t postPlanes = maskEffectiveAssigned & ~prePlanes;
                                postPlanes &= ~(1<<freePlane);
                                if ( ( ( maCachedPlaneCaps[freePlane].mSupportedZOrderPreMask & prePlanes ) == prePlanes )
                                  && ( ( maCachedPlaneCaps[freePlane].mSupportedZOrderPostMask & postPlanes ) == postPlanes ) )
                                    break;
                            }

                            if ( freePlane >= mNumPlanes )
                            {
                                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Invalid: Can't satisfy collapse of unhandled layers." );
                                bValid = false;
                            }
                            else
                            {
                                // Post shared plane index for unhandled run.
                                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Unhandled layers [%u - %u] require plane %u",
                                    firstUnhandledLayer, ly-1, freePlane );
                                for ( uint32_t shly = firstUnhandledLayer; (shly < ly) && (shly < mNumLayers); ++shly )
                                {
                                    if ( scratch[shly].assignedPlane != disabledPlaneIdx )
                                        scratch[shly].sharedPlane = freePlane;
                                }
                                maskEffectiveAssigned |= (1<<freePlane);
                                lastPlane = freePlane;
                                prePlanes |= (1<<lastPlane);
                            }
                            firstUnhandledLayer = -1;
                        }
                        if ( ly < mNumLayers )
                        {
                            // Process *this* layer.
                            ALOG_ASSERT( maskAssigned & (1<<scratch[ ly ].assignedPlane) );

                            lastPlane = scratch[ ly ].assignedPlane;
                            prePlanes |= (1<<lastPlane);
                        }
                    }
                    else if ( ly < mNumLayers )
                    {
                        if ( scratch[ ly ].assignedPlane != disabledPlaneIdx )
                        {
                            if ( firstUnhandledLayer == -1 )
                                firstUnhandledLayer = ly;
                        }
                    }
                }

                // Check required planes are all used.
                if ( maskRequiredPlanes & ~maskEffectiveAssigned )
                {
                    ALOGD_IF( PLANEALLOC_OPT_DEBUG,
                        "  Invalid: maskRequiredPlanes 0x%08x v maskEffectiveAssigned 0x%08x",
                        maskRequiredPlanes, maskEffectiveAssigned );
                    bValid = false;
                }

                if ( bValid )
                {
                    // ****************************************************************
                    // Score arrangement.
                    // ****************************************************************
                    int64_t totalScore = 0;

                    for ( uint32_t ly = 0; ly < mNumLayers; ++ly )
                    {
                        if ( scratch[ ly ].assignedPlane == disabledPlaneIdx )
                        {
                            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Layer %2u : Disabled      score:n/a", ly );
                        }
                        else if ( scratch[ ly ].assignedPlane == unhandledPlaneIdx )
                        {
                            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Layer %2u : Collapsed P%2u score:%" PRIi64,
                                ly,
                                scratch[ ly ].sharedPlane,
                                maLayerConfig[ ly ].mUnhandledEval.mScore );
                            AccumulateScore( totalScore, maLayerConfig[ ly ].mUnhandledEval.mScore );
                        }
                        else
                        {
                            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  Layer %2u : Dedicated P%2u score:%" PRIi64,
                                ly,
                                scratch[ ly ].assignedPlane,
                                maLayerConfig[ ly ].mHandledEval[scratch[ ly ].assignedPlane].mScore );
                            AccumulateScore( totalScore, maLayerConfig[ ly ].mHandledEval[scratch[ ly ].assignedPlane].mScore );
                        }
                    }

                    ALOGD_IF( PLANEALLOC_OPT_DEBUG, "finalHandledSets   : %u", finalHandledSets );
                    ALOGD_IF( PLANEALLOC_OPT_DEBUG, "finalUnhandledSets : %u", finalUnhandledSets );
                    ALOGD_IF( PLANEALLOC_OPT_DEBUG, "totalScore         : %" PRIi64, totalScore );

                    if ( !bValidSolution || ( totalScore > bestScore ) )
                    {
                        // This result beats our best so far.
                        ALOGD_IF( PLANEALLOC_OPT_DEBUG, "Better %" PRIi64, totalScore );

                        // Alternate solutions to each of mSolution[].
                        // We only replace the current best result if this new result passes validateConstraints()
                        const uint32_t si = solutionIndex^1;
                        Solution& solution = mSolution[si];

                        // Reset new proposed output.
                        solution.reset();

                        // Construct pchZOrderStr as a string with each plane given name 'A','B',... etc.
                        // The final string represents plane ZOrder, e.g.:
                        //  "A", "ABC", "BA", "CAB", "C",
                        //  etc.
                        char zOrderStr[ mNumPlanes+1 ];
                        memset( zOrderStr, 0, mNumPlanes+1 );
                        uint32_t zorder = 0;

                        for ( uint32_t ly = 0; ly < mNumLayers; ++ly )
                        {
                            if ( scratch[ ly ].assignedPlane == disabledPlaneIdx )
                                continue;
                            if ( scratch[ ly ].assignedPlane == unhandledPlaneIdx )
                            {
                                const uint32_t pl = scratch[ ly ].sharedPlane;
                                Solution::Plane& plane = solution.maPlanes[ pl ];
                                if ( plane.mbUsed )
                                {
                                    ALOG_ASSERT( plane.mbCollapsed );
                                    ALOG_ASSERT( plane.mFirst < ly );
                                    plane.mLast = ly;
                                }
                                else
                                {
                                    plane.mFirst = ly;
                                    plane.mLast = ly;
                                    plane.mbUsed = true;
                                    plane.mbCollapsed = true;
                                    ++solution.mCompositions;
                                    zOrderStr[ zorder++ ] = 'A' + pl;
                                }
                            }
                            else
                            {
                                const uint32_t pl = scratch[ ly ].assignedPlane;
                                Solution::Plane& plane = solution.maPlanes[ pl ];
                                ALOG_ASSERT( !plane.mbUsed );
                                plane.mFirst = ly;
                                plane.mLast = ly;
                                plane.mbUsed = true;
                                if ( maLayerConfig[ ly ].mHandledEval[ pl ].mFlags & Eval::FLAG_PREPROCESS )
                                {
                                    plane.mbPreProcess = true;
                                    plane.mTarget = maLayerConfig[ ly ].mHandledEval[ pl ].mTarget;
                                    ++solution.mCompositions;
                                }
                                zOrderStr[ zorder++ ] = 'A' + pl;
                            }
                        }
                        // Establish correct formats for collapse/pre-process compositions.
                        bool bPlanesValid = true;
                        for ( uint32_t pl = 0; pl < mNumPlanes; ++pl )
                        {
                            Solution::Plane& plane = solution.maPlanes[ pl ];
                            if ( !plane.mbUsed )
                                continue;
                            // Backmost layer needs to be made opaque.
                            const bool bOpaque = ( plane.mFirst == 0 );

                            // Set up a a layer describing the collapsed layer and ensure its valid to flip to the display
                            if (plane.mbCollapsed)
                            {
                                Layer& layer = plane.mTarget;
                                plane.mComposition.mpTarget = &layer;
                                layer.setBufferTilingFormat( TILE_X );
                                layer.setBlending( bOpaque ? EBlendMode::NONE : EBlendMode::PREMULT );
                                layer.setPlaneAlpha(1.0f);
                                layer.setComposition( &plane.mComposition );

                                // Establish collapsed composition target
                                DisplayCaps::ECSCClass formatClass = mDisplayCaps.halFormatToCSCClass( mDisplayInput.getFormat(), bOpaque );
                                hwc_frect_t src = { 0, 0, (float)mDisplayInput.getWidth(), (float)mDisplayInput.getHeight() };
                                hwc_rect_t dst = { 0, 0, (int32_t)mDisplayInput.getWidth(), (int32_t)mDisplayInput.getHeight() };
                                layer.setSrc( src );
                                layer.setDst( dst );
                                layer.setBufferFormat( mDisplayCaps.getPlaneCaps( pl ).getCSCFormat( formatClass ) );
                                layer.onUpdateFlags();

                                // Validate that this layer is actually supported on the plane
                                CachedOptions options( true, true, false );
                                options.mPermittedPreProcessCSCMask = 0;
                                bool bConsiderPreProcess = false;
                                bPlanesValid = isLayerSupportedOnPlane(pl, layer, mDisplayCaps.getPlaneCaps( pl ), options, formatClass, bConsiderPreProcess);
                                if (!bPlanesValid)
                                {
                                    ALOGD_IF( PLANEALLOC_CAPS_DEBUG, "%s No [Collapsed target invalid] ", layer.dump().string());
                                    break;
                                }
                            }
                        }
                        if (bPlanesValid)
                        {
                            // Find best ZOrder given caps.
                            solution.mZOrderStr = zOrderStr;
                            solution.mZOrder = findBestZOrder( zOrderStr );

                            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "Proposed solution:\n%s", solution.dump().string() );

                            // Finally, check this proposed output is valid.
                            // Individual layers<->planes are already checked and confirmed possible.
                            // However, it is still possible that specific plane state or combinations
                            // of state can *NOT* be supported. For this reason, we must make a final
                            // check with the final proposed arrangement.
                            if ( validateSolution( solution ) )
                            {
                                // Replace best result.
                                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "New best %" PRIi64 "->%" PRIi64, bestScore, totalScore );
                                bestScore = totalScore;
                                bValidSolution = true;
                                solutionIndex = si;
                            }
                            else
                            {
                                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "Did not satisfy complex constraints" );
                            }
                        }
                        else
                        {
                            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "No valid output planes" );
                        }
                    }
                    else
                    {
                        ALOGD_IF( PLANEALLOC_OPT_DEBUG, "No change (%" PRIi64 " v %" PRIi64 ")", bestScore, totalScore );
                    }
                    ++permutations;
                }

                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "***************************************************." );
            }
            else
            {
                // Iterate to the next layer.
                ++layer;
                pSL = &scratch[layer];
                // Propogate the run info.
                pSL->runHandled = runHandled;
                pSL->runUnhandled = runUnhandled;
                ALOGD_IF( PLANEALLOC_OPT_DEBUG, "  --> Recurse to layer %u/%p, runHandled %u, runUnhandled %u",
                    layer, pSL, pSL->runHandled, pSL->runUnhandled );
            }
        }
    }

    ALOGD_IF( PLANEALLOC_OPT_DEBUG, "Done [permutations:%u valid solution:%u score %" PRIi64"].",
        permutations, bValidSolution, bestScore );

    delete[] scratch;
    if ( bValidSolution )
    {
        ALOGD_IF( PLANEALLOC_SUMMARY_DEBUG,
            "PlaneAllocator::optimizeSolution %s Success\n--SOLUTION--\n%s",
            mDisplayCaps.getName(), mSolution[ solutionIndex ].dump().string() );
        return &mSolution[ solutionIndex ];
    }
    return NULL;
}

uint32_t PlaneAllocator::findBestZOrder( const char *pchZOrderStr )
{
    // Get ZOrder LUT.
    const DisplayCaps::ZOrderLUTEntry* pZOrderLUT = mDisplayCaps.getZOrderLUT( );
    const uint32_t numZOrders = mDisplayCaps.getNumZOrders( );

    if ( ( pZOrderLUT == NULL ) || !numZOrders )
    {
        ALOGD_IF( PLANEALLOC_SUMMARY_DEBUG || PLANEALLOC_CAPS_DEBUG || PLANEALLOC_OPT_DEBUG,
            "findBestZOrder Disabled ZOrder" );
        return 0;
    }

    // Prepare final ZOrder given the new state.
    bool bFoundMatching = false;
    uint32_t matchingZOrderIndex = 0;

    // If the primary is not used but the main plane disable feature is not enabled
    // then the main plane must remain at the back.
    const bool bDisabledPrimaryMustBeBackmost = ( strchr( pchZOrderStr, 'A' ) == NULL );

    // Search the ZOrder LUT for a matching sequence.
    uint32_t le = 0;
    while ( le < numZOrders )
    {
        // Since some planes can be disabled, it is *NOT* sufficient to just strcmp entries in the LUT.
        // e.g. Consider that "B" can use entry with ZOrderStr "ABCD" (where ACD are all disabled).
        // Instead, we must check the sequencing of enabled planes.
        const char* pchZ1 = pchZOrderStr;
        const char* pchZ2 = pZOrderLUT[ le ].getHWCZOrder( );
        bool bSameSequence = true;
        while ( *pchZ1 != '\0' )
        {
            // Match each char from pchZ1 in pchZ2.
            // Each must follow in pchZ2.
            pchZ2 = strchr( pchZ2, *pchZ1 );
            if ( !pchZ2 )
            {
                bSameSequence = false;
                break;
            }
            ++pchZ1;
        };

        // Filter out ZOrders if disabled primary must be backmost.
        if (( bSameSequence )
            && bDisabledPrimaryMustBeBackmost
            && ( pZOrderLUT[ le ].getHWCZOrder( )[0] != 'A' ) )
        {
            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "Refusing ZOrder because unused primary must be backmost [%s v %s]",
                        pZOrderLUT[ le ].getHWCZOrder( ), pchZOrderStr );
            bSameSequence = false;
        }

        // KMD may make assumptions based on ZOrder without checking the enabled status.
        // (e.g. whether to apply blending). This checks first used plane is first in matched.
        if (( bSameSequence )
            && ( !bDisabledPrimaryMustBeBackmost )
            && ( pZOrderLUT[ le ].getHWCZOrder( )[0] != pchZOrderStr[0] ))
        {
            ALOGD_IF( PLANEALLOC_OPT_DEBUG, "Refusing ZOrder because first enabled plane is not at front [%s v %s]",
                        pZOrderLUT[ le ].getHWCZOrder( ), pchZOrderStr );
            bSameSequence = false;
        }

        if ( bSameSequence )
        {

            matchingZOrderIndex = le;
            bFoundMatching = true;
            break;
        }
        ++le;
    }

    if ( le >= numZOrders )
    {
        ALOGE( "%s Failed to match ZOrder %s", __FUNCTION__, pchZOrderStr );
    }
    else if ( bFoundMatching )
    {
        ALOGD_IF( PLANEALLOC_OPT_DEBUG, "Matched ZOrder %s for %s",
            pZOrderLUT[ matchingZOrderIndex ].getHWCZOrder( ), pchZOrderStr );
    }

    // If we did not find a matching ZOrder then default to entry 0.
    uint32_t zOrder = bFoundMatching ? matchingZOrderIndex : 0;

    ALOG_ASSERT( zOrder < numZOrders );
    ALOGD_IF( PLANEALLOC_CAPS_DEBUG || PLANEALLOC_OPT_DEBUG,
        "findBestZOrder %u/%s [Drm index %u/%s]",
        zOrder, pZOrderLUT[ zOrder ].getHWCZOrder( ),
        pZOrderLUT[ zOrder ].getDisplayEnum( ),
        pZOrderLUT[ zOrder ].getDisplayString( ) );

    return pZOrderLUT[ zOrder ].getDisplayEnum( );
}

bool PlaneAllocator::validateSolution( const Solution& solution )
{
    if ( !mDisplayCaps.hasComplexConstraints() )
    {
        // Nothing more to validate.
        return true;
    }

    // Update generic state.
    mDisplayOutput.updateDisplayState( mDisplayInput );

    // Access stack.
    const Content::LayerStack& inputStack = mDisplayInput.getLayerStack();

    // And generate output stack.
    Content::LayerStack& outputStack = mDisplayOutput.editLayerStack();
    outputStack.resize( mNumPlanes );

    // Set up each output layer.
    for ( uint32_t pl = 0; pl < mNumPlanes; ++pl )
    {
        const Solution::Plane& plane = solution.maPlanes[ pl ];
        if ( plane.mbUsed )
        {
            if ( plane.mbCollapsed || plane.mbPreProcess)
            {
                outputStack.setLayer( pl, &plane.mTarget );
            }
            else
            {
                // Pass through.
                outputStack.setLayer( pl, &inputStack.getLayer( plane.mFirst ) );
            }
        }
        else
        {
            // Not used.
            outputStack.setLayer( pl, &hwc::Layer::Empty() );
        }
    }
    // Update layer stack flags.
    outputStack.updateLayerFlags();

    // Make final check against the caps.
    bool bIsSupported = mDisplayCaps.isSupported( mDisplayOutput, solution.mZOrder );

    ALOGD_IF( PLANEALLOC_OPT_DEBUG, "validateSolution isSupported? : %d : %s", bIsSupported, mDisplayOutput.dump().string());
    return bIsSupported;
}

PlaneAllocatorJB::PlaneAllocatorJB( bool bOptimizeIdleDisplay ) :
    mbOptimizeIdleDisplay( bOptimizeIdleDisplay )
{
    // Lazy allocation lookup of composition options on first access of the allocator.
    // Keep this in a static for rapid access
    if (spOptions == NULL)
        spOptions = new Options;
    ALOG_ASSERT(spOptions);
}

PlaneAllocatorJB::~PlaneAllocatorJB()
{
}

bool PlaneAllocatorJB::analyze( const Content::Display& display, const DisplayCaps& caps, PlaneComposition& composition )
{
    ALOGD_IF( PLANEALLOC_SUMMARY_DEBUG || PLANEALLOC_CAPS_DEBUG,
            "PlaneAllocator analyze %s : x%u layers into x%u planes ******************",
            caps.getName(), display.getNumLayers(), caps.getNumPlanes() );

    PlaneAllocator allocator( display, caps );

    // Enable all planes unless overlays are disabled.
    // Limit unhandled sets to 2 (this is the maximum number of collapsed sets of layers).
    uint32_t enabledPlanes = ( spOptions->mOverlay == 0 ) ? 1 : 0;
    if ( !allocator.init( enabledPlanes, 2 ) )
    {
        ALOGE( "Failed to initialize allocator input space" );
        return false;
    }

    // Pre-evaluate capabilites/weightings.
    allocator.preEvaluate( spOptions, mbOptimizeIdleDisplay );

    // Run optimizer
    const PlaneAllocator::Solution* pSolution = allocator.findOptimalSolution( );
    if ( pSolution == NULL )
    {
        ALOGE( "PlaneAllocator::optimizeSolution %s Failed\n%s", caps.getName(), display.getLayerStack().dump().string() );
        return false;
    }

    // Process solution.
    for ( uint32_t pl = 0; pl < pSolution->mNumPlanes; ++pl )
    {
        const PlaneAllocator::Solution::Plane& plane = pSolution->maPlanes[ pl ];
        if ( !plane.mbUsed )
        {
            continue;
        }
        if ( plane.mbCollapsed )
        {
            // TODO
            //   Use source/dest rects from allocator solution.
            if ( !composition.addFullScreenComposition( caps, pl, plane.mFirst, plane.mLast - plane.mFirst + 1, plane.mTarget.getBufferFormat() ) )
            {
                ALOGE( "Failed addFullScreenComposition for P%u", pl );
                return false;
            }
        }
        else if ( plane.mbPreProcess )
        {
            // TODO
            //   Use source/dest rects from allocator solution.
            if ( !composition.addSourcePreprocess( caps, pl, plane.mFirst, plane.mTarget.getBufferFormat() ) )
            {
                ALOGE( "Failed addSourcePreprocess for P%u", pl );
                return false;
            }
        }
        else
        {
            if ( !composition.addDedicatedLayer( pl, plane.mFirst ) )
            {
                ALOGE( "Failed addDedicatedLayer for P%u", pl );
                return false;
            }
        }
    }

    // Set Zorder;
    composition.setZOrder( pSolution->mZOrder );

    ALOGD_IF( PLANEALLOC_SUMMARY_DEBUG || PLANEALLOC_CAPS_DEBUG,
        "******************************************************************************" );

    return true;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
