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

#include "arrow_align_render_manager.h"
#include "app_util.h"
#include "object/object_math.h"
#include "renderer_ref.h"
#include "utils/log.h"
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>
#include <gtc/type_ptr.hpp>

namespace ArrowAlign {
namespace {
constexpr float kArrowScale = 0.3f;
// Red target arrow colors (rgb in [0,1]).
const float kRedShaft[3] = {0.902f, 0.224f, 0.275f}; // #E63946
const float kRedHead[3] = {0.784f, 0.114f, 0.145f};  // #C81D25

// Build a rotation matrix from an xyzw quaternion (glm::quat takes w,x,y,z).
glm::mat4 QuatXYZWToMat(const float q[4])
{
    return glm::mat4_cast(glm::quat(q[3], q[0], q[1], q[2]));
}
} // namespace

void ArrowAlignRenderManager::Initialize(void *window, AREngine_ARSession *arSession)
{
    if (!isInited) {
        mRenderContext.Init();
        mRenderSurface.Create(window);
        mRenderContext.MakeCurrent(&mRenderSurface);

        mBackgroundRenderer.InitializeBackGroundGlContent();
        mArrowRenderer.InitializeGlContent();
        CHECK(HMS_AREngine_ARSession_SetCameraGLTexture(arSession, mBackgroundRenderer.GetTextureId()));
        isInited = true;
        RenderRef::GetInstance().Increment();
    }
}

void ArrowAlignRenderManager::Release()
{
    if (isInited && RenderRef::GetInstance().IsOne()) {
        mArrowRenderer.Release();
        mRenderContext.ReleaseCurrent();
        mRenderSurface.Release();
        mRenderContext.Release();
        isInited = false;
    }
    RenderRef::GetInstance().Decrement();
}

void ArrowAlignRenderManager::DrawBlack()
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mRenderContext.SwapBuffers(&mRenderSurface);
}

bool ArrowAlignRenderManager::OnDrawFrame(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame, bool hasTarget,
                                          AREngine_ARAnchor *targetAnchor, const float *targetQuatXYZW,
                                          FrameCameraInfo *outCam)
{
    if (!isInited) {
        LOGE("ArrowAlignRenderManager not ready!");
        return false;
    }

    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glm::mat4 viewMat(1.0f);
    glm::mat4 projectionMat(1.0f);
    AREngine_ARCamera *arCamera = nullptr;
    CHECK(HMS_AREngine_ARFrame_AcquireCamera(arSession, arFrame, &arCamera));
    CHECK(HMS_AREngine_ARCamera_GetViewMatrix(arSession, arCamera, glm::value_ptr(viewMat), 16));
    CHECK(HMS_AREngine_ARCamera_GetProjectionMatrix(arSession, arCamera, {0.1f, 100.f}, glm::value_ptr(projectionMat),
                                                    16));
    AREngine_ARTrackingState camTracking = ARENGINE_TRACKING_STATE_STOPPED;
    CHECK(HMS_AREngine_ARCamera_GetTrackingState(arSession, arCamera, &camTracking));

    glm::vec3 cameraPos(0.0f);
    float camQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    {
        AREngine_ARPose *camPose = nullptr;
        if (HMS_AREngine_ARPose_Create(arSession, nullptr, 0, &camPose) == ARENGINE_SUCCESS && camPose != nullptr) {
            if (HMS_AREngine_ARCamera_GetPose(arSession, arCamera, camPose) == ARENGINE_SUCCESS) {
                float raw[7] = {0.0f};
                HMS_AREngine_ARPose_GetPoseRaw(arSession, camPose, raw, 7);
                cameraPos = glm::vec3(raw[4], raw[5], raw[6]);
                // Stage 2 calibration: SDK layout is XYZW.
                ARObject::UnpackQuatXYZW(raw, ARObject::QuatFormat::XYZW, camQuat);
            }
            HMS_AREngine_ARPose_Destroy(camPose);
        }
    }
    HMS_AREngine_ARCamera_Release(arCamera);

    if (outCam != nullptr) {
        outCam->tracking = (camTracking == ARENGINE_TRACKING_STATE_TRACKING);
        outCam->pos[0] = cameraPos.x;
        outCam->pos[1] = cameraPos.y;
        outCam->pos[2] = cameraPos.z;
        outCam->quatXYZW[0] = camQuat[0];
        outCam->quatXYZW[1] = camQuat[1];
        outCam->quatXYZW[2] = camQuat[2];
        outCam->quatXYZW[3] = camQuat[3];
    }

    mBackgroundRenderer.Draw(arSession, arFrame);

    if (camTracking != ARENGINE_TRACKING_STATE_TRACKING) {
        mRenderContext.SwapBuffers(&mRenderSurface);
        return false;
    }

    // RED target arrow: anchor position (world-tracked) + the random target orientation.
    if (hasTarget && targetAnchor != nullptr && targetQuatXYZW != nullptr) {
        AREngine_ARTrackingState anchorTracking = ARENGINE_TRACKING_STATE_STOPPED;
        CHECK(HMS_AREngine_ARAnchor_GetTrackingState(arSession, targetAnchor, &anchorTracking));
        if (anchorTracking == ARENGINE_TRACKING_STATE_TRACKING) {
            glm::mat4 anchorMat(1.0f);
            AREngine_ARPose *pose = nullptr;
            CHECK(HMS_AREngine_ARPose_Create(arSession, nullptr, 0, &pose));
            CHECK(HMS_AREngine_ARAnchor_GetPose(arSession, targetAnchor, pose));
            CHECK(HMS_AREngine_ARPose_GetMatrix(arSession, pose, glm::value_ptr(anchorMat), 16));
            HMS_AREngine_ARPose_Destroy(pose);
            glm::vec3 redPos(anchorMat[3].x, anchorMat[3].y, anchorMat[3].z);

            glm::mat4 redModel = glm::translate(glm::mat4(1.0f), redPos) * QuatXYZWToMat(targetQuatXYZW) *
                                 glm::scale(glm::mat4(1.0f), glm::vec3(kArrowScale));
            mArrowRenderer.Draw(projectionMat, viewMat, redModel, kRedShaft, kRedHead);
        }
    }

    mRenderContext.SwapBuffers(&mRenderSurface);
    return true;
}

} // namespace ArrowAlign
