//
// Copyright (c) Huawei Technologies Co., Ltd. 2020-2022. All rights reserved.
//

#ifndef RENDERCONTEXT_H
#define RENDERCONTEXT_H

#include "RenderHeader.h"
#include "RenderObject.h"
#include "RenderSurface.h"

class RenderContext : public RenderObject {
public:
    RenderContext();
    ~RenderContext();
    
    virtual bool Create(RenderContext *sharedContext = nullptr);
    virtual bool Init() override;
    virtual bool Release() override;
    virtual bool MakeCurrent(const RenderSurface *surface);
    virtual bool ReleaseCurrent();
    virtual bool SwapBuffers(const RenderSurface *surface);

private:
    EGLDisplay m_display;
    EGLContext m_context;
    EGLConfig m_config;
    RenderAttribute m_attribute;
    bool m_isReady { false };
};

typedef std::shared_ptr<RenderContext> RenderContextPtr;

#endif // RENDERCONTEXT_H
