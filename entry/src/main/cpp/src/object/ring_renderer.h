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

#ifndef RING_RENDERER_H
#define RING_RENDERER_H

#include <GLES2/gl2.h>
#include <glm.hpp>
#include <vector>

namespace ARObject {

// Emissive (unlit) renderer for the glowing ring + its center indicator arrow. Both share one
// model matrix and color per Draw call.
class RingRenderer {
public:
    RingRenderer() = default;
    ~RingRenderer() = default;

    void InitializeGlContent();
    void Release();

    // Ring and arrow drawn with independent rgba colors (Stage 8: 2-axis feedback).
    void Draw(const glm::mat4 &projectionMat, const glm::mat4 &viewMat, const glm::mat4 &modelMat,
              const float *ringColor4, const float *arrowColor4) const;

private:
    std::vector<GLfloat> mRingVertices = {};
    std::vector<GLushort> mRingIndices = {};
    std::vector<GLfloat> mArrowVertices = {};
    std::vector<GLushort> mArrowIndices = {};

    GLuint mShaderProgram = 0;
    GLint mUniformMvp = 0;
    GLint mUniformColor = 0;
    GLint mAttribPosition = 0;
};

} // namespace ARObject

#endif // RING_RENDERER_H
