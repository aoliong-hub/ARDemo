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

// Procedural torus (thin glowing ring) lying in the XY plane with its axis (normal) along +Z.
// Aligning the model +Z to a world direction makes the ring plane perpendicular to that
// direction. Pure geometry (no GL/AR deps).

#ifndef RING_GEOMETRY_H
#define RING_GEOMETRY_H

#include <cstdint>
#include <vector>

namespace ARObject {

// R = major radius, r = tube radius. major/minor = ring/tube segment counts.
void GenerateRingMesh(std::vector<float> &vertices, std::vector<float> &normals, std::vector<uint16_t> &indices,
                      float majorRadius = 0.10f, float tubeRadius = 0.005f, int major = 32, int minor = 8);

} // namespace ARObject

#endif // RING_GEOMETRY_H
