#
#  Copyright (c) 2024 Huawei Device Co., Ltd.
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#

# the minimum version of CMake.
cmake_minimum_required(VERSION 3.4.1)

# project
project(ArSample)

set(CMAKE_CXX_STANDARD 17)
set(NATIVE_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

find_library(arengine-lib libarengine_ndk.z.so)

# include directory
include_directories(
    ${NATIVE_ROOT_PATH}/src
    ${NATIVE_ROOT_PATH}/src/utils
    ${NATIVE_ROOT_PATH}/src/graphic
    ${NATIVE_ROOT_PATH}/thirdparty/glm
    ${NATIVE_ROOT_PATH}/thirdparty/stb
)

# build shared library
add_library(entry SHARED
    ${NATIVE_ROOT_PATH}/src/module.cpp
    ${NATIVE_ROOT_PATH}/src/napi_manager.cpp
    ${NATIVE_ROOT_PATH}/src/global.cpp
    ${NATIVE_ROOT_PATH}/src/utils/app_util.cpp
    ${NATIVE_ROOT_PATH}/src/graphic/GLUtils.cpp
    ${NATIVE_ROOT_PATH}/src/graphic/RenderAttribute.cpp
    ${NATIVE_ROOT_PATH}/src/graphic/RenderContext.cpp
    ${NATIVE_ROOT_PATH}/src/graphic/RenderSurface.cpp

    # world
    ${NATIVE_ROOT_PATH}/src/world/world_ar_application.cpp
    ${NATIVE_ROOT_PATH}/src/world/world_background_renderer.cpp
    ${NATIVE_ROOT_PATH}/src/world/world_object_renderer.cpp
    ${NATIVE_ROOT_PATH}/src/world/world_plane_renderer.cpp
    ${NATIVE_ROOT_PATH}/src/world/world_render_manager.cpp

)

# link OpenHarmony native library （Use OpenGL）
target_link_libraries(entry PUBLIC
    EGL
    GLESv3
    hilog_ndk.z
    ace_ndk.z
    ace_napi.z
    z
    uv
    ace_napi.z
    rawfile.z
    ${arengine-lib}
)
