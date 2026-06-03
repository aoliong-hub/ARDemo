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

// Procedural geometry for the Wayfinder beacon (Stage 11A). Pure CPU mesh generation, no GL.
// Local frame: +Y is up, the ground ring lies in the XZ plane at y=0, the pillar rises along +Y.
// The top badge (ring + disk) and phone icon are authored in the XY plane (z=0) centered at the
// origin, normal +Z; the renderer places them at the pillar top facing the camera (billboard).
// Rendering translates the ground/pillar to the anchor with world +Y preserved (no anchor
// rotation), so the ring stays on the floor.

#ifndef WAYFINDER_GEOMETRY_H
#define WAYFINDER_GEOMETRY_H

#include <cstdint>
#include <vector>

namespace ARObject {

// Pillar / badge / alignment-frame top height (m) above the ground anchor. Drives the pillar
// geometry height, the badge/frame world position, and the distance reference. Lowered to 0.9m so
// the alignment frame sits near a comfortable hold height (no need to raise the phone).
constexpr float kWayfinderTopHeight = 0.9f;

struct WayfinderMesh {
    std::vector<float> positions;   // x,y,z per vertex
    std::vector<float> normals;     // nx,ny,nz per vertex
    std::vector<float> uvs;         // u,v per vertex (uv.y = height fraction for the pillar)
    std::vector<uint16_t> indices;  // triangle list (or line list for the phone icon)
};

class WayfinderGeometry {
public:
    // Ground ring: flat annulus in the XZ plane at y=0, normal +Y. 30cm outer dia / 24cm inner.
    static WayfinderMesh CreateGroundRing(float outerRadius = 0.15f, float innerRadius = 0.12f, int segments = 64);

    // Main beam: thin "laser line" cylinder side, y in [0, height], 4mm dia. uv.y = y/height.
    static WayfinderMesh CreatePillarCore(float radius = 0.002f, float height = kWayfinderTopHeight, int segments = 24);

    // Fog beam: wide cylinder side around the core. uv.x = circumferential (0..1), uv.y = height
    // (bottom 0 -> top 1). The volumetric-noise fog shader scrolls noise up uv.y.
    static WayfinderMesh CreatePillarFog(float radius = 0.04f, float height = kWayfinderTopHeight, int segments = 24);

    // Pillar bloom: a wide, faint cylinder side OUTSIDE the fog — a smooth (no-noise) glow halo.
    static WayfinderMesh CreatePillarBloom(float radius = 0.07f, float height = kWayfinderTopHeight, int segments = 24);

    // Top badge ring: a flat annulus in the XY plane (z=0) centered at the origin, normal +Z.
    // 24cm outer dia / 21cm inner (1.5cm ring width). The gray medallion rim.
    static WayfinderMesh CreateTopBadgeRing(float outerRadius = 0.12f, float innerRadius = 0.105f, int segments = 48);

    // Badge ring bloom: a faint annulus ~20% larger than the badge ring (uv.y radial: inner bright,
    // outer faint), drawn co-planar with the badge to fake an edge glow around the medallion rim.
    static WayfinderMesh CreateBadgeRingBloom(float innerRadius = 0.124f, float outerRadius = 0.144f, int segments = 48);

    // Top badge disk: a solid disk (triangle fan) in the XY plane (z=0) centered at the origin,
    // normal +Z. 16cm dia. The semi-transparent gray medallion background behind ring + phone icon.
    static WayfinderMesh CreateTopBadgeDisk(float radius = 0.08f, int segments = 48);

    // Phone icon: white wireframe (outer body + inner screen) as a LINE list, in the XY plane
    // (z=0) centered at the origin, co-planar with the badge.
    static WayfinderMesh CreatePhoneIcon(float width = 0.05f, float height = 0.09f);

    // Rounded phone icon: like CreatePhoneIcon but the body + inner screen corners are rounded
    // (short line segments approximate the arcs) and it adds an earpiece slit + home-button line.
    // 5cm x 9cm body, 6mm body corner radius, 4mm screen corner radius. GL_LINES list.
    static WayfinderMesh CreatePhoneIconRounded(float width = 0.05f, float height = 0.09f);

    // Alignment frame (Stage 11D, Stage 12C-shrunk): a rounded-rectangle BORDER (a filled band of
    // `thickness`) in the XY plane (z=0), normal +Z, centered at the origin. Rendered as a triangle
    // list. uv.x runs 0..1 around the perimeter (drives the hue gradient); uv.y = 0 inner edge / 1
    // outer edge.
    // Phase 1.5(2026-06-02):aspect 1:2(0.075×0.15)→ 3:4(0.075×0.10),匹配用户实际看到的可见区
    //   (XComponent 顶 + 底黑条遮罩后剩下中间 1256×1680 strip,aspect 0.748 ≈ 3:4),配合 renderer
    //   scale 2.47 实现 30cm 处 88% 填满可见区。Oriented in 6DoF by the renderer.
    static WayfinderMesh CreateAlignmentFrame(float width = 0.075f, float height = 0.10f, float thickness = 0.005f,
                                              float cornerRadius = 0.005f, int segPerCorner = 8);

    // 3D arrow (Stage 11D, Stage 12C-shrunk to ~40%): a thin cylinder shaft + a single flat
    // triangular 2-barb arrowhead in the XZ plane, pointing along +Z. uv.y runs 0 (shaft base) ->
    // 1 (tip) for the lengthwise hue gradient. ~2.4cm long (was 6cm). Rendered with the arrow hue
    // shader. Proportions tuned to read as a small, precise aim indicator inside the shrunk frame.
    static WayfinderMesh CreateArrow3D(float shaftRadius = 0.0006f, float shaftLength = 0.016f,
                                       float wingSpan = 0.0048f, float wingThickness = 0.0008f,
                                       float headLength = 0.008f, int shaftSegments = 16);

    // Water drop: low-poly UV sphere centered at the origin, ~2.5cm radius (5cm diameter). uv.x =
    // longitude (0..1), uv.y = 0 bottom (south pole) → 1 top (north pole) so the iridescent FLOW
    // shader (axis=1) sweeps pearl colors vertically while the drop falls. Used by the placement
    // sequence animation (stage 2: drop falls from badge to ground).
    static WayfinderMesh CreateWaterDrop(float radius = 0.025f, int segments = 20, int stacks = 12);

    // Snap celebration FX(2026-06-03)— 炫彩薄膜:对齐框内部的填充矩形(2 三角形),与框几何同尺寸
    // (default 0.075×0.10,3:4)。uv (0..1, 0..1),供 MEMBRANE shader 沿 uv 流 pearl + flash + 转青绿。
    // 在框中央 z=0 平面,法线 +Z(同框)。aligned 0→1 边缘触发庆祝特效时显示。
    static WayfinderMesh CreateAlignmentMembrane(float width = 0.075f, float height = 0.10f);
};

} // namespace ARObject

#endif // WAYFINDER_GEOMETRY_H
