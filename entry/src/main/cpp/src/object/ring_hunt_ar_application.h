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

// Wayfinder beacon (Stage 11A): a beacon is dropped on the floor 1m ahead of the camera. The
// renderer draws the ground ring + light pillar + spinning top + phone icon there (fixed red).
// getRingState reports only the camera->beacon distance for now; the distance gradient + finish
// state machine return in 11B.
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
    void GetRingState(float &distance, bool &ringPlaced, int32_t &finishState, bool &isTargetInView,
                      float &screenEdgeX, float &screenEdgeY, bool &isBehind, float &indicatorAngleDeg, float &ndcX,
                      float &ndcY, int32_t &huntPhase, float &yawDiffRad, float &pitchDiffRad, bool &isAligned,
                      bool &isLocked, bool &isViewingFromBack) override;

private:
    AREngine_ARSession *mArSession = nullptr;
    AREngine_ARFrame *mArFrame = nullptr;

    // Beacon anchor — created/reset/read only on the task-queue thread.
    AREngine_ARAnchor *mRingAnchor = nullptr;
    int32_t mNextId = 1;

    // Animation clock (task thread only): drives the spinner/ripple/breathing. mFrameHueTime tracks
    // mAnimTime but freezes once LOCKED so the alignment frame's hue stops cycling.
    float mAnimTime = 0.0f;
    float mFrameHueTime = 0.0f;
    std::chrono::steady_clock::time_point mLastFrameTime;
    bool mHasLastFrame = false;

    // 6DoF alignment target (task thread only): a random orientation generated at placeRing.
    float mTargetYaw = 0.0f;   // about world +Y
    float mTargetPitch = 0.0f; // about world +X
    float mTargetRoll = 0.0f;  // about world +Z (forward axis)

    // State-machine debounce timers (task thread only), seconds.
    float mToAligningTimer = 0.0f;
    float mToApproachingTimer = 0.0f;
    float mLockTimer = 0.0f;

    // Shared with the ArkTS poll (getRingState).
    std::atomic<bool> mReady{false};
    std::atomic<bool> mHasRing{false};
    std::atomic<bool> mCameraTracking{false};
    std::atomic<float> mDistance{99.0f};

    // Stage 11D 6DoF alignment challenge state.
    std::atomic<int32_t> mHuntPhase{0}; // 0=APPROACHING 1=ALIGNING 2=LOCKED
    std::atomic<float> mYawDiffRad{0.0f};
    std::atomic<float> mPitchDiffRad{0.0f};
    std::atomic<bool> mIsAligned{false};
    std::atomic<bool> mIsLocked{false};
    std::atomic<bool> mIsViewingFromBack{false}; // ALIGNING: camera is on the wrong side of the frame

    // Off-screen target guidance (computed each tracked frame from the view/proj matrices, targeting
    // the beacon top). Default "in view" so guidance stays hidden until a beacon is placed.
    std::atomic<bool> mIsTargetInView{true};
    std::atomic<float> mScreenEdgeX{0.5f};
    std::atomic<float> mScreenEdgeY{0.5f};
    std::atomic<bool> mIsBehind{false};
    std::atomic<float> mIndicatorAngleDeg{0.0f};
    std::atomic<float> mNdcX{0.0f}; // projected ndc -> ArkTS screen-space guidance ray
    std::atomic<float> mNdcY{0.0f};

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
