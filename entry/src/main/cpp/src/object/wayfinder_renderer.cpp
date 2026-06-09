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

#include "wayfinder_renderer.h"
#include "utils/log.h"
#include <cmath>
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>
#include <gtc/type_ptr.hpp>

namespace ARObject {
namespace {
// Solid program: flat color with alpha interpolated along uv.y (bottom alphaBase -> top alphaTop).
constexpr char SOLID_VS[] = R"(
    uniform mat4 u_mvp;
    attribute vec4 a_pos;
    attribute vec2 a_uv;
    varying float v_h;
    void main() {
        v_h = a_uv.y;
        gl_Position = u_mvp * a_pos;
    }
)";
// Stage 11C: final alpha x0.85 for an overall softer/translucent look (the phone wireframe keeps
// full alpha via the separate line program).
constexpr char SOLID_FS[] = R"(
    precision mediump float;
    varying float v_h;
    uniform vec3 u_color;
    uniform float u_alphaBase;
    uniform float u_alphaTop;
    void main() {
        float a = mix(u_alphaBase, u_alphaTop, v_h);
        gl_FragColor = vec4(u_color, a * 0.85);
    }
)";

// Line program: flat color for the phone wireframe (kept crisp at full alpha).
constexpr char LINE_VS[] = R"(
    uniform mat4 u_mvp;
    attribute vec4 a_pos;
    void main() {
        gl_Position = u_mvp * a_pos;
    }
)";
constexpr char LINE_FS[] = R"(
    precision mediump float;
    uniform vec3 u_color;
    uniform float u_alpha;
    void main() {
        gl_FragColor = vec4(u_color, u_alpha);
    }
)";

// Fog program: volumetric-looking pillar fog. uv.x = circumferential, uv.y = height. Two octaves of
// value noise scroll upward over time; alpha also fades toward the top. No texture needed.
constexpr char FOG_VS[] = R"(
    uniform mat4 u_mvp;
    attribute vec4 a_pos;
    attribute vec2 a_uv;
    varying vec2 v_uv;
    void main() {
        v_uv = a_uv;
        gl_Position = u_mvp * a_pos;
    }
)";
constexpr char FOG_FS[] = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform vec3 u_color;
    uniform float u_alphaBase;
    uniform float u_time;
    float hash(vec2 p) {
        return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
    }
    float noise(vec2 p) {
        vec2 i = floor(p);
        vec2 f = fract(p);
        f = f * f * (3.0 - 2.0 * f);
        return mix(mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), f.x),
                   mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
    }
    vec3 pearl(float t) {
        t = fract(t);
        vec3 c0 = vec3(1.00, 0.87, 0.76);
        vec3 c1 = vec3(0.96, 0.82, 0.82);
        vec3 c2 = vec3(0.84, 0.74, 1.00);
        vec3 c3 = vec3(0.78, 0.84, 1.00);
        float x = t * 4.0;
        if (x < 1.0) return mix(c0, c1, x);
        if (x < 2.0) return mix(c1, c2, x - 1.0);
        if (x < 3.0) return mix(c2, c3, x - 2.0);
        return mix(c3, c0, x - 3.0);
    }
    void main() {
        float verticalFade = v_uv.y; // 0 bottom -> 1 top (top more transparent)
        vec2 nuv = vec2(v_uv.x * 4.0, v_uv.y * 2.0 - u_time * 0.3);
        float n = noise(nuv) * 0.7 + 0.3;
        vec2 nuv2 = vec2(v_uv.x * 8.0, v_uv.y * 4.0 - u_time * 0.15);
        float n2 = noise(nuv2);
        n = mix(n, n2, 0.4);
        float alpha = u_alphaBase * (1.0 - verticalFade) * n;
        vec3 col = pearl(v_uv.y + u_time * 0.25);
        gl_FragColor = vec4(col, alpha * 0.85);
    }
)";

// Iridescent flow program (Stage 12C-batch3): closed-loop pearl spectrum
// (orange→pink→purple→blue) swept by u_time along one axis. axis=0 → sweep uv.x (ground ring
// circumference). axis=1 → sweep uv.y (pillar height). Matches the ArkTS guide_ring/guide_tri
// PNG palette so the GL ground ring + pillar visually pair with the 2D off-screen indicator.
constexpr char FLOW_VS[] = R"(
    uniform mat4 u_mvp;
    attribute vec4 a_pos;
    attribute vec2 a_uv;
    varying vec2 v_uv;
    void main() { v_uv = a_uv; gl_Position = u_mvp * a_pos; }
)";
constexpr char FLOW_FS[] = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform float u_time;
    uniform float u_alphaBase;
    uniform float u_alphaTop;
    uniform float u_axis;
    uniform float u_speed;
    vec3 pearl(float t) {
        t = fract(t);
        vec3 c0 = vec3(1.00, 0.87, 0.76);
        vec3 c1 = vec3(0.96, 0.82, 0.82);
        vec3 c2 = vec3(0.84, 0.74, 1.00);
        vec3 c3 = vec3(0.78, 0.84, 1.00);
        float x = t * 4.0;
        if (x < 1.0) return mix(c0, c1, x);
        if (x < 2.0) return mix(c1, c2, x - 1.0);
        if (x < 3.0) return mix(c2, c3, x - 2.0);
        return mix(c3, c0, x - 3.0);
    }
    void main() {
        float coord = mix(v_uv.x, v_uv.y, u_axis);
        float t = coord + u_time * u_speed;
        vec3 col = pearl(t);
        float grad = mix(u_alphaBase, u_alphaTop, v_uv.y);
        gl_FragColor = vec4(col, grad * 0.85);
    }
)";

// Alignment frame program (Stage 11D): pink->purple->blue gradient along uv.x (the perimeter), with
// a YIQ-style RGB hue rotation by u_time so the whole frame cycles colors. alpha 0.85.
constexpr char FRAME_VS[] = R"(
    uniform mat4 u_mvp;
    attribute vec4 a_pos;
    attribute vec2 a_uv;
    varying vec2 v_uv;
    void main() {
        v_uv = a_uv;
        gl_Position = u_mvp * a_pos;
    }
)";
constexpr char FRAME_FS[] = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform float u_time;
    uniform float u_alphaMult;
    // Phase 3 ④:0 = pearl 自由流彩(吸附前),1 = 青绿锁定(吸附后)。同时驱动:
    //   流速:基础 0.15,锁定时 ×4(= 0.60)
    //   颜色:base pearl ↔ lockColor 线性混合
    //   alpha:0.85 ↔ 1.00 boost
    uniform float u_lockProgress;
    // Snap FX(2026-06-03)— 庆祝特效闪光强度,0..1。脉冲触发时拉到 1,~60ms exp 衰减。
    uniform float u_flash;
    vec3 pearl(float t) {
        t = fract(t);
        vec3 c0 = vec3(1.00, 0.87, 0.76);
        vec3 c1 = vec3(0.96, 0.82, 0.82);
        vec3 c2 = vec3(0.84, 0.74, 1.00);
        vec3 c3 = vec3(0.78, 0.84, 1.00);
        float x = t * 4.0;
        if (x < 1.0) return mix(c0, c1, x);
        if (x < 2.0) return mix(c1, c2, x - 1.0);
        if (x < 3.0) return mix(c2, c3, x - 2.0);
        return mix(c3, c0, x - 3.0);
    }
    void main() {
        float flow = 0.15 * (1.0 + 3.0 * u_lockProgress);
        vec3 base = pearl(v_uv.x + u_time * flow);
        vec3 lockColor = vec3(0.4, 1.0, 0.6); // 青绿
        vec3 col = mix(base, lockColor, u_lockProgress);
        col = mix(col, vec3(1.0), u_flash * 0.8); // Snap FX 闪白
        float a = (0.85 + 0.15 * u_lockProgress) * u_alphaMult;
        gl_FragColor = vec4(col, a);
    }
)";

// Snap FX 炫彩薄膜 shader(2026-06-03):
// 填充矩形,uv (0..1, 0..1)。沿 uv.y 流 pearl(类似水滴),触发时 u_flash 闪白,然后 u_lockProgress
// 拉到 1 把整体转青绿(吸附锁定色)。u_alpha 由外部按 4 阶段时间序列驱动(0→0.5→0.3→0)。
constexpr char MEMBRANE_VS[] = R"(
    uniform mat4 u_mvp;
    attribute vec4 a_pos;
    attribute vec2 a_uv;
    varying vec2 v_uv;
    void main() {
        v_uv = a_uv;
        gl_Position = u_mvp * a_pos;
    }
)";
constexpr char MEMBRANE_FS[] = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform float u_time;
    uniform float u_alpha;          // 外部驱动的整体 alpha(0..0.5,按阶段)
    uniform float u_flash;          // 0..1 闪白
    uniform float u_lockProgress;   // 0..1 pearl→青绿
    // 圆角 SDF mask(2026-06-03 问题 2):薄膜边缘按框圆角裁剪,角外 alpha=0,无 4 直角凸起。
    uniform vec2  u_halfDim;        // (halfW, halfH) 物理半尺寸(0.0375, 0.05)
    uniform float u_cornerR;        // 圆角半径(物理,同框 cornerRadius 0.010)
    vec3 pearl(float t) {
        t = fract(t);
        vec3 c0 = vec3(1.00, 0.87, 0.76);
        vec3 c1 = vec3(0.96, 0.82, 0.82);
        vec3 c2 = vec3(0.84, 0.74, 1.00);
        vec3 c3 = vec3(0.78, 0.84, 1.00);
        float x = t * 4.0;
        if (x < 1.0) return mix(c0, c1, x);
        if (x < 2.0) return mix(c1, c2, x - 1.0);
        if (x < 3.0) return mix(c2, c3, x - 2.0);
        return mix(c3, c0, x - 3.0);
    }
    void main() {
        // 流速:lockProgress 影响(同框,基础 0.30,锁定后 0.60),斜对角(uv.x + uv.y 各半)
        float flow = 0.30 * (1.0 + 1.0 * u_lockProgress);
        float coord = (v_uv.x + v_uv.y) * 0.5;
        vec3 base = pearl(coord + u_time * flow);
        vec3 lockColor = vec3(0.4, 1.0, 0.6);
        vec3 col = mix(base, lockColor, u_lockProgress);
        col = mix(col, vec3(1.0), u_flash * 0.8);
        // 圆角 SDF(Inigo Quilez 圆角矩形):mesh-space pos 从 uv 推
        //   p = (v_uv - 0.5) × 2 × halfDim  → 物理坐标 [-halfW, halfW] × [-halfH, halfH]
        //   q = |p|;距离矩形角点(halfDim - cornerR)外延圆弧 cornerR
        vec2 p = (v_uv - 0.5) * 2.0 * u_halfDim;
        vec2 q = abs(p);
        vec2 inCorner = max(q - (u_halfDim - vec2(u_cornerR, u_cornerR)), vec2(0.0, 0.0));
        float sdf = length(inCorner) - u_cornerR;  // < 0 内,0 边,> 0 外
        float aMask = 1.0 - smoothstep(0.0, 0.0008, sdf);  // 0.8mm 抗锯齿
        gl_FragColor = vec4(col, u_alpha * aMask);
    }
)";

// Arrow program (Stage 11D): lengthwise pink->purple->blue gradient (uv.y) + hue rotation; fades to
// a solid green (u_override) by u_aligned (0..1) when the user is aligned.
constexpr char ARROW_VS[] = R"(
    uniform mat4 u_mvp;
    attribute vec4 a_pos;
    attribute vec2 a_uv;
    varying vec2 v_uv;
    void main() {
        v_uv = a_uv;
        gl_Position = u_mvp * a_pos;
    }
)";
constexpr char ARROW_FS[] = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform float u_time;
    uniform float u_aligned;
    uniform vec3 u_override;
    uniform float u_alphaMult;
    vec3 pearl(float t) {
        t = fract(t);
        vec3 c0 = vec3(1.00, 0.87, 0.76);
        vec3 c1 = vec3(0.96, 0.82, 0.82);
        vec3 c2 = vec3(0.84, 0.74, 1.00);
        vec3 c3 = vec3(0.78, 0.84, 1.00);
        float x = t * 4.0;
        if (x < 1.0) return mix(c0, c1, x);
        if (x < 2.0) return mix(c1, c2, x - 1.0);
        if (x < 3.0) return mix(c2, c3, x - 2.0);
        return mix(c3, c0, x - 3.0);
    }
    void main() {
        vec3 base = pearl(v_uv.y + u_time * 0.15);
        vec3 finalColor = mix(base, u_override, u_aligned);
        gl_FragColor = vec4(finalColor, u_alphaMult);
    }
)";

const glm::vec3 kBadgeRingColor(0.70f, 0.70f, 0.74f); // gray rim
const glm::vec3 kBadgeDiskColor(0.45f, 0.45f, 0.48f); // darker semi-transparent medallion
const glm::vec3 kPhoneColor(1.0f, 1.0f, 1.0f);        // white wireframe
constexpr float kBadgeSpinDegPerSec = 90.0f;          // badge rotation: 4s per revolution
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kHalfPi = 1.57079632679489661923f;
} // namespace

void WayfinderRenderer::Init()
{
    mSolidProgram = GLUtils::CreateProgram(SOLID_VS, SOLID_FS);
    mLineProgram = GLUtils::CreateProgram(LINE_VS, LINE_FS);
    if (!mSolidProgram || !mLineProgram) {
        LOGE("WayfinderRenderer: could not create program(s).");
        return;
    }
    mSolidMvp = glGetUniformLocation(mSolidProgram, "u_mvp");
    mSolidColor = glGetUniformLocation(mSolidProgram, "u_color");
    mSolidAlphaBase = glGetUniformLocation(mSolidProgram, "u_alphaBase");
    mSolidAlphaTop = glGetUniformLocation(mSolidProgram, "u_alphaTop");
    mSolidPos = glGetAttribLocation(mSolidProgram, "a_pos");
    mSolidUv = glGetAttribLocation(mSolidProgram, "a_uv");

    mLineMvp = glGetUniformLocation(mLineProgram, "u_mvp");
    mLineColor = glGetUniformLocation(mLineProgram, "u_color");
    mLineAlpha = glGetUniformLocation(mLineProgram, "u_alpha");
    mLinePos = glGetAttribLocation(mLineProgram, "a_pos");

    // Fog program is non-fatal: if it fails to compile, DrawFog is skipped and the rest still draws.
    mFogProgram = GLUtils::CreateProgram(FOG_VS, FOG_FS);
    if (mFogProgram) {
        mFogMvp = glGetUniformLocation(mFogProgram, "u_mvp");
        mFogColor = glGetUniformLocation(mFogProgram, "u_color");
        mFogAlpha = glGetUniformLocation(mFogProgram, "u_alphaBase");
        mFogTime = glGetUniformLocation(mFogProgram, "u_time");
        mFogPos = glGetAttribLocation(mFogProgram, "a_pos");
        mFogUv = glGetAttribLocation(mFogProgram, "a_uv");
    } else {
        LOGE("WayfinderRenderer: fog program failed; falling back to plain fog.");
    }

    mFrameProgram = GLUtils::CreateProgram(FRAME_VS, FRAME_FS);
    if (mFrameProgram) {
        mFrameMvp = glGetUniformLocation(mFrameProgram, "u_mvp");
        mFrameTime = glGetUniformLocation(mFrameProgram, "u_time");
        mFrameAlphaMult = glGetUniformLocation(mFrameProgram, "u_alphaMult");
        mFrameLockProgress = glGetUniformLocation(mFrameProgram, "u_lockProgress");
        mFrameFlash = glGetUniformLocation(mFrameProgram, "u_flash");
        mFramePos = glGetAttribLocation(mFrameProgram, "a_pos");
        mFrameUv = glGetAttribLocation(mFrameProgram, "a_uv");
    } else {
        LOGE("WayfinderRenderer: alignment-frame program failed to compile.");
    }

    mArrowProgram = GLUtils::CreateProgram(ARROW_VS, ARROW_FS);
    if (mArrowProgram) {
        mArrowMvp = glGetUniformLocation(mArrowProgram, "u_mvp");
        mArrowTime = glGetUniformLocation(mArrowProgram, "u_time");
        mArrowAligned = glGetUniformLocation(mArrowProgram, "u_aligned");
        mArrowOverride = glGetUniformLocation(mArrowProgram, "u_override");
        mArrowAlphaMult = glGetUniformLocation(mArrowProgram, "u_alphaMult");
        mArrowPos = glGetAttribLocation(mArrowProgram, "a_pos");
        mArrowUv = glGetAttribLocation(mArrowProgram, "a_uv");
    } else {
        LOGE("WayfinderRenderer: arrow program failed to compile.");
    }

    mFlowProgram = GLUtils::CreateProgram(FLOW_VS, FLOW_FS);
    if (mFlowProgram) {
        mFlowMvp = glGetUniformLocation(mFlowProgram, "u_mvp");
        mFlowTime = glGetUniformLocation(mFlowProgram, "u_time");
        mFlowAlphaBase = glGetUniformLocation(mFlowProgram, "u_alphaBase");
        mFlowAlphaTop = glGetUniformLocation(mFlowProgram, "u_alphaTop");
        mFlowAxis = glGetUniformLocation(mFlowProgram, "u_axis");
        mFlowSpeed = glGetUniformLocation(mFlowProgram, "u_speed");
        mFlowPos = glGetAttribLocation(mFlowProgram, "a_pos");
        mFlowUv = glGetAttribLocation(mFlowProgram, "a_uv");
    } else {
        LOGE("WayfinderRenderer: iridescent flow program failed to compile.");
    }

    // Snap FX 炫彩薄膜 program(填充矩形 + flash + lockProgress 转青绿 + 圆角 SDF)。
    mMembraneProgram = GLUtils::CreateProgram(MEMBRANE_VS, MEMBRANE_FS);
    if (mMembraneProgram) {
        mMembraneMvp = glGetUniformLocation(mMembraneProgram, "u_mvp");
        mMembraneTime = glGetUniformLocation(mMembraneProgram, "u_time");
        mMembraneAlpha = glGetUniformLocation(mMembraneProgram, "u_alpha");
        mMembraneFlash = glGetUniformLocation(mMembraneProgram, "u_flash");
        mMembraneLockProgress = glGetUniformLocation(mMembraneProgram, "u_lockProgress");
        mMembraneHalfDim = glGetUniformLocation(mMembraneProgram, "u_halfDim");
        mMembraneCornerR = glGetUniformLocation(mMembraneProgram, "u_cornerR");
        mMembranePos = glGetAttribLocation(mMembraneProgram, "a_pos");
        mMembraneUv = glGetAttribLocation(mMembraneProgram, "a_uv");
    } else {
        LOGE("WayfinderRenderer: alignment-membrane program failed to compile (snap FX skipped).");
    }

    mGround = WayfinderGeometry::CreateGroundRing();
    mCore = WayfinderGeometry::CreatePillarCore();
    mFog = WayfinderGeometry::CreatePillarFog();
    mPillarBloom = WayfinderGeometry::CreatePillarBloom();
    mBadgeRing = WayfinderGeometry::CreateTopBadgeRing();
    mBadgeRingBloom = WayfinderGeometry::CreateBadgeRingBloom();
    mBadgeDisk = WayfinderGeometry::CreateTopBadgeDisk();
    mPhone = WayfinderGeometry::CreatePhoneIconRounded();
    // Phase 2(2026-06-02):framePos 改为 P + frameNormal×0.50,scale 4.16 → 50cm 处 88% 填可见区。
    // 2026-06-03:cornerRadius 0.0012 → 0.010,scale 4.16 后视觉圆角 ~4cm,框看着更柔和自然
    //   (之前 0.0012 × 4.16 = 5mm 圆角,在 31cm 宽的框上几乎看不出圆)。
    //   segPerCorner 8 → 12,圆角顶点更密,曲线更平滑。thickness 保持 0.0006(细线条)。
    mAlignFrame = WayfinderGeometry::CreateAlignmentFrame(0.075f, 0.10f, 0.0006f, 0.010f, 12);
    // Snap FX 炫彩薄膜:与框几何同尺寸,2 三角形填充。
    mAlignMembrane = WayfinderGeometry::CreateAlignmentMembrane(0.075f, 0.10f);
    mArrow = WayfinderGeometry::CreateArrow3D();
    // 放置序列动画用的水滴(默认 2.5cm 半径 = 5cm 直径)。FLOW shader 沿 uv.y 走 pearl 渐变。
    mWaterDrop = WayfinderGeometry::CreateWaterDrop();
    mAxes = WayfinderGeometry::CreateAxesLines();
    mAxesDashed = WayfinderGeometry::CreateAxesLinesDashed();
    mDebugSphere = WayfinderGeometry::CreateDebugSphere(0.015f);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

void WayfinderRenderer::Release()
{
    if (mSolidProgram) {
        GLUtils::ReleaseProgram(mSolidProgram);
        mSolidProgram = 0;
    }
    if (mLineProgram) {
        GLUtils::ReleaseProgram(mLineProgram);
        mLineProgram = 0;
    }
    if (mFogProgram) {
        GLUtils::ReleaseProgram(mFogProgram);
        mFogProgram = 0;
    }
    if (mFrameProgram) {
        GLUtils::ReleaseProgram(mFrameProgram);
        mFrameProgram = 0;
    }
    if (mArrowProgram) {
        GLUtils::ReleaseProgram(mArrowProgram);
        mArrowProgram = 0;
    }
    if (mFlowProgram) {
        GLUtils::ReleaseProgram(mFlowProgram);
        mFlowProgram = 0;
    }
    if (mMembraneProgram) {
        GLUtils::ReleaseProgram(mMembraneProgram);
        mMembraneProgram = 0;
    }
}

void WayfinderRenderer::DrawSolid(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color,
                                  float alphaBase, float alphaTop, bool depthWrite)
{
    if (mesh.indices.empty()) {
        return;
    }
    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    glUseProgram(mSolidProgram);
    glUniformMatrix4fv(mSolidMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3fv(mSolidColor, 1, glm::value_ptr(color));
    glUniform1f(mSolidAlphaBase, alphaBase);
    glUniform1f(mSolidAlphaTop, alphaTop);
    glEnableVertexAttribArray(mSolidPos);
    glEnableVertexAttribArray(mSolidUv);
    glVertexAttribPointer(mSolidPos, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
    glVertexAttribPointer(mSolidUv, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, mesh.indices.data());
    glDisableVertexAttribArray(mSolidPos);
    glDisableVertexAttribArray(mSolidUv);
}

void WayfinderRenderer::DrawLines(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color, float alpha)
{
    if (mesh.indices.empty()) {
        return;
    }
    glDepthMask(GL_TRUE);
    glUseProgram(mLineProgram);
    glUniformMatrix4fv(mLineMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3fv(mLineColor, 1, glm::value_ptr(color));
    glUniform1f(mLineAlpha, alpha);
    glLineWidth(2.0f);
    glEnableVertexAttribArray(mLinePos);
    glVertexAttribPointer(mLinePos, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
    glDrawElements(GL_LINES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, mesh.indices.data());
    glDisableVertexAttribArray(mLinePos);
}

void WayfinderRenderer::DrawFog(const glm::mat4 &mvp, const WayfinderMesh &mesh, const glm::vec3 &color,
                                float alphaBase, float time)
{
    if (mesh.indices.empty()) {
        return;
    }
    if (!mFogProgram) {
        // Fallback: plain solid fog (no noise) if the fog program failed to compile.
        DrawSolid(mvp, mesh, color, alphaBase, 0.0f, false);
        return;
    }
    glDepthMask(GL_FALSE);
    glUseProgram(mFogProgram);
    glUniformMatrix4fv(mFogMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3fv(mFogColor, 1, glm::value_ptr(color));
    glUniform1f(mFogAlpha, alphaBase);
    glUniform1f(mFogTime, time);
    glEnableVertexAttribArray(mFogPos);
    glEnableVertexAttribArray(mFogUv);
    glVertexAttribPointer(mFogPos, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
    glVertexAttribPointer(mFogUv, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, mesh.indices.data());
    glDisableVertexAttribArray(mFogPos);
    glDisableVertexAttribArray(mFogUv);
}

void WayfinderRenderer::DrawFrame(const glm::mat4 &mvp, const WayfinderMesh &mesh, float hueTime, float alphaMult,
                                  float lockProgress, float flashIntensity)
{
    if (mesh.indices.empty() || !mFrameProgram) {
        return;
    }
    glDepthMask(GL_TRUE); // write depth so the frame border occludes the co-planar disk from behind
    glUseProgram(mFrameProgram);
    glUniformMatrix4fv(mFrameMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(mFrameTime, hueTime);
    glUniform1f(mFrameAlphaMult, alphaMult);
    glUniform1f(mFrameLockProgress, lockProgress);
    glUniform1f(mFrameFlash, flashIntensity);
    glEnableVertexAttribArray(mFramePos);
    glEnableVertexAttribArray(mFrameUv);
    glVertexAttribPointer(mFramePos, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
    glVertexAttribPointer(mFrameUv, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, mesh.indices.data());
    glDisableVertexAttribArray(mFramePos);
    glDisableVertexAttribArray(mFrameUv);
}

// Snap FX 炫彩薄膜:填充矩形,半透明,在框中央渲染。同 framePos + frameRot + scale,放大涌出取景框。
void WayfinderRenderer::DrawMembrane(const glm::mat4 &mvp, float hueTime, float alpha,
                                     float lockProgress, float flashIntensity)
{
    if (mAlignMembrane.indices.empty() || !mMembraneProgram || alpha <= 0.001f) {
        return;
    }
    // 半透明叠加:不写深度(让后面的框/箭头能正确叠),已开 GL_BLEND。
    glDepthMask(GL_FALSE);
    glUseProgram(mMembraneProgram);
    glUniformMatrix4fv(mMembraneMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(mMembraneTime, hueTime);
    glUniform1f(mMembraneAlpha, alpha);
    glUniform1f(mMembraneFlash, flashIntensity);
    glUniform1f(mMembraneLockProgress, lockProgress);
    // 圆角 SDF 参数:框几何 0.075×0.10,half = (0.0375, 0.050),cornerR = 0.010(同 CreateAlignmentFrame)。
    glUniform2f(mMembraneHalfDim, 0.0375f, 0.050f);
    glUniform1f(mMembraneCornerR, 0.010f);
    glEnableVertexAttribArray(mMembranePos);
    glEnableVertexAttribArray(mMembraneUv);
    glVertexAttribPointer(mMembranePos, 3, GL_FLOAT, GL_FALSE, 0, mAlignMembrane.positions.data());
    glVertexAttribPointer(mMembraneUv, 2, GL_FLOAT, GL_FALSE, 0, mAlignMembrane.uvs.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mAlignMembrane.indices.size()), GL_UNSIGNED_SHORT,
                   mAlignMembrane.indices.data());
    glDisableVertexAttribArray(mMembranePos);
    glDisableVertexAttribArray(mMembraneUv);
    glDepthMask(GL_TRUE);
}

void WayfinderRenderer::DrawArrow(const glm::mat4 &mvp, float hueTime, float aligned, float alphaMult)
{
    if (mArrow.indices.empty() || !mArrowProgram) {
        return;
    }
    glDepthMask(GL_TRUE);
    glUseProgram(mArrowProgram);
    glUniformMatrix4fv(mArrowMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(mArrowTime, hueTime);
    glUniform1f(mArrowAligned, aligned);
    glUniform3f(mArrowOverride, 0.3f, 1.0f, 0.4f); // solid green when aligned
    glUniform1f(mArrowAlphaMult, alphaMult);
    glEnableVertexAttribArray(mArrowPos);
    glEnableVertexAttribArray(mArrowUv);
    glVertexAttribPointer(mArrowPos, 3, GL_FLOAT, GL_FALSE, 0, mArrow.positions.data());
    glVertexAttribPointer(mArrowUv, 2, GL_FLOAT, GL_FALSE, 0, mArrow.uvs.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mArrow.indices.size()), GL_UNSIGNED_SHORT,
                   mArrow.indices.data());
    glDisableVertexAttribArray(mArrowPos);
    glDisableVertexAttribArray(mArrowUv);
}

void WayfinderRenderer::DrawFlow(const glm::mat4 &mvp, const WayfinderMesh &mesh, float alphaBase,
                                 float alphaTop, float time, float axis, float speed, bool depthWrite)
{
    if (mesh.indices.empty() || !mFlowProgram) {
        return;
    }
    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    glUseProgram(mFlowProgram);
    glUniformMatrix4fv(mFlowMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(mFlowTime, time);
    glUniform1f(mFlowAlphaBase, alphaBase);
    glUniform1f(mFlowAlphaTop, alphaTop);
    glUniform1f(mFlowAxis, axis);
    glUniform1f(mFlowSpeed, speed);
    glEnableVertexAttribArray(mFlowPos);
    glEnableVertexAttribArray(mFlowUv);
    glVertexAttribPointer(mFlowPos, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
    glVertexAttribPointer(mFlowUv, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, mesh.indices.data());
    glDisableVertexAttribArray(mFlowPos);
    glDisableVertexAttribArray(mFlowUv);
    glDepthMask(GL_TRUE);
}

void WayfinderRenderer::DrawWorldAxes(const glm::mat4 &view, const glm::mat4 &proj)
{
    if (mAxes.indices.empty() || !mLineProgram) {
        return;
    }
    // Solid axes at AR Engine world origin. Model = identity.
    const glm::mat4 mvp = proj * view;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glUseProgram(mLineProgram);
    glUniformMatrix4fv(mLineMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glLineWidth(3.0f);
    glEnableVertexAttribArray(mLinePos);
    glVertexAttribPointer(mLinePos, 3, GL_FLOAT, GL_FALSE, 0, mAxes.positions.data());

    const glm::vec3 kColors[3] = {
        glm::vec3(1.0f, 0.2f, 0.2f),  // +X red
        glm::vec3(0.2f, 1.0f, 0.2f),  // +Y green
        glm::vec3(0.2f, 0.4f, 1.0f),  // +Z blue
    };
    for (int a = 0; a < 3; ++a) {
        glUniform3fv(mLineColor, 1, glm::value_ptr(kColors[a]));
        glUniform1f(mLineAlpha, 0.9f);
        glDrawElements(GL_LINES, 2, GL_UNSIGNED_SHORT,
                       mAxes.indices.data() + a * 2);
    }

    glDisableVertexAttribArray(mLinePos);
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

void WayfinderRenderer::DrawCameraAxes(const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &cameraPos)
{
    if (mAxesDashed.indices.empty() || !mLineProgram) {
        return;
    }

    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp   (view[0][1], view[1][1], view[2][1]);
    const glm::vec3 camFwd  (-view[0][2], -view[1][2], -view[2][2]);

    // Offset forward so all dashes sit beyond the near clip plane.
    const float kForwardOffset = 0.5f;
    glm::vec3 axesOrigin = cameraPos + camFwd * kForwardOffset;

    glm::mat4 axisModel(1.0f);
    axisModel[0] = glm::vec4(camRight, 0.0f);
    axisModel[1] = glm::vec4(camUp,    0.0f);
    axisModel[2] = glm::vec4(camFwd,   0.0f);
    axisModel[3] = glm::vec4(axesOrigin, 1.0f);

    const glm::mat4 mvp = proj * view * axisModel;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glUseProgram(mLineProgram);
    glUniformMatrix4fv(mLineMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glLineWidth(2.5f);
    glEnableVertexAttribArray(mLinePos);
    glVertexAttribPointer(mLinePos, 3, GL_FLOAT, GL_FALSE, 0, mAxesDashed.positions.data());

    // Dashed camera axes: same colours, 0.6 alpha for a softer look.
    const glm::vec3 kColors[3] = {
        glm::vec3(1.0f, 0.2f, 0.2f),  // right (red)
        glm::vec3(0.2f, 1.0f, 0.2f),  // up (green)
        glm::vec3(0.2f, 0.4f, 1.0f),  // forward (blue)
    };

    // Each axis has multiple dash segments (all GL_LINES). Total indices = 3 * N.
    int idxPerAxis = static_cast<int>(mAxesDashed.indices.size()) / 3;
    for (int a = 0; a < 3; ++a) {
        glUniform3fv(mLineColor, 1, glm::value_ptr(kColors[a]));
        glUniform1f(mLineAlpha, 0.6f);
        glDrawElements(GL_LINES, idxPerAxis, GL_UNSIGNED_SHORT,
                       mAxesDashed.indices.data() + a * idxPerAxis);
    }

    glDisableVertexAttribArray(mLinePos);
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

void WayfinderRenderer::Render(const glm::mat4 &view, const glm::mat4 &proj, const glm::mat4 &wayfinderToWorld,
                               const glm::vec3 &cameraPos, const glm::vec3 &color, float animTime, float distance,
                               int huntPhase, const glm::quat &frameOrientation, float frameHueTime, bool isAligned,
                               float deltaTime, float ringHeight, float badgeFadeProgress, float animAge,
                               float clipShiftY)
{
    (void)distance;   // 距离不再驱动渲染分支(被 badgeFadeProgress 取代),保留参数以维持 API
    (void)huntPhase;  // phase 仍在 caller 端用于 frameHueTime/LOCKED 冻结;渲染层不再分支
    if (!mSolidProgram || !mLineProgram) {
        return;
    }
    // animAge < 0 = 信标已放但用户还没看到 → 此帧不画任何东西(信标隐形等候)。一旦用户的相机
    // 视野中扫到信标顶端,caller 把 animAge 切到 0,放置序列才从头演。这样用户绝不会错过动画。
    if (animAge < 0.0f) {
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    // Arrow align transition (0 colorful+spinning -> 1 green+stopped over ~0.3s) + spin angle (3s/rev,
    // slows to a stop as it greens). Advanced every frame; arrow only renders when frameAlpha>0 below.
    float target = isAligned ? 1.0f : 0.0f;
    mAlignedTransition += (target - mAlignedTransition) * (deltaTime / 0.3f);
    mAlignedTransition = glm::clamp(mAlignedTransition, 0.0f, 1.0f);
    float spinSpeed = (kTwoPi / 3.0f) * (1.0f - mAlignedTransition);
    mSpinAngle += spinSpeed * deltaTime;

    // Phase 3 ④ — Frame snap effect: lock progress spring (150ms half-life)。decoupled from
    // mAlignedTransition (arrow) so frame lights up faster than arrow stops spinning.
    mLockProgress += (target - mLockProgress) * (deltaTime / 0.15f);
    mLockProgress = glm::clamp(mLockProgress, 0.0f, 1.0f);

    // Snap FX 庆祝特效:aligned 0→1 边缘触发 0.9s 4 阶段动画 + 60ms 闪白脉冲。
    //   触发条件:aligned 0→1 边缘 且 mSnapAnimAge<0(未在播)→ 启动。
    //   重置条件 1:aligned 1→0 边缘(C→B 角度解锁)→ mSnapAnimAge=-1 让框立刻重现,下次再播。
    //   重置条件 2:badgeFadeProgress<0.05(用户走出 30cm,灰盘回归)→ 同上。
    //   持续条件:0≤mSnapAnimAge<0.9 → +=dt 推进 4 阶段;≥0.9 演完(膜 alpha=0,scale=1)。
    if (isAligned && !mPrevAligned && mSnapAnimAge < 0.0f) {
        mSnapAnimAge = 0.0f;
        mFlashIntensity = 1.0f;
    }
    // ★ C→B 角度解锁重置(2026-06-03):aligned 1→0 边缘 → snap FX 重置,框立刻重现(用户回到 B 重新对)。
    if (!isAligned && mPrevAligned) {
        mSnapAnimAge = -1.0f;
        mFlashIntensity = 0.0f;
    }
    mPrevAligned = isAligned;
    if (mSnapAnimAge >= 0.0f) {
        mSnapAnimAge += deltaTime;
    }
    // 灰盘回归 = 用户走出 30cm,把 snap 状态全部重置(下次进 30cm + aligned 重新播)。
    if (badgeFadeProgress < 0.05f && mSnapAnimAge >= 0.0f) {
        mSnapAnimAge = -1.0f;
        mFlashIntensity = 0.0f;
    }
    // 闪白指数衰减(60ms 时间常数)。
    mFlashIntensity *= std::exp(-deltaTime / 0.06f);
    if (mFlashIntensity < 0.001f) {
        mFlashIntensity = 0.0f;
    }

    const glm::mat4 vp = proj * view;
    const glm::mat4 mvp = vp * wayfinderToWorld;
    // Per-beacon ring height: stretch pillar meshes (built at kWayfinderTopHeight 0.9m) so the
    // glow runs from floor to badge regardless of requested height. Ground ring + badge + frame
    // use unscaled matrices (their positions are explicit).
    float pillarYScale = ringHeight / kWayfinderTopHeight;
    // ★★★ 修(2026-06-03 问题 3 柱子歪)— 显式锁世界垂直:
    //   柱子顶 = badge 世界位置 = anchor + (0, ringHeight, 0)
    //   柱子底 = anchor 世界位置(badge 正下方,沿 worldUp 反向 ringHeight)
    //   只用 translate(pillarBottom) + scaleY,无任何旋转,杜绝任何旋转污染。
    //   wayfinderToWorld 本来就是纯 translate(ringPos),vec3(wayfinderToWorld[3]) = anchor 世界位置。
    glm::vec3 pillarBottomWorld(wayfinderToWorld[3].x, wayfinderToWorld[3].y, wayfinderToWorld[3].z);
    const glm::mat4 mvpPillar = vp
                                * glm::translate(glm::mat4(1.0f), pillarBottomWorld)
                                * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, pillarYScale, 1.0f));

    // ── 放置序列动画 0..1.3s + 稳态(≥1.3s)的 alpha/位置 调度 ──────────────────────────
    //   stage 1 (0..0.3):  灰盘渐入(disk+rim+phone+bloom alpha 0→1)。柱子/水滴/地圈/对齐框不画
    //   stage 2 (0.3..1.0):灰盘满。水滴从 y=ringHeight 直线下落到 y=0(地面)。柱子(雾+芯)同步淡入
    //   stage 3 (1.0..1.3):水滴消失。炫彩地圈淡入(alpha 0→1)+ 涌起(scale 0.5→1.0 类水波涟漪)。柱子满
    //   steady (>=1.3):    炫彩地圈常显;灰盘+柱子 ↔ 对齐框 由 badgeFadeProgress(30cm 距离)双向渐变
    constexpr float kAnimGraceInSec = 0.30f;
    constexpr float kAnimDropSec    = 0.70f;
    constexpr float kAnimRingInSec  = 0.30f;
    constexpr float kStage1End = kAnimGraceInSec;                                  // 0.30
    constexpr float kStage2End = kAnimGraceInSec + kAnimDropSec;                   // 1.00
    constexpr float kAnimTotal = kAnimGraceInSec + kAnimDropSec + kAnimRingInSec;  // 1.30

    float groundAlphaMult = 1.0f;   // 炫彩地圈乘子
    float groundScaleMult = 1.0f;   // 炫彩地圈尺度(stage 3 由 0.5→1.0,稳态恒 1)
    float pillarAlphaMult = 1.0f;   // 柱(雾+芯)乘子
    float badgeAlphaMult = 1.0f;    // 灰盘 group 乘子(disk+rim+phone+bloom)
    float dropAlphaMult = 0.0f;     // 水滴乘子(仅 stage 2 出现)
    float dropY = 0.0f;             // 水滴 y(local space,从 ringHeight 线性落到 0)
    float frameAlphaMult = 0.0f;    // 对齐框 + 3D 箭头乘子

    if (animAge < kStage1End) {
        // Stage 1:灰盘渐入
        float t = (kAnimGraceInSec > 1e-6f) ? (animAge / kAnimGraceInSec) : 1.0f;
        groundAlphaMult = 0.0f;
        pillarAlphaMult = 0.0f;
        badgeAlphaMult = glm::clamp(t, 0.0f, 1.0f);
        dropAlphaMult = 0.0f;
        frameAlphaMult = 0.0f;
    } else if (animAge < kStage2End) {
        // Stage 2:水滴下落 + 柱子渐入
        float t = (animAge - kStage1End) / kAnimDropSec;  // 0→1
        groundAlphaMult = 0.0f;
        pillarAlphaMult = glm::clamp(t, 0.0f, 1.0f);
        badgeAlphaMult = 1.0f;
        dropAlphaMult = 1.0f;
        dropY = ringHeight * (1.0f - glm::clamp(t, 0.0f, 1.0f));  // 顶端 → 地面
        frameAlphaMult = 0.0f;
    } else if (animAge < kAnimTotal) {
        // Stage 3:炫彩地圈涌起
        float t = (animAge - kStage2End) / kAnimRingInSec;  // 0→1
        float tc = glm::clamp(t, 0.0f, 1.0f);
        groundAlphaMult = tc;
        groundScaleMult = glm::mix(0.5f, 1.0f, tc);  // 类水波涟漪从 50% → 100%
        pillarAlphaMult = 1.0f;
        badgeAlphaMult = 1.0f;
        dropAlphaMult = 0.0f;
        frameAlphaMult = 0.0f;
    } else {
        // Steady state:距离驱动 badgeFadeProgress(动画期不介入)
        float ba = 1.0f - badgeFadeProgress;
        groundAlphaMult = 1.0f;
        groundScaleMult = 1.0f;
        pillarAlphaMult = ba;
        badgeAlphaMult = ba;
        frameAlphaMult = badgeFadeProgress;

        // 周期性滴水:稳态 2.5s 循环(1.8s 静默 + 0.7s 下落)。drop alpha 跟 badgeAlphaMult,所以
        // 走近 30cm 灰盘淡出时水滴一起淡出,远离时再恢复滴水。先静默后下落 ── 序列结束后让画面
        // 喘一下再开始第一次重滴,节奏更自然。
        constexpr float kDripCyclePeriodSec = 2.5f;
        constexpr float kDripFallSec = 0.7f;
        constexpr float kDripGapSec = kDripCyclePeriodSec - kDripFallSec;
        float steadyAge = animAge - kAnimTotal;
        float cycleT = std::fmod(steadyAge, kDripCyclePeriodSec);
        if (cycleT >= kDripGapSec) {
            float fallT = (cycleT - kDripGapSec) / kDripFallSec;  // 0→1
            dropAlphaMult = badgeAlphaMult;
            dropY = ringHeight * (1.0f - glm::clamp(fallT, 0.0f, 1.0f));
        } else {
            dropAlphaMult = 0.0f;
        }
    }

    // ── 1. 炫彩地圈 ─────────────────────────────────────────────────────────────────
    // Pearl iridescent flow, circumferential sweep (axis=0). Calm breath 0.7↔1.0 over 1.3s.
    // 动画期 stage 3 用 groundScaleMult 涌起;稳态恒 1.0。
    if (groundAlphaMult > 0.01f) {
        float breathPhase = std::fmod(animTime, 1.3f) / 1.3f;
        float breathAlpha = 0.7f + 0.3f * (0.5f + 0.5f * std::sin(breathPhase * kTwoPi - kHalfPi));
        glm::mat4 groundMvp = vp * wayfinderToWorld
                              * glm::scale(glm::mat4(1.0f), glm::vec3(groundScaleMult, 1.0f, groundScaleMult));
        float ga = 0.85f * breathAlpha * groundAlphaMult;
        DrawFlow(groundMvp, mGround, ga, ga, animTime, 0.0f, 0.25f, true);
    }

    // ── 2. 柱(雾 + 芯)──────────────────────────────────────────────────────────────
    if (pillarAlphaMult > 0.01f) {
        DrawFog(mvpPillar, mFog, color, 0.16f * pillarAlphaMult, animTime);
        DrawFlow(mvpPillar, mCore, 0.60f * pillarAlphaMult, 0.08f * pillarAlphaMult, animTime, 1.0f, 0.25f, true);
    }

    // ── 3. 灰盘 group(disk + rim + phone + bloom)──────────────────────────────────
    if (badgeAlphaMult > 0.01f) {
        glm::vec3 badgeWorldPos = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, ringHeight, 0.0f);
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 toCamHoriz(cameraPos.x - badgeWorldPos.x, 0.0f, cameraPos.z - badgeWorldPos.z);
        float thLen = std::sqrt(toCamHoriz.x * toCamHoriz.x + toCamHoriz.z * toCamHoriz.z);
        if (thLen > 1e-4f) {
            toCamHoriz /= thLen;
        } else {
            toCamHoriz = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        glm::vec3 right = glm::cross(worldUp, toCamHoriz);
        glm::mat4 billboard(1.0f);
        billboard[0] = glm::vec4(right, 0.0f);
        billboard[1] = glm::vec4(worldUp, 0.0f);
        billboard[2] = glm::vec4(toCamHoriz, 0.0f);
        glm::mat4 translate = glm::translate(glm::mat4(1.0f), badgeWorldPos);
        glm::mat4 spin = glm::rotate(glm::mat4(1.0f), glm::radians(animTime * kBadgeSpinDegPerSec), glm::vec3(0, 1, 0));
        const glm::mat4 badgeModel = translate * spin * billboard;
        const glm::mat4 badgeMvp = vp * badgeModel;

        DrawSolid(badgeMvp, mBadgeRingBloom, color, 0.22f * badgeAlphaMult, 0.0f, false);
        DrawSolid(badgeMvp, mBadgeDisk, kBadgeDiskColor, 0.55f * badgeAlphaMult, 0.55f * badgeAlphaMult, true);
        DrawSolid(badgeMvp, mBadgeRing, kBadgeRingColor, 0.90f * badgeAlphaMult, 0.90f * badgeAlphaMult, true);
        DrawLines(badgeMvp, mPhone, kPhoneColor, 1.0f * badgeAlphaMult);
    }

    // ── 4. 水滴 ─────────────────────────────────────────────────────────────────────
    // Stage 2 出现,从 (信标基础位置)+(0, ringHeight, 0) 直线落到 (0,0,0)。pearl 沿 uv.y 流动。
    if (dropAlphaMult > 0.01f) {
        glm::mat4 dropMvp = vp * wayfinderToWorld * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, dropY, 0.0f));
        float da = 0.85f * dropAlphaMult;
        DrawFlow(dropMvp, mWaterDrop, da, da, animTime, 1.0f, 0.40f, true);
    }

    // ── 5. 对齐框 + 3D 箭头 group ──────────────────────────────────────────────────
    // Phase 2(2026-06-02)— 框 = 目标机位 P 前方 50cm 的视野截面:
    //   P            = wayfinderToWorld[3] + (0, ringHeight, 0)            (灰盘/badge 现位置,= 目标机位)
    //   frameNormal  = frameOrientation × (0, 0, -1)                       (目标相机视线方向,世界坐标)
    //   framePos_new = P + frameNormal × 0.50m                             (沿目标视线前方 50cm)
    //   scale        = 4.16                                                (50cm 处 88% 填可见区,同 ring_hunt_ar_application.cpp:kFrameScale)
    // 用户站到 P 且朝向 R(= targetQ)时,framePos_new 正好在用户视线前方 50cm,框 88% 填满取景框 ✓
    // 配 clip-space y shift(clipShiftY = 可见区中心 NDC,典型 +0.218 for Mate 60)→ 框居中可见区,
    // 不再因底黑条遮罩偏下。badge / pillar / ground / drop 不偏移(它们贴 P,即用户对齐的目标位置)。
    if (frameAlphaMult > 0.01f) {
        glm::vec3 P = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, ringHeight, 0.0f);
        glm::vec3 frameNormal = glm::normalize(frameOrientation * glm::vec3(0.0f, 0.0f, -1.0f));
        glm::vec3 framePos = P + frameNormal * 0.50f;
        glm::mat4 frameRot = glm::mat4_cast(frameOrientation);
        // Snap FX(2026-06-03)— 4 阶段庆祝特效时序(mSnapAnimAge 驱动):
        //   A [0.00, 0.15]:膜 alpha 0→0.5 淡入(+ u_flash 60ms 衰减闪白),框 + 膜 scale = 1.0×base
        //   B [0.15, 0.40]:膜维持 alpha=0.5,框 + 膜 scale 1.0→1.10(EaseOut,~97% 填屏)
        //   C [0.40, 0.70]:scale 1.10→1.30(涌出屏幕边),框 + 膜 alpha 1→0.3 同步淡出
        //   D [0.70, 0.90]:scale 1.30→1.50(继续涌出),alpha 0.3→0(收尾消失)
        //   E [≥0.90]:    动画完,膜 alpha=0,框 alpha=0(本轮已"飞出"不再渲染,直到走出 30cm 重置)
        // 时间常量(s):
        constexpr float kSnapA  = 0.15f;
        constexpr float kSnapB  = 0.40f;
        constexpr float kSnapC  = 0.70f;
        constexpr float kSnapTotal = 0.90f;
        const float kBaseScale = 4.16f;
        float snapScaleMul = 1.0f;
        float frameAnimAlpha = 1.0f;
        float membraneAlpha = 0.0f;
        if (mSnapAnimAge < 0.0f) {
            // 未触发 / 已重置 → 正常稳态,无膜。
            snapScaleMul = 1.0f;
            frameAnimAlpha = 1.0f;
            membraneAlpha = 0.0f;
        } else if (mSnapAnimAge < kSnapA) {
            float t = mSnapAnimAge / kSnapA;                // 0→1
            membraneAlpha = 0.50f * t;
            snapScaleMul = 1.0f;
            frameAnimAlpha = 1.0f;
        } else if (mSnapAnimAge < kSnapB) {
            float t = (mSnapAnimAge - kSnapA) / (kSnapB - kSnapA); // 0→1
            float eased = 1.0f - (1.0f - t) * (1.0f - t);    // EaseOut quadratic
            membraneAlpha = 0.50f;
            snapScaleMul = 1.0f + 0.10f * eased;             // 1.00 → 1.10
            frameAnimAlpha = 1.0f;
        } else if (mSnapAnimAge < kSnapC) {
            float t = (mSnapAnimAge - kSnapB) / (kSnapC - kSnapB); // 0→1
            membraneAlpha = 0.50f * (1.0f - 0.4f * t);       // 0.50 → 0.30
            snapScaleMul = 1.10f + 0.20f * t;                // 1.10 → 1.30
            frameAnimAlpha = 1.0f - 0.7f * t;                // 1.00 → 0.30
        } else if (mSnapAnimAge < kSnapTotal) {
            float t = (mSnapAnimAge - kSnapC) / (kSnapTotal - kSnapC); // 0→1
            membraneAlpha = 0.30f * (1.0f - t);              // 0.30 → 0
            snapScaleMul = 1.30f + 0.20f * t;                // 1.30 → 1.50
            frameAnimAlpha = 0.30f * (1.0f - t);             // 0.30 → 0
        } else {
            // 动画演完,本轮"飞出"完毕 — 膜/框都不渲染,直到走出 30cm 重置后下次进 30cm + aligned 重播。
            membraneAlpha = 0.0f;
            snapScaleMul = 1.0f;
            frameAnimAlpha = 0.0f;
        }
        float effectiveScale = kBaseScale * snapScaleMul;
        glm::mat4 frameModel = glm::translate(glm::mat4(1.0f), framePos) * frameRot
                               * glm::scale(glm::mat4(1.0f), glm::vec3(effectiveScale));
        // clip-space y shift: post-multiply MVP so clip.y' = clip.y + clipShiftY × clip.w
        //   → ndc.y' = ndc.y + clipShiftY (将框中心从 FB 中心搬到可见区中心)
        // 矩阵列优先:translate(0, clipShiftY, 0) 即 [3][1] = clipShiftY。
        glm::mat4 clipShift(1.0f);
        clipShift[3][1] = clipShiftY;
        // 先画膜(半透明,不写深度),再画框(写深度,outline 盖在膜上)。
        glm::mat4 frameMvp = clipShift * vp * frameModel;
        DrawMembrane(frameMvp, frameHueTime, membraneAlpha * frameAlphaMult, mLockProgress, mFlashIntensity);
        DrawFrame(frameMvp, mAlignFrame, frameHueTime, frameAlphaMult * frameAnimAlpha,
                  mLockProgress, mFlashIntensity);

        glm::mat4 spinMat = glm::rotate(glm::mat4(1.0f), mSpinAngle, glm::vec3(0.0f, 0.0f, 1.0f));
        // Snap FX 阶段中,箭头跟着框一起淡出(scale 不跟,避免视觉爆炸过头)— 让"框飞出"时箭头同步消失。
        glm::mat4 arrowMvp = clipShift * vp * glm::translate(glm::mat4(1.0f), framePos) * frameRot * spinMat;
        DrawArrow(arrowMvp, animTime, mAlignedTransition, frameAlphaMult * frameAnimAlpha);
    }

    // ── Debug: beacon_world forward + up lines at target position ──────────────
    // Cyan line (30cm) = forward (local -Z, the "look" direction).
    // Green line (15cm) = up (local +Y). Yellow sphere = position marker.
    if (mLineProgram) {
        glm::vec3 origin = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, ringHeight, 0.0f);
        glm::vec3 fwd = frameOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 up  = frameOrientation * glm::vec3(0.0f, 1.0f,  0.0f);
        float lineVerts[] = {
            origin.x, origin.y, origin.z,
            origin.x + fwd.x * 0.3f, origin.y + fwd.y * 0.3f, origin.z + fwd.z * 0.3f,
            origin.x, origin.y, origin.z,
            origin.x + up.x * 0.15f, origin.y + up.y * 0.15f, origin.z + up.z * 0.15f,
        };
        glUseProgram(mLineProgram);
        glDepthFunc(GL_ALWAYS);
        glLineWidth(5.0f);
        glEnableVertexAttribArray(mLinePos);
        glVertexAttribPointer(mLinePos, 3, GL_FLOAT, GL_FALSE, 0, lineVerts);
        glUniformMatrix4fv(mLineMvp, 1, GL_FALSE, glm::value_ptr(vp));
        glUniform3f(mLineColor, 0.0f, 1.0f, 1.0f);
        glUniform1f(mLineAlpha, 1.0f);
        glDrawArrays(GL_LINES, 0, 2);
        glUniform3f(mLineColor, 0.2f, 1.0f, 0.2f);
        glDrawArrays(GL_LINES, 2, 2);
        glDisableVertexAttribArray(mLinePos);
        glDepthFunc(GL_LESS);
    }
    if (!mDebugSphere.indices.empty()) {
        glm::vec3 spherePos = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, ringHeight, 0.0f);
        glm::mat4 sphereMvp = vp * glm::translate(glm::mat4(1.0f), spherePos);
        const glm::vec3 kYellow(1.0f, 0.85f, 0.05f);
        DrawSolid(sphereMvp, mDebugSphere, kYellow, 1.0f, 1.0f, true);
    }

    glDepthMask(GL_TRUE);
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

} // namespace ARObject
