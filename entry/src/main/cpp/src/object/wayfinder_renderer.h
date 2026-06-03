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

    // Render the beacon. badgeFadeProgress ∈ [0, 1] smoothly crossfades the badge/pillar group out
    // and the alignment-frame/arrow group in as the user approaches (<30cm) — caller advances it
    // over 2s on both directions, replacing the old huntPhase 0↔1 hard switch. huntPhase still
    // drives the alignment frame's hue freeze (LOCKED) via frameHueTime.
    // animAge: seconds since placeRingAt fired this beacon's anchor. <1.3 plays the staged drop
    // sequence (badge fade-in / water drop falling / ground ring popping in); ≥1.3 is steady state
    // (badgeFadeProgress takes over).
    // Phase 2 — clipShiftY:框 + 3D 箭头的 clip-space y 平移(让框居中可见区,badge/pillar 不偏移)。
    //   从 ArkTS 算的可见区 NDC center 来,典型 +0.218(底黑条比顶厚)。0 = 不偏移。
    void Render(const glm::mat4 &view, const glm::mat4 &proj, const glm::mat4 &wayfinderToWorld,
                const glm::vec3 &cameraPos, const glm::vec3 &color, float animTime, float distance, int huntPhase,
                const glm::quat &frameOrientation, float frameHueTime, bool isAligned, float deltaTime,
                float ringHeight, float badgeFadeProgress, float animAge, float clipShiftY);

private:
    void DrawSolid(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alphaBase,
                   float alphaTop, bool depthWrite);
    void DrawLines(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alpha);
    // Volumetric-noise fog: alphaBase is the fog's base alpha; time scrolls the noise upward.
    void DrawFog(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alphaBase, float time);
    // Alignment frame: pink->purple->blue gradient along uv.x, hue-rotated by hueTime. alphaMult
    // multiplies the shader's baked 0.85 alpha (1.0 = full, 0 = invisible) so the caller can
    // crossfade with the badge group.
    // Phase 3 ④ — lockProgress ∈ [0, 1] 控制框的吸附状态:0=pearl 自由流彩,1=青绿锁定。
    // Snap FX — flashIntensity ∈ [0, 1] 闪白(庆祝特效首帧拉高,~60ms exp 衰减)。
    void DrawFrame(const glm::mat4 &mvp, const WayfinderMesh &mesh, float hueTime, float alphaMult,
                   float lockProgress, float flashIntensity);
    // Snap FX 炫彩薄膜:同框位置 + 旋转 + scale,用 MEMBRANE shader 画填充矩形。
    //   alpha 由阶段时间序列驱动(stage A 0→0.5,stage B 维持,stage C/D 0.5→0)。
    //   lockProgress / flashIntensity 同 DrawFrame(共享一套 mLockProgress / mFlashIntensity)。
    void DrawMembrane(const glm::mat4 &mvp, float hueTime, float alpha,
                      float lockProgress, float flashIntensity);
    // 3D arrow: lengthwise pink->purple->blue gradient + hue rotation; fades to green by aligned (0..1).
    // alphaMult multiplies the shader's baked 1.0 alpha (used to crossfade with the badge group).
    void DrawArrow(const glm::mat4 &mvp, float hueTime, float aligned, float alphaMult);
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
    GLint mFrameAlphaMult = -1;
    // Phase 3 ④ — 吸附效果:0 = pearl 自由流动,1 = 青绿锁定。同步控制流速 / 颜色 / alpha。
    GLint mFrameLockProgress = -1;
    // Snap FX — 庆祝特效闪光强度(0..1,exp 衰减)。
    GLint mFrameFlash = -1;
    GLint mFramePos = -1;
    GLint mFrameUv = -1;

    // Snap FX(2026-06-03)炫彩薄膜:对齐瞬间填充框内、流动 pearl + 闪白 + 转青绿,放大涌出 0.9s。
    GLuint mMembraneProgram = 0;
    GLint mMembraneMvp = -1;
    GLint mMembraneTime = -1;
    GLint mMembraneAlpha = -1;          // 外部阶段驱动的整体 alpha
    GLint mMembraneFlash = -1;
    GLint mMembraneLockProgress = -1;
    GLint mMembraneHalfDim = -1;        // 修(问题 2):圆角 SDF 用(物理半尺寸 halfW, halfH)
    GLint mMembraneCornerR = -1;        // 修(问题 2):圆角半径
    GLint mMembranePos = -1;
    GLint mMembraneUv = -1;

    GLuint mArrowProgram = 0;
    GLint mArrowMvp = -1;
    GLint mArrowTime = -1;
    GLint mArrowAligned = -1;
    GLint mArrowOverride = -1;
    GLint mArrowAlphaMult = -1;
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

    // Phase 3 ④ — 框吸附效果状态(persists across frames):
    //   mLockProgress  : 0 = pearl 自由流彩,1 = 青绿锁定。每帧弹簧逼近 (isAligned ? 1 : 0),150ms 半衰。
    //                    驱动 FRAME_FS 的 u_lockProgress(流速 ×1→×4 / 颜色 mix pearl→teal / alpha boost)。
    //   mPrevAligned   : 上一帧 isAligned,用于检测 0→1 边缘触发庆祝特效。
    float mLockProgress = 0.0f;
    bool mPrevAligned = false;

    // Snap FX(2026-06-03)庆祝特效时序状态:
    //   mSnapAnimAge      : 当前播放时刻。-1 = 没在播 / 未触发 / 已重置;0..0.9 = 4 阶段播放中;≥0.9 = 演完。
    //                       触发(aligned 0→1 边缘且 mSnapAnimAge<0)= 置 0;每帧 += dt;走出 30cm 重置为 -1。
    //                       4 阶段时间(s):A 膜+闪 [0, 0.15] / B 慢放大 [0.15, 0.40] / C 飞屏边 [0.40, 0.70] / D 消失 [0.70, 0.90]。
    //   mFlashIntensity   : 闪白强度,0..1。触发时置 1.0,每帧 exp(-dt/0.06) 衰减(~60ms 衰到 1/e),
    //                       u_flash 给 FRAME 和 MEMBRANE 双方 mix 向白色。
    float mSnapAnimAge = -1.0f;
    float mFlashIntensity = 0.0f;

    WayfinderMesh mGround;
    WayfinderMesh mCore;
    WayfinderMesh mFog;
    WayfinderMesh mPillarBloom;
    WayfinderMesh mBadgeRing;
    WayfinderMesh mBadgeRingBloom;
    WayfinderMesh mBadgeDisk;
    WayfinderMesh mPhone;
    WayfinderMesh mAlignFrame;
    // Snap FX 炫彩薄膜:对齐框内填充矩形(2 三角形)。
    WayfinderMesh mAlignMembrane;
    WayfinderMesh mArrow;
    WayfinderMesh mWaterDrop;
};

} // namespace ARObject

#endif // WAYFINDER_RENDERER_H
