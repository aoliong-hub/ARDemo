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

#ifndef ARROW_ALIGN_AR_APPLICATION_H
#define ARROW_ALIGN_AR_APPLICATION_H

#include "app_napi.h"
#include "ar/ar_engine_core.h"
#include "arrow_align_render_manager.h"
#include "object/object_math.h"
#include "task_queue.h"
#include <atomic>
#include <random>
#include <string>

namespace ArrowAlign {

// Alignment challenge scene: a world-anchored RED target arrow (orientation = the camera's
// orientation cached at session start) and a camera-locked BLUE player arrow (a HUD). The goal
// is to rotate the phone until the two arrow directions align.
class ArrowAlignApp : public AppNapi {
public:
    explicit ArrowAlignApp(std::string &id);
    ~ArrowAlignApp() override;

    void OnStart(const ConfigParams &params) override;
    void OnUpdate() override;
    void OnPause() override;
    void OnResume() override;
    void OnStop() override;

    void OnSurfaceCreated(OH_NativeXComponent *component, void *window) override;
    void OnSurfaceChanged(OH_NativeXComponent *component, void *window) override;
    void OnSurfaceDestroyed(OH_NativeXComponent *component, void *window) override;

    // NAPI-driven game controls.
    int32_t PlaceTargetArrow() override;
    void ResetArrowAlign() override;
    void GetAlignmentState(float &angleRad, bool &aligned, bool &ready, bool &targetPlaced) override;

private:
    AREngine_ARSession *mArSession = nullptr;
    AREngine_ARFrame *mArFrame = nullptr;

    // Target (red) anchor — created/reset/read only on the task-queue thread.
    AREngine_ARAnchor *mTargetAnchor = nullptr;
    int32_t mNextId = 1;

    // Random target orientation (xyzw) for the current target arrow. Task thread only.
    float mTargetQuatXYZW[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    // Cached camera orientation at session start (xyzw). Kept for reference; UNUSED in Stage 6
    // (the target direction is now random, not the initial camera direction).
    float mInitialQuatXYZW[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ARObject::AlignState mAlignState = ARObject::AlignState::NOT_ALIGNED; // task thread only
    std::mt19937 mRng{std::random_device{}()};                           // target-direction RNG

    // State shared with the ArkTS poll (getAlignmentState).
    std::atomic<bool> mReady{false};
    std::atomic<bool> mTargetPlaced{false};
    std::atomic<bool> mCameraTracking{false}; // current-frame tracking, for placement gating
    std::atomic<bool> mAligned{false};
    std::atomic<float> mAngleRad{0.0f};

    uint64_t mWidth = 1080;
    uint64_t mHeight = 1920;
    AREngine_ARPoseType mDisplayRotation = ARENGINE_POSE_TYPE_IDENTITY;
    double mX = 0.0;
    double mY = 0.0;
    ArrowAlignRenderManager mRenderManager;
    TaskQueue mTaskQueue;
    bool mIsSurfaceChange = false;
    std::atomic<bool> isPaused = false;
    ConfigParams mConfigParam{};
};

} // namespace ArrowAlign

#endif // ARROW_ALIGN_AR_APPLICATION_H
