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
#include "BxtDisplayCaps.h"
#include "BytDisplayCaps.h"
#include "HswDisplayCaps.h"

namespace intel {
namespace ufo {
namespace hwc {

DisplayCaps* DisplayCaps::create(uint32_t hardwarePipe, uint32_t deviceId)
{
    ALOGD_IF( sbLogViewerBuild, "DisplayCaps creating caps for device 0x%x pipe %u", deviceId, hardwarePipe );
    DisplayCaps* pDisplayCaps = NULL;
    switch ( deviceId )
    {
        // BYT
        case 0x0f30: /* Baytrail M */
        case 0x0f31: /* Baytrail M */
        case 0x0f32: /* Baytrail M */
        case 0x0f33: /* Baytrail M */
        case 0x0157: /* Baytrail M */
        case 0x0155: /* Baytrail D */
        // CHT (reference: Source/inc/common/igfxfmid.h)
        case 0x22b2: /* Cherrytrail D  */
        case 0x22b0: /* Cherrytrail M  */
        case 0x22b3: /* Cherrytrail D+ */
        case 0x22b1: /* Cherrytrail M+ */
            pDisplayCaps = new BytDisplayCaps( hardwarePipe, (deviceId >= 0x22b0) );
            ALOGE_IF( pDisplayCaps == NULL, "Failed to create Baytrail class caps" );
            break;

        case 0x1913: /* SKL ULT GT1.5 */ \
        case 0x1915: /* SKL ULX GT1.5 */ \
        case 0x1917: /* SKL DT  GT1.5 */ \
        case 0x1906: /* SKL ULT GT1 */ \
        case 0x190E: /* SKL ULX GT1 */ \
        case 0x1902: /* SKL DT  GT1 */ \
        case 0x190B: /* SKL Halo GT1 */ \
        case 0x190A: /* SKL SRV GT1 */ \
        case 0x1916: /* SKL ULT GT2 */ \
        case 0x1921: /* SKL ULT GT2F */ \
        case 0x191E: /* SKL ULX GT2 */ \
        case 0x1912: /* SKL DT  GT2 */ \
        case 0x191B: /* SKL Halo GT2 */ \
        case 0x191A: /* SKL SRV GT2 */ \
        case 0x191D: /* SKL WKS GT2 */ \
        case 0x1926: /* SKL ULT GT3 */ \
        case 0x192B: /* SKL Halo GT3 */ \
        case 0x192A: /* SKL SRV GT3 */ \
        case 0x1932: /* SKL DT  GT4 */ \
        case 0x193B: /* SKL Halo GT4 */ \
        case 0x193A: /* SKL SRV GT4 */ \
        case 0x193D: /* SKL WKS GT4 */ \

        case 0x0A84: /* BXT_GT_18EU */
        case 0x1A84: /* BXT_T  18EU */
        case 0x1A85: /* BXT_T  12EU */
        case 0x5A84: /* BXT_P  18EU */
        case 0x5A85: /* BXT_P  12EU */
            pDisplayCaps = new BxtDisplayCaps(hardwarePipe, BXT_PLATFORM_SCALAR_COUNT);
            ALOGE_IF( pDisplayCaps == NULL, "Failed to create Broxton class caps" );
            break;
        case 0x3E04: /* GLV PCI SIM Device ID*/
        case 0xFF10: /* GLV ID*/
            pDisplayCaps = new BxtDisplayCaps(hardwarePipe, GLV_PLATFORM_SCALAR_COUNT);
            ALOGE_IF( pDisplayCaps == NULL, "Failed to create Broxton class caps (for GLV)" );
            break;

        default:
            // Default to basic HSW/BDW class display caps
            pDisplayCaps = new HswDisplayCaps(hardwarePipe);
            ALOGE_IF( pDisplayCaps == NULL, "Failed to create Haswell class caps" );
            break;
    }
    return pDisplayCaps;
}

}; // namespace hwc
}; // namespace ufo
}; // namespace intel
