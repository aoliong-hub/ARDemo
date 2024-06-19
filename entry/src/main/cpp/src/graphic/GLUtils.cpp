/*
* Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
*/

#include "GLUtils.h"
#include <fstream>
#include <sstream>


GLuint GLUtils::CreateProgram(std::string vert, std::string frag)
{
    const char* vertShader = vert.c_str();
    const char* fragShader = frag.c_str();
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertShader, NULL);
    glCompileShader(vertex);
    GLint status = 0;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar message[512];
        glGetShaderInfoLog(vertex, sizeof(message), 0, &message[0]);
        LOGE("GLUtils: CompileShader: %{public}s", &message[0]);
    }
    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragShader, NULL);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar message[512];
        glGetShaderInfoLog(fragment, sizeof(message), 0, &message[0]);
        LOGE("GLUtils: CompileShader: %{public}s", &message[0]);
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        GLchar message[512];
        glGetProgramInfoLog(program, 512, NULL, message);
        LOGE("GLUtils: LinkProgram: %{public}s", &message[0]);
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

GLuint GLUtils::CreateProgram(std::string comp)
{
    const char* compShader = comp.c_str();
    GLuint compute = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(compute, 1, &compShader, NULL);
    glCompileShader(compute);
    GLint status = 0;
    glGetShaderiv(compute, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar message[512];
        glGetShaderInfoLog(compute, sizeof(message), 0, &message[0]);
        LOGE("GLUtils: CompileShader: %{public}s", &message[0]);
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, compute);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        GLchar message[512];
        glGetProgramInfoLog(program, 512, NULL, message);
        LOGE("GLUtils: LinkProgram: %{public}s", &message[0]);
    }
    glDeleteShader(compute);

    return program;
}

void GLUtils::ReleaseProgram(GLuint program)
{
    glDeleteProgram(program);
}

GLuint GLUtils::CreateTexture(int w, int h, GLenum internalFormat, const unsigned char *data)
{
    int align;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint tex = 0;
    int format, type;
    GetFormatAndTypeFromInternalFormat(internalFormat, format, type);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, w, h);
    if (data) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    return tex;
}

void GLUtils::ReleaseTexture(GLuint tex)
{
    if (tex != 0) {
        glDeleteTextures(1, &tex);
    }
}

GLuint GLUtils::CreateFbo(GLuint tex)
{
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (tex != 0) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

void GLUtils::BindTexToFbo(GLuint tex, int fbo)
{
    if (fbo != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (fbo != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void GLUtils::ReleaseFbo(GLuint fbo, bool releaseTex)
{
    if (fbo == 0) {
        return;
    }
    if (releaseTex) {
        GLuint tex = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                              reinterpret_cast<GLint *>(&tex));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glDeleteTextures(1, &tex);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glDeleteFramebuffers(1, &fbo);
}

void GLUtils::CheckError(const std::string& file, int lineNum)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOGE("GLUtils: [%{public}s:%{public}d] GL Error: %{public}d", file.c_str(), lineNum, error);
    }
}

std::vector<unsigned char> GLUtils::GetTexData(GLuint tex, int& w, int& h, int& pixelByteSize)
{
    int internalFormat;
    GetTexSizeAndInternalFormat(tex, w, h, internalFormat);
    pixelByteSize = GetInternalFormatPixelByteSize(internalFormat);
    GLuint fbo = CreateFbo(tex);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLint fmt, type;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &fmt);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE,  &type);
    std::vector<unsigned char> data(w * h * pixelByteSize);
    glReadPixels(0, 0, w, h, fmt, type, data.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ReleaseFbo(fbo);
    return data;
}

void GLUtils::GetFormatAndTypeFromInternalFormat(int internalFormat, int& format, int& type)
{
    switch (internalFormat) {
        case GL_R8:
            format = GL_RED;
            type = GL_UNSIGNED_BYTE;
            break;
        case GL_RGB565:
            format = GL_RGB;
            type = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case GL_RGBA4:
            format = GL_RGBA;
            type = GL_UNSIGNED_SHORT_4_4_4_4;
            break;
        case GL_RGBA8:
            format = GL_RGBA;
            type = GL_UNSIGNED_BYTE;
            break;
        case GL_RGBA16F:
            format = GL_RGBA;
            type = GL_HALF_FLOAT;
            break;
        case GL_R32F:
            format = GL_RED;
            type = GL_FLOAT;
            break;
        case GL_RGB8:
            format = GL_RGB;
            type = GL_UNSIGNED_BYTE;
            break;
        default :
            break;
    }
}

int GLUtils::GetInternalFormatPixelByteSize(GLenum internalFormat)
{
    int ret = 0;
    switch (internalFormat) {
        case GL_RGBA8:
        case GL_R32F:
            ret = 4;
            break;
        case GL_RGBA16F:
            ret = 8;
            break;
        case GL_R8:
            ret = 1;
            break;
        case GL_RGB565:
            ret = 2;
            break;
        case GL_RGBA4:
            ret = 2;
            break;
        default :
            break;
    }
    return ret;
}

void GLUtils::GetTexSizeAndInternalFormat(GLuint tex, int& w, int& h, int& internalFormat)
{
    std::vector<int> texParam(3);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GLUtils::WriteBin(char *path, char *buf, int size)
{
    FILE *outfile;
    if ((outfile = fopen(path, "wb")) == NULL) {
        LOGE("GLUtils: Can not open the path: %{public}s \n", path);
        exit(-1);
    }
    fwrite(buf, sizeof(char), size, outfile);
    fclose(outfile);
}

std::vector<unsigned char> GLUtils::GetBufferData(GLenum target, GLuint buffer, int offset, int length)
{
    glBindBuffer(target, buffer);
    unsigned char* ptr = (unsigned char*)glMapBufferRange(target, offset, length, GL_MAP_READ_BIT);
    std::vector<unsigned char> data(ptr, ptr + length);
    glUnmapBuffer(target);
    glBindBuffer(target, 0);
    return data;
}

GLuint GLUtils::CreateBuffer(GLenum target, void* data, int size, GLenum usage)
{
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(target, buffer);
    glBufferData(target, size, data, usage);
    glBindBuffer(target, 0);
    return buffer;
}

void GLUtils::ReleaseBuffer(GLuint buffer)
{
    glDeleteBuffers(1, &buffer);
}

void GLUtils::UpdataBufferData(GLenum target, GLuint buffer, int offset, int size, void* data)
{
    glBindBuffer(target, buffer);
    glBufferSubData(target, offset, size, data);
    glBindBuffer(target, 0);
}

GLuint GLUtils::CopyFboTexture(int width, int height)
{
    GLuint tex = CreateTexture(width, height, GL_RGBA8);
    glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void GLUtils::GetGroupSizeXY(GLuint& x, GLuint& y)
{
    int maxWorkGroupInvocation;
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxWorkGroupInvocation);
    if (maxWorkGroupInvocation >= 1024) {
        x = 32;
        y = 32;
    } else {
        x = 16;
        y = 8;
    }
}

std::string GLUtils::GetStringFromFile(std::string fileName) {
    std::string ret;
    std::ifstream fileStream;
    fileStream.open(fileName);
    if (fileStream.is_open()) {
        std::ostringstream stringStream;
        stringStream << fileStream.rdbuf();
        ret = stringStream.str();
        fileStream.close();
    }
    return ret;
}
