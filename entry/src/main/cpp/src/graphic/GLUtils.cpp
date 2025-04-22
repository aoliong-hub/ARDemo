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

#include "GLUtils.h"

GLuint GLUtils::CreateProgram(std::string vert, std::string frag)
{
    const char *vertShader = vert.c_str();
    const char *fragShader = frag.c_str();
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertShader, NULL);
    glCompileShader(vertex);
    GLint status = 0;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar message[512];
        glGetShaderInfoLog(vertex, sizeof(message), 0, &message[0]);
        LOGE("GLUtils: CompileShader: %{public}s.", &message[0]);
    }
    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragShader, NULL);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar message[512];
        glGetShaderInfoLog(fragment, sizeof(message), 0, &message[0]);
        LOGE("GLUtils: CompileShader: %{public}s.", &message[0]);
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        GLchar message[512];
        glGetProgramInfoLog(program, 512, NULL, message);
        LOGE("GLUtils: LinkProgram: %{public}s.", &message[0]);
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

void GLUtils::ReleaseProgram(GLuint program) { glDeleteProgram(program); }

void GLUtils::CheckError(const std::string &file, int lineNum)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOGE("GLUtils: [%{public}s:%{public}d] GL Error: %{public}d.", file.c_str(), lineNum, error);
    }
}
