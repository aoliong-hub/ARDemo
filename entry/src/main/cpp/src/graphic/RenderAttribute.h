//
// Copyright (c) Huawei Technologies Co., Ltd. 2020-2022. All rights reserved.
//

#ifndef RENDERATTRIBUTE_H
#define RENDERATTRIBUTE_H

#include "RenderHeader.h"

class RenderAttribute {
public:
    RenderAttribute() = default;
    ~RenderAttribute() = default;
    std::vector<int32_t> ToEglAttribList() const;

private:
    int32_t m_redBits = 8;
    int32_t m_greenBits = 8;
    int32_t m_blueBits = 8;
    int32_t m_alphaBits = 8;
    int32_t m_depthBits = 16;
};

#endif // RENDERATTRIBUTE_H
