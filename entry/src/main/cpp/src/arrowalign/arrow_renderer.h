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

#ifndef ARROW_RENDERER_H
#define ARROW_RENDERER_H

#include <GLES2/gl2.h>
#include <glm.hpp>
#include <vector>

namespace ArrowAlign {

// Draws the procedural arrow mesh with a simple directional-light shader. Shaft and head are
// drawn with separate color uniforms (two sub-draws per arrow).
class ArrowRenderer {
public:
    ArrowRenderer() = default;
    ~ArrowRenderer() = default;

    void InitializeGlContent();
    void Release();

    // colorShaft3 / colorHead3: rgb in [0,1].
    void Draw(const glm::mat4 &projectionMat, const glm::mat4 &viewMat, const glm::mat4 &modelMat,
              const float *colorShaft3, const float *colorHead3) const;

private:
    std::vector<GLfloat> mVertices = {};
    std::vector<GLfloat> mNormals = {};
    std::vector<GLushort> mShaftIndices = {};
    std::vector<GLushort> mHeadIndices = {};

    GLuint mShaderProgram = 0;
    GLint mUniformMvp = 0;
    GLint mUniformMv = 0;
    GLint mUniformColor = 0;
    GLint mAttribPosition = 0;
    GLint mAttribNormal = 0;
};

} // namespace ArrowAlign

#endif // ARROW_RENDERER_H
