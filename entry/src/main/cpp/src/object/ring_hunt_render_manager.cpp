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

#include "ring_hunt_render_manager.h"
#include "app_util.h"
#include "object_math.h"
#include "renderer_ref.h"
#include "utils/log.h"
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>
#include <gtc/type_ptr.hpp>

namespace ARObject {
namespace {
// rgb base colors: ring = distance feedback, arrow = angle feedback (each independently red/green).
const float kColorRed[3] = {0.902f, 0.224f, 0.275f};   // #E63946
const float kColorGreen[3] = {0.000f, 0.902f, 0.463f}; // #00E676
// Fresnel glow colors keyed to the base color.
const float kGlowRed[3] = {1.0f, 0.3f, 0.35f};
const float kGlowGreen[3] = {0.0f, 1.0f, 0.4f};

glm::mat4 QuatXYZWToMat(const float q[4])
{
    return glm::mat4_cast(glm::quat(q[3], q[0], q[1], q[2]));
}
} // namespace

void RingHuntRenderManager::Initialize(void *window, AREngine_ARSession *arSession)
{
    if (!isInited) {
        mRenderContext.Init();
        mRenderSurface.Create(window);
        mRenderContext.MakeCurrent(&mRenderSurface);
        mBackgroundRenderer.InitializeBackGroundGlContent();
        mRingRenderer.InitializeGlContent();
        mDiskRenderer.InitializeGlContent();
        CHECK(HMS_AREngine_ARSession_SetCameraGLTexture(arSession, mBackgroundRenderer.GetTextureId()));
        isInited = true;
        RenderRef::GetInstance().Increment();
    }
}

void RingHuntRenderManager::Release()
{
    if (isInited && RenderRef::GetInstance().IsOne()) {
        mRingRenderer.Release();
        mDiskRenderer.Release();
        mRenderContext.ReleaseCurrent();
        mRenderSurface.Release();
        mRenderContext.Release();
        isInited = false;
    }
    RenderRef::GetInstance().Decrement();
}

void RingHuntRenderManager::DrawBlack()
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mRenderContext.SwapBuffers(&mRenderSurface);
}

bool RingHuntRenderManager::OnDrawFrame(AREngine_ARSession *arSession, AREngine_ARFrame *arFrame, bool hasRing,
                                        AREngine_ARAnchor *ringAnchor, const float *ringQuatXYZW, bool distOnTarget,
                                        bool angOnTarget, float distance, RingCameraInfo *outCam)
{
    if (!isInited) {
        LOGE("RingHuntRenderManager not ready!");
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
    {
        AREngine_ARPose *camPose = nullptr;
        if (HMS_AREngine_ARPose_Create(arSession, nullptr, 0, &camPose) == ARENGINE_SUCCESS && camPose != nullptr) {
            // Display-oriented pose: local X/Y align with the screen (fixes the portrait 90deg
            // axis swap for the yaw/pitch aiming split). The -Z look-at is unchanged, so the
            // alignment angle/distance/finish logic is unaffected.
            if (HMS_AREngine_ARCamera_GetDisplayOrientedPose(arSession, arCamera, camPose) == ARENGINE_SUCCESS) {
                float raw[7] = {0.0f};
                HMS_AREngine_ARPose_GetPoseRaw(arSession, camPose, raw, 7);
                cameraPos = glm::vec3(raw[4], raw[5], raw[6]);
                if (outCam != nullptr) {
                    ARObject::UnpackQuatXYZW(raw, ARObject::QuatFormat::XYZW, outCam->quatXYZW);
                }
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
    }

    mBackgroundRenderer.Draw(arSession, arFrame);

    if (camTracking != ARENGINE_TRACKING_STATE_TRACKING) {
        mRenderContext.SwapBuffers(&mRenderSurface);
        return false;
    }

    if (hasRing && ringAnchor != nullptr && ringQuatXYZW != nullptr) {
        AREngine_ARTrackingState anchorTracking = ARENGINE_TRACKING_STATE_STOPPED;
        CHECK(HMS_AREngine_ARAnchor_GetTrackingState(arSession, ringAnchor, &anchorTracking));
        if (anchorTracking == ARENGINE_TRACKING_STATE_TRACKING) {
            glm::mat4 anchorMat(1.0f);
            AREngine_ARPose *pose = nullptr;
            CHECK(HMS_AREngine_ARPose_Create(arSession, nullptr, 0, &pose));
            CHECK(HMS_AREngine_ARAnchor_GetPose(arSession, ringAnchor, pose));
            CHECK(HMS_AREngine_ARPose_GetMatrix(arSession, pose, glm::value_ptr(anchorMat), 16));
            HMS_AREngine_ARPose_Destroy(pose);
            glm::vec3 ringPos(anchorMat[3].x, anchorMat[3].y, anchorMat[3].z);

            // Stage 8: no spin. Ring colored by distance. The center arrow only goes green once
            // you are BOTH close (distance on target) AND aligned -- so a distant lucky angle
            // does not light it up. (Per user feedback.)
            glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), ringPos) * QuatXYZWToMat(ringQuatXYZW);
            bool arrowOn = distOnTarget && angOnTarget;
            const float *ringBase = distOnTarget ? kColorGreen : kColorRed;
            const float *ringGlow = distOnTarget ? kGlowGreen : kGlowRed;
            const float *arrowBase = arrowOn ? kColorGreen : kColorRed;
            const float *arrowGlow = arrowOn ? kGlowGreen : kGlowRed;
            mRingRenderer.Draw(projectionMat, viewMat, modelMat, glm::value_ptr(cameraPos), ringBase, ringGlow,
                               arrowBase, arrowGlow);

            // Near-proximity billboard glow disk: keeps a visible anchor marker as the ring
            // grows huge / starts to clip near the camera. Color mirrors the ring (distance).
            if (distance < 0.30f) {
                glm::vec3 fwd = cameraPos - ringPos;
                float fwdLen = glm::length(fwd);
                if (fwdLen > 1e-5f) {
                    fwd /= fwdLen;
                    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
                    glm::vec3 right = glm::cross(worldUp, fwd);
                    if (glm::length(right) < 1e-4f) {
                        right = glm::vec3(1.0f, 0.0f, 0.0f); // camera directly above/below
                    } else {
                        right = glm::normalize(right);
                    }
                    glm::vec3 up = glm::cross(fwd, right);
                    glm::mat4 billboard(1.0f);
                    billboard[0] = glm::vec4(right, 0.0f);
                    billboard[1] = glm::vec4(up, 0.0f);
                    billboard[2] = glm::vec4(fwd, 0.0f);
                    glm::mat4 diskModel = glm::translate(glm::mat4(1.0f), ringPos) * billboard;
                    const float *diskColor = distOnTarget ? kColorGreen : kColorRed; // same as ring
                    mDiskRenderer.Draw(projectionMat, viewMat, diskModel, diskColor);
                }
            }
        }
    }

    mRenderContext.SwapBuffers(&mRenderSurface);
    return true;
}

} // namespace ARObject
