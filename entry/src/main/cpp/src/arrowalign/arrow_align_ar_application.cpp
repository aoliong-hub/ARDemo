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

#include "arrow_align_ar_application.h"
#include "app_util.h"
#include "object/object_math.h"
#include "utils/log.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <window_manager/oh_display_info.h>
#include <window_manager/oh_display_manager.h>

namespace ArrowAlign {

ArrowAlignApp::ArrowAlignApp(std::string &id) : AppNapi(id) { LOGD("ArrowAlignApp::Constructor"); }

ArrowAlignApp::~ArrowAlignApp()
{
    LOGD("ArrowAlignApp::Destructor");
    mTaskQueue.Stop();
}

void ArrowAlignApp::OnStart(const ConfigParams &params)
{
    mTaskQueue.Start();
    mTaskQueue.Push([this, params] {
        LOGD("ArrowAlignApp::OnStart");
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

void ArrowAlignApp::OnStop()
{
    isPaused = true;
    mTaskQueue.Push([this] {
        LOGD("ArrowAlignApp::OnStop");
        if (mTargetAnchor != nullptr) {
            CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, mTargetAnchor));
            HMS_AREngine_ARAnchor_Release(mTargetAnchor);
            mTargetAnchor = nullptr;
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

void ArrowAlignApp::OnPause()
{
    isPaused = true;
    mTaskQueue.Push([this] { CHECK(HMS_AREngine_ARSession_Pause(mArSession)); });
}

void ArrowAlignApp::OnResume()
{
    isPaused = false;
    mTaskQueue.Push([this] { HMS_AREngine_ARSession_Resume(mArSession); });
}

void ArrowAlignApp::OnUpdate()
{
    if (isPaused) {
        LOGD("ArrowAlignApp::OnUpdate is paused.");
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

        FrameCameraInfo cam;
        mRenderManager.OnDrawFrame(mArSession, mArFrame, mTargetPlaced.load(), mTargetAnchor, mTargetQuatXYZW, &cam);

        mCameraTracking.store(cam.tracking);
        if (!cam.tracking) {
            return;
        }
        // Mark ready (and cache initial orientation for reference only) once tracking starts.
        if (!mReady.load()) {
            mInitialQuatXYZW[0] = cam.quatXYZW[0];
            mInitialQuatXYZW[1] = cam.quatXYZW[1];
            mInitialQuatXYZW[2] = cam.quatXYZW[2];
            mInitialQuatXYZW[3] = cam.quatXYZW[3];
            mReady.store(true);
            LOGI("[ArrowAlign] tracking ready (initial quat cached for reference).");
        }
        // Alignment = angle between the random target direction and the camera forward (crosshair).
        if (mTargetPlaced.load()) {
            float angle = ARObject::ComputeAngleBetweenArrows(mTargetQuatXYZW, cam.quatXYZW);
            mAlignState = ARObject::UpdateAlignState(mAlignState, angle);
            mAngleRad.store(angle);
            mAligned.store(mAlignState == ARObject::AlignState::ALIGNED);
        } else {
            mAlignState = ARObject::AlignState::NOT_ALIGNED;
            mAngleRad.store(3.14159265358979323846f);
            mAligned.store(false);
        }
    });
}

void ArrowAlignApp::OnSurfaceCreated(OH_NativeXComponent *component, void *window)
{
    LOGD("ArrowAlignApp::OnSurfaceCreated");
    int32_t ret = OH_NativeXComponent_GetXComponentSize(component, window, &mWidth, &mHeight);
    if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        OH_NativeXComponent_GetXComponentOffset(component, window, &mX, &mY);
    }
    mTaskQueue.Push([this, window] {
        LOGD("ArrowAlignRenderManager is init.");
        mRenderManager.Initialize(window, mArSession);
    });
}

void ArrowAlignApp::OnSurfaceChanged(OH_NativeXComponent *component, void *window)
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

void ArrowAlignApp::OnSurfaceDestroyed(OH_NativeXComponent *component, void *window)
{
    LOGD("ArrowAlignApp::OnSurfaceDestroyed");
    mTaskQueue.Push([this] {
        LOGD("ArrowAlignRenderManager release.");
        mRenderManager.Release();
    });
}

int32_t ArrowAlignApp::PlaceTargetArrow()
{
    if (mArSession == nullptr || isPaused.load() || !mReady.load()) {
        LOGW("[ArrowAlign] placeTargetArrow: not ready.");
        return -1;
    }
    // Stage 6 hardening: reject if the camera is not currently tracking.
    if (!mCameraTracking.load()) {
        LOGW("[ArrowAlign] target placement rejected: not tracking");
        return -1;
    }
    int32_t objectId = mNextId;

    // Random direction in the front hemisphere: yaw [-90,90], pitch [-45,45]. Drawn on the
    // caller thread (mRng is only touched here) and captured into the task.
    std::uniform_real_distribution<float> yawDist(-static_cast<float>(M_PI) / 2.0f, static_cast<float>(M_PI) / 2.0f);
    std::uniform_real_distribution<float> pitchDist(-static_cast<float>(M_PI) / 4.0f, static_cast<float>(M_PI) / 4.0f);
    float yawOff = yawDist(mRng);
    float pitchOff = pitchDist(mRng);

    mTaskQueue.Push([this, objectId, yawOff, pitchOff] {
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

        // Random target direction in the front hemisphere, then a quaternion that points the
        // arrow's -Z at it. Position is 1m in front of the current camera.
        float targetDir[3];
        ARObject::ComputeRandomTargetDirection(camQuat, yawOff, pitchOff, targetDir);
        ARObject::QuaternionAlignZNegTo(targetDir, mTargetQuatXYZW);
        float worldPoint[3];
        ARObject::ComputeForwardPoint(camPos, camQuat, 1.0f, worldPoint);

        if (mTargetAnchor != nullptr) {
            CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, mTargetAnchor));
            HMS_AREngine_ARAnchor_Release(mTargetAnchor);
            mTargetAnchor = nullptr;
        }
        float poseRaw[7];
        ARObject::PackPoseRaw(mTargetQuatXYZW, worldPoint, ARObject::QuatFormat::XYZW, poseRaw);
        AREngine_ARPose *pose = nullptr;
        CHECK(HMS_AREngine_ARPose_Create(mArSession, poseRaw, 7, &pose));
        AREngine_ARAnchor *anchor = nullptr;
        AREngine_ARStatus st = HMS_AREngine_ARSession_AcquireNewAnchor(mArSession, pose, &anchor);
        HMS_AREngine_ARPose_Destroy(pose);
        if (st != ARENGINE_SUCCESS || anchor == nullptr) {
            LOGW("[ArrowAlign] target AcquireNewAnchor failed st=%{public}d.", st);
            return;
        }
        mTargetAnchor = anchor;
        mTargetPlaced.store(true);
        LOGI("[ArrowAlign] TargetArrow placed id=%{public}d yaw=%{public}f pitch=%{public}f "
             "targetDir=(%{public}f,%{public}f,%{public}f) quat=(%{public}f,%{public}f,%{public}f,%{public}f)",
             objectId, yawOff, pitchOff, targetDir[0], targetDir[1], targetDir[2], mTargetQuatXYZW[0],
             mTargetQuatXYZW[1], mTargetQuatXYZW[2], mTargetQuatXYZW[3]);
    });
    return objectId;
}

void ArrowAlignApp::ResetArrowAlign()
{
    mTaskQueue.Push([this] {
        if (mTargetAnchor != nullptr) {
            CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, mTargetAnchor));
            HMS_AREngine_ARAnchor_Release(mTargetAnchor);
            mTargetAnchor = nullptr;
        }
        mAlignState = ARObject::AlignState::NOT_ALIGNED;
        mTargetPlaced.store(false);
        mAligned.store(false);
        mAngleRad.store(3.14159265358979323846f);
        LOGI("[ArrowAlign] reset.");
    });
}

void ArrowAlignApp::GetAlignmentState(float &angleRad, bool &aligned, bool &ready, bool &targetPlaced)
{
    angleRad = mAngleRad.load();
    aligned = mAligned.load();
    ready = mReady.load();
    targetPlaced = mTargetPlaced.load();
}

} // namespace ArrowAlign
