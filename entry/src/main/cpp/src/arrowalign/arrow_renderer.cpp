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

#include "arrow_renderer.h"
#include "graphic/GLUtils.h"
#include "object/arrow_geometry.h"
#include "utils/log.h"
#include <gtc/type_ptr.hpp>

namespace ArrowAlign {
namespace {
constexpr char VERTEX_SHADER[] = R"(
    uniform mat4 u_ModelViewProjection;
    uniform mat4 u_ModelView;
    attribute vec4 a_Position;
    attribute vec3 a_Normal;
    varying vec3 v_ViewNormal;
    void main() {
        v_ViewNormal = normalize((u_ModelView * vec4(a_Normal, 0.0)).xyz);
        gl_Position = u_ModelViewProjection * a_Position;
    }
)";

constexpr char FRAGMENT_SHADER[] = R"(
    precision mediump float;
    uniform vec3 u_Color;
    varying vec3 v_ViewNormal;
    void main() {
        vec3 lightDir = normalize(vec3(0.3, 0.8, 0.5));
        float diffuse = max(dot(normalize(v_ViewNormal), lightDir), 0.0);
        float ambient = 0.45;
        gl_FragColor = vec4(u_Color * (ambient + 0.6 * diffuse), 1.0);
    }
)";
} // namespace

void ArrowRenderer::InitializeGlContent()
{
    mShaderProgram = GLUtils::CreateProgram(VERTEX_SHADER, FRAGMENT_SHADER);
    if (!mShaderProgram) {
        LOGE("ArrowRenderer: could not create program.");
        return;
    }
    mUniformMvp = glGetUniformLocation(mShaderProgram, "u_ModelViewProjection");
    mUniformMv = glGetUniformLocation(mShaderProgram, "u_ModelView");
    mUniformColor = glGetUniformLocation(mShaderProgram, "u_Color");
    mAttribPosition = glGetAttribLocation(mShaderProgram, "a_Position");
    mAttribNormal = glGetAttribLocation(mShaderProgram, "a_Normal");

    ARObject::GenerateArrowMesh(mVertices, mNormals, mShaftIndices, mHeadIndices, 16);
    LOGI("ArrowRenderer: mesh verts=%{public}zu shaftIdx=%{public}zu headIdx=%{public}zu", mVertices.size() / 3,
         mShaftIndices.size(), mHeadIndices.size());
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

void ArrowRenderer::Release()
{
    mVertices.clear();
    mNormals.clear();
    mShaftIndices.clear();
    mHeadIndices.clear();
    if (mShaderProgram) {
        GLUtils::ReleaseProgram(mShaderProgram);
        mShaderProgram = 0;
    }
}

void ArrowRenderer::Draw(const glm::mat4 &projectionMat, const glm::mat4 &viewMat, const glm::mat4 &modelMat,
                         const float *colorShaft3, const float *colorHead3) const
{
    if (!mShaderProgram || mVertices.empty()) {
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE); // draw both faces so winding never hides the solid arrow

    glUseProgram(mShaderProgram);
    glm::mat4 mvpMat = projectionMat * viewMat * modelMat;
    glm::mat4 mvMat = viewMat * modelMat;
    glUniformMatrix4fv(mUniformMvp, 1, GL_FALSE, glm::value_ptr(mvpMat));
    glUniformMatrix4fv(mUniformMv, 1, GL_FALSE, glm::value_ptr(mvMat));

    glEnableVertexAttribArray(mAttribPosition);
    glVertexAttribPointer(mAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, mVertices.data());
    glEnableVertexAttribArray(mAttribNormal);
    glVertexAttribPointer(mAttribNormal, 3, GL_FLOAT, GL_FALSE, 0, mNormals.data());

    glUniform3fv(mUniformColor, 1, colorShaft3);
    glDrawElements(GL_TRIANGLES, mShaftIndices.size(), GL_UNSIGNED_SHORT, mShaftIndices.data());
    glUniform3fv(mUniformColor, 1, colorHead3);
    glDrawElements(GL_TRIANGLES, mHeadIndices.size(), GL_UNSIGNED_SHORT, mHeadIndices.data());

    glDisableVertexAttribArray(mAttribPosition);
    glDisableVertexAttribArray(mAttribNormal);
    glUseProgram(0);
    GLUtils::CheckError(__FILE_NAME__, __LINE__);
}

} // namespace ArrowAlign
