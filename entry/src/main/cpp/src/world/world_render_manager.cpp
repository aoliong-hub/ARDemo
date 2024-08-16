/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#include "world_render_manager.h"
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/quaternion.hpp>

#include "app_util.h"
#include "utils/log.h"

int32_t ArWorld::WorldRenderManager::mPlaneCount = 0;
namespace ArWorld {
    void WorldRenderManager::Initialize(void* window, AREngine_ARSession *arSession)
    {
        LOGI("WorldRenderManager-----Initialize() start.");
    
        if (!isInited) {
            mRenderContext.Init();
            mRenderSurface.Create(window);
            mRenderContext.MakeCurrent(&mRenderSurface);
    
            mBackgroundRenderer.InitializeBackGroundGlContent();
            mObjectRenderer.InitializeObjectGlContent("AR_logo_obj.obj", "AR_logo.png");
            mPlaneRenderer.InitializePlaneGlContent();
            // 设置可用于存储相机预览流数据的openGL纹理。应用调用HMS_AREngine_ARSession_Update后，AREngine会更新相机预览到纹理中。
            CHECK(HMS_AREngine_ARSession_SetCameraGLTexture(arSession, mBackgroundRenderer.GetTextureId()));
            isInited = true;
        }
    
        LOGI("WorldRenderManager-----Initialize() end.");
    }

    void WorldRenderManager::Release() 
    {
        LOGD("WorldRenderManager-----Release() start.");
    
        if (isInited) {
            mPlaneRenderer.Release();
            mRenderContext.ReleaseCurrent();
            mRenderSurface.Release();
            mRenderContext.Release();
            isInited = false;
        }

        LOGD("WorldRenderManager-----Release() end.");
    }

    void WorldRenderManager::OnDrawFrame(AREngine_ARSession *arSession,
                                         AREngine_ARFrame *arFrame,
                                         const std::vector<ColoredAnchor> &mColoredAnchors)
    {
        if (!isInited) {
            LOGE("WorldRenderManager not ready!.");
            return;
        }
    
        glm::mat4 viewMat;
        glm::mat4 projectionMat;

        // If the initialization fails, AR scene rendering is not performed.
        if (!InitializeDraw(arSession, arFrame, &viewMat, &projectionMat)) {
            mRenderContext.SwapBuffers(&mRenderSurface);
            return;
        }
        RenderObject(arSession, viewMat, projectionMat, mColoredAnchors);
        RenderPlanes(arSession, viewMat, projectionMat);

        mRenderContext.SwapBuffers(&mRenderSurface);
    }

    bool WorldRenderManager::InitializeDraw(AREngine_ARSession *arSession,
                                            AREngine_ARFrame *arFrame,
                                            glm::mat4 *viewMat,
                                            glm::mat4 *projectionMat)
    {
        // Render the scene.
        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (arSession == nullptr) {
            return false;
        }
        // 更新AREngine的计算结果。
        CHECK(HMS_AREngine_ARSession_Update(arSession, arFrame));
        // 获取当前帧的相机参数。
        AREngine_ARCamera *arCamera = nullptr;
        CHECK(HMS_AREngine_ARFrame_AcquireCamera(arSession, arFrame, &arCamera));
        // 获取最新帧中相机的视图矩阵。
        CHECK(HMS_AREngine_ARCamera_GetViewMatrix(arSession, arCamera, glm::value_ptr(*viewMat), 16));
        // 获取用于在相机图像上层渲染虚拟内容的投影矩阵，可用于相机坐标系到裁剪坐标系转换。Near (0.1) Far (100)。
        CHECK(HMS_AREngine_ARCamera_GetProjectionMatrix(arSession, arCamera, {0.1f, 100.f},
                                                        glm::value_ptr(*projectionMat), 16));
        // 获取相机的当前追踪状态。
        AREngine_ARTrackingState cameraTrackingState = ARENGINE_TRACKING_STATE_STOPPED;
        CHECK(HMS_AREngine_ARCamera_GetTrackingState(arSession, arCamera, &cameraTrackingState));

        HMS_AREngine_ARCamera_Release(arCamera);
        mBackgroundRenderer.Draw(arSession, arFrame);
        // If the camera is not in tracking state, the current frame is not drawn.
        return !(cameraTrackingState != ARENGINE_TRACKING_STATE_TRACKING);
    }


    void WorldRenderManager::RenderObject(AREngine_ARSession *arSession,
                                          const glm::mat4 &viewMat,
                                          const glm::mat4 &projectionMat,
                                          const std::vector<ColoredAnchor> &mColoredAnchors)
    {
        // Set the lighting intensity. The value range from 0.0f to 1.0f.
        float lightIntensity = 0.8f;

        // Initialize the model matrix.
        glm::mat4 modelMat(1.0f);
        for (const auto &coloredAnchor :mColoredAnchors) {
            AREngine_ARTrackingState trackingState = ARENGINE_TRACKING_STATE_STOPPED;
            CHECK(HMS_AREngine_ARAnchor_GetTrackingState(arSession, coloredAnchor.anchor, &trackingState));
            if (trackingState == ARENGINE_TRACKING_STATE_TRACKING) {
                LOGI("WorldRenderManager::RenderObject RenderObject is ARENGINE_TRACKING_STATE_TRACKING!");
                // Draw a virtual object only when the tracking status is AR_TRACKING_STATE_TRACKING.
                AREngine_ARPose *pose = nullptr;
                CHECK(HMS_AREngine_ARPose_Create(arSession, nullptr, 0, &pose));
                CHECK(HMS_AREngine_ARAnchor_GetPose(arSession, coloredAnchor.anchor, pose));
                CHECK(HMS_AREngine_ARPose_GetMatrix(arSession, pose, glm::value_ptr(modelMat), 16));
                HMS_AREngine_ARPose_Destroy(pose);

                // The size of the drawn virtual object is 0.2 times the actual size.
                modelMat = glm::scale(modelMat, glm::vec3(0.2f, 0.2f, 0.2f));
                mObjectRenderer.Draw(projectionMat, viewMat, modelMat, lightIntensity, coloredAnchor.color);
            }
        }
    }

    void WorldRenderManager::RenderPlanes(AREngine_ARSession *arSession,
                                          const glm::mat4 &viewMat,
                                          const glm::mat4 &projectionMat)
    {
        // Update and render the plane.
        AREngine_ARTrackableList *planeList = nullptr;
        // 创建一个可跟踪对象列表。
        CHECK(HMS_AREngine_ARTrackableList_Create(arSession, &planeList));
        // 获取所有指定类型的可跟踪对像集合。
        AREngine_ARTrackableType planeTrackedType = ARENGINE_TRACKABLE_PLANE;
        CHECK(HMS_AREngine_ARSession_GetAllTrackables(arSession, planeTrackedType, planeList));

        int32_t planeListSize = 0;
        // 获取此列表中的可跟踪对象的数量。
        CHECK(HMS_AREngine_ARTrackableList_GetSize(arSession, planeList, &planeListSize));
        mPlaneCount = planeListSize;

        for (int i = 0; i < planeListSize; ++i) {
            AREngine_ARTrackable *arTrackable = nullptr;
            // 从可跟踪列表中获取指定index的对象。
            CHECK(HMS_AREngine_ARTrackableList_AcquireItem(arSession, planeList, i, &arTrackable));
            AREngine_ARPlane *arPlane = reinterpret_cast<AREngine_ARPlane *>(arTrackable);

            // 获取当前可跟踪对象的跟踪状态。如果状态为：ARENGINE_TRACKING_STATE_TRACKING（可跟踪状态）才进行绘制。
            AREngine_ARTrackingState outTrackingState;
            CHECK(HMS_AREngine_ARTrackable_GetTrackingState(arSession, arTrackable, &outTrackingState));
            AREngine_ARPlane *subsumePlane = nullptr;
            // 获取平面的父平面（一个平面被另一个平面合并时，会产生父平面），如果无父平面返回为NULL。
            CHECK(HMS_AREngine_ARPlane_AcquireSubsumedBy(arSession, arPlane, &subsumePlane));
            if (subsumePlane != nullptr) {
                HMS_AREngine_ARTrackable_Release(reinterpret_cast<AREngine_ARTrackable *>(subsumePlane));
                continue;
            }
            if (AREngine_ARTrackingState::ARENGINE_TRACKING_STATE_TRACKING != outTrackingState) {
                continue;
            }
            glm::vec3 color;
            RendererPlane(arPlane, arTrackable, color);
            mPlaneRenderer.Draw(projectionMat, viewMat, arSession, arPlane, color);
        }

        HMS_AREngine_ARTrackableList_Destroy(planeList);
        planeList = nullptr;
    }

    void WorldRenderManager::RendererPlane(AREngine_ARPlane *arPlane, AREngine_ARTrackable *arTrackable, glm::vec3 &color)
    {
        const auto iter = mPlaneColorMap.find(arPlane);
        if (iter != mPlaneColorMap.end()) {
            color = iter->second;
            HMS_AREngine_ARTrackable_Release(arTrackable);
        } else {
            // Set the plane color. The first plane is white, and the other planes are blue.
            if (!firstPlaneHasBeenFound) {
                firstPlaneHasBeenFound = true;
                color = {255, 255, 255};
            } else {
                color = {0, 206, 209};
            }
            mPlaneColorMap.insert(std::pair<AREngine_ARPlane *, glm::vec3>(arPlane, color));
        }
    }
}