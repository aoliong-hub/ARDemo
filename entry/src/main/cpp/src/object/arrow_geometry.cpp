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

#include "arrow_geometry.h"
#include <cmath>

namespace ARObject {
namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

// Parameterized arrow generator: tip at -Z, centered on origin. Shaft radius/length and head
// radius/length are explicit so both the full-size and small arrows share one implementation.
void GenerateArrowMeshParam(std::vector<float> &vertices, std::vector<float> &normals,
                            std::vector<uint16_t> &shaftIndices, std::vector<uint16_t> &headIndices, int segments,
                            float shaftRadius, float shaftLen, float headRadius, float headLen)
{
    vertices.clear();
    normals.clear();
    shaftIndices.clear();
    headIndices.clear();
    if (segments < 3) {
        segments = 3;
    }
    float total = shaftLen + headLen;
    float tailZ = total * 0.5f;
    float tipZ = -total * 0.5f;
    float junctionZ = tipZ + headLen;

    auto addVertex = [&](float x, float y, float z, float nx, float ny, float nz) -> uint16_t {
        uint16_t idx = static_cast<uint16_t>(vertices.size() / 3);
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(z);
        normals.push_back(nx);
        normals.push_back(ny);
        normals.push_back(nz);
        return idx;
    };

    // ---- Shaft: cylinder side (radial normals) ----
    for (int i = 0; i < segments; ++i) {
        float a0 = kTwoPi * i / segments;
        float a1 = kTwoPi * (i + 1) / segments;
        float c0 = std::cos(a0);
        float s0 = std::sin(a0);
        float c1 = std::cos(a1);
        float s1 = std::sin(a1);
        uint16_t t0 = addVertex(shaftRadius * c0, shaftRadius * s0, tailZ, c0, s0, 0.0f);
        uint16_t j0 = addVertex(shaftRadius * c0, shaftRadius * s0, junctionZ, c0, s0, 0.0f);
        uint16_t t1 = addVertex(shaftRadius * c1, shaftRadius * s1, tailZ, c1, s1, 0.0f);
        uint16_t j1 = addVertex(shaftRadius * c1, shaftRadius * s1, junctionZ, c1, s1, 0.0f);
        shaftIndices.push_back(t0);
        shaftIndices.push_back(j0);
        shaftIndices.push_back(j1);
        shaftIndices.push_back(t0);
        shaftIndices.push_back(j1);
        shaftIndices.push_back(t1);
    }

    // ---- Shaft: tail cap (disk at tailZ, normal +Z) ----
    uint16_t tailCenter = addVertex(0.0f, 0.0f, tailZ, 0.0f, 0.0f, 1.0f);
    for (int i = 0; i < segments; ++i) {
        float a0 = kTwoPi * i / segments;
        float a1 = kTwoPi * (i + 1) / segments;
        uint16_t v0 = addVertex(shaftRadius * std::cos(a0), shaftRadius * std::sin(a0), tailZ, 0.0f, 0.0f, 1.0f);
        uint16_t v1 = addVertex(shaftRadius * std::cos(a1), shaftRadius * std::sin(a1), tailZ, 0.0f, 0.0f, 1.0f);
        shaftIndices.push_back(tailCenter);
        shaftIndices.push_back(v1);
        shaftIndices.push_back(v0);
    }

    // ---- Head: cone base (disk at junctionZ, normal -Z) ----
    uint16_t baseCenter = addVertex(0.0f, 0.0f, junctionZ, 0.0f, 0.0f, -1.0f);
    for (int i = 0; i < segments; ++i) {
        float a0 = kTwoPi * i / segments;
        float a1 = kTwoPi * (i + 1) / segments;
        uint16_t v0 = addVertex(headRadius * std::cos(a0), headRadius * std::sin(a0), junctionZ, 0.0f, 0.0f, -1.0f);
        uint16_t v1 = addVertex(headRadius * std::cos(a1), headRadius * std::sin(a1), junctionZ, 0.0f, 0.0f, -1.0f);
        headIndices.push_back(baseCenter);
        headIndices.push_back(v0);
        headIndices.push_back(v1);
    }

    // ---- Head: cone side ----
    float coneLen = junctionZ - tipZ; // positive
    float nz = headRadius / coneLen;  // outward normal z-component
    for (int i = 0; i < segments; ++i) {
        float a0 = kTwoPi * i / segments;
        float a1 = kTwoPi * (i + 1) / segments;
        float am = (a0 + a1) * 0.5f;
        float c0 = std::cos(a0);
        float s0 = std::sin(a0);
        float c1 = std::cos(a1);
        float s1 = std::sin(a1);
        float cm = std::cos(am);
        float sm = std::sin(am);
        float l0 = std::sqrt(c0 * c0 + s0 * s0 + nz * nz);
        float l1 = std::sqrt(c1 * c1 + s1 * s1 + nz * nz);
        float lm = std::sqrt(cm * cm + sm * sm + nz * nz);
        uint16_t b0 = addVertex(headRadius * c0, headRadius * s0, junctionZ, c0 / l0, s0 / l0, nz / l0);
        uint16_t b1 = addVertex(headRadius * c1, headRadius * s1, junctionZ, c1 / l1, s1 / l1, nz / l1);
        uint16_t tip = addVertex(0.0f, 0.0f, tipZ, cm / lm, sm / lm, nz / lm);
        headIndices.push_back(b0);
        headIndices.push_back(tip);
        headIndices.push_back(b1);
    }
}
} // namespace

void GenerateArrowMesh(std::vector<float> &vertices, std::vector<float> &normals,
                       std::vector<uint16_t> &shaftIndices, std::vector<uint16_t> &headIndices, int segments)
{
    // Original full-size arrow (unchanged): shaft 0.04r x 0.6, head 0.11r x 0.4 -> spans [-0.5, 0.5].
    GenerateArrowMeshParam(vertices, normals, shaftIndices, headIndices, segments, 0.04f, 0.6f, 0.11f, 0.4f);
}

void GenerateSmallArrow(std::vector<float> &vertices, std::vector<float> &normals, std::vector<uint16_t> &indices,
                        int segments)
{
    std::vector<uint16_t> shaftIndices;
    std::vector<uint16_t> headIndices;
    GenerateArrowMeshParam(vertices, normals, shaftIndices, headIndices, segments, 0.005f, 0.05f, 0.012f, 0.02f);
    indices.clear();
    indices.reserve(shaftIndices.size() + headIndices.size());
    indices.insert(indices.end(), shaftIndices.begin(), shaftIndices.end());
    indices.insert(indices.end(), headIndices.begin(), headIndices.end());
}

} // namespace ARObject
