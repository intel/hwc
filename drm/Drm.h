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

#ifndef INTEL_UFO_HWC_DRM_H
#define INTEL_UFO_HWC_DRM_H

#include "Hwc.h"

#include "Singleton.h"
#include "Option.h"
#include <utils/RefBase.h>
#include <utils/Vector.h>
#include <xf86drmMode.h>    //< For structs and types.
#include <i915_drm.h>       //< For DRM_PRIMARY_DISABLE (if available)

// Declare the atomic API locally for now.
#include "DrmSetDisplay.h"

namespace intel {
namespace ufo {
namespace hwc {

// Extended DRM features:

// There are some nonstandard differences between the 3.10 BYT kernel and all
// future kernels. The presence of this define is an indicator that we
// are building against the libdrm that matches the 3.10 kernel
// Its not defined in any future libdrm.
#if defined(DRM_I915_GET_PSR_SUPPORT)
#define INTEL_HWC_ANDROID_BYT_3_10 1
#endif

// Can we build with Atomic set display API support?
// This can be enabled unconditionally on platforms for which
// backend driver support has been enabled and
// exposed using option "setdisplay"
#if defined(DRM_IOCTL_MODE_SETDISPLAY)
#define VPG_DRM_HAVE_ATOMIC_SETDISPLAY              1
#else
#define VPG_DRM_HAVE_ATOMIC_SETDISPLAY              0
#endif

// Can we build with i915 Nuclear Atomic API support?
// This can be enabled unconditionally on platforms for which
// backend driver support has been enabled and
// exposed using option "nuclear"
#if defined(DRM_IOCTL_MODE_ATOMIC)
#define VPG_DRM_HAVE_ATOMIC_NUCLEAR                 1
#else
#define VPG_DRM_HAVE_ATOMIC_NUCLEAR                 0
#endif

// Can we build with main plane disable?
// This must be enabled unconditionally on platforms for which
// backend driver support has been enabled.
#if defined(DRM_PRIMARY_DISABLE)
#define VPG_DRM_HAVE_MAIN_PLANE_DISABLE             (DRM_PRIMARY_DISABLE)
#else
#define VPG_DRM_HAVE_MAIN_PLANE_DISABLE             0
#endif

// Can we build with async DPMS feature?
// This must be enabled unconditionally on platforms for which
// backend driver support has been enabled.
#if ( defined(DRM_MODE_DPMS_ASYNC_ON) && defined(DRM_MODE_DPMS_ASYNC_OFF) )
#define VPG_DRM_HAVE_ASYNC_DPMS                     1
#else
#define VPG_DRM_HAVE_ASYNC_DPMS                     0
#endif

// Can we build with dynamic/variable ZOrder switching?
// This can be enabled unconditionally on platforms for which
// backend driver support has been enabled and
// exposed using option "OVERLZYZORDER"
#define VPG_DRM_HAVE_ZORDER_API (defined( PASASBCA ))

// Can we build with screen control API?
// Its only supported on BYT 3.10 builds, even though the define exists in future libdrm includes
#if defined(DRM_IOCTL_I915_DISP_SCREEN_CONTROL) && defined(INTEL_HWC_ANDROID_BYT_3_10)
#define VPG_DRM_HAVE_SCREEN_CTL                     1
#else
#define VPG_DRM_HAVE_SCREEN_CTL                     0
#endif

// Can we build with panel fitter control?
// This can be enabled unconditionally on platforms for which
// backend driver support has been enabled.
// NOTES:
// DRM_AUTOSCALE is assumed as a minimum feature.
//   This implies aspect preserving scaling to fit display with letterbox
//   or pillarbox if required.
// VPG_DRM_HAVE_PANEL_FITTER_SOURCE_SIZE
//   This can be enabled when pipe source size programming is supported.
//   This allows specification of source size independently of plane/buffers,
//   which means HWC can support multiple plane/buffers.
// VPG_DRM_HAVE_PANEL_FITTER_MANUAL
//   This can be enabled when full arbitrary destination windowing is supported.
//   (dstX,dstY,dstW,dstH)
#if defined(DRM_PFIT_PROP) && defined(DRM_SCALING_SRC_SIZE_PROP)
#define VPG_DRM_HAVE_PANEL_FITTER                   1
#define VPG_DRM_HAVE_PANEL_FITTER_SOURCE_SIZE       1
#define VPG_DRM_HAVE_PANEL_FITTER_MANUAL            (defined(DRM_PFIT_MANUAL))
#else
#define VPG_DRM_HAVE_PANEL_FITTER                   0
#define VPG_DRM_HAVE_PANEL_FITTER_SOURCE_SIZE       0
#define VPG_DRM_HAVE_PANEL_FITTER_MANUAL            0
#endif

// Does this platform implement powermanagement itself.
// This can be enabled unconditionally on platforms for which
// a PM service/KMD support has been enabled.
#if defined(INTEL_HWC_ANDROID_BYT_3_10)
#define VPG_DRM_HAVE_POWERMANAGER                   1
#else
#define VPG_DRM_HAVE_POWERMANAGER                   0
#endif

// Does DRM support PSR?
#if defined(DRM_I915_GET_PSR_SUPPORT)
#define VPG_DRM_HAVE_PSR                            1
#else
#define VPG_DRM_HAVE_PSR                            0
#endif

// Can we build with 180rotation support?
#if defined(DRM_IOCTL_I915_SET_PLANE_180_ROTATION)
#define VPG_DRM_HAVE_TRANSFORM_180                  1
#else
#define VPG_DRM_HAVE_TRANSFORM_180                  0
#endif

// Frame count for panel-self-refresh identical frames.
#define VPG_DRM_PSR_IDENTICAL_FRAME_COUNT           5

// Default buffer formats.
// These define the Gralloc and DRM formats used for blanking buffer and initial modeset.
#define DEFAULT_DISPLAY_GRALLOCFORMAT               HAL_PIXEL_FORMAT_RGBA_8888
#define DEFAULT_DISPLAY_DRMFORMAT                   DRM_FORMAT_XBGR8888


class DrmEventThread;
class DrmDisplay;
class DrmUEventThread;

/**
 */
class Drm : public Singleton<Drm>
{
public:
    static const int SUCCESS = 0;
    static const uint32_t INVALID_PROPERTY = 0xFFFFFFFF;

     // Display drm events.
    enum class UEvent
    {
        UNRECOGNISED,           // The event is not recognised.

        // Display hotplug events
        HOTPLUG_CHANGED,        // A display has been plugged or unplugged.
        HOTPLUG_CONNECTED,      // A display has been plugged.
        HOTPLUG_DISCONNECTED,   // A display has been unplugged.
        HOTPLUG_RECONNECT,      // A connected display requires reconnection (e.g. due to mode or monitor change).
        HOTPLUG_IMMINENT,       // A notification from the kernel that a hotplug will be coming soon.

        // ESD event.
        ESD_RECOVERY,           // ESD request to recovery.
    };
    static Drm& get() { return getInstance(); }

    // Initialise the Drm subsystem
    void init(Hwc& hwc);

    // Ensure all display (caps) are aware of changes to the number of active hardware displays.
    // Returns true if any component acknowledges the event.
    bool broadcastNumActiveDisplays( void );

    // Set display as active or inactive.
    // The change will be broadcast using broadcastNumActiveDisplays().
    // Returns true if the caller should synchronize the change.
    bool setActiveDisplay( uint32_t drmDisplay, bool bActive );

    // Get count of active displays.
    uint32_t getNumActiveDisplays( void ) { return mActiveDisplays; }

    // *****************************************************************
    // Hotplug and Modes
    // *****************************************************************

    // By default, probed displays and connectivity changes will be registerd with HWC/SF.
    // Call disableHwcRegistration( ) to disable registration.
    void disableHwcRegistration( void );

    // Probe a connector's current crtc mode/state.
    // This is used by the hotplug handler to check that a DrmDisplay's mode is still valid (has not changed).
    int probeMode( uint32_t connectorIndex, drmModeCrtc& crtc );

    // Function to probe for available displays and register them with the HWC.
    int probe(Hwc& hwc);

    // Function to probe for disconnected displays from the available displays and make them NULL.
    int unplug();

    // process hot plug event
    void onHotPlugEvent(UEvent eEvent );

    // process ESD event
    void onESDEvent( UEvent eEvent, uint32_t connectorID, uint32_t connectorType );

    // Enable vsync generation for the specified display device.
    bool enableVSync(DrmDisplay* pDisp);

    // Disable vsync generation for the specified display device.
    // Pass bWait = true to ensure vsyncs are quiescent before returning.
    bool disableVSync(DrmDisplay* pDisp, bool bWait);

    // *****************************************************************
    // Enumeration methods.
    // *****************************************************************

    // Set display mode.
    int setCrtc( uint32_t crtc_id, uint32_t fb, uint32_t x, uint32_t y,
                 uint32_t* connector_id, uint32_t count, drmModeModeInfoPtr modeInfo );

    // Get display mode.
    // Returns NULL on failure.
    drmModeCrtcPtr getCrtc( uint32_t crtc_id );

    // Free display mode.
    void freeCrtc( drmModeCrtcPtr ptr );

    // Get modes.
    // Returns NULL on failure.
    drmModeResPtr getResources( void );

    // Free resources.
    void freeResources( drmModeResPtr ptr );

    // Get encoder.
    // Returns NULL on failure.
    drmModeEncoderPtr getEncoder( uint32_t encoder_id );

    // Free encoder.
    void freeEncoder( drmModeEncoderPtr ptr );

    // Get connector.
    // Returns NULL on failure.
    drmModeConnectorPtr getConnector( uint32_t connectorId );

    // Free connector.
    void freeConnector( drmModeConnectorPtr ptr );

    // Get plane resource.
    // Returns NULL on failure.
    drmModePlaneResPtr getPlaneResources( void );

    // Free plane resources.
    void freePlaneResources( drmModePlaneResPtr ptr );

    // Get plane.
    // Returns NULL on failure.
    drmModePlanePtr getPlane( uint32_t plane_id );

    // Free plane.
    void freePlane( drmModePlanePtr ptr );

    // Get the capability levels
    int getCap(uint64_t capability, uint64_t& value);

    // Set the client capability levels
    int setClientCap(uint64_t capability, uint64_t value);

    // Get the panel fitter property id.
    // Returns INVALID_PROPERTY if not available.
    uint32_t getPanelFitterPropertyID( uint32_t connector_id );

    // Get the panel fitter source size property id.
    // Returns INVALID_PROPERTY if not available.
    uint32_t getPanelFitterSourceSizePropertyID( uint32_t connector_id );

    // Get the DPMS property id.
    // Returns INVALID_PROPERTY if not available.
    uint32_t getDPMSPropertyID( uint32_t connector_id );

    // Get the DRRS property id.
    // Returns INVALID_PROPERTY if not available.
    uint32_t getDRRSPropertyID( uint32_t connector_id );

    // Get the property id for an arbitrary property by string name.
    // Returns INVALID_PROPERTY if not available.
    uint32_t getPropertyID( uint32_t obj_id, uint32_t obj_type, const char* pchPropName );
    uint32_t getConnectorPropertyID( uint32_t connector_id, const char* pchPropName );
    uint32_t getPlanePropertyID( uint32_t plane_id, const char* pchPropName );

    // Acquire a panel fitter for exclusive use by this connector.
    // Returns 0 (SUCCESS) if succesful.
    int acquirePanelFitter( uint32_t connector_id );

    // Release a panel fitter previously acquired with acquirePanelFitter.
    // Returns 0 (SUCCESS) if succesful.
    int releasePanelFitter( uint32_t connector_id );

    // Returns true if this connector has acquired a panel fitter.
    bool isPanelFitterAcquired( uint32_t connector_id );

    // Set the panel fitter property.
    // The panel fitter must have been acquired prior to setting it.
    // mode is one of:
    //   DRM_PFIT_OFF
    //   DRM_AUTOSCALE
    //   DRM_PILLARBOX
    //   DRM_LETTERBOX
    //   DRM_PFIT_MANUAL        -- Requires VPG_DRM_HAVE_PANEL_FITTER_MANUAL.
    // If mode is DRM_PFIT_MANUAL then dstX/Y/W/H must be provided.
    // The dstX/Y/W/H define the output size and location within the display.
    // Returns 0 (SUCCESS) if succesful.
    int setPanelFitterProperty( uint32_t connector_id, int32_t pfit_prop_id, uint32_t mode,
                                int32_t  dstX = 0, int32_t  dstY = 0,
                                uint32_t dstW = 0, uint32_t dstH = 0 );

    // Set the panel fitter source size.
    // The srcW/H define the notional resolution (pipe source size).
    // The dstX/Y/W/H define the output size and location within the display.
    // The panel fitter must have been acquired prior to setting it.
    // Returns 0 (SUCCESS) if succesful.
    int setPanelFitterSourceSizeProperty( uint32_t connector_id, int32_t pfit_prop_id,
                                          uint32_t srcW, uint32_t srcH );

    // Get the value of the specified property.
    // Sets *pValue to the property's value;
    // pValue must be provided.
    // Returns (0) SUCCESS if successful.
    int getProperty( uint32_t obj_id, uint32_t obj_type, int32_t prop_id, uint64_t *pValue );
    int getConnectorProperty( uint32_t connector_id, int32_t prop_id, uint64_t *pValue );
    int getPlaneProperty( uint32_t plane_id, int32_t prop_id, uint64_t *pValue );

    // Get the value of the specified property.
    // Sets *pValue to the property's value;
    // pValue must be provided.
    // Returns (0) SUCCESS if successful.
    int setProperty( uint32_t obj_id, uint32_t obj_type, int32_t prop_id, uint64_t value );
    int setConnectorProperty( uint32_t connector_id, int32_t prop_id, uint64_t value );
    int setPlaneProperty( uint32_t plane_id, int32_t prop_id, uint64_t value );

    // Set the DPMS mode property.
    // mode is one of:
    //   DRM_MODE_DPMS_ON        0
    //   DRM_MODE_DPMS_STANDBY   1
    //   DRM_MODE_DPMS_SUSPEND   2
    //   DRM_MODE_DPMS_OFF       3
    //   DRM_MODE_DPMS_ASYNC_ON  4
    //   DRM_MODE_DPMS_ASYNC_OFF 5
    // The ASYNC modes require VPG_DRM_HAVE_ASYNC_DPMS.
    // Use getDPMSProperty to poll for current mode.
    // Returns 0 (SUCCESS) if successful.
    // Returns <0 on error.
    int setDPMSProperty( uint32_t connector_id, int32_t dpms_prop_id, uint32_t mode );

    // Get the DPMS mode property.
    // Returns mode if successful. See setDPMSProperty for modes
    // Returns <0 on error.
    int getDPMSProperty( uint32_t connector_id, int32_t dpms_prop_id );

    // Get the DPMS capability property.
    // value is one of:
    //   DRRS_NOT_SUPPORTED       0
    //   STATIC_DRRS_SUPPORT      1
    //   SEAMLESS_DRRS_SUPPORT    2
    //   SEAMLESS_DRRS_SUPPORT_SW 3
    // Returns value if successful.
    // Returns <0 on error.
    int getDRRSProperty( uint32_t connector_id, int32_t prop_id );

    // Return whether this panel supports Psr
    bool getPsrSupport();


    ETilingFormat getTilingFormat(uint32_t bo);

    // *****************************************************************
    // Dynamic state methods
    // *****************************************************************

    // Enable or disable decryption for specific crtc or plane.
    // Use DRM_MODE_OBJECT_CRTC or DRM_MODE_OBJECT_PLANE for objectType.
    // Returns 0 (SUCCESS) if successful.
    int setDecrypt( uint32_t objectType, uint32_t id, bool bEnable );

    // Move the cursor position.
    // Returns 0 (SUCCESS) if successful.
    int moveCursor( uint32_t crtc_id, int x, int y);

    // Set the cursor image and size.
    // Returns 0 (SUCCESS) if successful.
    int setCursor( uint32_t crtc_id, unsigned bo, uint32_t w, uint32_t h );

    // Set the ZOrder.
    // Returns 0 (SUCCESS) if successful.
    int setZOrder( uint32_t crtc_id, uint32_t zorder );

    // Set/flip an auxiliary plane.
    // Returns 0 (SUCCESS) if successful.
    int setPlane( uint32_t plane_id, uint32_t crtc_id, uint32_t fb_id, uint32_t flags,
                  uint32_t crtc_x, uint32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h,
                  uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h,
                  void *user_data );

    // Flip the main plane.
    // Returns 0 (SUCCESS) if successful.
    int pageFlip( uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void* user_data );

    // Set screen on/off.
    // Returns 0 (SUCCESS) if successful.
    int screenCtl( uint32_t crtc_id, uint32_t enable );

    // Wait for a buffer to become available.
    // If timeoutNs is 0 then this is a polling test.
    // Returns 0 (SUCCESS) if successful.
    int waitBufferObject( uint32_t boHandle, uint64_t timeoutNs );

    // Open a buffer by prime.
    // Returns 0 (SUCCESS) if successful
    // If successful, pHandle will be updated with the buffer object handle,
    // else it will be reset to 0.
    // Use closeBuffer() to close the buffer.
    int openPrimeBuffer( int primeFd, uint32_t* pHandle );


    // Close a buffer previously opened using openBufferByPrime().
    // Returns 0 (SUCCESS) if successful
    int closeBuffer( uint32_t handle );

    // Prime a dma_buf object from the specified buffer object.
    // dma_buf objects are required when using the ADF API to present frames.
    // Returns 0 (SUCCESS) if successful
    // If successful, pDmaBuf will be updated with the dma_buf object handle,
    // else it will be reset to -1.
    // Use close( ) to close the dma_buf.
    int registerBoAsDmaBuf( uint32_t boHandle, int* pDmaBuf );

    // Add a framebuffer given the specified buffer object.
    // Framebuffers are required when using the DRM API to present frames.
    // Returns 0 (SUCCESS) if successful
    // If successful, pFb will be updated with the fb handle,
    // else it will be reset to 0.
    // Use drmRemoveFb to release the fb object.
    int addFb( uint32_t width, uint32_t height, uint32_t fbFormat, uint32_t boHandle, uint32_t pitch, uint32_t uvPitch, uint32_t uvOffset, uint32_t* pFb, uint32_t auxPitch = 0, uint32_t auxOffset = 0 );


    // Remove a framebuffer previously created with drmAddFb().
    // Returns 0 (SUCCESS) if successful
    int removeFb( uint32_t fb );

    // Returns true for connector types that are supported as internal displays.
    bool isSupportedInternalConnectorType( uint32_t connectorType ) const;

    // Returns true for connector types that are supported as external displays.
    bool isSupportedExternalConnectorType( uint32_t connectorType ) const;

    // Returns the device ID of the chip
    uint32_t getDeviceID( void );

    // Wrapper for Property blobs.
    class Blob : public RefBase {
        public:
            static sp<Blob> create( Drm& drm, const void* pData, uint32_t size );
            ~Blob();
            uint32_t getID() { return mID; }
        private:
            Blob( Drm& drm, uint32_t blobID ) : mDrm(drm), mID(blobID) {}
            Drm& mDrm;
            uint32_t mID;
    };

    // Create proprerty blob.
    sp<Blob> createBlob( const void* pData, uint32_t size );

    // Set transform state for the next crtc or plane update.
    // Transform is Android HWC transform type.
    // Use DRM_MODE_OBJECT_CRTC or DRM_MODE_OBJECT_PLANE for objectType.
    // Returns 0 (SUCCESS) if successful.
    int setTransform( uint32_t objectType, uint32_t id, ETransform transform );

    // Acquire an available pipe.
    // Given a mask of possible crtcs (pipes) find the first unused one
    // and return its index along with the crtc ID.
    bool acquirePipe(uint32_t possible_crtcs, uint32_t& crtc_id, uint32_t& pipe_idx);

    // Release a pipe
    void releasePipe(uint32_t pipe_idx);


#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY
    // *****************************************************************
    // Atomic API
    // *****************************************************************

    // Set all state in one atomic call to Drm.
    int drmSetDisplay( struct drm_mode_set_display& display );
#endif

    // *****************************************************************
    // Accessor methods
    // *****************************************************************

    operator int() const { return mDrmFd; }
    int                 getDrmHandle()              { return mDrmFd; }
    DrmDisplay*         getDrmDisplay(uint32_t i)   { return i < cMaxSupportedPhysicalDisplays ? mDisplay[i] : NULL; }

    // *****************************************************************
    // Static methods
    // *****************************************************************

    // Get object type has human-readable string.
    static const char* getObjectTypeString( uint32_t objType );

    // Get panel fitter property mode as human-readable string.
    static const char* getPanelFitterModeString( uint32_t mode );

    // Get DPMS property mode as human-readable string.
    static const char* getDPMSModeString( int32_t mode );

    // Get string form of UEvent.
    static const char* UEventToString( UEvent eUE );

    // Get human-readable string for specific zorder.
    static const char* zOrderToString( uint32_t zorder );

    // Convert a connector type to its human-readable string equivalent.
    static const char* connectorTypeToString( uint32_t connectorType );

    // Convert a human-readable string equivalent to a connector type.
    static uint32_t stringToConnectorType( const char * connectorString );

    // Get human-readable string for a drm mode info block.
    static String8 modeInfoToString( const drmModeModeInfo& m );

    // Returns true if two modes are the same.
    static bool modeInfoCompare( const drmModeModeInfo& a, const drmModeModeInfo& b );

    // Get fb format as string.
    static String8 fbFormatToString( uint32_t fbFormat )
    {
        String8 str;
        str = String8::format( "%c%c%c%c",
            (char)(fbFormat & 0xFF),
            (char)((fbFormat>>8) & 0xFF),
            (char)((fbFormat>>16) & 0xFF),
            (char)((fbFormat>>24) & 0xFF) );
        return str;
    }

    static uint32_t hwcTransformToDrm(ETransform transform);

    // Get info about kernel capabilities to use
    bool useUniversalPlanes()                       { return mbCapUniversalPlanes; }
    bool useNuclear()                               { return mbCapNuclear; }
    bool useRenderCompression()                     { return mbCapRenderCompression; }

#if VPG_DRM_HAVE_ATOMIC_SETDISPLAY
    // Return complete display state as string (pipe + all planes).
    static String8 drmDisplayToString( const struct drm_mode_set_display& display );
    // Return display pipe state as string.
    static String8 drmDisplayPipeToString( const struct drm_mode_set_display& display );
    // Return one or all planes state as string.
    static String8 drmDisplayPlaneToString( const struct drm_mode_set_display& display, int32_t plane = -1 );
#endif

private:


    friend class Singleton<Drm>;
    Drm();
    ~Drm();

    Option                          mOptionPanel;                       // Enumerate Panels
    Option                          mOptionExternal;                    // Enumerate External devices
    Option                          mOptionDisplayInternal;             // Internal display override
    Option                          mOptionDisplayExternal;             // External display override

    Hwc*                            mpHwc;                              // Established on probe().

    int                             mDrmFd;                             // Gralloc's Drm file descriptor (DRM_MASTER priv)
    DrmDisplay*                     mDisplay[cMaxSupportedPhysicalDisplays];    // Array of displays

    sp<DrmEventThread>              mpEventThread;
    sp<DrmUEventThread>             mpUEventThread;

    uint64_t                        mAcquiredPanelFitters;
    uint32_t                        mAcquiredCrtcs;
    uint32_t                        mAcquiredPipes;
    uint32_t                        mActiveDisplays;
    uint32_t                        mActiveDisplaysMask;
    Mutex                           mLockForCrtcMask;

    bool                            mbRegisterWithHwc:1;
    bool                            mbCapNuclear:1;
    bool                            mbCapUniversalPlanes:1;
    bool                            mbCapRenderCompression:1;


    drmModeResPtr                   mpModeRes;
};

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_DRM_H
