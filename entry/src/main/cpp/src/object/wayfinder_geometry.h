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

// Pillar / spinner / phone-icon top height (m). The pillar core & fog share it.
constexpr float kWayfinderTopHeight = 1.2f;

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

    // Main beam: thin cylinder side, y in [0, height], radius ~0.008m. uv.y = y/height (bottom 0).
    static WayfinderMesh CreatePillarCore(float radius = 0.008f, float height = kWayfinderTopHeight, int segments = 24);

    // Fog beam: wide cylinder side around the core, soft alpha falloff via uv.y.
    static WayfinderMesh CreatePillarFog(float radius = 0.04f, float height = kWayfinderTopHeight, int segments = 24);

    // Top badge ring: a flat annulus in the XY plane (z=0) centered at the origin, normal +Z.
    // 12cm outer dia / 10cm inner (1cm ring width). The gray medallion rim; the renderer spins it.
    static WayfinderMesh CreateTopBadgeRing(float outerRadius = 0.06f, float innerRadius = 0.05f, int segments = 48);

    // Top badge disk: a solid disk (triangle fan) in the XY plane (z=0) centered at the origin,
    // normal +Z. 8cm dia. The semi-transparent gray medallion background behind ring + phone icon.
    static WayfinderMesh CreateTopBadgeDisk(float radius = 0.04f, int segments = 48);

    // Phone icon: white wireframe (outer body + inner screen) as a LINE list, in the XY plane
    // (z=0) centered at the origin, co-planar with the badge.
    static WayfinderMesh CreatePhoneIcon(float width = 0.025f, float height = 0.045f);
};

} // namespace ARObject

#endif // WAYFINDER_GEOMETRY_H
