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

#ifndef OBJECT_RENDER_MANAGER_H
#define OBJECT_RENDER_MANAGER_H

#include "ar/ar_engine_core.h"
#include "graphic/RenderContext.h"
#include "graphic/RenderSurface.h"
#include "object_math.h"
// Reused sample renderers (NOT modified): camera background + AR_logo model.
#include "world/world_background_renderer.h"
#include "world/world_object_renderer.h"
#include <glm.hpp>
#include <string>
#include <vector>

namespace ARObject {

// One placed object: an AR Engine anchor + the model to draw + a stable monotonic id.
struct PlacedObject {
    int32_t objectId = -1;
    AREngine_ARAnchor *anchor = nullptr;
    float color[4] = {66.0f, 133.0f, 244.0f, 255.0f};
    std::string modelId = "AR_logo";
};

class ObjectRenderManager {
public:
    ObjectRenderManager() = default;
    ~ObjectRenderManager() = default;

    void Initialize(void *window, AREngine_ARSession *arSession);
    void Release();

    // Per-frame draw: camera background + every TRACKING object's model. No planes, no mesh.
    // Returns true if the camera is in the TRACKING state this frame.
    bool OnDrawFrame(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame,
                     const std::vector<PlacedObject> &objects);
    void DrawBlack();

    GLuint GetPreviewTextureId() { return mBackgroundRenderer.GetTextureId(); }

private:
    // Fills *outCameraPos with the camera world position. Returns true if camera is TRACKING.
    bool InitializeDraw(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame, glm::mat4 *viewMat,
                        glm::mat4 *projectionMat, glm::vec3 *outCameraPos);
    // Per object: keep anchor TRANSLATION only, then face-camera yaw about Y, then scale.
    void RenderObject(AREngine_ARSession *arSession, const glm::mat4 &viewMat, const glm::mat4 &projectionMat,
                      const std::vector<PlacedObject> &objects, const glm::vec3 &cameraPos);

    ARWorld::WorldBackgroundRenderer mBackgroundRenderer = ARWorld::WorldBackgroundRenderer();
    ARWorld::WorldObjectRenderer mObjectRenderer = ARWorld::WorldObjectRenderer();

    RenderContext mRenderContext;
    RenderSurface mRenderSurface;
    bool isInited = false;

    // Throttle for the Stage 3 face-camera verification log (Gate 1: sampled per ~2s).
    int mFaceFrameCounter = 0;
};

} // namespace ARObject

#endif // OBJECT_RENDER_MANAGER_H
