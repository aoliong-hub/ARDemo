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

#ifndef RING_HUNT_RENDER_MANAGER_H
#define RING_HUNT_RENDER_MANAGER_H

#include "ar/ar_engine_core.h"
#include "graphic/RenderContext.h"
#include "graphic/RenderSurface.h"
#include "ring_renderer.h"
#include "world/world_background_renderer.h" // reused camera background (NOT modified)
#include <glm.hpp>

namespace ARObject {

struct RingCameraInfo {
    bool tracking = false;
    float quatXYZW[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float pos[3] = {0.0f, 0.0f, 0.0f};
};

class RingHuntRenderManager {
public:
    RingHuntRenderManager() = default;
    ~RingHuntRenderManager() = default;

    void Initialize(void *window, AREngine_ARSession *arSession);
    void Release();

    // Draws background + (if present) the ring. Stage 8: ring colored by distance (green/red),
    // center arrow colored by angle (green/red), independently. Fills *outCam.
    bool OnDrawFrame(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame, bool hasRing,
                     AREngine_ARAnchor *ringAnchor, const float *ringQuatXYZW, bool distOnTarget, bool angOnTarget,
                     RingCameraInfo *outCam);
    void DrawBlack();

    GLuint GetPreviewTextureId() { return mBackgroundRenderer.GetTextureId(); }

private:
    ARWorld::WorldBackgroundRenderer mBackgroundRenderer = ARWorld::WorldBackgroundRenderer();
    RingRenderer mRingRenderer;

    RenderContext mRenderContext;
    RenderSurface mRenderSurface;
    bool isInited = false;
};

} // namespace ARObject

#endif // RING_HUNT_RENDER_MANAGER_H
