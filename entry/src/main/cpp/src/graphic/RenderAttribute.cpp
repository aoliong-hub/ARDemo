//
// Copyright (c) Huawei Technologies Co., Ltd. 2020-2022. All rights reserved.
//

#include "RenderAttribute.h"

std::vector<int32_t> RenderAttribute::ToEglAttribList() const
{
    int32_t configAttribs[] = {
        EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
        EGL_RED_SIZE, m_redBits,
        EGL_GREEN_SIZE, m_greenBits,
        EGL_BLUE_SIZE, m_blueBits,
        EGL_ALPHA_SIZE, m_alphaBits,
        EGL_DEPTH_SIZE, m_depthBits,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };
    std::vector<int32_t> attribs(configAttribs, configAttribs + sizeof(configAttribs) / sizeof(EGLint));
    return attribs;
}
