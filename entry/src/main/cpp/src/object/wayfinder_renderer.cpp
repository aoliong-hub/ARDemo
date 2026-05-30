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
        gl_FragColor = vec4(col, 0.85);
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
        gl_FragColor = vec4(finalColor, 1.0);
    }
)";

const glm::vec3 kBadgeRingColor(0.70f, 0.70f, 0.74f); // gray rim
const glm::vec3 kBadgeDiskColor(0.45f, 0.45f, 0.48f); // darker semi-transparent medallion
const glm::vec3 kPhoneColor(1.0f, 1.0f, 1.0f);        // white wireframe
constexpr float kBadgeSpinDegPerSec = 90.0f;          // badge rotation: 4s per revolution
constexpr int kRippleCount = 3;                       // staggered expanding ground waves
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

void WayfinderRenderer::DrawFrame(const glm::mat4 &mvp, const WayfinderMesh &mesh, float hueTime)
{
    if (mesh.indices.empty() || !mFrameProgram) {
        return;
    }
    glDepthMask(GL_TRUE); // write depth so the frame border occludes the co-planar disk from behind
    glUseProgram(mFrameProgram);
    glUniformMatrix4fv(mFrameMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(mFrameTime, hueTime);
    glEnableVertexAttribArray(mFramePos);
    glEnableVertexAttribArray(mFrameUv);
    glVertexAttribPointer(mFramePos, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
    glVertexAttribPointer(mFrameUv, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, mesh.indices.data());
    glDisableVertexAttribArray(mFramePos);
    glDisableVertexAttribArray(mFrameUv);
}

void WayfinderRenderer::DrawArrow(const glm::mat4 &mvp, float hueTime, float aligned)
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
                               float deltaTime, float ringHeight)
{
    if (!mSolidProgram || !mLineProgram) {
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    // Arrow align transition (0 colorful+spinning -> 1 green+stopped over ~0.3s) + spin angle (3s/rev,
    // slows to a stop as it greens). Advanced every frame; only drawn in ALIGNING/LOCKED below.
    float target = isAligned ? 1.0f : 0.0f;
    mAlignedTransition += (target - mAlignedTransition) * (deltaTime / 0.3f);
    mAlignedTransition = glm::clamp(mAlignedTransition, 0.0f, 1.0f);
    float spinSpeed = (kTwoPi / 3.0f) * (1.0f - mAlignedTransition);
    mSpinAngle += spinSpeed * deltaTime;

    const glm::mat4 vp = proj * view;
    const glm::mat4 mvp = vp * wayfinderToWorld;
    // Stage 12C: per-beacon ring height. The pillar meshes (core/fog/bloom) were built with a
    // fixed kWayfinderTopHeight (0.9m); apply a Y-scale = ringHeight / 0.9 to stretch them so the
    // glow runs from the floor up to the badge regardless of the requested height. Ground ring,
    // ripples, badge, frame all use unscaled matrices (their positions are explicit anyway).
    float pillarYScale = ringHeight / kWayfinderTopHeight;
    const glm::mat4 mvpPillar = vp * wayfinderToWorld * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, pillarYScale, 1.0f));

    // ALIGNING (1) / LOCKED (2): hide the pillar/badge/ripple; show only the ground ring (with a
    // stronger, faster breath) + the 6DoF alignment frame at the beacon top. The frame's hue is
    // driven by frameHueTime, which the caller freezes once LOCKED.
    if (huntPhase != 0) {
        float bPhase = std::fmod(animTime, 0.8f) / 0.8f;
        float bAlpha = 0.5f + 0.5f * (0.5f + 0.5f * std::sin(bPhase * kTwoPi - kHalfPi)); // 0.5..1.0
        DrawSolid(mvp, mGround, color, 0.85f * bAlpha, 0.85f * bAlpha, true);

        glm::vec3 framePos = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, ringHeight, 0.0f);
        glm::mat4 frameRot = glm::mat4_cast(frameOrientation);
        // Stage 12C-batch3: scale the alignment frame by 1.2× on the model matrix (the arrow
        // stays unscaled). Combined with the halved mesh thickness, the on-screen line is finer
        // and the frame is slightly bigger.
        glm::mat4 frameModel = glm::translate(glm::mat4(1.0f), framePos) * frameRot
                               * glm::scale(glm::mat4(1.0f), glm::vec3(1.2f));
        DrawFrame(vp * frameModel, mAlignFrame, frameHueTime);

        // 3D arrow protruding from the frame center along its facing normal (frameRot * -Z, baked
        // into the geometry). It points the same way regardless of the spin; the spin only rotates
        // the wings about the shaft (its own ±Z axis). Iridescent + spinning when unaligned; eases to
        // solid green + still as you align.
        glm::mat4 spinMat = glm::rotate(glm::mat4(1.0f), mSpinAngle, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 arrowMvp = vp * glm::translate(glm::mat4(1.0f), framePos) * frameRot * spinMat;
        DrawArrow(arrowMvp, animTime, mAlignedTransition);

        glDepthMask(GL_TRUE);
        glUseProgram(0);
        GLUtils::CheckError(__FILE_NAME__, __LINE__);
        return;
    }

    // 1. ground ring (writes depth) — pearl iridescent flow swept circumferentially (axis=0). Breath
    // alpha still pulses 0.7↔1.0 over 1.3s. Per-distance red/mint tint dropped: the pearl spectrum
    // carries the visual now.
    float breathPhase = std::fmod(animTime, 1.3f) / 1.3f;
    float breathAlpha = 0.7f + 0.3f * (0.5f + 0.5f * std::sin(breathPhase * kTwoPi - kHalfPi));
    DrawFlow(mvp, mGround, 0.85f * breathAlpha, 0.85f * breathAlpha, animTime, 0.0f, 0.25f, true);

    // 2. ground ripple: expanding "water waves" on the floor that draw the eye from a DISTANCE.
    // Three phase-staggered rings (50->100cm radius over 1.5s), fading as they expand. No depth
    // write so they blend over the ground/background. Strength grows with distance (the beacon is
    // already obvious up close): off within 0.5m, ramps 0.5->2m, constant 0.7 beyond.
    float strength = glm::clamp((distance - 0.5f) / 1.5f, 0.0f, 0.7f);
    if (strength > 0.01f) {
        for (int k = 0; k < kRippleCount; ++k) {
            float phase = std::fmod(animTime / 1.5f + static_cast<float>(k) / kRippleCount, 1.0f);
            float inner = 0.50f + phase * 0.50f;
            float outer = inner + 0.03f;
            float a = strength * (1.0f - phase); // fade as the wave expands outward
            WayfinderMesh ripple = WayfinderGeometry::CreateRippleRing(inner, outer);
            DrawSolid(mvp, ripple, color, a, a * 0.25f, false);
        }
    }

    // 3. (pillar bloom halo removed per Stage 12C-batch3 brief — no external glow on pillar.)

    // 4. pillar fog: volumetric noise scrolling upward, now colored by pearl(uv.y + time) inside
    // the FOG_FS itself (u_color is ignored). DrawFog still passes color for API stability.
    DrawFog(mvpPillar, mFog, color, 0.16f, animTime);

    // 5. pillar core: pearl iridescent flow swept vertically (axis=1). Bright at base, vaporizes
    // toward the top via per-vertex alpha gradient (alphaBase 0.60 → alphaTop 0.08).
    DrawFlow(mvpPillar, mCore, 0.60f, 0.08f, animTime, 1.0f, 0.25f, true);

    // 6. top badge: bloom + disk + rim + phone icon. Hybrid orientation:
    //   - Y-axis (cylindrical) billboard frame: local +Y locked to worldUp → badge ALWAYS vertical
    //     (no flipping flat when camera looks down). Local +Z initially points horizontally toward
    //     the camera so the front face starts facing the user.
    //   - World-Y spin (4s/revolution): rotates the whole frame around the vertical axis. Because
    //     spin is applied AFTER billboard in the model matrix, it overrides the "face camera"
    //     property — at 90°/270° you see the badge's edge, at 180° its back. Intentional: the spin
    //     reads as "active beacon", trading the always-facing contract for visible motion.
    glm::vec3 badgeWorldPos = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, ringHeight, 0.0f);
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 toCamHoriz(cameraPos.x - badgeWorldPos.x, 0.0f, cameraPos.z - badgeWorldPos.z);
    float thLen = std::sqrt(toCamHoriz.x * toCamHoriz.x + toCamHoriz.z * toCamHoriz.z);
    if (thLen > 1e-4f) {
        toCamHoriz /= thLen;
    } else {
        // Camera directly above/below the badge — horizontal direction degenerate. Hold a fixed
        // orientation (badge front along world +Z) so the spin axis stays stable.
        toCamHoriz = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    glm::vec3 right = glm::cross(worldUp, toCamHoriz); // already unit since worldUp ⊥ toCamHoriz
    glm::mat4 billboard(1.0f); // pure rotation: initial "front faces camera horizontally" frame
    billboard[0] = glm::vec4(right, 0.0f);
    billboard[1] = glm::vec4(worldUp, 0.0f);
    billboard[2] = glm::vec4(toCamHoriz, 0.0f);
    glm::mat4 translate = glm::translate(glm::mat4(1.0f), badgeWorldPos);
    glm::mat4 spin = glm::rotate(glm::mat4(1.0f), glm::radians(animTime * kBadgeSpinDegPerSec), glm::vec3(0, 1, 0));
    const glm::mat4 badgeModel = translate * spin * billboard; // vertical signpost spinning around its Y axis
    const glm::mat4 badgeMvp = vp * badgeModel;

    // badge ring bloom (faint colored halo around the rim; no depth write)
    DrawSolid(badgeMvp, mBadgeRingBloom, color, 0.22f, 0.0f, false);
    // disk background (semi-transparent gray, writes depth so the rim/phone sit cleanly on it)
    DrawSolid(badgeMvp, mBadgeDisk, kBadgeDiskColor, 0.55f, 0.55f, true);
    // rim (gray) — rotates together with the disk + phone via the shared badgeMvp
    DrawSolid(badgeMvp, mBadgeRing, kBadgeRingColor, 0.90f, 0.90f, true);
    // phone wireframe (white lines), co-planar with the badge
    DrawLines(badgeMvp, mPhone, kPhoneColor, 1.0f);

    glDepthMask(GL_TRUE); // restore for the next frame's clear / other renderers
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

} // namespace ARObject
