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

#include "wayfinder_geometry.h"
#include <cmath>

namespace ARObject {
namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

void PushVtx(WayfinderMesh &m, float x, float y, float z, float nx, float ny, float nz, float u, float v)
{
    m.positions.push_back(x);
    m.positions.push_back(y);
    m.positions.push_back(z);
    m.normals.push_back(nx);
    m.normals.push_back(ny);
    m.normals.push_back(nz);
    m.uvs.push_back(u);
    m.uvs.push_back(v);
}
} // namespace

WayfinderMesh WayfinderGeometry::CreateGroundRing(float outerRadius, float innerRadius, int segments)
{
    WayfinderMesh m;
    if (segments < 8) {
        segments = 8;
    }
    // Two rings of vertices (outer, inner) in the XZ plane at y=0; normal +Y.
    for (int i = 0; i < segments; ++i) {
        float a = kTwoPi * i / segments;
        float cu = std::cos(a);
        float su = std::sin(a);
        float u = static_cast<float>(i) / segments;
        PushVtx(m, outerRadius * cu, 0.0f, outerRadius * su, 0.0f, 1.0f, 0.0f, u, 1.0f); // outer = 2i
        PushVtx(m, innerRadius * cu, 0.0f, innerRadius * su, 0.0f, 1.0f, 0.0f, u, 0.0f); // inner = 2i+1
    }
    for (int i = 0; i < segments; ++i) {
        uint16_t o0 = static_cast<uint16_t>(2 * i);
        uint16_t i0 = static_cast<uint16_t>(2 * i + 1);
        uint16_t o1 = static_cast<uint16_t>(2 * ((i + 1) % segments));
        uint16_t i1 = static_cast<uint16_t>(2 * ((i + 1) % segments) + 1);
        m.indices.push_back(o0);
        m.indices.push_back(i0);
        m.indices.push_back(i1);
        m.indices.push_back(o0);
        m.indices.push_back(i1);
        m.indices.push_back(o1);
    }
    return m;
}

WayfinderMesh WayfinderGeometry::CreateRippleRing(float innerRadius, float outerRadius, int segments)
{
    WayfinderMesh m;
    if (segments < 8) {
        segments = 8;
    }
    // Flat annulus in the XZ plane at y=0, normal +Y. uv.y encodes the radial position so the solid
    // shader fades alpha from the inner edge (uv.y=0) to the outer edge (uv.y=1).
    for (int i = 0; i < segments; ++i) {
        float a = kTwoPi * i / segments;
        float cu = std::cos(a);
        float su = std::sin(a);
        PushVtx(m, outerRadius * cu, 0.0f, outerRadius * su, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f); // outer = 2i
        PushVtx(m, innerRadius * cu, 0.0f, innerRadius * su, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f); // inner = 2i+1
    }
    for (int i = 0; i < segments; ++i) {
        uint16_t o0 = static_cast<uint16_t>(2 * i);
        uint16_t i0 = static_cast<uint16_t>(2 * i + 1);
        uint16_t o1 = static_cast<uint16_t>(2 * ((i + 1) % segments));
        uint16_t i1 = static_cast<uint16_t>(2 * ((i + 1) % segments) + 1);
        m.indices.push_back(o0);
        m.indices.push_back(i0);
        m.indices.push_back(i1);
        m.indices.push_back(o0);
        m.indices.push_back(i1);
        m.indices.push_back(o1);
    }
    return m;
}

// Shared cylinder-side builder for the pillar core/fog: axis along +Y, y in [0,height].
static WayfinderMesh BuildCylinderSide(float radius, float height, int segments)
{
    WayfinderMesh m;
    if (segments < 6) {
        segments = 6;
    }
    for (int i = 0; i < segments; ++i) {
        float a = kTwoPi * i / segments;
        float cu = std::cos(a);
        float su = std::sin(a);
        float u = static_cast<float>(i) / segments;
        PushVtx(m, radius * cu, 0.0f, radius * su, cu, 0.0f, su, u, 0.0f);     // bottom = 2i
        PushVtx(m, radius * cu, height, radius * su, cu, 0.0f, su, u, 1.0f);   // top = 2i+1
    }
    for (int i = 0; i < segments; ++i) {
        uint16_t b0 = static_cast<uint16_t>(2 * i);
        uint16_t t0 = static_cast<uint16_t>(2 * i + 1);
        uint16_t b1 = static_cast<uint16_t>(2 * ((i + 1) % segments));
        uint16_t t1 = static_cast<uint16_t>(2 * ((i + 1) % segments) + 1);
        m.indices.push_back(b0);
        m.indices.push_back(t0);
        m.indices.push_back(t1);
        m.indices.push_back(b0);
        m.indices.push_back(t1);
        m.indices.push_back(b1);
    }
    return m;
}

WayfinderMesh WayfinderGeometry::CreatePillarCore(float radius, float height, int segments)
{
    return BuildCylinderSide(radius, height, segments);
}

WayfinderMesh WayfinderGeometry::CreatePillarFog(float radius, float height, int segments)
{
    return BuildCylinderSide(radius, height, segments);
}

WayfinderMesh WayfinderGeometry::CreateTopBadgeRing(float outerRadius, float innerRadius, int segments)
{
    WayfinderMesh m;
    if (segments < 8) {
        segments = 8;
    }
    // Full annulus in the XY plane at z=0, normal +Z (camera-facing). Two rings of vertices.
    for (int i = 0; i < segments; ++i) {
        float a = kTwoPi * i / segments;
        float cu = std::cos(a);
        float su = std::sin(a);
        PushVtx(m, outerRadius * cu, outerRadius * su, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); // outer = 2i
        PushVtx(m, innerRadius * cu, innerRadius * su, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); // inner = 2i+1
    }
    for (int i = 0; i < segments; ++i) {
        uint16_t o0 = static_cast<uint16_t>(2 * i);
        uint16_t i0 = static_cast<uint16_t>(2 * i + 1);
        uint16_t o1 = static_cast<uint16_t>(2 * ((i + 1) % segments));
        uint16_t i1 = static_cast<uint16_t>(2 * ((i + 1) % segments) + 1);
        m.indices.push_back(o0);
        m.indices.push_back(i0);
        m.indices.push_back(i1);
        m.indices.push_back(o0);
        m.indices.push_back(i1);
        m.indices.push_back(o1);
    }
    return m;
}

WayfinderMesh WayfinderGeometry::CreateTopBadgeDisk(float radius, int segments)
{
    WayfinderMesh m;
    if (segments < 8) {
        segments = 8;
    }
    // Triangle fan in the XY plane at z=0, normal +Z. Center vertex (0) + a rim of segments+1.
    PushVtx(m, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); // center = 0
    for (int i = 0; i <= segments; ++i) {
        float a = kTwoPi * i / segments;
        PushVtx(m, radius * std::cos(a), radius * std::sin(a), 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); // rim = 1+i
    }
    for (int i = 0; i < segments; ++i) {
        m.indices.push_back(0);
        m.indices.push_back(static_cast<uint16_t>(1 + i));
        m.indices.push_back(static_cast<uint16_t>(1 + i + 1));
    }
    return m;
}

WayfinderMesh WayfinderGeometry::CreatePhoneIcon(float width, float height)
{
    WayfinderMesh m;
    const float y = 0.0f; // centered at the origin, co-planar with the badge
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    // Outer body corners (XY plane, z=0), CCW from bottom-left: 0,1,2,3
    PushVtx(m, -hw, y - hh, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    PushVtx(m, hw, y - hh, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    PushVtx(m, hw, y + hh, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    PushVtx(m, -hw, y + hh, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    // Inner screen (85%): 4,5,6,7
    const float iw = hw * 0.7f;
    const float ih = hh * 0.78f;
    PushVtx(m, -iw, y - ih, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    PushVtx(m, iw, y - ih, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    PushVtx(m, iw, y + ih, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    PushVtx(m, -iw, y + ih, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    // Line list: outer 4 edges + inner 4 edges.
    const uint16_t lines[] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4};
    for (uint16_t v : lines) {
        m.indices.push_back(v);
    }
    return m;
}

} // namespace ARObject
