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

// Procedural arrow mesh: a shaft cylinder + a head cone, pointing along local -Z.
// Pure geometry (no GL/AR deps). Shaft and head are returned as separate index sets so the
// renderer can draw them with different colors.

#ifndef ARROW_GEOMETRY_H
#define ARROW_GEOMETRY_H

#include <cstdint>
#include <vector>

namespace ARObject {

// Generates an arrow centered on the origin, tip at z = -0.5, tail at z = +0.5.
// vertices/normals: flat float triples (x,y,z). shaftIndices/headIndices: triangle lists.
void GenerateArrowMesh(std::vector<float> &vertices, std::vector<float> &normals,
                       std::vector<uint16_t> &shaftIndices, std::vector<uint16_t> &headIndices, int segments = 16);

// Small arrow (shaft 0.005r x 0.05len, head 0.012r x 0.02h), tip at -Z. Single combined index
// list (shaft + head), suitable for solid single-color drawing (used by the ring indicator).
void GenerateSmallArrow(std::vector<float> &vertices, std::vector<float> &normals, std::vector<uint16_t> &indices,
                        int segments = 16);

} // namespace ARObject

#endif // ARROW_GEOMETRY_H
