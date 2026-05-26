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

#ifndef RING_HUNT_AR_APPLICATION_H
#define RING_HUNT_AR_APPLICATION_H

#include "app_napi.h"
#include "ar/ar_engine_core.h"
#include "object_math.h"
#include "ring_hunt_render_manager.h"
#include "task_queue.h"
#include <atomic>
#include <chrono>
#include <string>

namespace ARObject {

// Ring hunt: a glowing ring is placed at a random nearby pose. The player must walk to it
// (distance) and orient the phone through it (angle). Both held for 0.5s -> FOUND (timed).
class RingHuntApp : public AppNapi {
public:
    explicit RingHuntApp(std::string &id);
    ~RingHuntApp() override;

    void OnStart(const ConfigParams &params) override;
    void OnUpdate() override;
    void OnPause() override;
    void OnResume() override;
    void OnStop() override;

    void OnSurfaceCreated(OH_NativeXComponent *component, void *window) override;
    void OnSurfaceChanged(OH_NativeXComponent *component, void *window) override;
    void OnSurfaceDestroyed(OH_NativeXComponent *component, void *window) override;

    int32_t PlaceRing() override;
    void ResetRing() override;
    void GetRingState(float &distance, float &angleRad, float &yawDiffRad, float &pitchDiffRad, bool &distOnTarget,
                      bool &angOnTarget, int32_t &finishState, float &foundSec, bool &isTargetInView,
                      float &screenEdgeX, float &screenEdgeY, bool &isBehind, float &indicatorAngleDeg) override;

private:
    AREngine_ARSession *mArSession = nullptr;
    AREngine_ARFrame *mArFrame = nullptr;

    // Ring target — created/reset/read only on the task-queue thread.
    AREngine_ARAnchor *mRingAnchor = nullptr;
    float mRingQuatXYZW[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int32_t mNextId = 1;

    // Finish state machine (task thread only).
    FinishState mFinishState = FinishState::NOT_FINISHED;
    float mFinishTimerSec = 0.0f;
    std::chrono::steady_clock::time_point mStartTime;
    std::chrono::steady_clock::time_point mLastFrameTime;
    bool mHasLastFrame = false;

    // Shared with the ArkTS poll (getRingState).
    std::atomic<bool> mReady{false};
    std::atomic<bool> mHasRing{false};
    std::atomic<bool> mCameraTracking{false};
    std::atomic<float> mDistance{0.0f};
    std::atomic<float> mAngleRad{3.14159265358979323846f};
    std::atomic<float> mYawDiffRad{0.0f};
    std::atomic<float> mPitchDiffRad{0.0f};
    std::atomic<bool> mDistOnTarget{false};
    std::atomic<bool> mAngOnTarget{false};
    std::atomic<int32_t> mFinishStateInt{0};
    std::atomic<float> mFoundSec{0.0f};

    // Stage 10: off-screen target guidance (computed each tracked frame from the view/proj
    // matrices). Default "in view" so guidance stays hidden until a ring is placed.
    std::atomic<bool> mIsTargetInView{true};
    std::atomic<float> mScreenEdgeX{0.5f};
    std::atomic<float> mScreenEdgeY{0.5f};
    std::atomic<bool> mIsBehind{false};
    std::atomic<float> mIndicatorAngleDeg{0.0f};

    uint64_t mWidth = 1080;
    uint64_t mHeight = 1920;
    AREngine_ARPoseType mDisplayRotation = ARENGINE_POSE_TYPE_IDENTITY;
    double mX = 0.0;
    double mY = 0.0;
    RingHuntRenderManager mRenderManager;
    TaskQueue mTaskQueue;
    bool mIsSurfaceChange = false;
    std::atomic<bool> isPaused = false;
    ConfigParams mConfigParam{};
};

} // namespace ARObject

#endif // RING_HUNT_AR_APPLICATION_H
