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

#ifndef OBJECT_AR_APPLICATION_H
#define OBJECT_AR_APPLICATION_H

#include "app_napi.h"
#include "ar/ar_engine_core.h"
#include "object_math.h"
#include "object_render_manager.h"
#include "task_queue.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace ARObject {

// Interface-style object placement scene. Unlike ARWorld/ARMesh, there is NO hit test,
// NO plane detection and NO finger interaction: objects are placed by the upper layer
// calling placeObjectAtWorld / placeObjectInFrontOfCamera (Stage 2/3).
class ARObjectApp : public AppNapi {
public:
    explicit ARObjectApp(std::string &id);
    ~ARObjectApp() override;

    // Lifecycle
    void OnStart(const ConfigParams &params) override;
    void OnUpdate() override;
    void OnPause() override;
    void OnResume() override;
    void OnStop() override;

    // XComponent Callback (no DispatchTouchEvent override on purpose — no finger input)
    void OnSurfaceCreated(OH_NativeXComponent *component, void *window) override;
    void OnSurfaceChanged(OH_NativeXComponent *component, void *window) override;
    void OnSurfaceDestroyed(OH_NativeXComponent *component, void *window) override;

    // Interface-style placement API.
    // STAGE 2: PlaceObjectAtWorld places at an absolute world coord with identity rotation
    // (facing-the-camera yaw is added in Stage 3). RemoveObject/ClearAllObjects are fully
    // implemented now so Stage 3 can use them directly. PlaceObjectInFrontOfCamera stays a
    // base stub (-1) until Stage 3.
    int32_t PlaceObjectAtWorld(float x, float y, float z, const std::string &modelId) override;
    int32_t PlaceObjectInFrontOfCamera(float distance, const std::string &modelId) override;
    bool RemoveObject(int32_t objectId) override;
    int32_t ClearAllObjects() override;

private:
    // Runtime self-check of the SDK quaternion component layout (Addendum 1). Runs once in
    // OnStart, before any placement task, on the task-queue thread.
    void ProbeQuaternionLayout();

    // Shared pre-flight for both placement entry points (caller thread): normalize modelId,
    // reject if session/probe/tracking/limit not ready, else reserve a slot and return a new
    // monotonic objectId (>=1). Returns -1 on rejection.
    int32_t ReservePlacement(std::string &useModel);

    // Build an identity-rotation pose at the given world point, acquire an anchor and store it.
    // Runs on the task-queue thread; releases the reserved slot if AcquireNewAnchor fails.
    void PlaceAtWorldOnTaskThread(float x, float y, float z, int32_t objectId, const std::string &modelId);

    AREngine_ARSession *mArSession = nullptr;
    AREngine_ARFrame *mArFrame = nullptr;

    // Placed objects. Mutated by placement tasks (task thread) and remove/clear (caller
    // thread); guarded by mObjectsMutex. Anchor Detach/Release are always deferred to the
    // task-queue thread.
    std::vector<PlacedObject> mObjects = {};
    std::mutex mObjectsMutex;
    std::atomic<int32_t> mObjectCount{0};   // mirrors mObjects.size() for lock-free limit checks
    std::atomic<int32_t> mNextObjectId{1};  // monotonic; 0 reserved as "invalid"

    // Cached camera tracking state (updated every frame) so placement can reject early
    // without touching the session off-thread.
    std::atomic<bool> mCameraTracking{false};

    // Quaternion layout probe result.
    std::atomic<bool> mProbeDone{false};
    std::atomic<bool> mProbeOk{false};
    QuatFormat mQuatFormat{QuatFormat::XYZW};

    uint64_t mWidth = 1080;
    uint64_t mHeight = 1920;
    AREngine_ARPoseType mDisplayRotation = ARENGINE_POSE_TYPE_IDENTITY;
    double mX = 0.0;
    double mY = 0.0;
    ObjectRenderManager mRenderManager;
    TaskQueue mTaskQueue;
    bool mIsSurfaceChange = false;
    std::atomic<bool> isPaused = false;
    ConfigParams mConfigParam{};
};

} // namespace ARObject

#endif // OBJECT_AR_APPLICATION_H
