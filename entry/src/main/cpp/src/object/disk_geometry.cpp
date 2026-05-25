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

#include "disk_geometry.h"

namespace ARObject {

void GenerateDiskQuad(std::vector<float> &vertices, std::vector<float> &normals, std::vector<float> &uvs,
                      std::vector<uint16_t> &indices)
{
    constexpr float h = 0.10f; // half edge -> 0.20m square
    vertices = {
        -h, -h, 0.0f, // 0
        h,  -h, 0.0f, // 1
        h,  h,  0.0f, // 2
        -h, h,  0.0f, // 3
    };
    normals = {
        0.0f, 0.0f, 1.0f, //
        0.0f, 0.0f, 1.0f, //
        0.0f, 0.0f, 1.0f, //
        0.0f, 0.0f, 1.0f, //
    };
    uvs = {
        0.0f, 0.0f, // 0
        1.0f, 0.0f, // 1
        1.0f, 1.0f, // 2
        0.0f, 1.0f, // 3
    };
    indices = {0, 1, 2, 0, 2, 3};
}

} // namespace ARObject
