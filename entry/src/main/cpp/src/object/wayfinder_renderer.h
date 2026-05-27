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

// Wayfinder beacon renderer (Stage 11A). Draws ground ring + pillar core/fog + spinner + phone
// icon. Two shader programs: a "solid" program (color + uv.y alpha gradient) for the ring/pillar/
// spinner, and a "line" program for the phone wireframe. Client-side vertex arrays (project style).

#ifndef WAYFINDER_RENDERER_H
#define WAYFINDER_RENDERER_H

#include "graphic/GLUtils.h"
#include "wayfinder_geometry.h"
#include <glm.hpp>

namespace ARObject {

class WayfinderRenderer {
public:
    WayfinderRenderer() = default;
    ~WayfinderRenderer() = default;

    void Init();
    void Release();

    // Render the whole beacon. wayfinderToWorld = translate(anchorPos) with world +Y up. cameraPos
    // (world) orients the top badge to face the camera. color drives ground ring + pillar (distance
    // red->green); animTime (seconds) revolves the whole badge + drives the ripple. distance
    // (camera->beacon top, m) sets the ground ripple strength (off within 0.5m, stronger farther).
    void Render(const glm::mat4 &view, const glm::mat4 &proj, const glm::mat4 &wayfinderToWorld,
                const glm::vec3 &cameraPos, const glm::vec3 &color, float animTime, float distance);

private:
    void DrawSolid(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alphaBase,
                   float alphaTop, bool depthWrite);
    void DrawLines(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alpha);
    // Volumetric-noise fog: alphaBase is the fog's base alpha; time scrolls the noise upward.
    void DrawFog(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alphaBase, float time);

    GLuint mSolidProgram = 0;
    GLint mSolidMvp = -1;
    GLint mSolidColor = -1;
    GLint mSolidAlphaBase = -1;
    GLint mSolidAlphaTop = -1;
    GLint mSolidPos = -1;
    GLint mSolidUv = -1;

    GLuint mLineProgram = 0;
    GLint mLineMvp = -1;
    GLint mLineColor = -1;
    GLint mLineAlpha = -1;
    GLint mLinePos = -1;

    GLuint mFogProgram = 0;
    GLint mFogMvp = -1;
    GLint mFogColor = -1;
    GLint mFogAlpha = -1;
    GLint mFogTime = -1;
    GLint mFogPos = -1;
    GLint mFogUv = -1;

    WayfinderMesh mGround;
    WayfinderMesh mCore;
    WayfinderMesh mFog;
    WayfinderMesh mPillarBloom;
    WayfinderMesh mBadgeRing;
    WayfinderMesh mBadgeRingBloom;
    WayfinderMesh mBadgeDisk;
    WayfinderMesh mPhone;
};

} // namespace ARObject

#endif // WAYFINDER_RENDERER_H
