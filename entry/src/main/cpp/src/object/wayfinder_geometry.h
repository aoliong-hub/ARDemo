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

    // Ripple ring: a thin flat annulus in the XZ plane at y=0, normal +Y, used for the expanding
    // ground "water ripple". uv.y = 0 on the inner edge, 1 on the outer edge, so the solid shader's
    // alphaBase/alphaTop fade the wave radially (strong inner -> faint outer). Radii animate per frame.
    static WayfinderMesh CreateRippleRing(float innerRadius, float outerRadius, int segments = 64);

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

    // Alignment frame (Stage 11D): a rounded-rectangle BORDER (a filled band of `thickness`) in the
    // XY plane (z=0), normal +Z, centered at the origin. Rendered as a triangle list. uv.x runs
    // 0..1 around the perimeter (drives the hue gradient); uv.y = 0 inner edge / 1 outer edge.
    // 15x30cm outer, 1.0cm band, 1.0cm outer corner radius. Oriented in 6DoF by the renderer.
    static WayfinderMesh CreateAlignmentFrame(float width = 0.15f, float height = 0.30f, float thickness = 0.010f,
                                              float cornerRadius = 0.010f, int segPerCorner = 8);

    // 3D arrow (Stage 11D), pointing along +Z. A thin cylinder shaft + a single flat triangular
    // 2-barb arrowhead in the XZ plane. uv.y runs 0 (shaft base) -> 1 (tip) for the lengthwise hue
    // gradient. ~6cm long. Rendered with the arrow hue shader.
    static WayfinderMesh CreateArrow3D(float shaftRadius = 0.0015f, float shaftLength = 0.04f,
                                       float wingSpan = 0.012f, float wingThickness = 0.002f,
                                       float headLength = 0.02f, int shaftSegments = 16);
};

} // namespace ARObject

#endif // WAYFINDER_GEOMETRY_H
