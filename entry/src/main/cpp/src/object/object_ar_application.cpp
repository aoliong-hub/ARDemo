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

#include "object_ar_application.h"
#include "app_util.h"
#include "object_math.h"
#include "utils/log.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <algorithm>
#include <cmath>
#include <window_manager/oh_display_info.h>
#include <window_manager/oh_display_manager.h>

namespace ARObject {

ARObjectApp::ARObjectApp(std::string &id) : AppNapi(id) { LOGD("ARObjectApp::Constructor"); }

ARObjectApp::~ARObjectApp()
{
    LOGD("ARObjectApp::Destructor");
    mTaskQueue.Stop();
}

void ARObjectApp::OnStart(const ConfigParams &params)
{
    mTaskQueue.Start();
    mTaskQueue.Push([this, params] {
        LOGD("ARObjectApp::OnStart");
        mConfigParam = params;
        // Create an AREngine_ARSession session.
        CHECK(HMS_AREngine_ARSession_Create(nullptr, nullptr, &mArSession));
        // Configure AREngine_ARSession.
        AREngine_ARConfig *arConfig = nullptr;
        CHECK(HMS_AREngine_ARConfig_Create(mArSession, &arConfig));
        CHECK(HMS_AREngine_ARConfig_SetPreviewSize(mArSession, arConfig, 1440, 1080));
        CHECK(HMS_AREngine_ARConfig_SetUpdateMode(mArSession, arConfig, ARENGINE_UPDATE_MODE_LATEST));
        // Interface-style placement does not rely on plane detection — disable it.
        CHECK(HMS_AREngine_ARConfig_SetPlaneFindingMode(mArSession, arConfig, ARENGINE_PLANE_FINDING_MODE_DISABLED));
        CHECK(HMS_AREngine_ARSession_Configure(mArSession, arConfig));
        HMS_AREngine_ARConfig_Destroy(arConfig);
        // Create an AREngine_ARFrame object.
        CHECK(HMS_AREngine_ARFrame_Create(mArSession, &mArFrame));
        // Self-check the SDK quaternion layout before any placement can run (same task thread,
        // so this completes before any placeObjectAtWorld task). Addendum 1.
        ProbeQuaternionLayout();
        NativeDisplayManager_Rotation displayRotation;
        if (OH_NativeDisplayManager_GetDefaultDisplayRotation(&displayRotation) == DISPLAY_MANAGER_OK) {
            mDisplayRotation = ArEngineRotateType(displayRotation);
        }
        HMS_AREngine_ARSession_SetCameraGLTexture(mArSession, mRenderManager.GetPreviewTextureId());
        CHECK(HMS_AREngine_ARSession_SetDisplayGeometry(mArSession, mDisplayRotation, mWidth, mHeight));
    });
}

void ARObjectApp::OnStop()
{
    isPaused = true;
    mTaskQueue.Push([this] {
        LOGD("ARObjectApp::OnStop");
        {
            std::lock_guard<std::mutex> lock(mObjectsMutex);
            for (auto &object : mObjects) {
                if (object.anchor != nullptr) {
                    CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, object.anchor));
                    HMS_AREngine_ARAnchor_Release(object.anchor);
                }
            }
            mObjects.clear();
        }
        mObjectCount.store(0);

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

void ARObjectApp::OnPause()
{
    isPaused = true;
    mTaskQueue.Push([this] { CHECK(HMS_AREngine_ARSession_Pause(mArSession)); });
}

void ARObjectApp::OnResume()
{
    isPaused = false;
    mTaskQueue.Push([this] { HMS_AREngine_ARSession_Resume(mArSession); });
}

void ARObjectApp::OnUpdate()
{
    if (isPaused) {
        LOGD("ARObjectApp::OnUpdate is paused.");
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
        // Snapshot the object list under lock so remove/clear on the caller thread can't
        // mutate the vector mid-draw. Anchor pointers stay valid this frame because their
        // Detach/Release is deferred to a later task on this same thread.
        std::vector<PlacedObject> snapshot;
        {
            std::lock_guard<std::mutex> lock(mObjectsMutex);
            snapshot = mObjects;
        }
        bool tracking = mRenderManager.OnDrawFrame(mArSession, mArFrame, snapshot);
        mCameraTracking.store(tracking);
    });
}

void ARObjectApp::OnSurfaceCreated(OH_NativeXComponent *component, void *window)
{
    LOGD("ARObjectApp::OnSurfaceCreated");
    int32_t ret = OH_NativeXComponent_GetXComponentSize(component, window, &mWidth, &mHeight);
    LOGD("ARObjectApp::OnSurfaceCreated size (%{public}lu, %{public}lu).", mWidth, mHeight);
    if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        ret = OH_NativeXComponent_GetXComponentOffset(component, window, &mX, &mY);
        if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            LOGD("ARObjectApp::OnSurfaceCreated Offset : x = %{public}lf, y = %{public}lf.", mX, mY);
        }
    }
    mTaskQueue.Push([this, window] {
        LOGD("ObjectRenderManager is init.");
        mRenderManager.Initialize(window, mArSession);
    });
}

void ARObjectApp::OnSurfaceChanged(OH_NativeXComponent *component, void *window)
{
    uint64_t width = 1080;
    uint64_t height = 1920;
    int32_t ret = OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
    LOGD("ARObjectApp::OnSurfaceChanged(%{public}lu, %{public}lu).", width, height);
    if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        ret = OH_NativeXComponent_GetXComponentOffset(component, window, &mX, &mY);
        if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            LOGE("Failed to get offset.");
        }
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

void ARObjectApp::OnSurfaceDestroyed(OH_NativeXComponent *component, void *window)
{
    LOGD("ARObjectApp::OnSurfaceDestroyed");
    mTaskQueue.Push([this] {
        LOGD("ObjectRenderManager release.");
        mRenderManager.Release();
    });
}

void ARObjectApp::ProbeQuaternionLayout()
{
    // cos(45 deg) == sin(45 deg). A unit quaternion for Ry(+90 deg) is (0, sin45, 0, cos45) in xyzw.
    constexpr float k = 0.70710678118654752440f;
    float candidateXYZW[7] = {0.0f, k, 0.0f, k, 0.0f, 0.0f, 0.0f}; // assumes SDK reads xyzw
    float candidateWXYZ[7] = {k, 0.0f, k, 0.0f, 0.0f, 0.0f, 0.0f}; // assumes SDK reads wxyz

    auto evalCandidate = [this](const float *raw, float *outMat16) -> bool {
        AREngine_ARPose *pose = nullptr;
        if (HMS_AREngine_ARPose_Create(mArSession, raw, 7, &pose) != ARENGINE_SUCCESS || pose == nullptr) {
            return false;
        }
        AREngine_ARStatus st = HMS_AREngine_ARPose_GetMatrix(mArSession, pose, outMat16, 16);
        HMS_AREngine_ARPose_Destroy(pose);
        return st == ARENGINE_SUCCESS;
    };

    // SDK matrix is column-major (ar_engine_core.h). Canonical Ry(+90): local +X -> world -Z,
    // so column 0 = (0,0,-1) -> m[2] ~ -1; Y axis unchanged -> m[5] ~ 1; and m[0] ~ 0.
    auto isRy90 = [](const float *m) -> bool {
        const float tol = 1e-3f;
        return std::fabs(m[2] - (-1.0f)) < tol && std::fabs(m[5] - 1.0f) < tol && std::fabs(m[0]) < tol;
    };

    float matXYZW[16] = {0.0f};
    float matWXYZ[16] = {0.0f};
    bool okXYZW = evalCandidate(candidateXYZW, matXYZW);
    bool okWXYZ = evalCandidate(candidateWXYZ, matWXYZ);
    bool hitXYZW = okXYZW && isRy90(matXYZW);
    bool hitWXYZ = okWXYZ && isRy90(matWXYZ);

    LOGI("[ARObject][QuatProbe] XYZW-candidate ok=%{public}d m0=%{public}f m2=%{public}f m5=%{public}f hit=%{public}d",
         okXYZW, matXYZW[0], matXYZW[2], matXYZW[5], hitXYZW);
    LOGI("[ARObject][QuatProbe] WXYZ-candidate ok=%{public}d m0=%{public}f m2=%{public}f m5=%{public}f hit=%{public}d",
         okWXYZ, matWXYZ[0], matWXYZ[2], matWXYZ[5], hitWXYZ);

    if (hitXYZW && !hitWXYZ) {
        mQuatFormat = QuatFormat::XYZW;
        mProbeOk.store(true);
        LOGI("[ARObject][QuatProbe] result=XYZW");
    } else if (hitWXYZ && !hitXYZW) {
        mQuatFormat = QuatFormat::WXYZ;
        mProbeOk.store(true);
        LOGI("[ARObject][QuatProbe] result=WXYZ");
    } else {
        mProbeOk.store(false);
        LOGE("[ARObject][QuatProbe] FATAL: ambiguous layout (hitXYZW=%{public}d hitWXYZ=%{public}d). "
             "Stage 2 fails, no fallback.",
             hitXYZW, hitWXYZ);
    }
    mProbeDone.store(true);
}

int32_t ARObjectApp::ReservePlacement(std::string &useModel)
{
    if (useModel != "AR_logo") {
        LOGW("[ARObject] placement: unsupported modelId '%{public}s', falling back to 'AR_logo'.", useModel.c_str());
        useModel = "AR_logo";
    }
    if (mArSession == nullptr || isPaused.load()) {
        LOGW("[ARObject] placement: session not ready.");
        return -1;
    }
    if (!mProbeOk.load()) {
        LOGW("[ARObject] placement: quaternion layout probe not OK; refusing to place.");
        return -1;
    }
    if (!mCameraTracking.load()) {
        LOGW("[ARObject] placement: session not ready (camera not tracking yet).");
        return -1;
    }
    if (mObjectCount.load() >= kMaxObjects) {
        LOGW("[ARObject] placement: object limit reached (%{public}d).", kMaxObjects);
        return -1;
    }
    int32_t objectId = mNextObjectId.fetch_add(1);
    mObjectCount.fetch_add(1); // reserve a slot; released if the async acquire fails
    return objectId;
}

void ARObjectApp::PlaceAtWorldOnTaskThread(float x, float y, float z, int32_t objectId, const std::string &modelId)
{
    // Stage 2: identity rotation. Facing-the-camera yaw is added in Stage 3.
    float identityXYZW[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float trans[3] = {x, y, z};
    float poseRaw[7];
    PackPoseRaw(identityXYZW, trans, mQuatFormat, poseRaw);

    AREngine_ARPose *pose = nullptr;
    CHECK(HMS_AREngine_ARPose_Create(mArSession, poseRaw, 7, &pose));
    AREngine_ARAnchor *anchor = nullptr;
    AREngine_ARStatus st = HMS_AREngine_ARSession_AcquireNewAnchor(mArSession, pose, &anchor);
    HMS_AREngine_ARPose_Destroy(pose);
    if (st != ARENGINE_SUCCESS || anchor == nullptr) {
        LOGW("[ARObject] AcquireNewAnchor failed st=%{public}d for objectId=%{public}d.", st, objectId);
        mObjectCount.fetch_sub(1);
        return;
    }
    PlacedObject placed;
    placed.objectId = objectId;
    placed.anchor = anchor;
    placed.modelId = modelId;
    {
        std::lock_guard<std::mutex> lock(mObjectsMutex);
        mObjects.push_back(placed);
    }
    LOGI("[ARObject] AnchorAdded id=%{public}d total=%{public}d pos=(%{public}f,%{public}f,%{public}f)", objectId,
         mObjectCount.load(), x, y, z);
}

int32_t ARObjectApp::PlaceObjectAtWorld(float x, float y, float z, const std::string &modelId)
{
    std::string useModel = modelId;
    int32_t objectId = ReservePlacement(useModel);
    if (objectId < 0) {
        return -1;
    }
    mTaskQueue.Push([this, x, y, z, useModel, objectId] { PlaceAtWorldOnTaskThread(x, y, z, objectId, useModel); });
    return objectId;
}

int32_t ARObjectApp::PlaceObjectInFrontOfCamera(float distance, const std::string &modelId)
{
    std::string useModel = modelId;
    int32_t objectId = ReservePlacement(useModel);
    if (objectId < 0) {
        return -1;
    }
    QuatFormat fmt = mQuatFormat;
    mTaskQueue.Push([this, distance, useModel, objectId, fmt] {
        // Read the current camera world pose (display-oriented, so "forward" matches the screen).
        AREngine_ARCamera *cam = nullptr;
        CHECK(HMS_AREngine_ARFrame_AcquireCamera(mArSession, mArFrame, &cam));
        AREngine_ARPose *camPose = nullptr;
        CHECK(HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &camPose));
        CHECK(HMS_AREngine_ARCamera_GetDisplayOrientedPose(mArSession, cam, camPose));
        float raw[7] = {0.0f};
        HMS_AREngine_ARPose_GetPoseRaw(mArSession, camPose, raw, 7);
        HMS_AREngine_ARPose_Destroy(camPose);
        HMS_AREngine_ARCamera_Release(cam);

        float camPos[3] = {raw[4], raw[5], raw[6]};
        float camQuat[4];
        UnpackQuatXYZW(raw, fmt, camQuat);
        float worldPoint[3];
        ComputeForwardPoint(camPos, camQuat, distance, worldPoint);
        LOGI("[ARObject] InFrontOfCamera dist=%{public}f camPos=(%{public}f,%{public}f,%{public}f) -> "
             "worldPoint=(%{public}f,%{public}f,%{public}f)",
             distance, camPos[0], camPos[1], camPos[2], worldPoint[0], worldPoint[1], worldPoint[2]);
        PlaceAtWorldOnTaskThread(worldPoint[0], worldPoint[1], worldPoint[2], objectId, useModel);
    });
    return objectId;
}

bool ARObjectApp::RemoveObject(int32_t objectId)
{
    if (objectId < 1) {
        LOGW("[ARObject] removeObject: invalid objectId=%{public}d.", objectId);
        return false;
    }
    AREngine_ARAnchor *anchorToRelease = nullptr;
    {
        std::lock_guard<std::mutex> lock(mObjectsMutex);
        auto it = std::find_if(mObjects.begin(), mObjects.end(),
                               [objectId](const PlacedObject &o) { return o.objectId == objectId; });
        if (it == mObjects.end()) {
            LOGW("[ARObject] removeObject: objectId=%{public}d not found.", objectId);
            return false;
        }
        anchorToRelease = it->anchor;
        mObjects.erase(it);
    }
    mObjectCount.fetch_sub(1);
    // Defer anchor teardown to the task-queue thread (session ops off the caller thread).
    mTaskQueue.Push([this, anchorToRelease] {
        if (anchorToRelease != nullptr) {
            CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, anchorToRelease));
            HMS_AREngine_ARAnchor_Release(anchorToRelease);
        }
    });
    LOGI("[ARObject] removeObject: objectId=%{public}d removed, total=%{public}d.", objectId, mObjectCount.load());
    return true;
}

int32_t ARObjectApp::ClearAllObjects()
{
    std::vector<AREngine_ARAnchor *> anchors;
    {
        std::lock_guard<std::mutex> lock(mObjectsMutex);
        for (auto &o : mObjects) {
            if (o.anchor != nullptr) {
                anchors.push_back(o.anchor);
            }
        }
        mObjects.clear();
    }
    int32_t count = static_cast<int32_t>(anchors.size());
    // Best-effort reset; ARObject UI calls are serialized so concurrent place+clear is not expected.
    mObjectCount.store(0);
    if (!anchors.empty()) {
        mTaskQueue.Push([this, anchors] {
            for (auto *anchor : anchors) {
                CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, anchor));
                HMS_AREngine_ARAnchor_Release(anchor);
            }
        });
    }
    LOGI("[ARObject] clearAllObjects: cleared %{public}d objects.", count);
    return count;
}

} // namespace ARObject
