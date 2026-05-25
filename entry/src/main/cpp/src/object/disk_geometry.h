/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// A flat 0.20m x 0.20m quad in the XY plane (normal +Z), centered at origin. UVs at the four
// corners feed a radial-gradient glow in the disk fragment shader.

#ifndef DISK_GEOMETRY_H
#define DISK_GEOMETRY_H

#include <cstdint>
#include <vector>

namespace ARObject {

void GenerateDiskQuad(std::vector<float> &vertices, std::vector<float> &normals, std::vector<float> &uvs,
                      std::vector<uint16_t> &indices);

} // namespace ARObject

#endif // DISK_GEOMETRY_H
