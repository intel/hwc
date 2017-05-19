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
#include "DrmNuclearPageFlipHandler.h"
#include "Drm.h"
#include "DrmDisplay.h"
#include "DisplayCaps.h"
#include "Utils.h"
#include "Log.h"

#if VPG_DRM_HAVE_ATOMIC_NUCLEAR

#define DRM_PFH_NAME "DrmNuclearPageFlip"

namespace intel {
namespace ufo {
namespace hwc {


Option DrmNuclearPageFlipHandler::sOptionNuclearDrrs("nucleardrrs", 0, false);


// helper class to construct the properties object to send to drmAtomic
class DrmNuclearHelper::Properties
{
public:
    Properties() : mNumObjs(0), mNumProps(0), mObjProps(0) {}
    ~Properties() {}

    // Helper to make the add code visually much simpler. An error should
    // be reported during enum if the property isnt valid, not here.
    void addIfValid(uint32_t id, uint64_t value)
    {
        if (id != Drm::INVALID_PROPERTY)
            add(id, value);
    }

    void add(uint32_t id, uint64_t value)
    {
        ALOG_ASSERT(mNumProps < MAX_PROPERTIES);
        mProps[mNumProps] = id;
        mValues[mNumProps] = value;
        mNumProps++;
        mObjProps++;
    }

    void addObject(uint32_t obj)
    {
        if (mObjProps)
        {
            ALOG_ASSERT(mNumObjs < MAX_OBJS);
            mObjs[mNumObjs] = obj;
            mPropCounts[mNumObjs] = mObjProps;
            mNumObjs++;
            mObjProps = 0;
        }
    }

    uint32_t          getNumObjs() const        { return mNumObjs; }
    const uint32_t*   getObjs() const           { return mObjs; }
    const uint32_t*   getPropCounts() const     { return mPropCounts; }
    const uint32_t*   getProps() const          { return mProps; }
    const uint64_t*   getValues() const         { return mValues; }



private:
    static const int MAX_OBJS = 6;
    static const int MAX_PROPERTIES = MAX_OBJS * 15;

    uint32_t    mObjs[MAX_OBJS];
    uint32_t    mPropCounts[MAX_OBJS];
    uint32_t    mProps[MAX_PROPERTIES];
    uint64_t    mValues[MAX_PROPERTIES];

    uint32_t    mNumObjs;
    uint32_t    mNumProps;
    uint32_t    mObjProps;
};

uint32_t DrmNuclearHelper::getPropertyIDIfValid(const char *name)
{
    // Query first plane whether gets this property: if not, disable it.
    const DrmDisplayCaps& drmCaps = mDisplay.getDrmDisplayCaps( );
    const DrmDisplayCaps::PlaneCaps& planeCaps = drmCaps.getPlaneCaps( 0 );
    int plane_id = planeCaps.getDrmID( );
    uint32_t propertyID = mDrm.getPlanePropertyID( plane_id, name );

    if( propertyID == Drm::INVALID_PROPERTY )
    {
        return Drm::INVALID_PROPERTY;
    }

    // Query other planes whether support: if not, disable it.
    const uint32_t planeNum = mDisplay.getDisplayCaps( ).getNumPlanes( );
    for( uint32_t p = 1; p < planeNum; p++ )
    {
        const DrmDisplayCaps::PlaneCaps& otherPlaneCaps = drmCaps.getPlaneCaps( p );
        plane_id = otherPlaneCaps.getDrmID( );
        uint32_t prop_id = mDrm.getPlanePropertyID( plane_id, name );

        if( prop_id != propertyID )
        {
            propertyID = Drm::INVALID_PROPERTY;
            break;
        }
    }

    return propertyID;
}

DrmNuclearHelper::DrmNuclearHelper( DrmDisplay& display ) :
    mDisplay( display ),
    mDrm( Drm::get() )
{
    const DrmDisplayCaps& drmCaps = mDisplay.getDrmDisplayCaps( );

    // The property IDs are common across all planes, so just query the first plane.
    const DrmDisplayCaps::PlaneCaps& planeCaps = drmCaps.getPlaneCaps( 0 );
    int plane_id = planeCaps.getDrmID();

    // These are required and expected of all kernels.
    mPropCrtc = mDrm.getPlanePropertyID(plane_id, "CRTC_ID");
    mPropFb   = mDrm.getPlanePropertyID(plane_id, "FB_ID");
    mPropDstX = mDrm.getPlanePropertyID(plane_id, "CRTC_X");
    mPropDstY = mDrm.getPlanePropertyID(plane_id, "CRTC_Y");
    mPropDstW = mDrm.getPlanePropertyID(plane_id, "CRTC_W");
    mPropDstH = mDrm.getPlanePropertyID(plane_id, "CRTC_H");
    mPropSrcX = mDrm.getPlanePropertyID(plane_id, "SRC_X");
    mPropSrcY = mDrm.getPlanePropertyID(plane_id, "SRC_Y");
    mPropSrcW = mDrm.getPlanePropertyID(plane_id, "SRC_W");
    mPropSrcH = mDrm.getPlanePropertyID(plane_id, "SRC_H");

    ALOG_ASSERT(mPropFb   != Drm::INVALID_PROPERTY);
    ALOG_ASSERT(mPropDstX != Drm::INVALID_PROPERTY);
    ALOG_ASSERT(mPropDstY != Drm::INVALID_PROPERTY);
    ALOG_ASSERT(mPropDstW != Drm::INVALID_PROPERTY);
    ALOG_ASSERT(mPropDstH != Drm::INVALID_PROPERTY);
    ALOG_ASSERT(mPropSrcX != Drm::INVALID_PROPERTY);
    ALOG_ASSERT(mPropSrcY != Drm::INVALID_PROPERTY);
    ALOG_ASSERT(mPropSrcW != Drm::INVALID_PROPERTY);
    ALOG_ASSERT(mPropSrcH != Drm::INVALID_PROPERTY);

    // Optional properties. TODO: update the caps if these arnt present
    // getPlaneProperty should report not found to logcat
    mPropRot        = getPropertyIDIfValid("rotation");
    mPropEnc        = getPropertyIDIfValid("RRB2");
    mPropRC         = getPropertyIDIfValid("render compression");
    mProcBlendFunc  = getPropertyIDIfValid("blend_func");
    mProcBlendColor = getPropertyIDIfValid("blend_color");
    mPropCrtcMode = mDrm.getPlanePropertyID(plane_id, "MODE_ID");
    mPropCrtcActive = mDrm.getPlanePropertyID(plane_id, "ACTIVE");
}

uint32_t DrmNuclearHelper::getBlendFunc(const Layer& layer)
{
    EBlendMode blending = layer.getBlending();
    uint32_t blendFunc = 0;
    bool     bPlaneAlpha = layer.isPlaneAlpha();

    // Blend func and color - kernel/bxt/include/drm/drm_crtc.h
    switch(blending)
    {
         case EBlendMode::NONE:
             // No blend, ignore plane alpha.
             {
                 blendFunc = DRM_BLEND_FUNC(ONE, ZERO);
             }
             break;
         case EBlendMode::PREMULT:
             if( bPlaneAlpha )
             {
                 blendFunc = DRM_BLEND_FUNC(CONSTANT_ALPHA, ONE_MINUS_CONSTANT_ALPHA_TIMES_SRC_ALPHA);
             }
             else
             {
                 blendFunc = DRM_BLEND_FUNC(ONE, ONE_MINUS_SRC_ALPHA);
             }
             break;
         case EBlendMode::COVERAGE:
             if( bPlaneAlpha )
             {
                 blendFunc = DRM_BLEND_FUNC(CONSTANT_ALPHA_TIMES_SRC_ALPHA, ONE_MINUS_CONSTANT_ALPHA_TIMES_SRC_ALPHA);
             }
             else
             {
                 blendFunc = DRM_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
             }
             break;
    }
    return blendFunc;
}

uint64_t DrmNuclearHelper::getBlendColor(const Layer& layer)
{
    uint64_t blendColor = 0;

    // Bit 63:56 - alpha
    blendColor = ((uint64_t)(layer.getPlaneAlpha()*255)) << (48 + 8);
    // TODO: bit 55:0 not used in KMD so far.

    return blendColor;
}

void DrmNuclearHelper::updatePlane(const Layer* pLayer, Properties& props, uint32_t drmPlaneId)
{
    if (!pLayer)
    {
        // Disable any planes without layers
        props.add( mPropCrtc, 0 );
        props.add( mPropFb, 0 );
        props.addObject(drmPlaneId);
        return;
    }
    const Layer& layer = *pLayer;
    props.add       ( mPropCrtc, mDisplay.getDrmCrtcID());
    props.add       ( mPropFb,   layer.getBufferDeviceId() );
    props.add       ( mPropDstX, layer.getDstX() );
    props.add       ( mPropDstY, layer.getDstY() );
    props.add       ( mPropDstW, layer.getDstWidth() );
    props.add       ( mPropDstH, layer.getDstHeight() );
    props.add       ( mPropSrcX, floatToFixed16(layer.getSrcX()) );
    props.add       ( mPropSrcY, floatToFixed16(layer.getSrcY()) );
    props.add       ( mPropSrcW, floatToFixed16(layer.getSrcWidth()) );
    props.add       ( mPropSrcH, floatToFixed16(layer.getSrcHeight()) );
    props.addIfValid( mPropRot,  Drm::hwcTransformToDrm(layer.getTransform()));
    props.addIfValid( mPropEnc,  layer.isEncrypted() ? 1 : 0);
    props.addIfValid( mPropRC, (layer.getBufferCompression() == COMPRESSION_ARCH_START) ? 1 : 0);
    props.addIfValid( mProcBlendFunc, getBlendFunc(layer));
    props.addIfValid( mProcBlendColor, getBlendColor(layer));
    props.addObject(drmPlaneId);
}

void DrmNuclearHelper::updateMode(bool active, uint32_t drmModeId, Properties& props)
{
    props.add( mPropCrtcMode, active ? drmModeId : 0 );
    props.add( mPropCrtcActive, active ? 1 : 0 );
    props.addObject( mDisplay.getDrmCrtcID() );
    props.add( mPropCrtc, active ? mDisplay.getDrmCrtcID() : 0 );
    props.addObject( mDisplay.getDrmConnectorID() );
}

int DrmNuclearHelper::drmAtomic(uint32_t flags, const Properties& props, uint32_t user_data)
{
    ATRACE_CALL_IF(DRM_CALL_TRACE);

    struct drm_mode_atomic atomic;
    memset(&atomic, 0, sizeof(atomic));

    atomic.flags            = flags;
    atomic.count_objs       = props.getNumObjs();
    atomic.objs_ptr         = uintptr_t (props.getObjs() );
    atomic.count_props_ptr  = uintptr_t (props.getPropCounts() );
    atomic.props_ptr        = uintptr_t (props.getProps() );
    atomic.prop_values_ptr  = uintptr_t (props.getValues() );
    atomic.user_data        = user_data;

    Log::alogd( DRM_STATE_DEBUG, "drmAtomic\n%s", dump(props).string());
    int ret = drmIoctl(mDrm, DRM_IOCTL_MODE_ATOMIC, &atomic);
    Log::aloge( ret != Drm::SUCCESS, "Failed drmAtomic ret=%d\n%s", ret, dump(props).string());

    return ret;
}

const char* drmBlendFactorToString(uint64_t factor)
{
    switch (factor)
    {
        case DRM_BLEND_FACTOR_AUTO:                                     return "Auto";
        case DRM_BLEND_FACTOR_ZERO:                                     return "0";
        case DRM_BLEND_FACTOR_ONE:                                      return "1";
        case DRM_BLEND_FACTOR_SRC_ALPHA:                                return "A";
        case DRM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:                      return "1-A";
        case DRM_BLEND_FACTOR_CONSTANT_ALPHA:                           return "Pa";
        case DRM_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:                 return "1-Pa";
        case DRM_BLEND_FACTOR_CONSTANT_ALPHA_TIMES_SRC_ALPHA:           return "A*Pa";
        case DRM_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA_TIMES_SRC_ALPHA: return "1-A*Pa";
    }
    return "UNKNOWN";
};

String8 blendFuncToString(uint64_t func)
{
    return String8::format("(%s,%s)", drmBlendFactorToString((func >> 16) & 0xffff),
                                      drmBlendFactorToString(func & 0xffff));
}

String8 DrmNuclearHelper::dump(const Properties& props) const
{
    String8 output;
    const uint32_t* pObjs         = props.getObjs();
    const uint32_t* pPropsCounts  = props.getPropCounts();
    const uint32_t* pProps        = props.getProps();
    const uint64_t* pValues       = props.getValues();

    for (uint32_t o = 0; o < props.getNumObjs(); o++)
    {
        String8 str;
        for (uint32_t p = 0; p < pPropsCounts[o]; p++)
        {
            if      ((mPropCrtc       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropCrtc))        { str.appendFormat("CRTC:%-2d "      , int32_t(pValues[p])); }
            else if ((mPropFb         != Drm::INVALID_PROPERTY) && (pProps[p] == mPropFb  ))        { str.appendFormat("FB:%-3d "        , int32_t(pValues[p])); }
            else if ((mPropDstX       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropDstX))        { str.appendFormat("DX:%-5u"         , uint32_t(pValues[p])); }
            else if ((mPropDstY       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropDstY))        { str.appendFormat("DY:%-5u"         , uint32_t(pValues[p])); }
            else if ((mPropDstW       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropDstW))        { str.appendFormat("DW:%-5u"         , uint32_t(pValues[p])); }
            else if ((mPropDstH       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropDstH))        { str.appendFormat("DH:%-5u"         , uint32_t(pValues[p])); }
            else if ((mPropSrcX       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropSrcX))        { str.appendFormat("SX:%-7.1f"       , fixed16ToFloat(pValues[p])); }
            else if ((mPropSrcY       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropSrcY))        { str.appendFormat("SY:%-7.1f"       , fixed16ToFloat(pValues[p])); }
            else if ((mPropSrcW       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropSrcW))        { str.appendFormat("SW:%-7.1f"       , fixed16ToFloat(pValues[p])); }
            else if ((mPropSrcH       != Drm::INVALID_PROPERTY) && (pProps[p] == mPropSrcH))        { str.appendFormat("SH:%-7.1f"       , fixed16ToFloat(pValues[p])); }
            else if ((mPropRot        != Drm::INVALID_PROPERTY) && (pProps[p] == mPropRot ))        { str.appendFormat("Rot:%-2u"        , uint32_t(pValues[p])); }
            else if ((mPropEnc        != Drm::INVALID_PROPERTY) && (pProps[p] == mPropEnc ))        { str.appendFormat("%s "             , uint32_t(pValues[p]) ? "Enc" : "Clr"); }
            else if ((mPropRC         != Drm::INVALID_PROPERTY) && (pProps[p] == mPropRC  ))        { str.appendFormat("Rc:%d "          , uint32_t(pValues[p])); }
            else if ((mProcBlendFunc  != Drm::INVALID_PROPERTY) && (pProps[p] == mProcBlendFunc  )) { str.appendFormat("BF:%s "          , blendFuncToString(pValues[p]).string()); }
            else if ((mProcBlendColor != Drm::INVALID_PROPERTY) && (pProps[p] == mProcBlendColor )) { str.appendFormat("BC:%x "          , uint32_t(pValues[p] >> 56)); }
            else if ((mPropCrtcMode   != Drm::INVALID_PROPERTY) && (pProps[p] == mPropCrtcMode ))   { str.appendFormat("MODE BLOB ID:%" PRIx64 " ", pValues[p]); }
            else if ((mPropCrtcActive != Drm::INVALID_PROPERTY) && (pProps[p] == mPropCrtcActive )) { str.appendFormat("ACTIVE:%" PRIx64 " "      , pValues[p]); }
            else                                                                                    { str.appendFormat("UNKNOWN:%" PRIx64 " "     , pValues[p]); }
        }
        output.appendFormat("%s:%-2d %s\n", mDisplay.getName(), pObjs[o], str.string());
        pProps += pPropsCounts[o];
        pValues += pPropsCounts[o];
    }
    return output;
}

int DrmNuclearHelper::setCrtcNuclear( const drmModeModeInfoPtr pModeInfo, const Layer* pLayer )
{
    if (pLayer && !pLayer->isBufferDeviceIdValid())
    {
        Log::aloge(true, "DrmNuclearPageFlipHandler: Invalid fb during mode set: %s", pLayer->dump().string());
        return BAD_VALUE;
    }

    uint32_t modeId = 0;
    sp<Drm::Blob> pModeBlob;
    uint32_t active = false;
    if (pModeInfo)
    {
        pModeBlob = mDrm.createBlob( pModeInfo, sizeof(drmModeModeInfo) );
        if (pModeBlob == NULL)
        {
            Log::aloge(true, "DrmNuclearPageFlipHandler: Failed to create mode blob");
            return BAD_VALUE;
        }
        modeId = pModeBlob->getID();
        active = true;
    }

    DrmNuclearHelper::Properties props;
    updateMode(active, modeId, props);

    // We will reset all layers here regardless.
    // If a blanking layer is specified then we will set it.
    const DrmDisplayCaps& drmCaps = mDisplay.getDrmDisplayCaps( );

    updatePlane(pLayer, props, drmCaps.getPlaneCaps( 0 ).getDrmID());

    for ( uint32_t p = 1; p < mDisplay.getDisplayCaps( ).getNumPlanes( ); ++p )
    {
        updatePlane(NULL, props, drmCaps.getPlaneCaps( p ).getDrmID());
    }

    return drmAtomic(DRM_MODE_ATOMIC_ALLOW_MODESET, props, 0);
}


// *****************************************************************************
// DrmNuclearPageFlipHandler
// *****************************************************************************

DrmNuclearPageFlipHandler::DrmNuclearPageFlipHandler( DrmDisplay& display ) :
    mDisplay( display ),
    mDrm( Drm::get() )
{
}

DrmNuclearPageFlipHandler::~DrmNuclearPageFlipHandler( )
{
}

bool DrmNuclearPageFlipHandler::test( DrmDisplay& )
{
    bool ret = Drm::get().useNuclear();
    ALOGI("DRM/KMS Nuclear is %s", ret ? "available" : "unavailable");
    return ret;
}

bool DrmNuclearPageFlipHandler::doFlip( DisplayQueue::Frame* pNewFrame, bool /* bMainBlanked */, uint32_t flipEvData )
{
    DrmNuclearHelper::Properties props;

    // *************************************************************************
    // Panel fitter processing.
    // *************************************************************************
    // TODO: Panel Fitter enable.

    // *************************************************************************
    // Plane processing.
    // *************************************************************************
    const DrmDisplayCaps& drmCaps = mDisplay.getDrmDisplayCaps( );

    // Pointers to the start of the current object
    for ( uint32_t p = 0; p < mDisplay.getDisplayCaps( ).getNumPlanes( ); ++p )
    {
        // Get layer.
        const Layer* pLayer = NULL;
        if ( p < pNewFrame->getLayerCount( ) )
        {
            pLayer = &pNewFrame->getLayer( p )->getLayer( );
        }

        if (pLayer && !pLayer->isBufferDeviceIdValid())
        {
            Log::aloge(true, "DrmNuclearPageFlipHandler: Invalid fb during flip: %s", pLayer->dump().string());
            pLayer = NULL;
        }
        mDisplay.mpNuclearHelper->updatePlane( pLayer, props, drmCaps.getPlaneCaps( p ).getDrmID() );
    }

    uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT; /* TODO, this needs enabling | DRM_MODE_PAGE_FLIP_ASYNC */

    drmModeModeInfo seamlesModeInfo;
    sp<Drm::Blob> pModeBlob;
    if (sOptionNuclearDrrs && mDisplay.getSeamlessMode(seamlesModeInfo))
    {
        pModeBlob = mDrm.createBlob( &seamlesModeInfo, sizeof(seamlesModeInfo) );
        if (pModeBlob != NULL)
        {
            mDisplay.mpNuclearHelper->updateMode(true, pModeBlob->getID(), props);
            flags = flags | DRM_MODE_ATOMIC_ALLOW_MODESET;
        }
    }

    int ret = mDisplay.mpNuclearHelper->drmAtomic(flags, props, flipEvData);

    if (ret == Drm::SUCCESS)
    {
        if (pModeBlob != NULL)
        {
            mDisplay.applySeamlessMode(seamlesModeInfo);
        }
        else if (!sOptionNuclearDrrs)
        {
            // Falback to legacy drrs if we have to
            // We need a 'main' plane for this api so pick the first.
            const Layer* pLayer = NULL;
            for ( uint32_t p = 0;
                (p < mDisplay.getDisplayCaps( ).getNumPlanes( )) &&
                (p < pNewFrame->getLayerCount( )) &&
                !pLayer;
                 ++p )
            {
                pLayer = &pNewFrame->getLayer( p )->getLayer( );
            }
            if (pLayer)
            {
                mDisplay.legacySeamlessAdaptMode( pLayer );
            }
        }
    }

    return ret == Drm::SUCCESS;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif
