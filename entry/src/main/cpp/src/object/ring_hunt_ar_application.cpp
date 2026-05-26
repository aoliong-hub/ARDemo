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
#include <random>
#include <window_manager/oh_display_info.h>
#include <window_manager/oh_display_manager.h>

namespace ARObject {

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

        // Render with last frame's on-target flags (1-frame lag is imperceptible at 30fps).
        RingCameraInfo cam;
        mRenderManager.OnDrawFrame(mArSession, mArFrame, mHasRing.load(), mRingAnchor, mRingQuatXYZW,
                                   mDistOnTarget.load(), mAngOnTarget.load(), mDistance.load(), &cam);

        mCameraTracking.store(cam.tracking);
        if (!cam.tracking) {
            return;
        }
        if (!mReady.load()) {
            mReady.store(true);
            LOGI("[RingHunt] tracking ready.");
        }

        // Frame delta time (clamped so a stall can't jump the finish timer).
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

        if (!mHasRing.load() || mRingAnchor == nullptr) {
            mDistance.store(99.0f);
            mAngleRad.store(3.14159265358979323846f);
            mDistOnTarget.store(false);
            mAngOnTarget.store(false);
            mIsTargetInView.store(true); // no ring -> keep guidance hidden
            mIsBehind.store(false);
            return;
        }

        // Ring world position (translation of its anchor pose).
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

        float camPos[3] = {cam.pos[0], cam.pos[1], cam.pos[2]};
        float ringPos3[3] = {ringPos.x, ringPos.y, ringPos.z};
        float dist = Compute3DDistance(camPos, ringPos3);

        // Angle: aligned when camera forward points opposite the ring normal (looking through it).
        float camForward[3];
        ComputeArrowDirection(cam.quatXYZW, camForward);
        float ringNormal[3];
        {
            float zPos[3] = {0.0f, 0.0f, 1.0f};
            RotateVectorByQuatXYZW(mRingQuatXYZW, zPos, ringNormal);
        }
        float alignVec[3] = {-ringNormal[0], -ringNormal[1], -ringNormal[2]};
        float d = camForward[0] * alignVec[0] + camForward[1] * alignVec[1] + camForward[2] * alignVec[2];
        if (d > 1.0f) {
            d = 1.0f;
        }
        if (d < -1.0f) {
            d = -1.0f;
        }
        float angle = std::acos(d);

        // Stage 8: independent 2-axis feedback + yaw/pitch split for the 2D aiming HUD.
        bool distOnTarget = IsDistanceOnTarget(dist);
        bool angOnTarget = IsAngleOnTarget(angle);
        float yawDiff = 0.0f;
        float pitchDiff = 0.0f;
        ComputeYawPitchDiff(cam.quatXYZW, ringNormal, yawDiff, pitchDiff);

        FinishState prevFinish = mFinishState;
        mFinishState = UpdateFinishState(mFinishState, mFinishTimerSec, dist, angle, dt);
        if (mFinishState == FinishState::FINISHED && prevFinish != FinishState::FINISHED) {
            float elapsed = std::chrono::duration<float>(now - mStartTime).count();
            mFoundSec.store(elapsed);
            LOGI("[RingHunt] FOUND in %{public}f s", elapsed);
        }

        // Stage 10: project the ring to screen space (using the matrices the renderer just used)
        // to drive the off-screen guidance UI: in/out of view, which edge, behind, arrow angle.
        OffscreenGuidance guide;
        ComputeOffscreenGuidance(cam.viewMat, cam.projMat, ringPos3, guide);
        mIsTargetInView.store(guide.isInView);
        mScreenEdgeX.store(guide.screenEdgeX);
        mScreenEdgeY.store(guide.screenEdgeY);
        mIsBehind.store(guide.isBehind);
        mIndicatorAngleDeg.store(guide.indicatorAngleDeg);

        mDistance.store(dist);
        mAngleRad.store(angle);
        mYawDiffRad.store(yawDiff);
        mPitchDiffRad.store(pitchDiff);
        mDistOnTarget.store(distOnTarget);
        mAngOnTarget.store(angOnTarget);
        mFinishStateInt.store(static_cast<int32_t>(mFinishState));
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

        RingTargetParams params;
        ComputeRingTargetParams(camPos, camQuat, seed, params);
        mRingQuatXYZW[0] = params.targetQuat[0];
        mRingQuatXYZW[1] = params.targetQuat[1];
        mRingQuatXYZW[2] = params.targetQuat[2];
        mRingQuatXYZW[3] = params.targetQuat[3];

        if (mRingAnchor != nullptr) {
            CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, mRingAnchor));
            HMS_AREngine_ARAnchor_Release(mRingAnchor);
            mRingAnchor = nullptr;
        }
        float poseRaw[7];
        ARObject::PackPoseRaw(params.targetQuat, params.targetPos, ARObject::QuatFormat::XYZW, poseRaw);
        AREngine_ARPose *pose = nullptr;
        CHECK(HMS_AREngine_ARPose_Create(mArSession, poseRaw, 7, &pose));
        AREngine_ARAnchor *anchor = nullptr;
        AREngine_ARStatus st = HMS_AREngine_ARSession_AcquireNewAnchor(mArSession, pose, &anchor);
        HMS_AREngine_ARPose_Destroy(pose);
        if (st != ARENGINE_SUCCESS || anchor == nullptr) {
            LOGW("[RingHunt] ring AcquireNewAnchor failed st=%{public}d.", st);
            return;
        }
        mRingAnchor = anchor;
        mFinishState = FinishState::NOT_FINISHED;
        mFinishTimerSec = 0.0f;
        mFoundSec.store(0.0f);
        mFinishStateInt.store(0);
        mStartTime = std::chrono::steady_clock::now();
        mHasRing.store(true);
        LOGI("[RingHunt] Ring placed id=%{public}d pos=(%{public}f,%{public}f,%{public}f) "
             "quat=(%{public}f,%{public}f,%{public}f,%{public}f)",
             objectId, params.targetPos[0], params.targetPos[1], params.targetPos[2], params.targetQuat[0],
             params.targetQuat[1], params.targetQuat[2], params.targetQuat[3]);
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
        mFinishState = FinishState::NOT_FINISHED;
        mFinishTimerSec = 0.0f;
        mHasRing.store(false);
        mDistance.store(99.0f);
        mAngleRad.store(3.14159265358979323846f);
        mYawDiffRad.store(0.0f);
        mPitchDiffRad.store(0.0f);
        mDistOnTarget.store(false);
        mAngOnTarget.store(false);
        mFinishStateInt.store(0);
        mFoundSec.store(0.0f);
        mIsTargetInView.store(true);
        mScreenEdgeX.store(0.5f);
        mScreenEdgeY.store(0.5f);
        mIsBehind.store(false);
        mIndicatorAngleDeg.store(0.0f);
        LOGI("[RingHunt] reset.");
    });
}

void RingHuntApp::GetRingState(float &distance, float &angleRad, float &yawDiffRad, float &pitchDiffRad,
                               bool &distOnTarget, bool &angOnTarget, int32_t &finishState, float &foundSec,
                               bool &isTargetInView, float &screenEdgeX, float &screenEdgeY, bool &isBehind,
                               float &indicatorAngleDeg)
{
    distance = mDistance.load();
    angleRad = mAngleRad.load();
    yawDiffRad = mYawDiffRad.load();
    pitchDiffRad = mPitchDiffRad.load();
    distOnTarget = mDistOnTarget.load();
    angOnTarget = mAngOnTarget.load();
    finishState = mFinishStateInt.load();
    foundSec = mFoundSec.load();
    isTargetInView = mIsTargetInView.load();
    screenEdgeX = mScreenEdgeX.load();
    screenEdgeY = mScreenEdgeY.load();
    isBehind = mIsBehind.load();
    indicatorAngleDeg = mIndicatorAngleDeg.load();
}

} // namespace ARObject
