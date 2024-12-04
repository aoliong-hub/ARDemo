/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef C_ARENGINE_HELLOE_AR_UTIL_H
#define C_ARENGINE_HELLOE_AR_UTIL_H

#include <string>
#include <vector>
#include <GLES2/gl2.h>
#include "GLUtils.h"

#ifndef CHECK
#define SPLIT_FUNC(str) strtok((str), "(")
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        auto ret = (condition);                                                                                        \
        if (ret) {                                                                                                     \
            char sentence[] = #condition;                                                                              \
            LOGE("*** CHECK FAILED at %{public}s:%{public}d: %{public}s ret: %{public}d",                              \
                 __FILE__, __LINE__, SPLIT_FUNC(sentence), ret);                                                       \
        }                                                                                                              \
    } while (false);
#endif

namespace ArWorld {

using FileInfor = struct {
    std::string fileName;
};

using FileData = struct {
    std::vector<char *> perVertInfoList;
    int i;
};

using DrawTempData = struct {
    std::vector<GLfloat> tempPositions;
    std::vector<GLfloat> tempNormals;
    std::vector<GLfloat> tempUvs;
    std::vector<GLushort> vertexIndices;
    std::vector<GLushort> normalIndices;
    std::vector<GLushort> uvIndices;
};

bool LoadPngFromAssetManager(const std::string &path);

/**
 * Load the obj file from the assets folder in the application.
 *
 * @param fileInformation Pointer to the AAssetManager,the name of the obj file.
 * @param outVertices Output vertex.
 * @param outNormals Output normal.
 * @param outUv UV coordinate of the output texture.
 * @param outIndices Output triangular exponent.
 * @return True if obj is loaded correctly, false otherwise.
 */
bool LoadObjFile(FileInfor fileInformation,
                 std::vector<GLfloat> &outVertices,
                 std::vector<GLfloat> &outNormals,
                 std::vector<GLfloat> &outUv,
                 std::vector<GLushort> &outIndices);

}

#endif