/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#ifndef GLUTILS_H
#define GLUTILS_H

#include "RenderHeader.h"
#include <memory>

class GLUtils
{
public:
    static GLuint CreateProgram(std::string vert, std::string frag);
    static GLuint CreateProgram(std::string comp);
    static void ReleaseProgram(GLuint program);

    static GLuint CreateTexture(int w, int h, GLenum internalFormat, const unsigned char* data = nullptr);
    static void ReleaseTexture(GLuint tex);

    static GLuint CreateFbo(GLuint tex = 0);
    static void ReleaseFbo(GLuint fbo, bool releaseTex = false);
    static void BindTexToFbo(GLuint tex, int fbo = 0);

    static GLuint CopyFboTexture(int width, int height);

    static std::vector<unsigned char> GetTexData(GLuint tex, int& w, int& h, int& pixelByteSize);

    static int GetInternalFormatPixelByteSize(GLenum internalFormat);

    static void GetTexSizeAndInternalFormat(GLuint tex, int& w, int& h, int& internalFormat);

    static void GetFormatAndTypeFromInternalFormat(int internalFormat, int& format, int& type);

    static std::vector<unsigned char> GetBufferData(GLenum target, GLuint buffer, int offset, int length);

    static GLuint CreateBuffer(GLenum target, void* data, int size, GLenum usage);
    static void UpdataBufferData(GLenum target, GLuint buffer, int offset, int size, void* data);
    static void ReleaseBuffer(GLuint buffer);

    static void GetGroupSizeXY(GLuint& x, GLuint& y);

    static void CheckError(const std::string& file, int lineNum);

    static void WriteBin(char* path, char* buf, int size);

    template<typename... Args>
    static std::string FormatString(const std::string& format, Args... args)
    {
        const auto size = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
        const auto buffer = std::make_unique<char[]>(size);
        std::snprintf(buffer.get(), size, format.c_str(), args...);
        return std::string(buffer.get(), buffer.get() + size - 1);
    }

    static std::string GetStringFromFile(std::string fileName);
};

#endif //GLUTILS_H
