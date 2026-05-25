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

#include "object_render_manager.h"
#include "app_util.h"
#include "utils/log.h"
#include "renderer_ref.h"
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

namespace ARObject {

void ObjectRenderManager::Initialize(void *window, AREngine_ARSession *arSession)
{
    LOGI("ObjectRenderManager-----Initialize start.");
    if (!isInited) {
        mRenderContext.Init();
        mRenderSurface.Create(window);
        mRenderContext.MakeCurrent(&mRenderSurface);

        mBackgroundRenderer.InitializeBackGroundGlContent();
        mObjectRenderer.InitializeObjectGlContent("AR_logo_obj.obj", "AR_logo.png");
        // Bind the camera preview stream to the background renderer's texture.
        CHECK(HMS_AREngine_ARSession_SetCameraGLTexture(arSession, mBackgroundRenderer.GetTextureId()));
        isInited = true;
        RenderRef::GetInstance().Increment();
    }
    LOGI("ObjectRenderManager-----Initialize end.");
}

void ObjectRenderManager::Release()
{
    LOGD("ObjectRenderManager-----Release start.");
    if (isInited && RenderRef::GetInstance().IsOne()) {
        mRenderContext.ReleaseCurrent();
        mRenderSurface.Release();
        mRenderContext.Release();
        isInited = false;
    }
    RenderRef::GetInstance().Decrement();
    LOGD("ObjectRenderManager-----Release end.");
}

bool ObjectRenderManager::OnDrawFrame(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame,
                                      const std::vector<PlacedObject> &objects)
{
    if (!isInited) {
        LOGE("ObjectRenderManager not ready!");
        return false;
    }

    glm::mat4 viewMat;
    glm::mat4 projectionMat;
    glm::vec3 cameraPos(0.0f);

    // Background is drawn inside InitializeDraw regardless of tracking state.
    // If the camera is not tracking, skip object rendering (camera image still shows).
    bool tracking = InitializeDraw(arSession, arFrame, &viewMat, &projectionMat, &cameraPos);
    if (!tracking) {
        mRenderContext.SwapBuffers(&mRenderSurface);
        return false;
    }
    RenderObject(arSession, viewMat, projectionMat, objects, cameraPos);
    mRenderContext.SwapBuffers(&mRenderSurface);
    return true;
}

void ObjectRenderManager::DrawBlack()
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mRenderContext.SwapBuffers(&mRenderSurface);
}

bool ObjectRenderManager::InitializeDraw(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame, glm::mat4 *viewMat,
                                         glm::mat4 *projectionMat, glm::vec3 *outCameraPos)
{
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (arSession == nullptr) {
        return false;
    }
    AREngine_ARCamera *arCamera = nullptr;
    CHECK(HMS_AREngine_ARFrame_AcquireCamera(arSession, arFrame, &arCamera));
    CHECK(HMS_AREngine_ARCamera_GetViewMatrix(arSession, arCamera, glm::value_ptr(*viewMat), 16));
    CHECK(HMS_AREngine_ARCamera_GetProjectionMatrix(arSession, arCamera, {0.1f, 100.f}, glm::value_ptr(*projectionMat),
                                                    16));
    AREngine_ARTrackingState cameraTrackingState = ARENGINE_TRACKING_STATE_STOPPED;
    CHECK(HMS_AREngine_ARCamera_GetTrackingState(arSession, arCamera, &cameraTrackingState));

    // Camera world position (translation only; component-order independent) for face-camera yaw.
    if (outCameraPos != nullptr) {
        AREngine_ARPose *camPose = nullptr;
        if (HMS_AREngine_ARPose_Create(arSession, nullptr, 0, &camPose) == ARENGINE_SUCCESS && camPose != nullptr) {
            if (HMS_AREngine_ARCamera_GetPose(arSession, arCamera, camPose) == ARENGINE_SUCCESS) {
                float raw[7] = {0.0f};
                HMS_AREngine_ARPose_GetPoseRaw(arSession, camPose, raw, 7);
                *outCameraPos = glm::vec3(raw[4], raw[5], raw[6]);
            }
            HMS_AREngine_ARPose_Destroy(camPose);
        }
    }

    HMS_AREngine_ARCamera_Release(arCamera);
    mBackgroundRenderer.Draw(arSession, arFrame);
    // Only render virtual objects when the camera is tracking.
    return !(cameraTrackingState != ARENGINE_TRACKING_STATE_TRACKING);
}

void ObjectRenderManager::RenderObject(AREngine_ARSession *arSession, const glm::mat4 &viewMat,
                                       const glm::mat4 &projectionMat, const std::vector<PlacedObject> &objects,
                                       const glm::vec3 &cameraPos)
{
    float lightIntensity = 0.8f;
    bool logThisFrame = (mFaceFrameCounter++ % 60 == 0); // ~1 log / 2s for Gate 1 sampling
    glm::mat4 anchorMat(1.0f);
    for (const auto &object : objects) {
        if (object.anchor == nullptr) {
            continue;
        }
        AREngine_ARTrackingState trackingState = ARENGINE_TRACKING_STATE_STOPPED;
        CHECK(HMS_AREngine_ARAnchor_GetTrackingState(arSession, object.anchor, &trackingState));
        if (trackingState == ARENGINE_TRACKING_STATE_TRACKING) {
            AREngine_ARPose *pose = nullptr;
            CHECK(HMS_AREngine_ARPose_Create(arSession, nullptr, 0, &pose));
            CHECK(HMS_AREngine_ARAnchor_GetPose(arSession, object.anchor, pose));
            CHECK(HMS_AREngine_ARPose_GetMatrix(arSession, pose, glm::value_ptr(anchorMat), 16));
            HMS_AREngine_ARPose_Destroy(pose);

            // Use the anchor's TRANSLATION only; discard its rotation (face-camera fully owns
            // orientation). Then yaw about world +Y so the model front (-Z) points at the camera.
            glm::vec3 anchorPos(anchorMat[3].x, anchorMat[3].y, anchorMat[3].z);
            float objPos[3] = {anchorPos.x, anchorPos.y, anchorPos.z};
            float camPos[3] = {cameraPos.x, cameraPos.y, cameraPos.z};
            float yaw = ComputeYawToFaceCamera(objPos, camPos, kLogoFrontYawOffset);

            glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), anchorPos) *
                                 glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                 glm::scale(glm::mat4(1.0f), glm::vec3(0.2f, 0.2f, 0.2f));

            if (logThisFrame) {
                LOGI("[ARObject][Face] id=%{public}d objPos=(%{public}f,%{public}f,%{public}f) "
                     "camPos=(%{public}f,%{public}f,%{public}f) yaw=%{public}f",
                     object.objectId, anchorPos.x, anchorPos.y, anchorPos.z, cameraPos.x, cameraPos.y, cameraPos.z,
                     yaw);
            }
            mObjectRenderer.Draw(projectionMat, viewMat, modelMat, lightIntensity, object.color);
        }
    }
}

} // namespace ARObject
