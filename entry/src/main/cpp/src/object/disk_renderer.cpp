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

#include "disk_renderer.h"
#include "disk_geometry.h"
#include "graphic/GLUtils.h"
#include "utils/log.h"
#include <gtc/type_ptr.hpp>

namespace ARObject {
namespace {
constexpr float kGlowStrength = 2.0f;

constexpr char VERTEX_SHADER[] = R"(
    uniform mat4 u_ModelViewProjection;
    attribute vec4 a_Position;
    attribute vec2 a_TexCoord;
    varying vec2 v_uv;
    void main() {
        v_uv = a_TexCoord;
        gl_Position = u_ModelViewProjection * a_Position;
    }
)";

constexpr char FRAGMENT_SHADER[] = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform vec3 u_diskColor;
    uniform float u_glowStrength;
    void main() {
        vec2 centered = v_uv - vec2(0.5);
        float distFromCenter = length(centered) * 2.0; // 0 center, 1 edge
        float alpha = 1.0 - smoothstep(0.0, 1.0, distFromCenter);
        float brightness = (1.0 - distFromCenter * 0.5) * u_glowStrength;
        gl_FragColor = vec4(u_diskColor * brightness, alpha * 0.8);
    }
)";
} // namespace

void DiskRenderer::InitializeGlContent()
{
    mShaderProgram = GLUtils::CreateProgram(VERTEX_SHADER, FRAGMENT_SHADER);
    if (!mShaderProgram) {
        LOGE("DiskRenderer: could not create program.");
        return;
    }
    mUniformMvp = glGetUniformLocation(mShaderProgram, "u_ModelViewProjection");
    mUniformColor = glGetUniformLocation(mShaderProgram, "u_diskColor");
    mUniformGlowStrength = glGetUniformLocation(mShaderProgram, "u_glowStrength");
    mAttribPosition = glGetAttribLocation(mShaderProgram, "a_Position");
    mAttribUv = glGetAttribLocation(mShaderProgram, "a_TexCoord");

    std::vector<GLfloat> normals;
    GenerateDiskQuad(mVertices, normals, mUvs, mIndices);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

void DiskRenderer::Release()
{
    mVertices.clear();
    mUvs.clear();
    mIndices.clear();
    if (mShaderProgram) {
        GLUtils::ReleaseProgram(mShaderProgram);
        mShaderProgram = 0;
    }
}

void DiskRenderer::Draw(const glm::mat4 &projectionMat, const glm::mat4 &viewMat, const glm::mat4 &modelMat,
                        const float *color3) const
{
    if (!mShaderProgram || mVertices.empty()) {
        return;
    }
    // Translucent additive-ish glow; don't write depth so it never occludes the ring.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glUseProgram(mShaderProgram);
    glm::mat4 mvpMat = projectionMat * viewMat * modelMat;
    glUniformMatrix4fv(mUniformMvp, 1, GL_FALSE, glm::value_ptr(mvpMat));
    glUniform3fv(mUniformColor, 1, color3);
    glUniform1f(mUniformGlowStrength, kGlowStrength);

    glEnableVertexAttribArray(mAttribPosition);
    glVertexAttribPointer(mAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, mVertices.data());
    glEnableVertexAttribArray(mAttribUv);
    glVertexAttribPointer(mAttribUv, 2, GL_FLOAT, GL_FALSE, 0, mUvs.data());
    glDrawElements(GL_TRIANGLES, mIndices.size(), GL_UNSIGNED_SHORT, mIndices.data());

    glDisableVertexAttribArray(mAttribPosition);
    glDisableVertexAttribArray(mAttribUv);
    glUseProgram(0);

    // Restore default state.
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

} // namespace ARObject
