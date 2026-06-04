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
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 1.57079632679489661923f;

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

// Append a closed rounded-rectangle outline (XY plane, z=0, normal +Z) centered at (cx,cy) to a
// LINE-list mesh. Each of the 4 corners is a quarter-arc approximated by segPerCorner segments;
// the straight edges fall out of connecting consecutive corner-arc endpoints (incl. the wrap).
void AppendRoundedRectLoop(WayfinderMesh &m, float cx, float cy, float hw, float hh, float r, int segPerCorner)
{
    if (segPerCorner < 1) {
        segPerCorner = 1;
    }
    uint16_t base = static_cast<uint16_t>(m.positions.size() / 3);
    const float ccx[4] = {cx + hw - r, cx + hw - r, cx - hw + r, cx - hw + r}; // BR, TR, TL, BL centers
    const float ccy[4] = {cy - hh + r, cy + hh - r, cy + hh - r, cy - hh + r};
    const float a0[4] = {-kHalfPi, 0.0f, kHalfPi, kPi};
    int count = 0;
    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s <= segPerCorner; ++s) {
            float a = a0[c] + kHalfPi * static_cast<float>(s) / segPerCorner;
            PushVtx(m, ccx[c] + r * std::cos(a), ccy[c] + r * std::sin(a), 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            ++count;
        }
    }
    for (int i = 0; i < count; ++i) {
        m.indices.push_back(static_cast<uint16_t>(base + i));
        m.indices.push_back(static_cast<uint16_t>(base + (i + 1) % count));
    }
}

// Append a single line segment (two endpoints, XY plane z=0) to a LINE-list mesh.
void AppendLineSeg(WayfinderMesh &m, float x0, float y0, float x1, float y1)
{
    uint16_t base = static_cast<uint16_t>(m.positions.size() / 3);
    PushVtx(m, x0, y0, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    PushVtx(m, x1, y1, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    m.indices.push_back(base);
    m.indices.push_back(static_cast<uint16_t>(base + 1));
}

// Fill `pts` (x,y pairs) with the perimeter points of a rounded rect (XY plane), 4 corner arcs of
// (segPerCorner+1) points each in order BR, TR, TL, BL. Returns the point count. Straight edges fall
// out of connecting consecutive corner-arc endpoints.
int RoundedRectPerimeter(std::vector<float> &pts, float hw, float hh, float r, int segPerCorner)
{
    if (segPerCorner < 1) {
        segPerCorner = 1;
    }
    const float ccx[4] = {hw - r, hw - r, -(hw - r), -(hw - r)};
    const float ccy[4] = {-(hh - r), hh - r, hh - r, -(hh - r)};
    const float a0[4] = {-kHalfPi, 0.0f, kHalfPi, kPi};
    int count = 0;
    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s <= segPerCorner; ++s) {
            float a = a0[c] + kHalfPi * static_cast<float>(s) / segPerCorner;
            pts.push_back(ccx[c] + r * std::cos(a));
            pts.push_back(ccy[c] + r * std::sin(a));
            ++count;
        }
    }
    return count;
}
} // namespace

WayfinderMesh WayfinderGeometry::CreateGroundRing(float outerRadius, float innerRadius, int segments)
{
    WayfinderMesh m;
    if (segments < 8) {
        segments = 8;
    }
    // Two rings of vertices (outer, inner) in the XZ plane at y=0; normal +Y.
    // Stage 12C: emit segments+1 pairs so the seam pair (i=segments) has identical xz as i=0 but
    // u=1.0 instead of u=0. With pearl(0)==pearl(1) in the FLOW shader, the last quad now
    // interpolates u smoothly from (segments-1)/segments → 1.0 instead of wrapping → 0 and
    // reverse-scanning the whole spectrum. Indexing drops the modulo since the seam vertex pair
    // exists.
    for (int i = 0; i <= segments; ++i) {
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
        uint16_t o1 = static_cast<uint16_t>(2 * (i + 1));
        uint16_t i1 = static_cast<uint16_t>(2 * (i + 1) + 1);
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

WayfinderMesh WayfinderGeometry::CreatePillarBloom(float radius, float height, int segments)
{
    return BuildCylinderSide(radius, height, segments);
}

WayfinderMesh WayfinderGeometry::CreateBadgeRingBloom(float innerRadius, float outerRadius, int segments)
{
    WayfinderMesh m;
    if (segments < 8) {
        segments = 8;
    }
    // Annulus in the XY plane at z=0, normal +Z. uv.y = 0 inner (bright) -> 1 outer (faint) so the
    // solid shader fades the glow outward from the badge rim.
    for (int i = 0; i < segments; ++i) {
        float a = kTwoPi * i / segments;
        float cu = std::cos(a);
        float su = std::sin(a);
        PushVtx(m, outerRadius * cu, outerRadius * su, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f); // outer = 2i
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

WayfinderMesh WayfinderGeometry::CreatePhoneIconRounded(float width, float height)
{
    WayfinderMesh m;
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float bodyR = 0.006f; // 6mm body corner radius
    // Outer body: rounded rectangle (5 segments per corner).
    AppendRoundedRectLoop(m, 0.0f, 0.0f, hw, hh, bodyR, 5);
    // Earpiece slit near the top + home-button line near the bottom (single short segments).
    AppendLineSeg(m, -hw * 0.28f, hh * 0.80f, hw * 0.28f, hh * 0.80f);
    AppendLineSeg(m, -hw * 0.32f, -hh * 0.82f, hw * 0.32f, -hh * 0.82f);
    // Inner screen: smaller rounded rectangle (4mm corner radius), inset to leave room top/bottom.
    AppendRoundedRectLoop(m, 0.0f, 0.0f, hw * 0.72f, hh * 0.60f, 0.004f, 4);
    return m;
}

WayfinderMesh WayfinderGeometry::CreateArrow3D(float shaftRadius, float shaftLength, float wingSpan,
                                               float wingThickness, float headLength, int shaftSegments)
{
    (void)wingThickness; // wings are modelled flat (double-sided); thickness kept for API parity
    WayfinderMesh m;
    if (shaftSegments < 6) {
        shaftSegments = 6;
    }
    const float total = shaftLength + headLength;
    // Shaft: a cylinder side along -Z, z in [0, -shaftLength]. The arrow points down -Z so its
    // facing normal matches the alignment frame's normal (frameRot * -Z) — no flip needed at render.
    // uv.y = |z| / total (lengthwise gradient: 0 at the base, 1 at the tip).
    for (int i = 0; i < shaftSegments; ++i) {
        float a = kTwoPi * i / shaftSegments;
        float c = std::cos(a);
        float s = std::sin(a);
        PushVtx(m, shaftRadius * c, shaftRadius * s, 0.0f, c, s, 0.0f, 0.0f, 0.0f);                    // base = 2i
        PushVtx(m, shaftRadius * c, shaftRadius * s, -shaftLength, c, s, 0.0f, 0.0f, shaftLength / total); // top = 2i+1
    }
    for (int i = 0; i < shaftSegments; ++i) {
        uint16_t b0 = static_cast<uint16_t>(2 * i);
        uint16_t t0 = static_cast<uint16_t>(2 * i + 1);
        uint16_t b1 = static_cast<uint16_t>(2 * ((i + 1) % shaftSegments));
        uint16_t t1 = static_cast<uint16_t>(2 * ((i + 1) % shaftSegments) + 1);
        m.indices.push_back(b0);
        m.indices.push_back(t0);
        m.indices.push_back(t1);
        m.indices.push_back(b0);
        m.indices.push_back(t1);
        m.indices.push_back(b1);
    }
    // A single flat 2-barb arrowhead in the XZ plane (normal +Y), at the shaft top (z=-shaftLength)
    // converging to the tip on the -Z axis. Double-sided (culling off).
    const float baseV = shaftLength / total;
    uint16_t base = static_cast<uint16_t>(m.positions.size() / 3);
    PushVtx(m, wingSpan, 0.0f, -shaftLength, 0.0f, 1.0f, 0.0f, 0.0f, baseV);  // +span barb
    PushVtx(m, -wingSpan, 0.0f, -shaftLength, 0.0f, 1.0f, 0.0f, 1.0f, baseV); // -span barb
    PushVtx(m, 0.0f, 0.0f, -total, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f);            // tip
    m.indices.push_back(base);
    m.indices.push_back(static_cast<uint16_t>(base + 1));
    m.indices.push_back(static_cast<uint16_t>(base + 2));
    return m;
}

WayfinderMesh WayfinderGeometry::CreateAlignmentFrame(float width, float height, float thickness, float cornerRadius,
                                                      int segPerCorner)
{
    WayfinderMesh m;
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    float rInner = cornerRadius - thickness;
    if (rInner < 0.001f) {
        rInner = 0.001f;
    }
    std::vector<float> outer;
    std::vector<float> inner;
    int count = RoundedRectPerimeter(outer, hw, hh, cornerRadius, segPerCorner);
    RoundedRectPerimeter(inner, hw - thickness, hh - thickness, rInner, segPerCorner);
    // Pair outer[i] / inner[i] (same perimeter index) into a band. uv.x = perimeter param for the
    // hue gradient; uv.y = 1 outer / 0 inner.
    // Stage 12C: emit count+1 vertex pairs so the seam pair (i=count) has identical xy as i=0 but
    // u=1.0 instead of u=0. With pearl(0)==pearl(1) in FRAME_FS, the last quad interpolates u
    // smoothly from (count-1)/count → 1.0 instead of wrapping → 0 and reverse-scanning the
    // spectrum. Coords still read from modulo so the seam pair coincides with the start pair.
    for (int i = 0; i <= count; ++i) {
        int src = i % count;
        float u = static_cast<float>(i) / count;
        PushVtx(m, outer[2 * src], outer[2 * src + 1], 0.0f, 0.0f, 0.0f, 1.0f, u, 1.0f); // outer = 2i
        PushVtx(m, inner[2 * src], inner[2 * src + 1], 0.0f, 0.0f, 0.0f, 1.0f, u, 0.0f); // inner = 2i+1
    }
    for (int i = 0; i < count; ++i) {
        uint16_t o0 = static_cast<uint16_t>(2 * i);
        uint16_t i0 = static_cast<uint16_t>(2 * i + 1);
        uint16_t o1 = static_cast<uint16_t>(2 * (i + 1));
        uint16_t i1 = static_cast<uint16_t>(2 * (i + 1) + 1);
        m.indices.push_back(o0);
        m.indices.push_back(i0);
        m.indices.push_back(i1);
        m.indices.push_back(o0);
        m.indices.push_back(i1);
        m.indices.push_back(o1);
    }
    return m;
}

// Snap FX 炫彩薄膜:在 XY 平面 z=0 的填充矩形,与框几何同尺寸。2 三角形,4 顶点,uv (0..1)。
// MEMBRANE shader 沿 uv.y 流 pearl,触发庆祝特效时从框内"涌出"。
WayfinderMesh WayfinderGeometry::CreateAlignmentMembrane(float width, float height)
{
    WayfinderMesh m;
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    // 4 顶点:左下、右下、右上、左上;normal +Z。
    PushVtx(m, -hw, -hh, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    PushVtx(m,  hw, -hh, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    PushVtx(m,  hw,  hh, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    PushVtx(m, -hw,  hh, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    // 2 三角形(CCW from +Z 看):0-1-2,0-2-3。
    m.indices.push_back(0);
    m.indices.push_back(1);
    m.indices.push_back(2);
    m.indices.push_back(0);
    m.indices.push_back(2);
    m.indices.push_back(3);
    return m;
}

// Water drop: low-poly UV sphere centered at origin, radius `radius`. (segments+1) longitude
// vertices × (stacks+1) latitude rings (poles included). uv.x = longitude (0..1, seam-aware via
// the duplicated segments+1 column), uv.y = latitude with 0 at the south pole / 1 at the north
// pole so the FLOW shader's vertical sweep paints pearl colors from bottom up.
WayfinderMesh WayfinderGeometry::CreateWaterDrop(float radius, int segments, int stacks)
{
    WayfinderMesh m;
    if (segments < 6) {
        segments = 6;
    }
    if (stacks < 4) {
        stacks = 4;
    }
    for (int i = 0; i <= stacks; ++i) {
        // phi = 0 at south pole → kPi at north pole. y = -radius..+radius.
        float v = static_cast<float>(i) / static_cast<float>(stacks);
        float phi = v * kPi;
        float y = -std::cos(phi) * radius;
        float r = std::sin(phi) * radius;
        for (int j = 0; j <= segments; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(segments);
            float theta = u * kTwoPi;
            float x = r * std::cos(theta);
            float z = r * std::sin(theta);
            // Normal = position / radius (outward). uv.y = v (0 bottom → 1 top), uv.x = u (seam).
            float invR = (radius > 1e-6f) ? (1.0f / radius) : 1.0f;
            PushVtx(m, x, y, z, x * invR, y * invR, z * invR, u, v);
        }
    }
    int stride = segments + 1;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < segments; ++j) {
            uint16_t a = static_cast<uint16_t>(i * stride + j);
            uint16_t b = static_cast<uint16_t>((i + 1) * stride + j);
            uint16_t c = static_cast<uint16_t>(i * stride + j + 1);
            uint16_t d = static_cast<uint16_t>((i + 1) * stride + j + 1);
            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(c);
            m.indices.push_back(c);
            m.indices.push_back(b);
            m.indices.push_back(d);
        }
    }
    return m;
}

WayfinderMesh WayfinderGeometry::CreateAxesLines(float length)
{
    WayfinderMesh m;
    // 6 vertices: origin + tip for each of X/Y/Z axes.
    // Vertex layout: each vertex has position (x,y,z), normal (nx,ny,nz), uv (u,v).
    // Normals and uvs are unused by the line shader but must be present for PushVtx.
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const float axes[3][3] = {
        {length, 0.0f, 0.0f},   // +X
        {0.0f, length, 0.0f},   // +Y
        {0.0f, 0.0f, length},   // +Z
    };
    for (int a = 0; a < 3; ++a) {
        // Origin vertex for this axis
        PushVtx(m, origin[0], origin[1], origin[2], 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        // Tip vertex for this axis
        PushVtx(m, axes[a][0], axes[a][1], axes[a][2], 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        // Line segment: index a*2 → a*2+1
        uint16_t base = static_cast<uint16_t>(a * 2);
        m.indices.push_back(base);
        m.indices.push_back(base + 1);
    }
    return m;
}

WayfinderMesh WayfinderGeometry::CreateAxesLinesDashed(float length, float dashLen, float gapLen)
{
    WayfinderMesh m;
    if (dashLen <= 0.0f) { dashLen = 0.04f; }
    if (gapLen  <= 0.0f) { gapLen  = 0.03f; }
    const float period = dashLen + gapLen;  // one dash + one gap
    const float axes[3][3] = {
        {length, 0.0f, 0.0f},  // +X
        {0.0f, length, 0.0f},  // +Y
        {0.0f, 0.0f, length},  // +Z
    };
    for (int a = 0; a < 3; ++a) {
        float dist = 0.0f;
        while (dist + dashLen <= length) {
            float d0 = dist;
            float d1 = dist + dashLen;
            // start of dash
            uint16_t base = static_cast<uint16_t>(m.positions.size() / 3);
            float t0 = d0 / length;
            float t1 = d1 / length;
            PushVtx(m, axes[a][0]*t0, axes[a][1]*t0, axes[a][2]*t0, 0,0,0, 0,0);
            PushVtx(m, axes[a][0]*t1, axes[a][1]*t1, axes[a][2]*t1, 0,0,0, 1,1);
            m.indices.push_back(base);
            m.indices.push_back(base + 1);
            dist += period;
        }
    }
    return m;
}

WayfinderMesh WayfinderGeometry::CreateDebugSphere(float radius, int segments, int stacks)
{
    // Same UV-sphere construction as CreateWaterDrop but with caller-specified radius.
    WayfinderMesh m;
    if (segments < 6) { segments = 6; }
    if (stacks < 4)   { stacks = 4; }
    for (int i = 0; i <= stacks; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(stacks);
        float phi = v * kPi;
        float y = -std::cos(phi) * radius;
        float r = std::sin(phi) * radius;
        for (int j = 0; j <= segments; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(segments);
            float theta = u * kTwoPi;
            float x = r * std::cos(theta);
            float z = r * std::sin(theta);
            float invR = (radius > 1e-6f) ? (1.0f / radius) : 1.0f;
            PushVtx(m, x, y, z, x * invR, y * invR, z * invR, u, v);
        }
    }
    int stride = segments + 1;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < segments; ++j) {
            uint16_t a = static_cast<uint16_t>(i * stride + j);
            uint16_t b = static_cast<uint16_t>((i + 1) * stride + j);
            uint16_t c = static_cast<uint16_t>(i * stride + j + 1);
            uint16_t d = static_cast<uint16_t>((i + 1) * stride + j + 1);
            m.indices.push_back(a); m.indices.push_back(b); m.indices.push_back(c);
            m.indices.push_back(c); m.indices.push_back(b); m.indices.push_back(d);
        }
    }
    return m;
}

} // namespace ARObject
