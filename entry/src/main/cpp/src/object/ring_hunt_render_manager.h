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
#include "wayfinder_renderer.h"
#include "world/world_background_renderer.h" // reused camera background (NOT modified)
#include <glm.hpp>

namespace ARObject {

// Camera info the app thread needs each frame: tracking + world position (distance feedback) and
// the column-major view/projection matrices, so the app can project the beacon top to screen space
// for the off-screen guidance UI. Filled every drawn frame.
struct RingCameraInfo {
    bool tracking = false;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float viewMat[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float projMat[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

class RingHuntRenderManager {
public:
    RingHuntRenderManager() = default;
    ~RingHuntRenderManager() = default;

    void Initialize(void *window, AREngine_ARSession *arSession);
    void Release();

    // Draws background + (if a beacon is placed and its anchor is tracking) the Wayfinder beacon at
    // the anchor: ground ring + pillar core/fog + spinning top + phone icon. color drives the
    // ring/pillar tint (Stage 11A: fixed red), animTime spins the top ring. Fills *outCam.
    bool OnDrawFrame(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame, bool hasRing,
                     AREngine_ARAnchor *ringAnchor, float animTime, const glm::vec3 &color, float distance,
                     int huntPhase, const glm::quat &frameOrientation, float frameHueTime, bool isAligned,
                     float deltaTime, RingCameraInfo *outCam);
    void DrawBlack();

    GLuint GetPreviewTextureId() { return mBackgroundRenderer.GetTextureId(); }

private:
    ARWorld::WorldBackgroundRenderer mBackgroundRenderer = ARWorld::WorldBackgroundRenderer();
    WayfinderRenderer mWayfinderRenderer;

    RenderContext mRenderContext;
    RenderSurface mRenderSurface;
    bool isInited = false;
};

} // namespace ARObject

#endif // RING_HUNT_RENDER_MANAGER_H
