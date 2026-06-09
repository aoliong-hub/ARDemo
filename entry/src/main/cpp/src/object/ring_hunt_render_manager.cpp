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
                                        float badgeFadeProgress, float animAge, float clipShiftY,
                                        RingCameraInfo *outCam,
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
                // Hand the full camera pose to OnUpdate so it can snapshot at capture time
                if (outCam != nullptr) {
                    std::memcpy(outCam->camPoseRaw, raw, sizeof(float) * 7);
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
        // Hand the matrices to the app thread to project the beacon top for off-screen guidance.
        std::memcpy(outCam->viewMat, glm::value_ptr(viewMat), sizeof(float) * 16);
        std::memcpy(outCam->projMat, glm::value_ptr(projectionMat), sizeof(float) * 16);
    }

    mBackgroundRenderer.Draw(arSession, arFrame);

    // ── Frame captures: do these BEFORE the tracking check so DA3 / photo captures succeed
    //     even when AR tracking is temporarily lost. Cloud-side VGGT inference does its own
    //     pose estimation from images and does not depend on AR Engine tracking state.
    //     Captured frames are camera-only (no AR overlay drawn yet). ──────────────────────
    //
    // 性能优化:glReadPixels 是同步阻塞调用,会强制 GPU 管线排空再回读像素。
    //   1) 用缩放后的尺寸(capScale=0.5)读,6MB→1.5MB,大幅减少 GPU→CPU 拷贝量
    //   2) Y-flip 改为逐行写入目标 buffer,省掉临时 buffer + 二次拷贝
    //   3) 预分配 target buffer(capBuf_),避免每帧 vector::resize 堆分配
    //   4) clean 和 da3 不会同时触发(互斥),不会双倍阻塞

    constexpr float kCleanCapScale = 1.0f;  // 拍照纯净帧保持全分辨率
    constexpr float kGuideCapScale = 1.0f; // glReadPixels 不做降采样,0.5 只会读左下角 1/4;ArkTS 侧再缩到 512px

    // 拍照纯净帧（全分辨率）
    if (wantCleanCapture && outCleanRGBA != nullptr && captureW > 0 && captureH > 0) {
        const int cleanW = std::max(1, static_cast<int>(captureW * kCleanCapScale));
        const int cleanH = std::max(1, static_cast<int>(captureH * kCleanCapScale));
        size_t bytes = static_cast<size_t>(cleanW) * static_cast<size_t>(cleanH) * 4u;
        if (capBuf_.size() < bytes) { capBuf_.resize(bytes); }
        glReadPixels(0, 0, cleanW, cleanH, GL_RGBA, GL_UNSIGNED_BYTE, capBuf_.data());
        outCleanRGBA->resize(bytes);
        const size_t rowStride = static_cast<size_t>(cleanW) * 4u;
        for (int y = 0; y < cleanH; ++y) {
            uint8_t *dst = outCleanRGBA->data() + y * rowStride;
            const uint8_t *src = capBuf_.data() + (cleanH - 1 - y) * rowStride;
            std::memcpy(dst, src, rowStride);
        }
        if (outCleanW != nullptr) { *outCleanW = cleanW; }
        if (outCleanH != nullptr) { *outCleanH = cleanH; }
        LOGI("ARDA3-CAPTURE clean glReadPixels done %{public}dx%{public}d (scaled from %{public}dx%{public}d)",
             cleanW, cleanH, captureW, captureH);
    }

    // Da3 / guide capture（半分辨率,减少 GPU 停顿）
    if (wantCapture && outCaptureRGBA != nullptr && captureW > 0 && captureH > 0) {
        const int guideW = std::max(1, static_cast<int>(captureW * kGuideCapScale));
        const int guideH = std::max(1, static_cast<int>(captureH * kGuideCapScale));
        size_t bytes = static_cast<size_t>(guideW) * static_cast<size_t>(guideH) * 4u;
        if (capBuf_.size() < bytes) { capBuf_.resize(bytes); }
        glReadPixels(0, 0, guideW, guideH, GL_RGBA, GL_UNSIGNED_BYTE, capBuf_.data());
        outCaptureRGBA->resize(bytes);
        const size_t rowStride = static_cast<size_t>(guideW) * 4u;
        for (int y = 0; y < guideH; ++y) {
            uint8_t *dst = outCaptureRGBA->data() + y * rowStride;
            const uint8_t *src = capBuf_.data() + (guideH - 1 - y) * rowStride;
            std::memcpy(dst, src, rowStride);
        }
        if (outCapW != nullptr) { *outCapW = guideW; }
        if (outCapH != nullptr) { *outCapH = guideH; }
        LOGI("ARDA3-CAP glReadPixels done %{public}dx%{public}d (scaled from %{public}dx%{public}d)",
             guideW, guideH, captureW, captureH);
    }

    // AR overlays (axes, wayfinder) require tracking. If lost, swap and bail — but captures
    // above already succeeded so cloud inference keeps running.
    if (camTracking != ARENGINE_TRACKING_STATE_TRACKING) {
        mRenderContext.SwapBuffers(&mRenderSurface);
        return false;
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
                                      badgeFadeProgress, animAge, clipShiftY);
        }
    }

    mRenderContext.SwapBuffers(&mRenderSurface);
    return true;
}

} // namespace ARObject
