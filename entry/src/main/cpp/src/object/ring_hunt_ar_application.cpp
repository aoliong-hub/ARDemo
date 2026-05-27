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

#include "ring_hunt_ar_application.h"
#include "app_util.h"
#include "object_math.h"
#include "utils/log.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <cmath>
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>
#include <random>
#include <window_manager/oh_display_info.h>
#include <window_manager/oh_display_manager.h>

namespace ARObject {
namespace {
// Stage 11C (style C "clean modern"): the beacon tints warm-red -> soft-mint by distance to its
// top badge. Stage 11D widened the gradient band to 50cm (red) -> 30cm (mint), matching the
// ALIGNING entry threshold.
const glm::vec3 kColorRed(0.95f, 0.4f, 0.4f);  // warm red (far)
const glm::vec3 kColorGreen(0.5f, 0.9f, 0.7f); // soft mint (near)
constexpr float kColorRedDist = 0.50f;   // >= this metres: fully warm-red
constexpr float kColorGreenDist = 0.30f; // <= this metres: fully soft-mint (20cm smooth band between)
constexpr float kPlaceDistance = 1.0f;   // beacon dropped 1m ahead of the camera
constexpr float kGroundDrop = 1.0f;      // lower it ~1m to approximate the floor

// Stage 11D 6DoF alignment challenge.
constexpr float kAligningDistMeters = 0.30f; // distance threshold to enter ALIGNING
constexpr float kAlignAngleRad = 0.0872665f; // 5 degrees: yaw & pitch tolerance for LOCKED
constexpr float kDebounceSec = 0.30f;        // hold time for APPROACHING<->ALIGNING transitions
constexpr float kLockSec = 0.50f;            // hold time aligned before LOCKED
constexpr float kRandYawRad = 0.3490659f;     // +/-20 deg
constexpr float kRandRollRad = 0.2617994f;    // +/-15 deg
constexpr float kPitchDownMinRad = 0.1745329f; // 10 deg minimum downward tilt
constexpr float kPitchDownSpan = 0.3490659f;   // + up to 20 deg more (so -10..-30 deg)
} // namespace

RingHuntApp::RingHuntApp(std::string &id) : AppNapi(id) { LOGD("RingHuntApp::Constructor"); }

RingHuntApp::~RingHuntApp()
{
    LOGD("RingHuntApp::Destructor");
    mTaskQueue.Stop();
}

void RingHuntApp::OnStart(const ConfigParams &params)
{
    mTaskQueue.Start();
    mTaskQueue.Push([this, params] {
        LOGD("RingHuntApp::OnStart");
        mConfigParam = params;
        CHECK(HMS_AREngine_ARSession_Create(nullptr, nullptr, &mArSession));
        AREngine_ARConfig *arConfig = nullptr;
        CHECK(HMS_AREngine_ARConfig_Create(mArSession, &arConfig));
        CHECK(HMS_AREngine_ARConfig_SetPreviewSize(mArSession, arConfig, 1440, 1080));
        CHECK(HMS_AREngine_ARConfig_SetUpdateMode(mArSession, arConfig, ARENGINE_UPDATE_MODE_LATEST));
        CHECK(HMS_AREngine_ARConfig_SetPlaneFindingMode(mArSession, arConfig, ARENGINE_PLANE_FINDING_MODE_DISABLED));
        CHECK(HMS_AREngine_ARSession_Configure(mArSession, arConfig));
        HMS_AREngine_ARConfig_Destroy(arConfig);
        CHECK(HMS_AREngine_ARFrame_Create(mArSession, &mArFrame));
        NativeDisplayManager_Rotation displayRotation;
        if (OH_NativeDisplayManager_GetDefaultDisplayRotation(&displayRotation) == DISPLAY_MANAGER_OK) {
            mDisplayRotation = ArEngineRotateType(displayRotation);
        }
        HMS_AREngine_ARSession_SetCameraGLTexture(mArSession, mRenderManager.GetPreviewTextureId());
        CHECK(HMS_AREngine_ARSession_SetDisplayGeometry(mArSession, mDisplayRotation, mWidth, mHeight));
    });
}

void RingHuntApp::OnStop()
{
    isPaused = true;
    mTaskQueue.Push([this] {
        LOGD("RingHuntApp::OnStop");
        if (mRingAnchor != nullptr) {
            CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, mRingAnchor));
            HMS_AREngine_ARAnchor_Release(mRingAnchor);
            mRingAnchor = nullptr;
        }
        if (mArFrame != nullptr) {
            HMS_AREngine_ARFrame_Destroy(mArFrame);
            mArFrame = nullptr;
        }
        if (mArSession != nullptr) {
            HMS_AREngine_ARSession_Stop(mArSession);
            HMS_AREngine_ARSession_Destroy(mArSession);
            mArSession = nullptr;
        }
    });
}

void RingHuntApp::OnPause()
{
    isPaused = true;
    mTaskQueue.Push([this] { CHECK(HMS_AREngine_ARSession_Pause(mArSession)); });
}

void RingHuntApp::OnResume()
{
    isPaused = false;
    mTaskQueue.Push([this] { HMS_AREngine_ARSession_Resume(mArSession); });
}

void RingHuntApp::OnUpdate()
{
    if (isPaused) {
        LOGD("RingHuntApp::OnUpdate is paused.");
        return;
    }
    mTaskQueue.Push([this] {
        CHECK(HMS_AREngine_ARSession_SetDisplayGeometry(mArSession, mDisplayRotation, mWidth, mHeight));
        HMS_AREngine_ARSession_Update(mArSession, mArFrame);
        if (mIsSurfaceChange) {
            glViewport(0, 0, mWidth, mHeight);
            mRenderManager.DrawBlack();
            mIsSurfaceChange = false;
            return;
        }

        // Frame delta time (clamped) to advance the spinner clock.
        auto now = std::chrono::steady_clock::now();
        float dt = 0.0f;
        if (mHasLastFrame) {
            dt = std::chrono::duration<float>(now - mLastFrameTime).count();
            if (dt < 0.0f) {
                dt = 0.0f;
            }
            if (dt > 0.1f) {
                dt = 0.1f;
            }
        }
        mLastFrameTime = now;
        mHasLastFrame = true;
        mAnimTime += dt; // raw seconds: the renderer derives spin angle (deg/s) + ripple phase

        // Color from last frame's distance (1-frame lag is imperceptible): red far, green near.
        float lastDist = mDistance.load();
        float t = glm::clamp((kColorRedDist - lastDist) / (kColorRedDist - kColorGreenDist), 0.0f, 1.0f);
        glm::vec3 color = glm::mix(kColorRed, kColorGreen, t);

        // 6DoF alignment-frame orientation (from the per-placement random Euler angles). Built here
        // so the same quaternion drives both the render and the alignment math below.
        glm::quat targetQ = glm::quat(glm::vec3(mTargetPitch, mTargetYaw, mTargetRoll));

        RingCameraInfo cam;
        mRenderManager.OnDrawFrame(mArSession, mArFrame, mHasRing.load(), mRingAnchor, mAnimTime, color, lastDist,
                                   mHuntPhase.load(), targetQ, mFrameHueTime, &cam);

        mCameraTracking.store(cam.tracking);
        if (!cam.tracking) {
            return;
        }
        if (!mReady.load()) {
            mReady.store(true);
            LOGI("[RingHunt] tracking ready.");
        }

        if (!mHasRing.load() || mRingAnchor == nullptr) {
            mDistance.store(99.0f);
            mIsTargetInView.store(true); // no beacon -> keep guidance hidden
            mIsBehind.store(false);
            mHuntPhase.store(0);
            mIsAligned.store(false);
            mIsLocked.store(false);
            mIsViewingFromBack.store(false);
            mToAligningTimer = 0.0f;
            mToApproachingTimer = 0.0f;
            mLockTimer = 0.0f;
            mFrameHueTime = mAnimTime;
            return;
        }

        // Beacon world position (translation of its anchor pose) -> distance to the camera.
        glm::vec3 ringPos(0.0f);
        {
            AREngine_ARPose *pose = nullptr;
            if (HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &pose) == ARENGINE_SUCCESS && pose != nullptr) {
                if (HMS_AREngine_ARAnchor_GetPose(mArSession, mRingAnchor, pose) == ARENGINE_SUCCESS) {
                    float m[16] = {0.0f};
                    HMS_AREngine_ARPose_GetMatrix(mArSession, pose, m, 16);
                    ringPos = glm::vec3(m[12], m[13], m[14]);
                }
                HMS_AREngine_ARPose_Destroy(pose);
            }
        }

        // Distance to the beacon TOP (the badge/phone icon at the pillar top), not the ground ring
        // — that is what the user looks at and walks toward.
        float camPos[3] = {cam.pos[0], cam.pos[1], cam.pos[2]};
        float beaconTop[3] = {ringPos.x, ringPos.y + kWayfinderTopHeight, ringPos.z};
        mDistance.store(Compute3DDistance(camPos, beaconTop));

        // --- Stage 11D: 6DoF alignment + 3-state machine (with debounce) ---
        // Camera forward in world space = -(3rd row of the view rotation) for a column-major view.
        glm::vec3 camFwd = -glm::vec3(cam.viewMat[2], cam.viewMat[6], cam.viewMat[10]);
        float camFwdLen = glm::length(camFwd);
        camFwd = (camFwdLen > 1e-5f) ? (camFwd / camFwdLen) : glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 frameNormal = glm::normalize(targetQ * glm::vec3(0.0f, 0.0f, -1.0f));
        float fn[3] = {frameNormal.x, frameNormal.y, frameNormal.z};
        float cf[3] = {camFwd.x, camFwd.y, camFwd.z};
        float yawDiff = 0.0f;
        float pitchDiff = 0.0f;
        ComputeFrameAlignDiff(fn, cf, yawDiff, pitchDiff);
        mYawDiffRad.store(yawDiff);
        mPitchDiffRad.store(pitchDiff);

        // Which side of the frame is the camera on? Aligning needs camFwd ~ frameNormal, achievable
        // only from the -frameNormal side (looking toward +frameNormal through the frame). On that
        // side the camera->frame vector points along +frameNormal (dot > 0). dot < 0 = wrong side.
        glm::vec3 framePosV(beaconTop[0], beaconTop[1], beaconTop[2]);
        glm::vec3 camPosV(camPos[0], camPos[1], camPos[2]);
        glm::vec3 toFrame = glm::normalize(framePosV - camPosV);
        bool viewingBack = glm::dot(frameNormal, toFrame) < 0.0f;
        mIsViewingFromBack.store(viewingBack);

        float dist = mDistance.load();
        bool inRange = dist < kAligningDistMeters;
        bool aligned = inRange && std::fabs(yawDiff) < kAlignAngleRad && std::fabs(pitchDiff) < kAlignAngleRad;
        mIsAligned.store(aligned);

        int phase = mHuntPhase.load();
        if (phase == 0) { // APPROACHING
            mToAligningTimer = inRange ? (mToAligningTimer + dt) : 0.0f;
            if (mToAligningTimer >= kDebounceSec) {
                phase = 1;
                mToApproachingTimer = 0.0f;
                mLockTimer = 0.0f;
                LOGI("[RingHunt] -> ALIGNING");
            }
        } else if (phase == 1) { // ALIGNING
            mToApproachingTimer = (!inRange) ? (mToApproachingTimer + dt) : 0.0f;
            if (mToApproachingTimer >= kDebounceSec) {
                phase = 0;
                mToAligningTimer = 0.0f;
                mLockTimer = 0.0f;
                LOGI("[RingHunt] -> APPROACHING");
            } else {
                mLockTimer = aligned ? (mLockTimer + dt) : 0.0f;
                if (mLockTimer >= kLockSec) {
                    phase = 2;
                    LOGI("[RingHunt] -> LOCKED");
                }
            }
        }
        // phase == 2 (LOCKED) stays until reset.
        mHuntPhase.store(phase);
        mIsLocked.store(phase == 2);
        if (phase != 2) {
            mFrameHueTime = mAnimTime; // keep hue live until locked, then freeze at this value
        }

        // Off-screen guidance (droplet). APPROACHING: target the beacon top. ALIGNING + wrong side:
        // target a point 0.5m on the correct (-frameNormal) side so the droplet leads you around.
        // Otherwise (ALIGNING front / LOCKED): no droplet (HUD handles it) -> mark "in view".
        bool wantGuide = (phase == 0) || (phase == 1 && viewingBack);
        if (wantGuide) {
            float guideTarget[3];
            if (phase == 0) {
                guideTarget[0] = beaconTop[0];
                guideTarget[1] = beaconTop[1];
                guideTarget[2] = beaconTop[2];
            } else {
                glm::vec3 g = framePosV - frameNormal * 0.5f; // correct side of the frame
                guideTarget[0] = g.x;
                guideTarget[1] = g.y;
                guideTarget[2] = g.z;
            }
            OffscreenGuidance guide;
            ComputeOffscreenGuidance(cam.viewMat, cam.projMat, guideTarget, guide);
            mIsTargetInView.store(guide.isInView);
            mScreenEdgeX.store(guide.screenEdgeX);
            mScreenEdgeY.store(guide.screenEdgeY);
            mIsBehind.store(guide.isBehind);
            mIndicatorAngleDeg.store(guide.indicatorAngleDeg);
            mNdcX.store(guide.ndcX);
            mNdcY.store(guide.ndcY);
        } else {
            mIsTargetInView.store(true); // hide the droplet; ALIGNING-front shows the HUD
            mIsBehind.store(false);
        }
    });
}

void RingHuntApp::OnSurfaceCreated(OH_NativeXComponent *component, void *window)
{
    LOGD("RingHuntApp::OnSurfaceCreated");
    int32_t ret = OH_NativeXComponent_GetXComponentSize(component, window, &mWidth, &mHeight);
    if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        OH_NativeXComponent_GetXComponentOffset(component, window, &mX, &mY);
    }
    mTaskQueue.Push([this, window] {
        LOGD("RingHuntRenderManager is init.");
        mRenderManager.Initialize(window, mArSession);
    });
}

void RingHuntApp::OnSurfaceChanged(OH_NativeXComponent *component, void *window)
{
    uint64_t width = 1080;
    uint64_t height = 1920;
    int32_t ret = OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
    if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        OH_NativeXComponent_GetXComponentOffset(component, window, &mX, &mY);
    }
    mTaskQueue.Push([this, width, height] {
        mHeight = height;
        mWidth = width;
        mIsSurfaceChange = true;
        NativeDisplayManager_Rotation displayRotation;
        if (OH_NativeDisplayManager_GetDefaultDisplayRotation(&displayRotation) == DISPLAY_MANAGER_OK) {
            mDisplayRotation = ArEngineRotateType(displayRotation);
        }
    });
}

void RingHuntApp::OnSurfaceDestroyed(OH_NativeXComponent *component, void *window)
{
    LOGD("RingHuntApp::OnSurfaceDestroyed");
    mTaskQueue.Push([this] {
        LOGD("RingHuntRenderManager release.");
        mRenderManager.Release();
    });
}

int32_t RingHuntApp::PlaceRing()
{
    if (mArSession == nullptr || isPaused.load() || !mReady.load()) {
        LOGW("[RingHunt] placeRing: not ready.");
        return -1;
    }
    if (!mCameraTracking.load()) {
        LOGW("[RingHunt] ring placement rejected: not tracking");
        return -1;
    }
    int32_t objectId = mNextId;
    uint32_t seed = std::random_device{}();

    mTaskQueue.Push([this, objectId, seed] {
        // Fresh random 6DoF orientation for the alignment frame each placement. Tuned so the frame
        // generally faces the user, always tilts downward, with a small roll — moderate difficulty.
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> u11(-1.0f, 1.0f);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        mTargetYaw = u11(rng) * kRandYawRad;                          // +/-20 deg
        mTargetPitch = -(kPitchDownMinRad + u01(rng) * kPitchDownSpan); // -30..-10 deg (always down)
        mTargetRoll = u11(rng) * kRandRollRad;                        // +/-15 deg

        AREngine_ARCamera *cam = nullptr;
        CHECK(HMS_AREngine_ARFrame_AcquireCamera(mArSession, mArFrame, &cam));
        AREngine_ARPose *camPose = nullptr;
        CHECK(HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &camPose));
        CHECK(HMS_AREngine_ARCamera_GetPose(mArSession, cam, camPose));
        float raw[7] = {0.0f};
        HMS_AREngine_ARPose_GetPoseRaw(mArSession, camPose, raw, 7);
        HMS_AREngine_ARPose_Destroy(camPose);
        HMS_AREngine_ARCamera_Release(cam);

        float camPos[3] = {raw[4], raw[5], raw[6]};
        float camQuat[4];
        ARObject::UnpackQuatXYZW(raw, ARObject::QuatFormat::XYZW, camQuat);

        // Drop the beacon 1m ahead of the camera, then lower it ~1m to approximate the floor so the
        // ground ring lies flat and the pillar rises to roughly eye level. Identity rotation: the
        // renderer translates only (world +Y up), so the beacon's orientation is irrelevant.
        float targetPos[3];
        ComputeForwardPoint(camPos, camQuat, kPlaceDistance, targetPos);
        targetPos[1] = camPos[1] - kGroundDrop;
        const float identityQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        if (mRingAnchor != nullptr) {
            CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, mRingAnchor));
            HMS_AREngine_ARAnchor_Release(mRingAnchor);
            mRingAnchor = nullptr;
        }
        float poseRaw[7];
        ARObject::PackPoseRaw(identityQuat, targetPos, ARObject::QuatFormat::XYZW, poseRaw);
        AREngine_ARPose *pose = nullptr;
        CHECK(HMS_AREngine_ARPose_Create(mArSession, poseRaw, 7, &pose));
        AREngine_ARAnchor *anchor = nullptr;
        AREngine_ARStatus st = HMS_AREngine_ARSession_AcquireNewAnchor(mArSession, pose, &anchor);
        HMS_AREngine_ARPose_Destroy(pose);
        if (st != ARENGINE_SUCCESS || anchor == nullptr) {
            LOGW("[RingHunt] beacon AcquireNewAnchor failed st=%{public}d.", st);
            return;
        }
        mRingAnchor = anchor;
        // A fresh beacon always starts in APPROACHING.
        mHuntPhase.store(0);
        mIsAligned.store(false);
        mIsLocked.store(false);
        mIsViewingFromBack.store(false);
        mToAligningTimer = 0.0f;
        mToApproachingTimer = 0.0f;
        mLockTimer = 0.0f;
        mFrameHueTime = mAnimTime;
        mHasRing.store(true);
        LOGI("[RingHunt] Beacon placed id=%{public}d pos=(%{public}f,%{public}f,%{public}f) "
             "yaw=%{public}f pitch=%{public}f roll=%{public}f",
             objectId, targetPos[0], targetPos[1], targetPos[2], mTargetYaw, mTargetPitch, mTargetRoll);
    });
    return objectId;
}

void RingHuntApp::ResetRing()
{
    mTaskQueue.Push([this] {
        if (mRingAnchor != nullptr) {
            CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, mRingAnchor));
            HMS_AREngine_ARAnchor_Release(mRingAnchor);
            mRingAnchor = nullptr;
        }
        mHasRing.store(false);
        mDistance.store(99.0f);
        mIsTargetInView.store(true);
        mScreenEdgeX.store(0.5f);
        mScreenEdgeY.store(0.5f);
        mIsBehind.store(false);
        mIndicatorAngleDeg.store(0.0f);
        mNdcX.store(0.0f);
        mNdcY.store(0.0f);
        mHuntPhase.store(0);
        mIsAligned.store(false);
        mIsLocked.store(false);
        mIsViewingFromBack.store(false);
        mYawDiffRad.store(0.0f);
        mPitchDiffRad.store(0.0f);
        mToAligningTimer = 0.0f;
        mToApproachingTimer = 0.0f;
        mLockTimer = 0.0f;
        LOGI("[RingHunt] reset.");
    });
}

void RingHuntApp::GetRingState(float &distance, bool &ringPlaced, int32_t &finishState, bool &isTargetInView,
                               float &screenEdgeX, float &screenEdgeY, bool &isBehind, float &indicatorAngleDeg,
                               float &ndcX, float &ndcY, int32_t &huntPhase, float &yawDiffRad, float &pitchDiffRad,
                               bool &isAligned, bool &isLocked, bool &isViewingFromBack)
{
    distance = mDistance.load();
    ringPlaced = mHasRing.load();
    finishState = 0; // Stage 11A: legacy finish state machine retired; see huntPhase below.
    isTargetInView = mIsTargetInView.load();
    screenEdgeX = mScreenEdgeX.load();
    screenEdgeY = mScreenEdgeY.load();
    isBehind = mIsBehind.load();
    indicatorAngleDeg = mIndicatorAngleDeg.load();
    ndcX = mNdcX.load();
    ndcY = mNdcY.load();
    huntPhase = mHuntPhase.load();
    yawDiffRad = mYawDiffRad.load();
    pitchDiffRad = mPitchDiffRad.load();
    isAligned = mIsAligned.load();
    isLocked = mIsLocked.load();
    isViewingFromBack = mIsViewingFromBack.load();
}

} // namespace ARObject
