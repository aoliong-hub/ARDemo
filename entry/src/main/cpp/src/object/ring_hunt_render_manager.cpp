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
#include "renderer_ref.h"
#include "utils/log.h"
#include <cstring>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

namespace ARObject {

void RingHuntRenderManager::Initialize(void *window, AREngine_ARSession *arSession)
{
    if (!isInited) {
        mRenderContext.Init();
        mRenderSurface.Create(window);
        mRenderContext.MakeCurrent(&mRenderSurface);
        mBackgroundRenderer.InitializeBackGroundGlContent();
        mWayfinderRenderer.Init();
        CHECK(HMS_AREngine_ARSession_SetCameraGLTexture(arSession, mBackgroundRenderer.GetTextureId()));
        isInited = true;
        RenderRef::GetInstance().Increment();
    }
}

void RingHuntRenderManager::Release()
{
    if (isInited && RenderRef::GetInstance().IsOne()) {
        mWayfinderRenderer.Release();
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
                                        AREngine_ARAnchor *ringAnchor, float animTime, const glm::vec3 &color,
                                        float distance, int huntPhase, const glm::quat &frameOrientation,
                                        float frameHueTime, bool isAligned, float deltaTime, float ringHeight,
                                        float badgeFadeProgress, float animAge, RingCameraInfo *outCam,
                                        bool wantCapture, int captureW, int captureH,
                                        std::vector<uint8_t> *outCaptureRGBA, int *outCapW, int *outCapH,
                                        bool wantCleanCapture,
                                        std::vector<uint8_t> *outCleanRGBA, int *outCleanW, int *outCleanH)
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
            if (HMS_AREngine_ARCamera_GetDisplayOrientedPose(arSession, arCamera, camPose) == ARENGINE_SUCCESS) {
                float raw[7] = {0.0f};
                HMS_AREngine_ARPose_GetPoseRaw(arSession, camPose, raw, 7);
                cameraPos = glm::vec3(raw[4], raw[5], raw[6]);
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
        // Hand the matrices to the app thread to project the beacon top for off-screen guidance.
        std::memcpy(outCam->viewMat, glm::value_ptr(viewMat), sizeof(float) * 16);
        std::memcpy(outCam->projMat, glm::value_ptr(projectionMat), sizeof(float) * 16);
    }

    mBackgroundRenderer.Draw(arSession, arFrame);

    if (camTracking != ARENGINE_TRACKING_STATE_TRACKING) {
        mRenderContext.SwapBuffers(&mRenderSurface);
        return false;
    }

    // 拍照纯净帧:这一刻 framebuffer 只含相机背景画面,wayfinder(信标/炫彩圈/对齐框)还没画。
    // glReadPixels 抓出来的就是不带任何 AR 物体的纯相机照片。Y-flip 同 da3 路径(GL 原点左下 →
    // 图像消费者左上)。共用 captureW/captureH 作为读取尺寸。
    if (wantCleanCapture && outCleanRGBA != nullptr && captureW > 0 && captureH > 0) {
        size_t bytes = static_cast<size_t>(captureW) * static_cast<size_t>(captureH) * 4u;
        outCleanRGBA->resize(bytes);
        glReadPixels(0, 0, captureW, captureH, GL_RGBA, GL_UNSIGNED_BYTE, outCleanRGBA->data());
        size_t rowStride = static_cast<size_t>(captureW) * 4u;
        std::vector<uint8_t> tmp(rowStride);
        for (int y = 0; y < captureH / 2; ++y) {
            uint8_t *top = outCleanRGBA->data() + y * rowStride;
            uint8_t *bot = outCleanRGBA->data() + (captureH - 1 - y) * rowStride;
            std::memcpy(tmp.data(), top, rowStride);
            std::memcpy(top, bot, rowStride);
            std::memcpy(bot, tmp.data(), rowStride);
        }
        if (outCleanW != nullptr) { *outCleanW = captureW; }
        if (outCleanH != nullptr) { *outCleanH = captureH; }
        LOGI("ARDA3-CAPTURE clean glReadPixels done %{public}dx%{public}d", captureW, captureH);
    }

    if (hasRing && ringAnchor != nullptr) {
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

            // Translate the whole beacon to the anchor with world +Y preserved (no anchor
            // rotation), so the ground ring lies flat on the floor and the pillar rises straight up.
            glm::mat4 wayfinderToWorld = glm::translate(glm::mat4(1.0f), ringPos);
            mWayfinderRenderer.Render(viewMat, projectionMat, wayfinderToWorld, cameraPos, color, animTime, distance,
                                      huntPhase, frameOrientation, frameHueTime, isAligned, deltaTime, ringHeight,
                                      badgeFadeProgress, animAge);
        }
    }

    // Da3 capture hook: glReadPixels before SwapBuffers, on the success path only (after camera
    // background quad + beacon overlay). zoom>1 enlarges glViewport off-screen, so reading
    // (0,0,W,H) returns exactly the user-visible region — zoom comes through automatically.
    if (wantCapture && outCaptureRGBA != nullptr && captureW > 0 && captureH > 0) {
        size_t bytes = static_cast<size_t>(captureW) * static_cast<size_t>(captureH) * 4u;
        outCaptureRGBA->resize(bytes);
        glReadPixels(0, 0, captureW, captureH, GL_RGBA, GL_UNSIGNED_BYTE, outCaptureRGBA->data());
        // Y flip: glReadPixels origin is bottom-left; image consumers (encoders, PixelMap) want
        // top-left. Swap row 0<->H-1, 1<->H-2, ... in-place.
        size_t rowStride = static_cast<size_t>(captureW) * 4u;
        std::vector<uint8_t> tmp(rowStride);
        for (int y = 0; y < captureH / 2; ++y) {
            uint8_t *top = outCaptureRGBA->data() + y * rowStride;
            uint8_t *bot = outCaptureRGBA->data() + (captureH - 1 - y) * rowStride;
            std::memcpy(tmp.data(), top, rowStride);
            std::memcpy(top, bot, rowStride);
            std::memcpy(bot, tmp.data(), rowStride);
        }
        if (outCapW != nullptr) { *outCapW = captureW; }
        if (outCapH != nullptr) { *outCapH = captureH; }
        LOGI("ARDA3-CAP glReadPixels done %{public}dx%{public}d", captureW, captureH);
    }

    mRenderContext.SwapBuffers(&mRenderSurface);
    return true;
}

} // namespace ARObject
