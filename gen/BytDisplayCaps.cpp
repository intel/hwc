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
#include "BytDisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

BytDisplayCaps::BytDisplayCaps(uint32_t pipe, bool bCherrytrail) :
    mPipe(pipe),
    mbCherrytrail(bCherrytrail)
{
}

void BytDisplayCaps::probe()
{
    ALOGI_IF( sbLogViewerBuild, "DisplayCaps construct Baytrail class caps for display pipe %d", mPipe);

    // TODO:
    // Add correct CAP_OPAQUE_CONTROL support.
    // NOTE:
    //  We don't strictly support CAP_OPAQUE_CONTROL yet since we ALWAYS blend on VLV
    //  and do not force blending off when required. If we remove CAP_OPAQUE
    //  then we do not get the NavigationBar going to overlay because it
    //  is unblended but has an alpha channel.


    if ( getNumPlanes() > 0 )
    {
        // First plane is the main plane
        PlaneCaps& caps = editPlaneCaps( 0 );

        // Enable main capabilities.
        caps.setBlendingMasks( static_cast<uint32_t>(EBlendMode::PREMULT) );
        caps.enablePlaneAlpha( false );
        caps.enableDisable( );
        caps.setMaxSourcePitch( 16*1024 );
    }


    if ( getNumPlanes() > 1 )
    {
        // Subsequent planes are sprite planes
        for ( uint32_t s = 1; s < getNumPlanes(); ++s )
        {
            PlaneCaps& caps = editPlaneCaps( s );

            // Enable sprite capabilities.
            caps.setBlendingMasks( static_cast<uint32_t>(EBlendMode::PREMULT) );
            caps.enablePlaneAlpha( false );
            caps.enableDisable( );
            caps.enableDecrypt( );
            caps.enableWindowing( );
            caps.enableSourceOffset( );
            caps.enableSourceCrop( );
            caps.setMaxSourcePitch( 16*1024 );

            // Set transforms to NONE/ROT180.
            static const ETransform transforms[] =
            {
                ETransform::NONE,
                ETransform::ROT_180
            };
            caps.setTransforms( DISPLAY_CAPS_COUNT_OF( transforms ), transforms );
        }
    }

#if VPG_DRM_HAVE_ZORDER_API
    // Specify the ZOrder LUT.
    switch( mPipe )
    {
#if defined(DRM_ZORDER_WITH_ID)
        // If the kernel supports specifying the crtc_id explicitly
        // then we can use the same set of DrmEnums for all pipes.
        default:
#endif
        case 0:
                                          // ZOrderStr       DrmEnum         DrmStr
            mZOrderLUT.push( ZOrderLUTEntry( "ABCD"        , PASASBCA      , "PASASBCA" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "BACD"        , SAPASBCA      , "SAPASBCA" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "ACBD"        , PASBSACA      , "PASBSACA" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "BCAD"        , SASBPACA      , "SASBPACA" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "CABD"        , SBPASACA      , "SBPASACA" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "CBAD"        , SBSAPACA      , "SBSAPACA" ) );
            break;
#if !defined(DRM_ZORDER_WITH_ID)
        case 1:
                                          // ZOrderStr       DrmEnum         DrmStr
            mZOrderLUT.push( ZOrderLUTEntry( "ABCD"        , PBSCSDCB      , "PBSCSDCB" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "BACD"        , SCPBSDCB      , "SCPBSDCB" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "ACBD"        , PBSDSCCB      , "PBSDSCCB" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "BCAD"        , SCSDPBCB      , "SCSDPBCB" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "CABD"        , SDPBSCCB      , "SDPBSCCB" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "CBAD"        , SDSCPBCB      , "SDSCPBCB" ) );
            break;
#ifdef PCSESFCC
        case 2:
                                          // ZOrderStr       DrmEnum         DrmStr
            mZOrderLUT.push( ZOrderLUTEntry( "ABCD"        , PCSESFCC      , "PCSESFCC" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "BACD"        , SEPCSFCC      , "SEPCSFCC" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "ACBD"        , PCSFSECC      , "PCSFSECC" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "BCAD"        , SESFPCCC      , "SESFPCCC" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "CABD"        , SFPCSECC      , "SFPCSECC" ) );
            mZOrderLUT.push( ZOrderLUTEntry( "CBAD"        , SFSEPCCC      , "SFSEPCCC" ) );
            break;
#endif
        default:
            ALOGE( "ZOrder LUT missing for pipe=%d", mPipe);
            break;
#endif
    }
#endif

    // Note, this needs to be called after adding planes
    updateZOrderMasks();

    // Global scalng caps for BYT platorm
    GlobalScalingCaps& globalScalingCaps = editGlobalScalingCaps();
    globalScalingCaps.setMinScale( 0.875f );         // 12.5% limit for downscaling.
    globalScalingCaps.setMaxScale( 0.0f );           // No limit for upscaling.
    globalScalingCaps.setMinSourceWidth( 0.0f );     // No minimimum source size requirement.
    globalScalingCaps.setMinSourceHeight( 0.0f );    // No minimimum source size requirement.
    globalScalingCaps.setMaxSourceWidth( 2048.0f );  // 2K limit in each axis (independent).
    globalScalingCaps.setMaxSourceHeight( 2048.0f ); // 2K limit in each axis (independent).

    // Global scaling is supportted from the BYT HW perspective, so enable it here.
    // if there are some limitations in the DRM/ADF driver, it can be overwritten in DrmDisplayCaps/AdfDisplayCaps.
    uint32_t globalScalingFlags = globalScalingCaps.getFlags( );
    globalScalingFlags |=   DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_SUPPORTED
                          | DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_OVERSCAN;
    if ( mbCherrytrail )
    {
        // Cherrytrail supports pillar/letter.
        // These are disabled for Baytrail.
        globalScalingFlags |= DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_PILLARBOX
                            | DisplayCaps::GlobalScalingCaps::GLOBAL_SCALING_CAP_LETTERBOX;
    }
    globalScalingCaps.setFlags( globalScalingFlags);
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
