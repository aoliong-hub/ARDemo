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
#include <window_manager/oh_display_info.h>
#include <window_manager/oh_display_manager.h>

namespace ARObject {
namespace {
// Stage 11C (style C "clean modern"): the beacon tints warm-red -> soft-mint by distance to its
// top badge, softer than the 11A primaries.
const glm::vec3 kColorRed(0.95f, 0.4f, 0.4f);  // warm red (far)
const glm::vec3 kColorGreen(0.5f, 0.9f, 0.7f); // soft mint (near)
constexpr float kColorRedDist = 0.50f;   // >= this metres: fully warm-red
constexpr float kColorGreenDist = 0.40f; // <= this metres: fully soft-mint (10cm smooth band between)
constexpr float kPlaceDistance = 1.0f;   // beacon dropped 1m ahead of the camera
constexpr float kGroundDrop = 1.0f;      // lower it ~1m to approximate the floor
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

        RingCameraInfo cam;
        mRenderManager.OnDrawFrame(mArSession, mArFrame, mHasRing.load(), mRingAnchor, mAnimTime, color, lastDist,
                                   &cam);

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

        // Off-screen guidance: project the beacon TOP (the badge the user aims at) to screen space
        // using the matrices the renderer just used, driving the edge-pinned droplet arrow UI.
        OffscreenGuidance guide;
        ComputeOffscreenGuidance(cam.viewMat, cam.projMat, beaconTop, guide);
        mIsTargetInView.store(guide.isInView);
        mScreenEdgeX.store(guide.screenEdgeX);
        mScreenEdgeY.store(guide.screenEdgeY);
        mIsBehind.store(guide.isBehind);
        mIndicatorAngleDeg.store(guide.indicatorAngleDeg);
        mNdcX.store(guide.ndcX);
        mNdcY.store(guide.ndcY);
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

    mTaskQueue.Push([this, objectId] {
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
        mHasRing.store(true);
        LOGI("[RingHunt] Beacon placed id=%{public}d pos=(%{public}f,%{public}f,%{public}f)", objectId, targetPos[0],
             targetPos[1], targetPos[2]);
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
        LOGI("[RingHunt] reset.");
    });
}

void RingHuntApp::GetRingState(float &distance, bool &ringPlaced, int32_t &finishState, bool &isTargetInView,
                               float &screenEdgeX, float &screenEdgeY, bool &isBehind, float &indicatorAngleDeg,
                               float &ndcX, float &ndcY)
{
    distance = mDistance.load();
    ringPlaced = mHasRing.load();
    finishState = 0; // Stage 11A: no finish state machine yet (returns in 11B).
    isTargetInView = mIsTargetInView.load();
    screenEdgeX = mScreenEdgeX.load();
    screenEdgeY = mScreenEdgeY.load();
    isBehind = mIsBehind.load();
    indicatorAngleDeg = mIndicatorAngleDeg.load();
    ndcX = mNdcX.load();
    ndcY = mNdcY.load();
}

} // namespace ARObject
