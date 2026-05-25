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

#ifndef ARROW_ALIGN_RENDER_MANAGER_H
#define ARROW_ALIGN_RENDER_MANAGER_H

#include "ar/ar_engine_core.h"
#include "arrow_renderer.h"
#include "graphic/RenderContext.h"
#include "graphic/RenderSurface.h"
#include "world/world_background_renderer.h" // reused camera background (NOT modified)
#include <glm.hpp>

namespace ArrowAlign {

// Camera state read once per frame and handed back to the app for alignment math/caching.
struct FrameCameraInfo {
    bool tracking = false;
    float quatXYZW[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float pos[3] = {0.0f, 0.0f, 0.0f};
};

class ArrowAlignRenderManager {
public:
    ArrowAlignRenderManager() = default;
    ~ArrowAlignRenderManager() = default;

    void Initialize(void *window, AREngine_ARSession *arSession);
    void Release();

    // Draws camera background, then (if present) the world-anchored RED target arrow oriented by
    // targetQuatXYZW. Fills *outCam with this frame's camera pose/tracking. Returns true if the
    // camera is tracking. (The blue player arrow was removed in Stage 6.)
    bool OnDrawFrame(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame, bool hasTarget,
                     AREngine_ARAnchor *targetAnchor, const float *targetQuatXYZW, FrameCameraInfo *outCam);
    void DrawBlack();

    GLuint GetPreviewTextureId() { return mBackgroundRenderer.GetTextureId(); }

private:
    ARWorld::WorldBackgroundRenderer mBackgroundRenderer = ARWorld::WorldBackgroundRenderer();
    ArrowRenderer mArrowRenderer;

    RenderContext mRenderContext;
    RenderSurface mRenderSurface;
    bool isInited = false;
};

} // namespace ArrowAlign

#endif // ARROW_ALIGN_RENDER_MANAGER_H
