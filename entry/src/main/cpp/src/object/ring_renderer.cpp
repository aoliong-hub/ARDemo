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
constexpr char VERTEX_SHADER[] = R"(
    uniform mat4 u_ModelViewProjection;
    attribute vec4 a_Position;
    void main() {
        gl_Position = u_ModelViewProjection * a_Position;
    }
)";

// Emissive: flat color, no lighting (the ring "glows").
constexpr char FRAGMENT_SHADER[] = R"(
    precision mediump float;
    uniform vec4 u_Color;
    void main() {
        gl_FragColor = u_Color;
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
    mUniformColor = glGetUniformLocation(mShaderProgram, "u_Color");
    mAttribPosition = glGetAttribLocation(mShaderProgram, "a_Position");

    std::vector<GLfloat> ringNormals;
    std::vector<GLfloat> arrowNormals;
    GenerateRingMesh(mRingVertices, ringNormals, mRingIndices);
    GenerateSmallArrow(mArrowVertices, arrowNormals, mArrowIndices);
    LOGI("RingRenderer: ringVerts=%{public}zu ringIdx=%{public}zu arrowVerts=%{public}zu arrowIdx=%{public}zu",
         mRingVertices.size() / 3, mRingIndices.size(), mArrowVertices.size() / 3, mArrowIndices.size());
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

void RingRenderer::Release()
{
    mRingVertices.clear();
    mRingIndices.clear();
    mArrowVertices.clear();
    mArrowIndices.clear();
    if (mShaderProgram) {
        GLUtils::ReleaseProgram(mShaderProgram);
        mShaderProgram = 0;
    }
}

void RingRenderer::Draw(const glm::mat4 &projectionMat, const glm::mat4 &viewMat, const glm::mat4 &modelMat,
                        const float *ringColor4, const float *arrowColor4) const
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

    glEnableVertexAttribArray(mAttribPosition);
    // Ring (distance feedback color).
    glUniform4fv(mUniformColor, 1, ringColor4);
    glVertexAttribPointer(mAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, mRingVertices.data());
    glDrawElements(GL_TRIANGLES, mRingIndices.size(), GL_UNSIGNED_SHORT, mRingIndices.data());
    // Center indicator arrow (angle feedback color).
    glUniform4fv(mUniformColor, 1, arrowColor4);
    glVertexAttribPointer(mAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, mArrowVertices.data());
    glDrawElements(GL_TRIANGLES, mArrowIndices.size(), GL_UNSIGNED_SHORT, mArrowIndices.data());

    glDisableVertexAttribArray(mAttribPosition);
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

} // namespace ARObject
