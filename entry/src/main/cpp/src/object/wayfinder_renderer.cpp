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
    void main() {
        float verticalFade = v_uv.y; // 0 bottom -> 1 top (top more transparent)
        vec2 nuv = vec2(v_uv.x * 4.0, v_uv.y * 2.0 - u_time * 0.3);
        float n = noise(nuv) * 0.7 + 0.3;
        vec2 nuv2 = vec2(v_uv.x * 8.0, v_uv.y * 4.0 - u_time * 0.15);
        float n2 = noise(nuv2);
        n = mix(n, n2, 0.4);
        float alpha = u_alphaBase * (1.0 - verticalFade) * n;
        gl_FragColor = vec4(u_color, alpha * 0.85);
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
    vec3 hueRotate(vec3 col, float angle) {
        float c = cos(angle);
        float s = sin(angle);
        vec3 r = vec3(0.299 + 0.701 * c + 0.168 * s, 0.587 - 0.587 * c + 0.330 * s, 0.114 - 0.114 * c - 0.497 * s);
        vec3 g = vec3(0.299 - 0.299 * c - 0.328 * s, 0.587 + 0.413 * c + 0.035 * s, 0.114 - 0.114 * c + 0.292 * s);
        vec3 b = vec3(0.299 - 0.300 * c + 1.250 * s, 0.587 - 0.588 * c - 1.050 * s, 0.114 + 0.886 * c - 0.203 * s);
        return vec3(dot(r, col), dot(g, col), dot(b, col));
    }
    void main() {
        vec3 pink = vec3(0.95, 0.3, 0.7);
        vec3 purple = vec3(0.5, 0.3, 0.95);
        vec3 blue = vec3(0.3, 0.6, 0.95);
        float x = v_uv.x;
        vec3 base = (x < 0.5) ? mix(pink, purple, x * 2.0) : mix(purple, blue, (x - 0.5) * 2.0);
        vec3 col = clamp(hueRotate(base, u_time * 0.7853982), 0.0, 1.0); // 8s per full hue cycle
        gl_FragColor = vec4(col, 0.85);
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

    mGround = WayfinderGeometry::CreateGroundRing();
    mCore = WayfinderGeometry::CreatePillarCore();
    mFog = WayfinderGeometry::CreatePillarFog();
    mPillarBloom = WayfinderGeometry::CreatePillarBloom();
    mBadgeRing = WayfinderGeometry::CreateTopBadgeRing();
    mBadgeRingBloom = WayfinderGeometry::CreateBadgeRingBloom();
    mBadgeDisk = WayfinderGeometry::CreateTopBadgeDisk();
    mPhone = WayfinderGeometry::CreatePhoneIconRounded();
    mAlignFrame = WayfinderGeometry::CreateAlignmentFrame();
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
    glDepthMask(GL_FALSE);
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

void WayfinderRenderer::Render(const glm::mat4 &view, const glm::mat4 &proj, const glm::mat4 &wayfinderToWorld,
                               const glm::vec3 &cameraPos, const glm::vec3 &color, float animTime, float distance,
                               int huntPhase, const glm::quat &frameOrientation, float frameHueTime)
{
    if (!mSolidProgram || !mLineProgram) {
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    const glm::mat4 vp = proj * view;
    const glm::mat4 mvp = vp * wayfinderToWorld;

    // ALIGNING (1) / LOCKED (2): hide the pillar/badge/ripple; show only the ground ring (with a
    // stronger, faster breath) + the 6DoF alignment frame at the beacon top. The frame's hue is
    // driven by frameHueTime, which the caller freezes once LOCKED.
    if (huntPhase != 0) {
        float bPhase = std::fmod(animTime, 0.8f) / 0.8f;
        float bAlpha = 0.5f + 0.5f * (0.5f + 0.5f * std::sin(bPhase * kTwoPi - kHalfPi)); // 0.5..1.0
        DrawSolid(mvp, mGround, color, 0.85f * bAlpha, 0.85f * bAlpha, true);
        glm::vec3 framePos = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, kWayfinderTopHeight, 0.0f);
        glm::mat4 frameModel = glm::translate(glm::mat4(1.0f), framePos) * glm::mat4_cast(frameOrientation);
        DrawFrame(vp * frameModel, mAlignFrame, frameHueTime);
        glDepthMask(GL_TRUE);
        glUseProgram(0);
        GLUtils::CheckError(__FILE_NAME__, __LINE__);
        return;
    }

    // 1. ground ring (writes depth) — breathes alpha 0.7<->1.0 over 1.3s.
    float breathPhase = std::fmod(animTime, 1.3f) / 1.3f;
    float breathAlpha = 0.7f + 0.3f * (0.5f + 0.5f * std::sin(breathPhase * kTwoPi - kHalfPi));
    DrawSolid(mvp, mGround, color, 0.85f * breathAlpha, 0.85f * breathAlpha, true);

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

    // 3. pillar bloom: wide, faint smooth glow halo OUTSIDE the fog (no noise, no depth write).
    DrawSolid(mvp, mPillarBloom, color, 0.05f, 0.0f, false);

    // 4. pillar fog: volumetric noise scrolling upward (no depth write).
    DrawFog(mvp, mFog, color, 0.16f, animTime);

    // 5. pillar core: thin "laser line" (bright at base, vaporizes toward the top; writes depth).
    DrawSolid(mvp, mCore, color, 0.60f, 0.08f, true);

    // 6. top badge: a camera-facing medallion at the pillar top (bloom + disk + rim + phone icon)
    // that revolves about the vertical pillar axis like a carousel. The billboard frame (local +Z
    // toward the camera) is built first, then the world-Y spin is applied in WORLD space (before the
    // billboard, after the translate) so the badge orbits the vertical axis rather than flipping.
    glm::vec3 badgeWorldPos = glm::vec3(wayfinderToWorld[3]) + glm::vec3(0.0f, kWayfinderTopHeight, 0.0f);
    glm::vec3 toCamera = cameraPos - badgeWorldPos;
    float tcLen = glm::length(toCamera);
    toCamera = (tcLen > 1e-5f) ? (toCamera / tcLen) : glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(worldUp, toCamera);
    float rLen = glm::length(right);
    right = (rLen > 1e-4f) ? (right / rLen) : glm::vec3(1.0f, 0.0f, 0.0f); // camera directly above/below
    glm::vec3 newUp = glm::cross(toCamera, right);
    glm::mat4 billboard(1.0f); // pure rotation (no translation): orients the badge to face the camera
    billboard[0] = glm::vec4(right, 0.0f);
    billboard[1] = glm::vec4(newUp, 0.0f);
    billboard[2] = glm::vec4(toCamera, 0.0f);
    glm::mat4 translate = glm::translate(glm::mat4(1.0f), badgeWorldPos);
    glm::mat4 spin = glm::rotate(glm::mat4(1.0f), glm::radians(animTime * kBadgeSpinDegPerSec), glm::vec3(0, 1, 0));
    const glm::mat4 badgeModel = translate * spin * billboard; // orbit the vertical (world Y) axis
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
