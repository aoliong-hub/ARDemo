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
#include <gtc/quaternion.hpp>

namespace ARObject {

class WayfinderRenderer {
public:
    WayfinderRenderer() = default;
    ~WayfinderRenderer() = default;

    void Init();
    void Release();

    // Render the beacon. huntPhase 0 = APPROACHING (full wayfinder); 1/2 = ALIGNING/LOCKED (ground
    // ring with stronger breathing + the 6DoF alignment frame at the beacon top, oriented by
    // frameOrientation). frameHueTime drives the frame's hue rotation (frozen by the caller when
    // LOCKED). Other params as before: color (distance red->mint), animTime (s), distance (m).
    void Render(const glm::mat4 &view, const glm::mat4 &proj, const glm::mat4 &wayfinderToWorld,
                const glm::vec3 &cameraPos, const glm::vec3 &color, float animTime, float distance, int huntPhase,
                const glm::quat &frameOrientation, float frameHueTime, bool isAligned, float deltaTime,
                float ringHeight);

private:
    void DrawSolid(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alphaBase,
                   float alphaTop, bool depthWrite);
    void DrawLines(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alpha);
    // Volumetric-noise fog: alphaBase is the fog's base alpha; time scrolls the noise upward.
    void DrawFog(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alphaBase, float time);
    // Alignment frame: pink->purple->blue gradient along uv.x, hue-rotated by hueTime.
    void DrawFrame(const glm::mat4 &mvp, const WayfinderMesh &mesh, float hueTime);
    // 3D arrow: lengthwise pink->purple->blue gradient + hue rotation; fades to green by aligned (0..1).
    void DrawArrow(const glm::mat4 &mvp, float hueTime, float aligned);
    // Stage 12C iridescent flow: pearl orange/pink/purple/blue closed-loop swept by time along one
    // axis. axis=0 sweeps uv.x (circumferential — ground ring); axis=1 sweeps uv.y (vertical —
    // pillar core). alphaBase/alphaTop drive the per-vertex alpha gradient along uv.y.
    void DrawFlow(const glm::mat4 &mvp, const WayfinderMesh &mesh, float alphaBase, float alphaTop,
                  float time, float axis, float speed, bool depthWrite);

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

    GLuint mFrameProgram = 0;
    GLint mFrameMvp = -1;
    GLint mFrameTime = -1;
    GLint mFramePos = -1;
    GLint mFrameUv = -1;

    GLuint mArrowProgram = 0;
    GLint mArrowMvp = -1;
    GLint mArrowTime = -1;
    GLint mArrowAligned = -1;
    GLint mArrowOverride = -1;
    GLint mArrowPos = -1;
    GLint mArrowUv = -1;

    GLuint mFlowProgram = 0;
    GLint mFlowMvp = -1;
    GLint mFlowTime = -1;
    GLint mFlowAlphaBase = -1;
    GLint mFlowAlphaTop = -1;
    GLint mFlowAxis = -1;
    GLint mFlowSpeed = -1;
    GLint mFlowPos = -1;
    GLint mFlowUv = -1;

    // Arrow animation state (persists across frames): align color/spin transition + spin angle.
    float mAlignedTransition = 0.0f; // 0 = colorful + spinning, 1 = green + stopped
    float mSpinAngle = 0.0f;         // current spin about the arrow's own axis (rad)

    WayfinderMesh mGround;
    WayfinderMesh mCore;
    WayfinderMesh mFog;
    WayfinderMesh mPillarBloom;
    WayfinderMesh mBadgeRing;
    WayfinderMesh mBadgeRingBloom;
    WayfinderMesh mBadgeDisk;
    WayfinderMesh mPhone;
    WayfinderMesh mAlignFrame;
    WayfinderMesh mArrow;
};

} // namespace ARObject

#endif // WAYFINDER_RENDERER_H
