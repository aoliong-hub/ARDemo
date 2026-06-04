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
#include <cmath>
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>
#include <random>
#include <window_manager/oh_display_info.h>
#include <window_manager/oh_display_manager.h>

namespace ARObject {
namespace {
// Stage 11C (style C "clean modern"): the beacon tints warm-red -> soft-mint by distance to its
// top badge. Stage 11D widened the gradient band to 50cm (red) -> 30cm (mint), matching the
// ALIGNING entry threshold.
const glm::vec3 kColorRed(0.95f, 0.4f, 0.4f);  // warm red (far)
const glm::vec3 kColorGreen(0.5f, 0.9f, 0.7f); // soft mint (near)
constexpr float kColorRedDist = 0.50f;   // >= this metres: fully warm-red
constexpr float kColorGreenDist = 0.30f; // <= this metres: fully soft-mint (20cm smooth band between)
constexpr float kPlaceDistance = 1.0f;   // beacon dropped 1m ahead of the camera
constexpr float kGroundDrop = 1.0f;      // lower it ~1m to approximate the floor

// Stage 11D 6DoF alignment challenge.
// Stage 12C: stays at 30cm after the on-device test — once the frame was shrunk to 5x10cm with a
// hairline border, closing to 15cm put the camera so close that the frame couldn't be seen at all
// (out of focus / clipping). 30cm gives the user enough standoff to actually see and align the
// reticle. Measured to the visible ring/badge at the top of the beacon (ringPos.y + mRingHeight).
constexpr float kAligningDistMeters = 0.30f; // distance threshold to enter ALIGNING
// 角度门限(2026-06-03 状态机 A/B/C/D):
//   D (LOCKED) 进 5° / 出 7°(滞回)— 复用 kFrameAlignEnter/Exit
//   C/D → B(snapped 解锁)= 角度 > kAngleHudExitRad(10°)→ HUD 消失,炫彩框重现,允许重新吸附
constexpr float kFrameAlignEnterRad = 0.0872665f; // 5°
constexpr float kFrameAlignExitRad = 0.1221730f;  // 7°
constexpr float kAngleHudExitRad   = 0.1745329f;  // 10° — snapped 解锁阈值(C/D → B)
constexpr float kDebounceSec = 0.30f;        // hold time for APPROACHING<->ALIGNING transitions
constexpr float kLockSec = 2.00f;            // (deprecated;D 现在角度<5° 即触发,无需 hold)
constexpr float kFadeDurationSec = 2.0f;     // 灰盘/柱子 ↔ 对齐框 渐变 2.0s 全程

// Phase 2(2026-06-02)— 框=目标机位 P 前方 50cm 的视野截面。
//   框中心 = P + frameNormal(targetQ)×0.50m,框尺寸按可见区 FOV 在 50cm 处 88% 填满。
//   ★ 这三个常量必须和 wayfinder_renderer.cpp 里的对应值一致(scale + 几何),否则 fillRatio 算
//     的是"用 A 尺寸 + 用 B 尺寸渲染"两套不同的框,判据失真。
constexpr float kFramePlacementForwardM = 0.50f;  // 框中心离 P 的距离(沿 targetQ 视线)
constexpr float kFrameScale = 4.16f;              // 50cm 处 88% 填可见区(同 wayfinder_renderer.cpp:655)
constexpr float kFrameGeoHalfW = 0.075f * 0.5f;   // 几何 width 半值(同 wayfinder_renderer.cpp:323)
constexpr float kFrameGeoHalfH = 0.10f * 0.5f;    // 几何 height 半值
// fillRatio 判据:框 4 角(已乘 zoom)NDC 占可见区比例 ∈ [enter, max] 才算填满。滞回 enter→exit。
// 修(2026-06-03 问题 4)— 用户选 Y(只 EMA 平滑,门限保持严格):
//   门限保持 0.92/0.88(精准机位优先,宁严勿松);抗抖通过 EMA 平滑(0.30 系数,~100ms 时间常数)
//   实现:fill 计算后做 EMA 平滑,snapReady 判定用平滑值。调试浮层同时显示 raw + smoothed 两个数。
constexpr float kFillEnter = 0.92f;
constexpr float kFillExit  = 0.88f;
constexpr float kFillMax   = 1.00f;
constexpr float kFillEmaAlpha = 0.30f;
constexpr float kSnapHoldSec = 1.5f;   // snapReady 持续多久才触发吸附(特效 + 震动 + snappedOnce)
constexpr float kCGraceSec   = 2.0f;   // B→C 后,HUD 至少展示 2.0s 才允许 C→D(给用户更多对角度时间)
constexpr float kRandYawRad = 0.3490659f;     // +/-20 deg
constexpr float kRandRollRad = 0.2617994f;    // +/-15 deg
constexpr float kPitchDownMinRad = 0.1745329f; // 10 deg minimum downward tilt
constexpr float kPitchDownSpan = 0.3490659f;   // + up to 20 deg more (so -10..-30 deg)
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

        // v13 digital zoom: enlarge glViewport so the camera background quad (clip-space full-
        // screen) and all AR meshes get sampled from a SMALLER center rectangle of the original
        // framebuffer space. Offset is negative (-w*(z-1)/2) so the center stays put. AR Engine's
        // SetDisplayGeometry above keeps using mWidth/mHeight so tracking is unaffected.
        {
            float z = mZoom.load();
            float w = static_cast<float>(mWidth) * z;
            float h = static_cast<float>(mHeight) * z;
            int ox = static_cast<int>((static_cast<float>(mWidth) - w) * 0.5f);
            int oy = static_cast<int>((static_cast<float>(mHeight) - h) * 0.5f);
            glViewport(ox, oy, static_cast<int>(w), static_cast<int>(h));
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

        // 放置序列动画年龄(秒):0..1.3 演序列,≥1.3 稳态(交给 badgeFadeProgress 控制)。
        // -1 = 等待首次可见(信标已放但用户没看到)→ 给 -1,renderer 早 return 不画任何东西。
        float animAge = (mBeaconPlacedAnimTime < 0.0f) ? -1.0f : (mAnimTime - mBeaconPlacedAnimTime);

        // 灰盘/柱子 ↔ 对齐框 渐变进度(基于上一帧距离)。distance<30cm → 朝 1(灰盘淡出/对齐框淡入);
        // ≥30cm → 朝 0(反向)。2.0s 全程。dt 已 clamp ≤0.1s。
        // 关键:只在动画完成(animAge ≥ 1.3)后才推进 — 否则用户走到 30cm 内放信标的话,
        // 1.3s 仪式刚结束 progress 已经是 1,直接看到对齐框,放置序列白演。冻在 0 让序列结束后
        // 再用 2s 平滑过渡到对齐框。
        {
            constexpr float kAnimTotalForFade = 1.3f;
            if (animAge >= kAnimTotalForFade) {
                bool insideForFade = lastDist < kAligningDistMeters;
                float step = (insideForFade ? +1.0f : -1.0f) * (dt / kFadeDurationSec);
                mBadgeFadeProgress = glm::clamp(mBadgeFadeProgress + step, 0.0f, 1.0f);
            } else {
                mBadgeFadeProgress = 0.0f;  // 动画期 + pending 期都冻结为 0(完整看到灰盘形态)
            }
        }

        // 6DoF alignment-frame orientation, built with an EXPLICIT yaw->pitch->roll order (instead of
        // glm::quat(vec3)'s non-intuitive Y*X*Z) so external 6DoF inputs match expectations:
        //   yaw>0 -> frame faces the camera's right (+X), pitch>0 -> up, roll>0 -> clockwise (camera POV).
        // The same quaternion drives both the render and the alignment math below.
        glm::mat4 yawMat = glm::rotate(glm::mat4(1.0f), -mTargetYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 pitchMat = glm::rotate(glm::mat4(1.0f), mTargetPitch, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::mat4 rollMat = glm::rotate(glm::mat4(1.0f), mTargetRoll, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat targetQ = glm::quat_cast(yawMat * pitchMat * rollMat);

        RingCameraInfo cam;
        // Da3 capture: drain mCaptureRequested for this frame, hand a temp buffer to OnDrawFrame.
        // If filled, lock + move into the member buffer + set mFrameReady. Capture size = mWidth/
        // mHeight (the visible viewport — same as what SetDisplayGeometry sees).
        bool wantCap = mCaptureRequested.exchange(false);
        bool wantCleanCap = mCleanCaptureRequested.exchange(false);
        std::vector<uint8_t> capBuf;
        std::vector<uint8_t> cleanCapBuf;
        int capW = 0, capH = 0;
        int cleanCapW = 0, cleanCapH = 0;
        mRenderManager.OnDrawFrame(mArSession, mArFrame, mHasRing.load(), mRingAnchor, mAnimTime, color, lastDist,
                                   mHuntPhase.load(), targetQ, mFrameHueTime, mIsAligned.load(), dt,
                                   mRingHeight.load(), mBadgeFadeProgress, animAge,
                                   // Phase 2 — 把可见区 NDC y center 一起送给 renderer 做 clip-space 框居中。
                                   mVisibleNdcYCenter.load(),
                                   &cam,
                                   wantCap, static_cast<int>(mWidth), static_cast<int>(mHeight),
                                   &capBuf, &capW, &capH,
                                   wantCleanCap, &cleanCapBuf, &cleanCapW, &cleanCapH);
        if (wantCap && !capBuf.empty() && capW > 0 && capH > 0) {
            {
                std::lock_guard<std::mutex> lk(mFrameMutex);
                mLastFrameRGBA = std::move(capBuf);
                mLastFrameW = capW;
                mLastFrameH = capH;
            }
            mFrameReady.store(true);
            // Snapshot the camera pose at capture time so PlaceBeaconInternal can use
            // the same reference frame the cloud saw — prevents beacon drift when the
            // phone moves between capture and placement.
            if (cam.tracking) {
                std::memcpy(mCaptureCamPose, cam.camPoseRaw, sizeof(float) * 7);
                mHasCapturePose = true;
                mCapturePoseTime = std::chrono::steady_clock::now();
            }
            LOGI("ARDA3-CAP frame cached %{public}dx%{public}d bytes=%{public}zu", capW, capH,
                 static_cast<size_t>(capW) * static_cast<size_t>(capH) * 4u);
        } else if (wantCap) {
            // tracking 丢失/render 跳过 → 这一帧 OnDrawFrame 在 swap 之前早 return,glReadPixels
            // 没跑。重新置请求,下一帧(~33ms)再试。和 wantCleanCap 同套修复,避免炫彩球抓帧
            // 在瞬时跟踪丢失时报"抓帧失败"。ArkTS 那 3s poll 期间 tracking 恢复就能抓到。
            mCaptureRequested.store(true);
            LOGW("ARDA3-CAP frame skipped (tracking=%{public}d), re-armed for next frame",
                 cam.tracking ? 1 : 0);
        }

        if (wantCleanCap && !cleanCapBuf.empty() && cleanCapW > 0 && cleanCapH > 0) {
            {
                std::lock_guard<std::mutex> lk(mCleanFrameMutex);
                mLastCleanRGBA = std::move(cleanCapBuf);
                mLastCleanW = cleanCapW;
                mLastCleanH = cleanCapH;
            }
            mCleanFrameReady.store(true);
            LOGI("ARDA3-CAPTURE clean frame cached %{public}dx%{public}d", cleanCapW, cleanCapH);
        } else if (wantCleanCap) {
            // tracking 丢失/render 跳过 → 这一帧 OnDrawFrame 在 swap 之前早 return 了,glReadPixels
            // 根本没跑。把请求重新置上,下一帧(~33ms 后)再试。ArkTS 端 60×50ms=3s poll 期间,
            // tracking 一恢复就能抓到。避免用户因瞬时晃动看到"抓帧失败"。
            mCleanCaptureRequested.store(true);
            LOGW("ARDA3-CAPTURE clean frame skipped (tracking=%{public}d), re-armed for next frame",
                 cam.tracking ? 1 : 0);
        }

        mCameraTracking.store(cam.tracking);
        if (!cam.tracking) {
            return;
        }
        if (!mReady.load()) {
            mReady.store(true);
            LOGI("[RingHunt] tracking ready.");
        }

        // 每帧从 cam.viewMat 算出物理朝向,store 到 mOrientation。ArkTS 抓帧前会读这个判断旋转方向。
        // viewMat 已是 GetDisplayOrientedPose 推出的(renderer 用同一来源,见 render_manager:91),
        // 与 PlaceBeaconInternal 的 camRoll 公式一致。
        {
            glm::vec3 camFwd = -glm::vec3(cam.viewMat[2], cam.viewMat[6], cam.viewMat[10]);
            glm::vec3 camUp(cam.viewMat[1], cam.viewMat[5], cam.viewMat[9]);
            glm::vec3 camRight(cam.viewMat[0], cam.viewMat[4], cam.viewMat[8]);
            float dotR = camRight.y; // worldUp=(0,1,0) dot camRight = camRight.y
            float dotU = camUp.y;
            float frameCamRoll = std::atan2(dotR, dotU);
            constexpr float kRad45 = 0.785398f;
            int32_t ori;
            if (frameCamRoll <= -kRad45) {
                ori = 1;
            } else if (frameCamRoll >= kRad45) {
                ori = 2;
            } else {
                ori = 0;
            }
            mOrientation.store(ori);
        }

        // 标定工具:每帧 snapshot 相机姿态(GetPose + GetDisplayOrientedPose)到锁保护成员。
        // JS 通过 NAPI 同步读取最近一帧的快照(用于标定步骤记录真值)。
        {
            AREngine_ARCamera *calibCam = nullptr;
            if (HMS_AREngine_ARFrame_AcquireCamera(mArSession, mArFrame, &calibCam) == ARENGINE_SUCCESS &&
                calibCam != nullptr) {
                AREngine_ARPose *poseRaw = nullptr;
                AREngine_ARPose *poseDisp = nullptr;
                float bufRaw[7] = {0, 0, 0, 1, 0, 0, 0};
                float bufDisp[7] = {0, 0, 0, 1, 0, 0, 0};
                bool okRaw = false;
                bool okDisp = false;
                if (HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &poseRaw) == ARENGINE_SUCCESS &&
                    poseRaw != nullptr) {
                    if (HMS_AREngine_ARCamera_GetPose(mArSession, calibCam, poseRaw) == ARENGINE_SUCCESS) {
                        HMS_AREngine_ARPose_GetPoseRaw(mArSession, poseRaw, bufRaw, 7);
                        okRaw = true;
                    }
                    HMS_AREngine_ARPose_Destroy(poseRaw);
                }
                if (HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &poseDisp) == ARENGINE_SUCCESS &&
                    poseDisp != nullptr) {
                    if (HMS_AREngine_ARCamera_GetDisplayOrientedPose(mArSession, calibCam, poseDisp) ==
                        ARENGINE_SUCCESS) {
                        HMS_AREngine_ARPose_GetPoseRaw(mArSession, poseDisp, bufDisp, 7);
                        okDisp = true;
                    }
                    HMS_AREngine_ARPose_Destroy(poseDisp);
                }
                HMS_AREngine_ARCamera_Release(calibCam);
                if (okRaw && okDisp) {
                    std::lock_guard<std::mutex> lk(mCalibPoseMutex);
                    for (int i = 0; i < 7; ++i) {
                        mCalibCamRawPose[i] = bufRaw[i];
                        mCalibCamDispPose[i] = bufDisp[i];
                    }
                    mCalibPoseValid = true;
                }
            }
        }

        if (!mHasRing.load() || mRingAnchor == nullptr) {
            mDistance.store(99.0f);
            mIsTargetInView.store(true); // no beacon -> keep guidance hidden
            mIsBehind.store(false);
            mHuntPhase.store(0);
            mIsAligned.store(false);
            mIsAngleAligned.store(false);
            mIsLocked.store(false);
            mToAligningTimer = 0.0f;
            mToApproachingTimer = 0.0f;
            mLockTimer = 0.0f;
            mSnapHoldTimer = 0.0f;
            mSnapHoldSec.store(0.0f);
            mIsSnapReady.store(false);
            mPrevSnapReady = false;
            mWasAngleAligned = false;
            mCEnteredTime = -1.0f;
            mCGraceSec.store(0.0f);
            mFillRatioSmoothed = 0.0f; // 修(问题 4):reset EMA,下次首帧重新用瞬时值初始化
            mBadgeFadeProgress = 0.0f;
            mBeaconPlacedAnimTime = -1.0f;
            mFrameHueTime = mAnimTime;
            return;
        }

        // Beacon world position (translation of its anchor pose) -> distance to the camera.
        glm::vec3 ringPos(0.0f);
        // 修(问题 3 诊断)— 1Hz 打印 anchor 矩阵的 Y 基(应为 (0,1,0) 才证明 anchor 无旋转,柱子真垂直)。
        // 如果 Y 基不是 (0,1,0),说明 anchor 在 AR Engine 里有旋转,而我们只用 translate 部分忽略旋转,
        // 这种情况下柱子虽然渲染时按 worldY 走,看起来还是会"歪"(因为期望 vs 实际有偏差)。
        float anchorYBasisX = 0.0f, anchorYBasisY = 1.0f, anchorYBasisZ = 0.0f;
        {
            AREngine_ARPose *pose = nullptr;
            if (HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &pose) == ARENGINE_SUCCESS && pose != nullptr) {
                if (HMS_AREngine_ARAnchor_GetPose(mArSession, mRingAnchor, pose) == ARENGINE_SUCCESS) {
                    float m[16] = {0.0f};
                    HMS_AREngine_ARPose_GetMatrix(mArSession, pose, m, 16);
                    ringPos = glm::vec3(m[12], m[13], m[14]);
                    // 列优先 mat4:column 1 = Y 基向量(m[4..7])。理想 (0,1,0)。
                    anchorYBasisX = m[4]; anchorYBasisY = m[5]; anchorYBasisZ = m[6];
                }
                HMS_AREngine_ARPose_Destroy(pose);
            }
        }
        // 1Hz 节流诊断打印(60 帧 / 秒 → 60 次为一次)
        {
            static int sPillarDiagCount = 0;
            if ((sPillarDiagCount++ % 60) == 0) {
                LOGI("ARDA3-PILLAR anchor.Y=(%{public}.3f, %{public}.3f, %{public}.3f) "
                     "ringPos=(%{public}.2f, %{public}.2f, %{public}.2f) ringH=%{public}.2f",
                     anchorYBasisX, anchorYBasisY, anchorYBasisZ,
                     ringPos.x, ringPos.y, ringPos.z, mRingHeight.load());
            }
        }

        // Stage 12C (revised): distance is to the RING/badge at the top of the beacon — that's the
        // visible aim point the user lines the camera up with, and what placeRingAt's y parameter
        // controls. Ring height comes from mRingHeight (per-beacon: legacy paths keep the constant
        // 0.9m default, placeRingAt overrides per call). 15cm ALIGNING gate uses this same distance.
        float camPos[3] = {cam.pos[0], cam.pos[1], cam.pos[2]};
        float ringH = mRingHeight.load();
        float ringTop[3] = {ringPos.x, ringPos.y + ringH, ringPos.z};
        mDistance.store(Compute3DDistance(camPos, ringTop));

        // --- Stage 11D: 6DoF alignment + 3-state machine (with debounce) ---
        // Camera forward in world space = -(3rd row of the view rotation) for a column-major view.
        glm::vec3 camFwd = -glm::vec3(cam.viewMat[2], cam.viewMat[6], cam.viewMat[10]);
        float camFwdLen = glm::length(camFwd);
        camFwd = (camFwdLen > 1e-5f) ? (camFwd / camFwdLen) : glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 frameNormal = glm::normalize(targetQ * glm::vec3(0.0f, 0.0f, -1.0f));
        float fn[3] = {frameNormal.x, frameNormal.y, frameNormal.z};
        float cf[3] = {camFwd.x, camFwd.y, camFwd.z};
        float yawDiff = 0.0f;
        float pitchDiff = 0.0f;
        ComputeFrameAlignDiff(fn, cf, yawDiff, pitchDiff);
        mYawDiffRad.store(yawDiff);
        mPitchDiffRad.store(pitchDiff);

        // Stage 12B-1: roll diff — signed radians by which the camera's local up axis is rotated
        // around its forward axis relative to the target frame's up axis. Positive = phone rolled
        // clockwise from user POV vs the target. Camera up = row 1 of the column-major view rotation;
        // frame up = targetQ * (0,1,0). Project frame up onto the camera right-up plane (i.e.
        // strip the forward component) so the angle is measured purely about the camera forward.
        // Used by the on-screen banner only; alignment gate stays yaw/pitch as before.
        glm::vec3 camUp(cam.viewMat[1], cam.viewMat[5], cam.viewMat[9]);
        glm::vec3 frameUp = glm::normalize(targetQ * glm::vec3(0.0f, 1.0f, 0.0f));
        float rollDiff = 0.0f;
        glm::vec3 projFrameUp = frameUp - glm::dot(frameUp, camFwd) * camFwd;
        float projLen = glm::length(projFrameUp);
        if (projLen > 1e-4f) {
            projFrameUp = projFrameUp / projLen;
            float cosA = glm::dot(camUp, projFrameUp);
            glm::vec3 cross = glm::cross(projFrameUp, camUp);
            float sinA = glm::dot(cross, camFwd);
            rollDiff = std::atan2(sinA, cosA);
        }
        mRollDiffRad.store(rollDiff);

        float dist = mDistance.load();
        bool inRange = dist < kAligningDistMeters;

        // Phase 2 — fillRatio 计算:框 4 角投影到 NDC,看是否填可见区。同 renderer 端 framePos /
        // orientation / scale 完全对齐,× zoom(数字 zoom 放大视口,用户看到的 NDC = intrinsic × zoom)。
        glm::vec3 framePosNew = glm::vec3(ringTop[0], ringTop[1], ringTop[2])
                                + frameNormal * kFramePlacementForwardM;
        glm::mat4 frameRot = glm::mat4_cast(targetQ);
        glm::mat4 frameModel = glm::translate(glm::mat4(1.0f), framePosNew) * frameRot;
        const float halfW = kFrameGeoHalfW * kFrameScale;
        const float halfH = kFrameGeoHalfH * kFrameScale;
        float corners[4][3];
        for (int i = 0; i < 4; ++i) {
            float lx = ((i & 1) == 0) ? -halfW : halfW;
            float ly = ((i & 2) == 0) ? -halfH : halfH;
            glm::vec4 wp = frameModel * glm::vec4(lx, ly, 0.0f, 1.0f);
            corners[i][0] = wp.x;
            corners[i][1] = wp.y;
            corners[i][2] = wp.z;
        }
        // 修(2026-06-03)— 把相机视线 + 框法线传给 ComputeFillRatio,几何根治拿反虚满。
        //   camFwd / frameNormal 在 line ~398-401 已算好,直接复用。
        const float camFwdArr[3] = {camFwd.x, camFwd.y, camFwd.z};
        const float frameNormalArr[3] = {frameNormal.x, frameNormal.y, frameNormal.z};
        float fillRatioRaw = ARObject::ComputeFillRatio(cam.viewMat, cam.projMat, corners,
                                                        mVisibleNdcYHalfExtent.load(),
                                                        camFwdArr, frameNormalArr);
        fillRatioRaw *= mZoom.load();
        mFillRatioRaw.store(fillRatioRaw); // 调试浮层显示原始值,对比平滑值
        // 修(问题 4)— EMA 平滑:抑制 fill 在 0.92 边界的抖动,防 snapReady 反复 0↔1 导致 1.5s 持续门重置。
        //   门限保持 0.92(用户要严格);平滑只抑抖,不降低实际要求。
        //   alpha=0.30 → 新观测 30% 权重,历史 70% → 平滑时间常数 ~0.10s(60fps 下)。
        //   首帧或 reset 后用瞬时值初始化(无历史)。
        if (mFillRatioSmoothed < 1e-6f) {
            mFillRatioSmoothed = fillRatioRaw;
        } else {
            mFillRatioSmoothed = (1.0f - kFillEmaAlpha) * mFillRatioSmoothed + kFillEmaAlpha * fillRatioRaw;
        }
        float fillRatio = mFillRatioSmoothed;
        mFillRatio.store(fillRatio); // 调试浮层读这个平滑后的值,snapReady 也用它

        // 重构(2026-06-03)— 三阶段对齐(吸附 = 持续 1.5s 才触发,修 HUD 瞬触 bug + 防误吸附):
        //   ① snapReady(瞬时):dist<30cm + fillRatio ∈ [0.92↔0.88, 1.0]。不含角度。
        //   ② snapTriggered(持续):snapReady 持续 kSnapHoldSec(1.5s)→ 真正"吸附"触发 → phase 0→1。
        //      → 解决"瞬间路过/抖动假触发";同时让 mIsAligned 与 phase 同步,fix HUD 触发时 phase 还没到 1 的旧 bug。
        //   ③ angleAligned:|yaw| + |pitch| 都 < 5°↔7° 滞回。HUD 引导用,LOCK timer 在 phase 1 内按此累加。
        float fillT = mPrevSnapReady ? kFillExit : kFillEnter;
        bool fillOk = (fillRatio >= fillT) && (fillRatio <= kFillMax);
        // 修(2026-06-03)— 朝向粗判 facingOk:yaw < 90° 才算"大致朝向目标",防反向死循环。
        //   配合 ComputeFillRatio 的几何根治(dot<0 时 fill=0)为双保险。
        //   精对角度 5° 仍在 HUD 阶段做(angleAligned),这里只排除"明显拿反 yaw>90°"。
        constexpr float kSnapFacingRad = 1.5707963f; // 90°
        bool facingOk = std::fabs(yawDiff) < kSnapFacingRad;
        bool snapReady = inRange && fillOk && facingOk;
        mPrevSnapReady = snapReady;
        mIsSnapReady.store(snapReady); // 瞬时(调试/UI)

        // snap hold timer:snapReady 为 true 时累加 dt,false 时清零。≥ kSnapHoldSec 即"持续完成"。
        mSnapHoldTimer = snapReady ? (mSnapHoldTimer + dt) : 0.0f;
        mSnapHoldSec.store(mSnapHoldTimer);
        bool snapTriggered = (mSnapHoldTimer >= kSnapHoldSec);

        float angleMag = std::max(std::fabs(yawDiff), std::fabs(pitchDiff));
        float aThresh = mWasAngleAligned ? kFrameAlignExitRad : kFrameAlignEnterRad;
        bool angleAligned = (angleMag < aThresh);
        mWasAngleAligned = angleAligned;
        mIsAngleAligned.store(angleAligned);

        // 重构(2026-06-03 状态机 A/B/C/D)— phase 1 内由 mIsAligned (sticky snapped 标志) 区分 B/C/D:
        //   phase 0 (A 圆环)         : !inRange. 灰盘可见,框不可见,HUD 不可见
        //   phase 1 (B 炫彩框)        : inRange + !snapped. 框可见(pearl),HUD 不可见,snapHoldTimer 累加
        //   phase 1 (C HUD)          : inRange + snapped + angle 5°~10°. 框消失,HUD 可见,引导对角度
        //   phase 2 (D 对齐成功)      : inRange + snapped + angle<5°. + LOCKED banner ("对齐成功/可点拍摄")
        //
        // 关键转换(问题 2 修):
        //   B → C : snapTriggered (snapReady 持续 1.5s)
        //   C → B : angle > 10°  (snapped 解锁,框重现,HUD 消失,允许重新吸附)
        //   C → D : angle < 5°
        //   D → C : angle > 7° (滞回出门)
        //   D → B : angle > 10°
        //   *→A   : !inRange 持续 0.3s
        int phase = mHuntPhase.load();
        bool snapped = mIsAligned.load(); // sticky 标志:由 phase 1→1 内角度变化 / phase 0→1→0 控制

        if (!inRange) {
            // *→A:dist > 30cm 持续 0.3s 缓冲
            mToApproachingTimer += dt;
            if (phase != 0 && mToApproachingTimer >= kDebounceSec) {
                phase = 0;
                snapped = false;
                mSnapHoldTimer = 0.0f;
                mLockTimer = 0.0f;
                mCEnteredTime = -1.0f;
                LOGI("[RingHunt] *→A (dist>30cm)");
            }
        } else {
            // 进入 30cm 范围
            mToApproachingTimer = 0.0f;
            if (phase == 0) {
                phase = 1; // A → B(立即进 B,无防抖)
                snapped = false;
                LOGI("[RingHunt] A→B (entered 30cm)");
            }

            if (!snapped) {
                // B(炫彩框)→ C(HUD):snapReady 持续 1.5s
                if (snapTriggered) {
                    snapped = true;
                    mLockTimer = 0.0f;
                    mCEnteredTime = mAnimTime; // 修(问题 2):记下进 C 的时刻,C→D 需 grace 1.5s
                    LOGI("[RingHunt] B→C (snap held %{public}.2fs, fill=%{public}.2f)",
                         mSnapHoldTimer, fillRatio);
                }
            } else {
                // C 或 D(snapped):由角度决定子状态
                if (angleMag > kAngleHudExitRad) {
                    // C/D → B:角度 > 10°,snapped 解锁,框重现,允许重新吸附
                    snapped = false;
                    mSnapHoldTimer = 0.0f; // 重新计 1.5s
                    mLockTimer = 0.0f;
                    mCEnteredTime = -1.0f; // 修:重置 grace,下次 B→C 重新计 1.5s
                    if (phase == 2) {
                        phase = 1;
                        LOGI("[RingHunt] D→B (angle>%.0f°)", kAngleHudExitRad * 57.2958f);
                    } else {
                        LOGI("[RingHunt] C→B (angle>%.0f°)", kAngleHudExitRad * 57.2958f);
                    }
                } else if (angleAligned) {
                    // C → D:角度 < 5° 且 grace ≥ 1.5s。修(问题 2):防"angle 天然 <5° 时 D 一闪而过"。
                    float graceElapsed = (mCEnteredTime >= 0.0f) ? (mAnimTime - mCEnteredTime) : 0.0f;
                    bool graceOk = (graceElapsed >= kCGraceSec);
                    if (phase != 2 && graceOk) {
                        phase = 2;
                        LOGI("[RingHunt] C→D (LOCKED, grace=%{public}.2fs)", graceElapsed);
                    }
                    // grace 未到时停在 C,继续展示 HUD。D→C 切换不重置 mCEnteredTime(grace 已过)。
                } else {
                    // C(snapped, 角度 5°~10°,HUD 引导中)
                    if (phase == 2) {
                        phase = 1;
                        LOGI("[RingHunt] D→C (angle>7° hysteresis)");
                    }
                }
            }
        }

        // 修(问题 2):暴露 C grace 经过秒数 — snapped 后 = mAnimTime - mCEnteredTime,否则 0。
        {
            float graceElapsed = (snapped && mCEnteredTime >= 0.0f) ? (mAnimTime - mCEnteredTime) : 0.0f;
            mCGraceSec.store(graceElapsed);
        }

        mHuntPhase.store(phase);
        // mIsAligned 现在 = snapped (sticky):
        //   - B→C 边缘 0→1 → renderer 触发吸附特效 + ArkTS 触发 snappedOnce
        //   - C/D→B 边缘 1→0 → renderer 重置 snap FX (mSnapAnimAge=-1,框重现) + ArkTS 淡出 HUD
        //   - D↔C 之间 phase 变但 snapped 保持 → 不重置
        mIsAligned.store(snapped);
        mIsLocked.store(phase == 2);
        if (phase != 2) {
            mFrameHueTime = mAnimTime; // keep hue live until locked, then freeze at this value
        }

        // Off-screen guidance (droplet) only in APPROACHING: project the beacon ring (visible top
        // badge) to screen space. In ALIGNING/LOCKED the droplet is off (orientation is shown by
        // the co-planar disk + HUD). Same ringTop point as the distance / 15cm gate.
        //
        // 修(2026-06-03 方案 A)— mBeaconPlacedAnimTime 移到 PlaceBeaconInternal 里(放置瞬间设),
        //   不再在 OnUpdate 里用 isInView 触发(那条路径在近距离下天生 false,见 PlaceBeaconInternal 注释)。
        //   这里只剩 droplet 引导(仍保留 phase==0 守门)。
        if (phase == 0) {
            OffscreenGuidance guide;
            ComputeOffscreenGuidance(cam.viewMat, cam.projMat, ringTop, guide);
            mIsTargetInView.store(guide.isInView);
            mScreenEdgeX.store(guide.screenEdgeX);
            mScreenEdgeY.store(guide.screenEdgeY);
            mIsBehind.store(guide.isBehind);
            mIndicatorAngleDeg.store(guide.indicatorAngleDeg);
            mNdcX.store(guide.ndcX);
            mNdcY.store(guide.ndcY);
        } else {
            mIsTargetInView.store(true); // hide the droplet in ALIGNING/LOCKED
            mIsBehind.store(false);
        }
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
    // Demo path: random orientation, legacy floor-heuristic position (1m ahead, 1m down),
    // default ring height (0.9m).
    mRingHeight.store(kWayfinderTopHeight);
    return PlaceBeaconInternal(false, 0.0f, 0.0f, 0.0f, false, 0.0f, 0.0f, kWayfinderTopHeight);
}

int32_t RingHuntApp::PlaceRingWithOrientation(float yawDeg, float pitchDeg, float rollDeg)
{
    // External path: caller supplies the 6DoF target. Position + ring height stay legacy.
    float yawRad = 0.0f;
    float pitchRad = 0.0f;
    float rollRad = 0.0f;
    ClampOrientationDegToRad(yawDeg, pitchDeg, rollDeg, yawRad, pitchRad, rollRad);
    mRingHeight.store(kWayfinderTopHeight);
    return PlaceBeaconInternal(true, yawRad, pitchRad, rollRad, false, 0.0f, 0.0f, kWayfinderTopHeight);
}

int32_t RingHuntApp::PlaceRingAt(float x, float y, float z, float yawDeg, float pitchDeg, float rollDeg)
{
    // Stage 12C: camera-relative HORIZONTAL placement with a per-beacon ring/badge height.
    //   x = right        (metres, horizontal — perpendicular to user's heading)
    //   y = height       (metres, world vertical — height of the visible ring/badge above ground)
    //   z = forward      (metres, horizontal — user's heading at call time)
    // Beacon BASE stays ground-snapped (same kGroundDrop heuristic as PlaceRing); only x/z place
    // the base horizontally and y stretches the pillar so the badge lands at the desired height.
    // Aim point + distance gate (15cm) measure to the badge, so y controls "what height the user
    // is asked to aim at". The AR Engine world origin is NOT pinned to session-start camera —
    // hence the camera-relative contract instead of an absolute-world one.
    float yawRad = 0.0f;
    float pitchRad = 0.0f;
    float rollRad = 0.0f;
    ClampOrientationDegToRad(yawDeg, pitchDeg, rollDeg, yawRad, pitchRad, rollRad);
    // da3 的 y 是"目标机位相对当前手机的垂直偏移"(米,+=上抬)。基座 anchor 永远在
    // camPos.y - kGroundDrop(写死地面假设),灰盘/对齐框渲染时位于 anchor.y + mRingHeight。
    // 让灰盘落到 camPos.y + y:
    //   anchor.y + mRingHeight = camPos.y + y
    //   (camPos.y - kGroundDrop) + mRingHeight = camPos.y + y
    //   ∴ mRingHeight = y + kGroundDrop
    // 防御:y 为负(da3"向下")或过小时,光柱高度不能 ≤ 0 → 最小 5cm。
    float ringVisualHeight = y + kGroundDrop;
    if (ringVisualHeight < 0.05f) {
        ringVisualHeight = 0.05f;
    }
    mRingHeight.store(ringVisualHeight);
    return PlaceBeaconInternal(true, yawRad, pitchRad, rollRad, true, x, z, y);
}

int32_t RingHuntApp::PlaceBeaconInternal(bool useGivenOrientation, float yawRad, float pitchRad, float rollRad,
                                         bool useGivenPosition, float relX, float relZ, float ringY)
{
    (void)ringY; // currently stored via mRingHeight at the caller; reserved for future use.
    if (mArSession == nullptr || isPaused.load() || !mReady.load()) {
        LOGW("[RingHunt] placeRing: not ready.");
        return -1;
    }
    if (!mCameraTracking.load()) {
        LOGW("[RingHunt] ring placement rejected: not tracking");
        return -1;
    }
    int32_t objectId = mNextId;
    uint32_t seed = std::random_device{}();

    mTaskQueue.Push([this, objectId, seed, useGivenOrientation, yawRad, pitchRad, rollRad,
                     useGivenPosition, relX, relZ] {
        // Acquire the camera pose first: it gives both the placement position AND the heading
        // (world yaw) that the target orientation is measured relative to.
        // Prefer the snapshot taken at frame-capture time (if within 2 seconds) so the cloud-
        // returned relative displacement is applied to the SAME reference frame the cloud saw.
        // This prevents beacon drift when the phone moves between capture and placement.
        constexpr float kCapturePoseMaxAgeSec = 2.0f;
        float raw[7] = {0.0f};
        bool useSnapshot = false;
        if (mHasCapturePose) {
            auto age = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - mCapturePoseTime).count();
            if (age >= 0.0f && age < kCapturePoseMaxAgeSec) {
                std::memcpy(raw, mCaptureCamPose, sizeof(float) * 7);
                useSnapshot = true;
            }
        }
        if (!useSnapshot) {
            // Fallback: real-time acquisition (for manual ring placement, axis tests, etc.)
            AREngine_ARCamera *cam = nullptr;
            CHECK(HMS_AREngine_ARFrame_AcquireCamera(mArSession, mArFrame, &cam));
            AREngine_ARPose *camPose = nullptr;
            CHECK(HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &camPose));
            // v13 (e): 用 GetDisplayOrientedPose,与 renderer (ring_hunt_render_manager.cpp:91) 对齐。
            // GetPose 返回 sensor 物理姿态(因 sensor 物理 90° 横装,竖屏端平时给出 camRoll≈-90° 脏值);
            // GetDisplayOrientedPose 吃当前 mDisplayRotation 做校正,给"用户视角"姿态,camRoll/Pitch/Yaw
            // 提取出来的都是真实的"用户手机姿态"(端平时 camRoll≈0,camUp≈worldUp)。
            CHECK(HMS_AREngine_ARCamera_GetDisplayOrientedPose(mArSession, cam, camPose));
            HMS_AREngine_ARPose_GetPoseRaw(mArSession, camPose, raw, 7);
            HMS_AREngine_ARPose_Destroy(camPose);
            HMS_AREngine_ARCamera_Release(cam);
        }

        float camPos[3] = {raw[4], raw[5], raw[6]};
        float camQuat[4];
        ARObject::UnpackQuatXYZW(raw, ARObject::QuatFormat::XYZW, camQuat);

        // Camera heading from its FORWARD vector (robust to the raw-pose quaternion convention, which
        // doesn't encode heading as a simple yaw-about-Y). yaw=0 -> world -Z, +yaw -> +X (right). With
        // this, mTargetYaw=relYaw+camYaw makes yaw=0 face along your gaze into the distance (you see
        // the frame's back). No +pi needed — the forward vector already points "away".
        float camForward[3];
        ARObject::ComputeArrowDirection(camQuat, camForward);
        float camYaw = std::atan2(camForward[0], -camForward[2]);
        // 相机俯仰:forward.y 反正弦。相机抬头 → forward.y>0 → camPitch>0(仰为正,和 mTargetPitch
        // 的约定一致:L177 pitchMat=rotate(I, mTargetPitch, (1,0,0)),正=仰)。实测若叠加方向反,
        // 把 +std::asin 改成 -std::asin。
        float camPitch = std::asin(camForward[1]);
        // 相机绕自身视线轴(forward)的扭转 = roll。在"垂直于 forward 的平面内"取
        // 相机 up 相对世界 up 的夹角。稳健公式:roll = atan2(dot(worldUp, camRight), dot(worldUp, camUp))。
        // 相机不歪 → camUp≈worldUp → dotR≈0 → camRoll≈0。
        // 符号方向(顺/逆 vs L177 rollMat=rotate(I, mTargetRoll, (0,0,1)))无法纯推理,实测反了改 -std::atan2。
        const float upLocal[3] = {0.0f, 1.0f, 0.0f};
        const float rightLocal[3] = {1.0f, 0.0f, 0.0f};
        float camUp[3];
        float camRight[3];
        ARObject::RotateVectorByQuatXYZW(camQuat, upLocal, camUp);
        ARObject::RotateVectorByQuatXYZW(camQuat, rightLocal, camRight);
        const float worldUp[3] = {0.0f, 1.0f, 0.0f};
        float dotR = worldUp[0] * camRight[0] + worldUp[1] * camRight[1] + worldUp[2] * camRight[2];
        float dotU = worldUp[0] * camUp[0]    + worldUp[1] * camUp[1]    + worldUp[2] * camUp[2];
        float camRoll = std::atan2(dotR, dotU);

        float relYaw = 0.0f;
        float relPitch = 0.0f;
        float relRoll = 0.0f;
        if (useGivenOrientation) {
            relYaw = yawRad;
            relPitch = pitchRad;
            relRoll = rollRad;
        } else {
            // Fresh random 6DoF orientation: frame generally faces the user, always tilts down,
            // with a small roll — moderate difficulty.
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> u11(-1.0f, 1.0f);
            std::uniform_real_distribution<float> u01(0.0f, 1.0f);
            relYaw = u11(rng) * kRandYawRad;                            // +/-20 deg
            relPitch = -(kPitchDownMinRad + u01(rng) * kPitchDownSpan); // -30..-10 deg (always down)
            relRoll = u11(rng) * kRandRollRad;                          // +/-15 deg
        }
        // yaw / pitch 是 relative,加 cam* 映射到世界绝对值。
        mTargetYaw = relYaw + camYaw;
        mTargetPitch = relPitch + camPitch;
        // roll 不做斜构图:只用 camRoll 判断横竖,吸附到最近的标准端正姿态。da3 relRoll 不使用。
        // |camRoll|<45° → 竖拍 → 端正(0);camRoll≤-45° → 顺时针横 → -90°;camRoll≥+45° → 逆时针横 → +90°。
        // |camRoll|>135° 倒置不特殊处理,会落入 ±90 分支(可接受)。
        constexpr float kRad45 = 0.785398f;   // 45°
        constexpr float kRad90 = 1.570796f;   // 90°
        float snapRoll;
        int32_t orientationCode;
        if (camRoll <= -kRad45) {
            snapRoll = -kRad90;
            orientationCode = 1;
        } else if (camRoll >= kRad45) {
            snapRoll = +kRad90;
            orientationCode = 2;
        } else {
            snapRoll = 0.0f;
            orientationCode = 0;
        }
        mTargetRoll = snapRoll;
        mOrientation.store(orientationCode);
        // getRingState reports the RELATIVE target (what the caller requested / the random range).
        mTargetYawDeg.store(glm::degrees(relYaw));
        mTargetPitchDeg.store(glm::degrees(relPitch));
        mTargetRollDeg.store(glm::degrees(relRoll));

        // Position: legacy heuristic (1m ahead) or camera-relative HORIZONTAL offset (Stage 12C
        // PlaceRingAt). In both paths the BASE is ground-snapped via kGroundDrop — the pillar/badge
        // rises from the floor; ring/badge height is per-beacon (mRingHeight) and lives in the
        // renderer, not in the anchor pose.
        float targetPos[3];
        if (useGivenPosition) {
            // Camera forward in world frame, projected onto the horizontal plane so "forward"
            // tracks the user's heading regardless of phone tilt. Horizontal right is
            // cross(fwdHoriz, worldUp(0,1,0)) = (-fwdHoriz.z, 0, fwdHoriz.x). The reverse order
            // (cross(up, fwd)) gives LEFT — verified on device: x=+1 placed beacon on user's left
            // until this fix.
            const float fwdLocal[3] = {0.0f, 0.0f, -1.0f};
            float camFwdWorld[3] = {0.0f, 0.0f, 0.0f};
            ARObject::RotateVectorByQuatXYZW(camQuat, fwdLocal, camFwdWorld);
            float fwdHoriz[3] = {camFwdWorld[0], 0.0f, camFwdWorld[2]};
            float fwdLen = std::sqrt(fwdHoriz[0] * fwdHoriz[0] + fwdHoriz[2] * fwdHoriz[2]);
            if (fwdLen < 1e-4f) {
                // Camera pointing straight up/down — horizontal projection degenerate. Fallback +Z.
                fwdHoriz[0] = 0.0f;
                fwdHoriz[2] = 1.0f;
            } else {
                fwdHoriz[0] /= fwdLen;
                fwdHoriz[2] /= fwdLen;
            }
            float rightHoriz[3] = {-fwdHoriz[2], 0.0f, fwdHoriz[0]};
            targetPos[0] = camPos[0] + rightHoriz[0] * relX + fwdHoriz[0] * relZ;
            targetPos[1] = camPos[1] - kGroundDrop;
            targetPos[2] = camPos[2] + rightHoriz[2] * relX + fwdHoriz[2] * relZ;
        } else {
            ComputeForwardPoint(camPos, camQuat, kPlaceDistance, targetPos);
            targetPos[1] = camPos[1] - kGroundDrop;
        }
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
        // A fresh beacon always starts in APPROACHING.
        mHuntPhase.store(0);
        mIsAligned.store(false);
        mIsAngleAligned.store(false);
        mIsLocked.store(false);
        mToAligningTimer = 0.0f;
        mToApproachingTimer = 0.0f;
        mLockTimer = 0.0f;
        mSnapHoldTimer = 0.0f;
        mSnapHoldSec.store(0.0f);
        mIsSnapReady.store(false);
        mPrevSnapReady = false;
        mWasAngleAligned = false;
        mFrameHueTime = mAnimTime;
        // 修(2026-06-03 方案 A)— 放置瞬间立即触发动画,不再等 isInView。
        //   原 bug:isInView 在近距离(7cm)下因 badge 投影超出 viewport(用户看远处的框,badge 偏出镜头)
        //          天生 false → mBeaconPlacedAnimTime 永远 -1 → animAge=-1 → renderer 早 return → 框不画。
        //   修法:放下信标的瞬间(PlaceBeaconInternal 创建 anchor 成功后)直接设 mBeaconPlacedAnimTime,
        //         放置动画立即从头播 1.3s。完成后 badgeFadeProgress 开始按 dist<30cm 推进框 alpha。
        //   远处放置场景:也立即播(用户可能没看到 1.3s 序列,但损失可接受 — 用户主动点了目标图)。
        mBeaconPlacedAnimTime = mAnimTime;
        mHasRing.store(true);
        LOGI("[RingHunt] Beacon placed id=%{public}d pos=(%{public}f,%{public}f,%{public}f) "
             "yaw=%{public}f pitch=%{public}f roll=%{public}f",
             objectId, targetPos[0], targetPos[1], targetPos[2], mTargetYaw, mTargetPitch, mTargetRoll);
    });
    return objectId;
}

void RingHuntApp::SetZoom(float level)
{
    // v13 digital zoom — atomically store clamped level. OnUpdate reads it next frame and uses it
    // to widen glViewport (background quad + AR overlays scale together at the framebuffer level).
    // AR tracking still gets the native mWidth/mHeight via SetDisplayGeometry, so distance/yaw/
    // pitch/placeRingAt math is untouched.
    float z = level;
    if (z < 1.0f) {
        z = 1.0f;
    }
    if (z > 5.0f) {
        z = 5.0f;
    }
    mZoom.store(z);
}

// Phase 2 — ArkTS push 可见区 NDC y 边界(跨机型自适应)。center 给 renderer 做 clip-space y
// shift,halfExtent 给 fillRatio 判据做纵向归一。clamp halfExtent 下限避免 0 除。
void RingHuntApp::SetVisibleNdcY(float center, float halfExtent)
{
    if (halfExtent < 0.01f) {
        halfExtent = 0.01f;
    }
    mVisibleNdcYCenter.store(center);
    mVisibleNdcYHalfExtent.store(halfExtent);
    LOGI("[RingHunt] SetVisibleNdcY center=%{public}.4f halfExtent=%{public}.4f", center, halfExtent);
}

void RingHuntApp::SetDisplayRotation(int32_t rotation)
{
    // ArkTS 监听 display.on('change') 后回喂新的 rotation。push 到 GL/AR 任务队列里跑,确保
    // 和 SetDisplayGeometry 的其他调用在同一线程,避免和渲染 race。
    mTaskQueue.Push([this, rotation] {
        mDisplayRotation = ArEngineRotateType(rotation);
        if (mArSession != nullptr) {
            HMS_AREngine_ARSession_SetDisplayGeometry(mArSession, mDisplayRotation, mWidth, mHeight);
        }
    });
}

bool RingHuntApp::GetLatestCamRawPose(float out[7])
{
    std::lock_guard<std::mutex> lk(mCalibPoseMutex);
    if (!mCalibPoseValid) {
        return false;
    }
    for (int i = 0; i < 7; ++i) {
        out[i] = mCalibCamRawPose[i];
    }
    return true;
}

bool RingHuntApp::GetLatestCamDispPose(float out[7])
{
    std::lock_guard<std::mutex> lk(mCalibPoseMutex);
    if (!mCalibPoseValid) {
        return false;
    }
    for (int i = 0; i < 7; ++i) {
        out[i] = mCalibCamDispPose[i];
    }
    return true;
}

void RingHuntApp::RequestCapture()
{
    mCaptureRequested.store(true);
    LOGI("ARDA3-CAP requested");
}

bool RingHuntApp::IsFrameReady() const
{
    return mFrameReady.load();
}

bool RingHuntApp::TakeFrameRGBA(std::vector<uint8_t> &outRGBA, int &outW, int &outH)
{
    if (!mFrameReady.load()) {
        return false;
    }
    std::lock_guard<std::mutex> lk(mFrameMutex);
    if (mLastFrameRGBA.empty() || mLastFrameW <= 0 || mLastFrameH <= 0) {
        mFrameReady.store(false);
        return false;
    }
    outRGBA = std::move(mLastFrameRGBA);
    outW = mLastFrameW;
    outH = mLastFrameH;
    mLastFrameRGBA.clear();
    mLastFrameW = 0;
    mLastFrameH = 0;
    mFrameReady.store(false);
    LOGI("ARDA3-CAP frame taken %{public}dx%{public}d", outW, outH);
    return true;
}

void RingHuntApp::RequestCleanCapture()
{
    mCleanCaptureRequested.store(true);
    LOGI("ARDA3-CAPTURE clean frame requested");
}

bool RingHuntApp::IsCleanFrameReady() const
{
    return mCleanFrameReady.load();
}

bool RingHuntApp::TakeCleanFrameRGBA(std::vector<uint8_t> &outRGBA, int &outW, int &outH)
{
    if (!mCleanFrameReady.load()) {
        return false;
    }
    std::lock_guard<std::mutex> lk(mCleanFrameMutex);
    if (mLastCleanRGBA.empty() || mLastCleanW <= 0 || mLastCleanH <= 0) {
        mCleanFrameReady.store(false);
        return false;
    }
    outRGBA = std::move(mLastCleanRGBA);
    outW = mLastCleanW;
    outH = mLastCleanH;
    mLastCleanRGBA.clear();
    mLastCleanW = 0;
    mLastCleanH = 0;
    mCleanFrameReady.store(false);
    LOGI("ARDA3-CAPTURE clean frame taken %{public}dx%{public}d", outW, outH);
    return true;
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
        mHuntPhase.store(0);
        mIsAligned.store(false);
        mIsAngleAligned.store(false);
        mIsLocked.store(false);
        mYawDiffRad.store(0.0f);
        mPitchDiffRad.store(0.0f);
        mRollDiffRad.store(0.0f);
        mToAligningTimer = 0.0f;
        mToApproachingTimer = 0.0f;
        mLockTimer = 0.0f;
        mSnapHoldTimer = 0.0f;
        mSnapHoldSec.store(0.0f);
        mIsSnapReady.store(false);
        mPrevSnapReady = false;
        mWasAngleAligned = false;
        mBadgeFadeProgress = 0.0f;
        mBeaconPlacedAnimTime = -1.0f;
        LOGI("[RingHunt] reset.");
    });
}

void RingHuntApp::GetRingState(float &distance, bool &ringPlaced, int32_t &finishState, bool &isTargetInView,
                               float &screenEdgeX, float &screenEdgeY, bool &isBehind, float &indicatorAngleDeg,
                               float &ndcX, float &ndcY, int32_t &huntPhase, float &yawDiffRad, float &pitchDiffRad,
                               float &rollDiffRad, bool &isAligned, bool &isLocked, float &targetYawDeg,
                               float &targetPitchDeg, float &targetRollDeg, bool &isAngleAligned, float &fillRatio,
                               bool &snapReady, float &snapHoldSec, float &cGraceSec, float &fillRatioRaw)
{
    distance = mDistance.load();
    ringPlaced = mHasRing.load();
    finishState = 0; // Stage 11A: legacy finish state machine retired; see huntPhase below.
    isTargetInView = mIsTargetInView.load();
    screenEdgeX = mScreenEdgeX.load();
    screenEdgeY = mScreenEdgeY.load();
    isBehind = mIsBehind.load();
    indicatorAngleDeg = mIndicatorAngleDeg.load();
    ndcX = mNdcX.load();
    ndcY = mNdcY.load();
    huntPhase = mHuntPhase.load();
    yawDiffRad = mYawDiffRad.load();
    pitchDiffRad = mPitchDiffRad.load();
    rollDiffRad = mRollDiffRad.load();
    isAligned = mIsAligned.load();         // 重构后 = snapReady(距离 + 填屏,不含角度)
    isLocked = mIsLocked.load();
    targetYawDeg = mTargetYawDeg.load();
    targetPitchDeg = mTargetPitchDeg.load();
    targetRollDeg = mTargetRollDeg.load();
    isAngleAligned = mIsAngleAligned.load(); // 角度对准(吸附后 HUD 视觉 + LOCK timer)
    fillRatio = mFillRatio.load();           // fillRatio × zoom 实时值(调试)
    snapReady = mIsSnapReady.load();         // 瞬时 snapReady(dist + fill,无 hold)
    snapHoldSec = mSnapHoldSec.load();       // snapReady 当前持续秒数(0..1.5+)
    cGraceSec = mCGraceSec.load();           // 进 C 后已过秒数(snapped 时累加,否则 0)
    fillRatioRaw = mFillRatioRaw.load();     // 修(问题 4)— 原始 fill,对比平滑后判断 fill 计算是否准
}

} // namespace ARObject
