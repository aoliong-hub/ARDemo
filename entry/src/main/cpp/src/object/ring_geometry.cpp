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

#include "ring_geometry.h"
#include <cmath>

namespace ARObject {
namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
}

void GenerateRingMesh(std::vector<float> &vertices, std::vector<float> &normals, std::vector<uint16_t> &indices,
                      float majorRadius, float tubeRadius, int major, int minor)
{
    vertices.clear();
    normals.clear();
    indices.clear();
    if (major < 3) {
        major = 3;
    }
    if (minor < 3) {
        minor = 3;
    }

    // Torus around the Z axis: center ring in the XY plane, tube swept around it.
    for (int i = 0; i < major; ++i) {
        float u = kTwoPi * i / major;
        float cu = std::cos(u);
        float su = std::sin(u);
        for (int j = 0; j < minor; ++j) {
            float v = kTwoPi * j / minor;
            float cv = std::cos(v);
            float sv = std::sin(v);
            // Surface normal (points outward from the tube center).
            float nx = cv * cu;
            float ny = cv * su;
            float nz = sv;
            // Tube center is on the major ring; vertex = center + tubeRadius * normal.
            float x = majorRadius * cu + tubeRadius * nx;
            float y = majorRadius * su + tubeRadius * ny;
            float z = tubeRadius * nz;
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            normals.push_back(nx);
            normals.push_back(ny);
            normals.push_back(nz);
        }
    }

    auto idx = [major, minor](int i, int j) -> uint16_t {
        return static_cast<uint16_t>((i % major) * minor + (j % minor));
    };

    for (int i = 0; i < major; ++i) {
        for (int j = 0; j < minor; ++j) {
            uint16_t a = idx(i, j);
            uint16_t b = idx(i + 1, j);
            uint16_t c = idx(i + 1, j + 1);
            uint16_t d = idx(i, j + 1);
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(d);
        }
    }
}

} // namespace ARObject
