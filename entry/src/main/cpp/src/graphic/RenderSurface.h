//
// Copyright (c) Huawei Technologies Co., Ltd. 2020-2022. All rights reserved.
//

#ifndef RENDERSURFACE_H
#define RENDERSURFACE_H

#include "RenderHeader.h"
#include "RenderObject.h"
#include "RenderAttribute.h"

class RenderSurface : public RenderObject {
public:
    enum class SurfaceType {
        SURFACE_TYPE_NULL,
        SURFACE_TYPE_ON_SCREEN,
        SURFACE_TYPE_OFF_SCREEN
    };

    RenderSurface();
    ~RenderSurface() override;

    virtual void SetAttrib(const RenderAttribute& attrib);
    virtual RenderAttribute GetAttrib();
    virtual bool Create(void* window);
    virtual bool Init() override;
    virtual bool Release() override;

    void* GetRawSurface() const;
    SurfaceType GetSurfaceType() const;
private:
    EGLDisplay m_display;
    EGLConfig m_config;
    EGLSurface m_surface;
    RenderAttribute m_attribute;
    SurfaceType m_surfaceType;
};


#endif // RENDERSURFACE_H
