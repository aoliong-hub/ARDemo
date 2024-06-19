/**
 * Copyright 2022. Huawei Technologies Co., Ltd. All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#ifndef C_ARENGINE_HELLOE_AR_UTIL_H
#define C_ARENGINE_HELLOE_AR_UTIL_H

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/quaternion.hpp>
#include "utils/log.h"
#include "ar/ar_engine_core.h"
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
            abort();                                                                                                   \
        }                                                                                                              \
    } while (false);
#endif

namespace ArWorld {

using FileInfor = struct {
//     AAssetManager *mgr;
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

glm::vec3 GetPlaneNormal(const AREngine_ARSession* arSession, const AREngine_ARPose* planePose);

/**
 * Calculate the normal distance from the camera to the plane.
 * The y-axis of a given plane should be parallel to the normal of the plane.
 * Such as a center position of a plane or a hit test position.
 */
float CalculateDistanceToPlane(const AREngine_ARSession* arSession,
                               const AREngine_ARPose* planePose,
                               const AREngine_ARPose* cameraPose);

bool LoadPngFromAssetManager(int target, const std::string &path);

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