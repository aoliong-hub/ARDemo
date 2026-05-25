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

#include "ring_renderer.h"
#include "arrow_geometry.h"
#include "graphic/GLUtils.h"
#include "ring_geometry.h"
#include "utils/log.h"
#include <gtc/type_ptr.hpp>

namespace ARObject {
namespace {
// Tuned on-device (Stage 9): opaque + boosted base (vivid neon body, colors preserved) + fresnel
// rim highlight. Additive blending was rejected (washed everything to white).
constexpr float kGlowPower = 1.0f;
constexpr float kGlowIntensity = 2.5f;
constexpr float kBaseBoost = 1.6f;

constexpr char VERTEX_SHADER[] = R"(
    uniform mat4 u_ModelViewProjection;
    uniform mat4 u_Model;
    attribute vec4 a_Position;
    attribute vec3 a_Normal;
    varying vec3 v_worldPos;
    varying vec3 v_worldNormal;
    void main() {
        v_worldPos = (u_Model * a_Position).xyz;
        v_worldNormal = normalize(mat3(u_Model) * a_Normal);
        gl_Position = u_ModelViewProjection * a_Position;
    }
)";

// Fresnel edge glow: brighter where the surface faces away from the camera (grazing angle).
constexpr char FRAGMENT_SHADER[] = R"(
    precision mediump float;
    varying vec3 v_worldPos;
    varying vec3 v_worldNormal;
    uniform vec3 u_cameraPos;
    uniform vec3 u_baseColor;
    uniform vec3 u_glowColor;
    uniform float u_glowPower;
    uniform float u_glowIntensity;
    uniform float u_baseBoost;
    void main() {
        vec3 N = normalize(v_worldNormal);
        vec3 V = normalize(u_cameraPos - v_worldPos);
        float fresnel = pow(1.0 - max(0.0, dot(N, V)), u_glowPower);
        vec3 finalColor = u_baseColor * u_baseBoost + fresnel * u_glowColor * u_glowIntensity;
        gl_FragColor = vec4(finalColor, 1.0);
    }
)";
} // namespace

void RingRenderer::InitializeGlContent()
{
    mShaderProgram = GLUtils::CreateProgram(VERTEX_SHADER, FRAGMENT_SHADER);
    if (!mShaderProgram) {
        LOGE("RingRenderer: could not create program.");
        return;
    }
    mUniformMvp = glGetUniformLocation(mShaderProgram, "u_ModelViewProjection");
    mUniformModel = glGetUniformLocation(mShaderProgram, "u_Model");
    mUniformCameraPos = glGetUniformLocation(mShaderProgram, "u_cameraPos");
    mUniformBaseColor = glGetUniformLocation(mShaderProgram, "u_baseColor");
    mUniformGlowColor = glGetUniformLocation(mShaderProgram, "u_glowColor");
    mUniformGlowPower = glGetUniformLocation(mShaderProgram, "u_glowPower");
    mUniformGlowIntensity = glGetUniformLocation(mShaderProgram, "u_glowIntensity");
    mUniformBaseBoost = glGetUniformLocation(mShaderProgram, "u_baseBoost");
    mAttribPosition = glGetAttribLocation(mShaderProgram, "a_Position");
    mAttribNormal = glGetAttribLocation(mShaderProgram, "a_Normal");

    GenerateRingMesh(mRingVertices, mRingNormals, mRingIndices);
    GenerateSmallArrow(mArrowVertices, mArrowNormals, mArrowIndices);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

void RingRenderer::Release()
{
    mRingVertices.clear();
    mRingNormals.clear();
    mRingIndices.clear();
    mArrowVertices.clear();
    mArrowNormals.clear();
    mArrowIndices.clear();
    if (mShaderProgram) {
        GLUtils::ReleaseProgram(mShaderProgram);
        mShaderProgram = 0;
    }
}

void RingRenderer::Draw(const glm::mat4 &projectionMat, const glm::mat4 &viewMat, const glm::mat4 &modelMat,
                        const float *cameraPos3, const float *ringBase3, const float *ringGlow3,
                        const float *arrowBase3, const float *arrowGlow3) const
{
    if (!mShaderProgram || mRingVertices.empty()) {
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    glUseProgram(mShaderProgram);
    glm::mat4 mvpMat = projectionMat * viewMat * modelMat;
    glUniformMatrix4fv(mUniformMvp, 1, GL_FALSE, glm::value_ptr(mvpMat));
    glUniformMatrix4fv(mUniformModel, 1, GL_FALSE, glm::value_ptr(modelMat));
    glUniform3fv(mUniformCameraPos, 1, cameraPos3);
    glUniform1f(mUniformGlowPower, kGlowPower);
    glUniform1f(mUniformGlowIntensity, kGlowIntensity);
    glUniform1f(mUniformBaseBoost, kBaseBoost);

    glEnableVertexAttribArray(mAttribPosition);
    glEnableVertexAttribArray(mAttribNormal);

    // Ring (distance feedback color + glow).
    glUniform3fv(mUniformBaseColor, 1, ringBase3);
    glUniform3fv(mUniformGlowColor, 1, ringGlow3);
    glVertexAttribPointer(mAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, mRingVertices.data());
    glVertexAttribPointer(mAttribNormal, 3, GL_FLOAT, GL_FALSE, 0, mRingNormals.data());
    glDrawElements(GL_TRIANGLES, mRingIndices.size(), GL_UNSIGNED_SHORT, mRingIndices.data());

    // Center indicator arrow (angle feedback color + glow).
    glUniform3fv(mUniformBaseColor, 1, arrowBase3);
    glUniform3fv(mUniformGlowColor, 1, arrowGlow3);
    glVertexAttribPointer(mAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, mArrowVertices.data());
    glVertexAttribPointer(mAttribNormal, 3, GL_FLOAT, GL_FALSE, 0, mArrowNormals.data());
    glDrawElements(GL_TRIANGLES, mArrowIndices.size(), GL_UNSIGNED_SHORT, mArrowIndices.data());

    glDisableVertexAttribArray(mAttribPosition);
    glDisableVertexAttribArray(mAttribNormal);
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

} // namespace ARObject
