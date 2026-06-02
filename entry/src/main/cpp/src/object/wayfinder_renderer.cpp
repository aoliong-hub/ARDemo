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
        vec3 col = pearl(v_uv.x + u_time * 0.15);
        gl_FragColor = vec4(col, 0.85 * u_alphaMult);
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

    mGround = WayfinderGeometry::CreateGroundRing();
    mCore = WayfinderGeometry::CreatePillarCore();
    mFog = WayfinderGeometry::CreatePillarFog();
    mPillarBloom = WayfinderGeometry::CreatePillarBloom();
    mBadgeRing = WayfinderGeometry::CreateTopBadgeRing();
    mBadgeRingBloom = WayfinderGeometry::CreateBadgeRingBloom();
    mBadgeDisk = WayfinderGeometry::CreateTopBadgeDisk();
    mPhone = WayfinderGeometry::CreatePhoneIconRounded();
    // Stage 12C-batch3 polish: thickness halved (0.005 → 0.0025) for a finer hairline. Frame is
    // scaled +20% on the render side (frameModel * scale(1.2)); together that nets to "thinner
    // line AND a slightly bigger frame".
    mAlignFrame = WayfinderGeometry::CreateAlignmentFrame(0.075f, 0.15f, 0.0025f, 0.005f, 8);
    mArrow = WayfinderGeometry::CreateArrow3D();
    // 放置序列动画用的水滴(默认 2.5cm 半径 = 5cm 直径)。FLOW shader 沿 uv.y 走 pearl 渐变。
    mWaterDrop = WayfinderGeometry::CreateWaterDrop();
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

void WayfinderRenderer::DrawFrame(const glm::mat4 &mvp, const WayfinderMesh &mesh, float hueTime, float alphaMult)
{
    if (mesh.indices.empty() || !mFrameProgram) {
        return;
    }
    glDepthMask(GL_TRUE); // write depth so the frame border occludes the co-planar disk from behind
    glUseProgram(mFrameProgram);
    glUniformMatrix4fv(mFrameMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(mFrameTime, hueTime);
    glUniform1f(mFrameAlphaMult, alphaMult);
    glEnableVertexAttribArray(mFramePos);
    glEnableVertexAttribArray(mFrameUv);
    glVertexAttribPointer(mFramePos, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
    glVertexAttribPointer(mFrameUv, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, mesh.indices.data());
    glDisableVertexAttribArray(mFramePos);
    glDisableVertexAttribArray(mFrameUv);
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

void WayfinderRenderer::Render(const glm::mat4 &view, const glm::mat4 &proj, const glm::mat4 &wayfinderToWorld,
                               const glm::vec3 &cameraPos, const glm::vec3 &color, float animTime, float distance,
                               int huntPhase, const glm::quat &frameOrientation, float frameHueTime, bool isAligned,
                               float deltaTime, float ringHeight, float badgeFadeProgress, float animAge)
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

    const glm::mat4 vp = proj * view;
    const glm::mat4 mvp = vp * wayfinderToWorld;
    // Per-beacon ring height: stretch pillar meshes (built at kWayfinderTopHeight 0.9m) so the
    // glow runs from floor to badge regardless of requested height. Ground ring + badge + frame
    // use unscaled matrices (their positions are explicit).
    float pillarYScale = ringHeight / kWayfinderTopHeight;
    const glm::mat4 mvpPillar = vp * wayfinderToWorld * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, pillarYScale, 1.0f));

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
    if (frameAlphaMult > 0.01f) {
        glm::vec3 framePos = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, ringHeight, 0.0f);
        glm::mat4 frameRot = glm::mat4_cast(frameOrientation);
        glm::mat4 frameModel = glm::translate(glm::mat4(1.0f), framePos) * frameRot
                               * glm::scale(glm::mat4(1.0f), glm::vec3(1.2f));
        DrawFrame(vp * frameModel, mAlignFrame, frameHueTime, frameAlphaMult);

        glm::mat4 spinMat = glm::rotate(glm::mat4(1.0f), mSpinAngle, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 arrowMvp = vp * glm::translate(glm::mat4(1.0f), framePos) * frameRot * spinMat;
        DrawArrow(arrowMvp, animTime, mAlignedTransition, frameAlphaMult);
    }

    glDepthMask(GL_TRUE);
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

} // namespace ARObject
