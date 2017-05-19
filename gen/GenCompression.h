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

#ifndef INTEL_UFO_HWC_GENCOMPRESSION_H
#define INTEL_UFO_HWC_GENCOMPRESSION_H

namespace intel {
namespace ufo {
namespace hwc {

enum class ECompressionType {
    NONE          = static_cast<int>(COMPRESSION_NONE),
    // Implementation specific buffer compression types.
    GL_RC         = static_cast<int>(COMPRESSION_ARCH_START),
    GL_CLEAR_RC,
    MMC,
};

} // namespace hwc
} // namespace ufo
} // namespace intel


#endif // INTEL_UFO_HWC_GENCOMPRESSION_H
