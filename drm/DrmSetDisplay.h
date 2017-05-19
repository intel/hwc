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

#ifndef INTEL_UFO_HWC_DRM_ATOMIC_H
#define INTEL_UFO_HWC_DRM_ATOMIC_H

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <i915_drm.h>

// TODO:
// Kernel interface is defined locally for now (replicated from kernel/include/uapi/drm/drm_mode.h).
// This should be moved to $ANDROID_TOP/external/drm

/* Set display IOCTL */
#define DRM_IOCTL_MODE_SETDISPLAY DRM_IOWR(0xBC, struct drm_mode_set_display)

/* drm_mode_set_display API */

/* Version */
#define DRM_MODE_SET_DISPLAY_VERSION 1

/* Max supported planes by drm_mode_set_display API per pipe basis */
#define DRM_MODE_SET_DISPLAY_MAX_PLANES			4
/* Per-display update flag */
#define DRM_MODE_SET_DISPLAY_UPDATE_ZORDER		(1 << 0)
#define DRM_MODE_SET_DISPLAY_UPDATE_PANEL_FITTER        (1 << 1)
#define DRM_MODE_SET_DISPLAY_UPDATE_PLANE(N)		(1 << (8+(N)))
/* Per-plane update flag */
#define DRM_MODE_SET_DISPLAY_PLANE_UPDATE_PRESENT	(1 << 0)
#define DRM_MODE_SET_DISPLAY_PLANE_UPDATE_RRB2		(1 << 1)
#define DRM_MODE_SET_DISPLAY_PLANE_UPDATE_TRANSFORM	(1 << 2)
#define DRM_MODE_SET_DISPLAY_PLANE_UPDATE_ALPHA		(1 << 3)
/* Transforms */
#define DRM_MODE_SET_DISPLAY_PLANE_TRANSFORM_NONE	0
#define DRM_MODE_SET_DISPLAY_PLANE_TRANSFORM_ROT180	1

/**
 * struct drm_mode_set_display_panel_fitter - panel fitter data
 * @mode:	modes are:
 *		DRM_PFIT_OFF	off.
 *		DRM_AUTOSCALE	stretch source to display. Ignore
 *				destination frame.
 *		DRM_PFIT_MANUAL	fit source to destination frame in display.
 *		DRM_PILLARBOX	fit source to display preserving A/R with
 *				bars left/right. Ignore destination frame.
 *		DRM_LETTERBOX	fit source to display preserving A/R with
 *				bars top/bottom. Ignore destination frame.
 * @src_w:	source width
 * @src_h:	source height
 * @dst_x:	destination x xo-ordinate
 * @dst_y:	destination y co-ordinate
 * @dst_w:	destination width
 * @dst_h:	destination height
 *
 * Data for panel fitter.
 * Source size describes the input source co-ordinate space 0,0 - src_w x src_h.
 * The mode describes how content is scaled from the source co-ordinate space to
 * the display.
 */
struct drm_mode_set_display_panel_fitter {
	__u32 mode;                 /* Mode */
	__u16 src_w;				/* Source width */
	__u16 src_h;				/* Source height */
	__s16 dst_x;				/* Destination left */
	__s16 dst_y;				/* Destination top */
	__u16 dst_w;				/* Destination width */
	__u16 dst_h;				/* Destination height */
};

/**
 * struct drm_mode_set_disaplay_plane - plane data maybe display or sprite palne
 * @obj_id:	object id
 * @obj_type:	object type
 * @update_flag:indicates which plane property to update
 * @fb_id:	framebuffer containing surface format types
 * @flags	flag to inform the page flip on sprite or display plane
 * @crtc_x:	signed dest x-co-ordinate to be partially off screen
 * @crtc_y:	signed dest y-co-ordinate to be partially off screen
 * @crtc_w:	signed dest width to be partially off screen
 * @crtc_h:	signed dest height to be partially off screen\
 * @src_x:	source co-ordinate
 * @src_y:	source co-ordinate
 * @src_w:	source width
 * @src_h:	source height
 * @user_data:	user data if flags = DRM_MODE_PAGE_FLIP_EVENT
 * @rrb2_enable:RRB2 data
 * @transform:	180deg rotation data
 * @alpha:	plane alpha data
 *
 */
struct drm_mode_set_display_plane {
	__u32 obj_id;
	__u32 obj_type;
	__u32 update_flag;
	__u32 fb_id;
	__u32 flags;
	__s32 crtc_x, crtc_y;
	__u32 crtc_w, crtc_h;
	__u32 src_x, src_y;
	__u32 src_h, src_w;
	__u32 rrb2_enable;
	__u32 transform;
	__u32 alpha;
	__u64 user_data;
};

/**
 * struct drm_mode_set_display - data for the whole display
 * @version:		version number
 * @crtc_id:		crtc id corresponds to the pipe id
 * @update_flag:	flags that inform the display plane propertied that are
 *			to be updated
 * @zorder:		z-order value
 * @panel_fitter:	struct that holds the panel_fitter data
 * @num_planes:		total number of planes
 * @plane:		pointer to the plane related data structure
 * @presented:		mask for plane that succeddfully presented bit0->plane0
 * @errored:		mask for plane that errored, bit0->plane0
 *
 */
struct drm_mode_set_display {
	__u32 size;
	__u32 version;
	__u32 crtc_id;
	__u32 update_flag;
	__u32 zorder;
	__u32 num_planes;
	/*
	 * NOTE: These returns are temporary.
	 * The final drm_mode_set_display implementation should be atomic and
	 * all should succeed or all fail
	 */
	__u32 presented;
	__u32 errored;
	struct drm_mode_set_display_panel_fitter panel_fitter;
	struct drm_mode_set_display_plane plane[DRM_MODE_SET_DISPLAY_MAX_PLANES];
};

#endif // INTEL_UFO_HWC_DRM_ATOMIC_H
