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

// Fresnel edge-glow renderer for the ring + center arrow. Ring and arrow are drawn separately
// with independent base/glow colors (Stage 8: ring=distance, arrow=angle; Stage 9: + Fresnel).
class RingRenderer {
public:
    RingRenderer() = default;
    ~RingRenderer() = default;

    void InitializeGlContent();
    void Release();

    // cameraPos3: camera world position (for Fresnel view vector).
    // *Base3 / *Glow3: rgb base color and glow color for ring and arrow respectively.
    void Draw(const glm::mat4 &projectionMat, const glm::mat4 &viewMat, const glm::mat4 &modelMat,
              const float *cameraPos3, const float *ringBase3, const float *ringGlow3, const float *arrowBase3,
              const float *arrowGlow3) const;

private:
    std::vector<GLfloat> mRingVertices = {};
    std::vector<GLfloat> mRingNormals = {};
    std::vector<GLushort> mRingIndices = {};
    std::vector<GLfloat> mArrowVertices = {};
    std::vector<GLfloat> mArrowNormals = {};
    std::vector<GLushort> mArrowIndices = {};

    GLuint mShaderProgram = 0;
    GLint mUniformMvp = 0;
    GLint mUniformModel = 0;
    GLint mUniformCameraPos = 0;
    GLint mUniformBaseColor = 0;
    GLint mUniformGlowColor = 0;
    GLint mUniformGlowPower = 0;
    GLint mUniformGlowIntensity = 0;
    GLint mUniformBaseBoost = 0;
    GLint mAttribPosition = 0;
    GLint mAttribNormal = 0;
};

} // namespace ARObject

#endif // RING_RENDERER_H
