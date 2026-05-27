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
constexpr char SOLID_FS[] = R"(
    precision mediump float;
    varying float v_h;
    uniform vec3 u_color;
    uniform float u_alphaBase;
    uniform float u_alphaTop;
    void main() {
        float a = mix(u_alphaBase, u_alphaTop, v_h);
        gl_FragColor = vec4(u_color, a);
    }
)";

// Line program: flat color for the phone wireframe.
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

const glm::vec3 kBadgeRingColor(0.70f, 0.70f, 0.74f); // gray rim
const glm::vec3 kBadgeDiskColor(0.45f, 0.45f, 0.48f); // darker semi-transparent medallion
const glm::vec3 kPhoneColor(1.0f, 1.0f, 1.0f);        // white wireframe
constexpr float kBadgeSpinDegPerSec = 90.0f;          // badge rotation: 4s per revolution
constexpr int kRippleCount = 3;                       // staggered expanding ground waves
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

    mGround = WayfinderGeometry::CreateGroundRing();
    mCore = WayfinderGeometry::CreatePillarCore();
    mFog = WayfinderGeometry::CreatePillarFog();
    mBadgeRing = WayfinderGeometry::CreateTopBadgeRing();
    mBadgeDisk = WayfinderGeometry::CreateTopBadgeDisk();
    mPhone = WayfinderGeometry::CreatePhoneIcon();
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

void WayfinderRenderer::Render(const glm::mat4 &view, const glm::mat4 &proj, const glm::mat4 &wayfinderToWorld,
                               const glm::vec3 &cameraPos, const glm::vec3 &color, float animTime, float distance)
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

    // 1. ground ring (opaque-ish, writes depth)
    DrawSolid(mvp, mGround, color, 0.85f, 0.85f, true);

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

    // 3. pillar core (bright at base, vaporizes toward the top; writes depth)
    DrawSolid(mvp, mCore, color, 0.60f, 0.08f, true);

    // 4. top badge: a camera-facing medallion at the pillar top (disk + rim + phone icon) that
    // revolves about the vertical pillar axis like a carousel. The billboard frame (local +Z toward
    // the camera) is built first, then the world-Y spin is applied in WORLD space (before the
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

    // disk background (semi-transparent gray, writes depth so the rim/phone sit cleanly on it)
    DrawSolid(badgeMvp, mBadgeDisk, kBadgeDiskColor, 0.55f, 0.55f, true);
    // rim (gray) — rotates together with the disk + phone via the shared badgeMvp
    DrawSolid(badgeMvp, mBadgeRing, kBadgeRingColor, 0.90f, 0.90f, true);
    // phone wireframe (white lines), co-planar with the badge
    DrawLines(badgeMvp, mPhone, kPhoneColor, 1.0f);

    // 5. fog beam last (very soft, does NOT write depth so it never occludes the core)
    DrawSolid(mvp, mFog, color, 0.16f, 0.0f, false);

    glDepthMask(GL_TRUE); // restore for the next frame's clear / other renderers
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

} // namespace ARObject
