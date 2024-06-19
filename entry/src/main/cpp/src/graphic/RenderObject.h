//
// Copyright (c) Huawei Technologies Co., Ltd. 2020-2022. All rights reserved.
//

#ifndef RENDEROBJECT_H
#define RENDEROBJECT_H

#include "RenderHeader.h"

class RenderObject {
public:
    explicit RenderObject()
        : m_isReady(false)
    {
    }
    virtual ~RenderObject()
    {
        if (m_isReady) {
            LOGE("Destructor called without Release()! %s", m_tag.c_str());
        }
    }

    virtual bool Init() = 0;
    virtual bool Release() = 0;

    bool IsReady() { return m_isReady; }

    void SetReady(bool flag)
    {
        m_isReady = flag;
    }

    void SetTag(const std::string& tag)
    {
        m_tag = tag;
    }

    std::string GetTag()
    {
        return m_tag;
    }

private:
    bool m_isReady;
    std::string m_tag;
};

using RenderObjectPtr = std::shared_ptr<RenderObject>;

#define TO_RENDER_OBJECT(obj) (std::dynamic_pointer_cast<RenderObject>((obj)))

#endif // RENDEROBJECT_H
