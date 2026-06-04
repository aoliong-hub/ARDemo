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

#ifndef RING_HUNT_AR_APPLICATION_H
#define RING_HUNT_AR_APPLICATION_H

#include "app_napi.h"
#include "ar/ar_engine_core.h"
#include "object_math.h"
#include "ring_hunt_render_manager.h"
#include "task_queue.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace ARObject {

// Wayfinder beacon (Stage 11A): a beacon is dropped on the floor 1m ahead of the camera. The
// renderer draws the ground ring + light pillar + spinning top + phone icon there (fixed red).
// getRingState reports only the camera->beacon distance for now; the distance gradient + finish
// state machine return in 11B.
class RingHuntApp : public AppNapi {
public:
    explicit RingHuntApp(std::string &id);
    ~RingHuntApp() override;

    void OnStart(const ConfigParams &params) override;
    void OnUpdate() override;
    void OnPause() override;
    void OnResume() override;
    void OnStop() override;

    void OnSurfaceCreated(OH_NativeXComponent *component, void *window) override;
    void OnSurfaceChanged(OH_NativeXComponent *component, void *window) override;
    void OnSurfaceDestroyed(OH_NativeXComponent *component, void *window) override;

    int32_t PlaceRing() override;
    int32_t PlaceRingWithOrientation(float yawDeg, float pitchDeg, float rollDeg) override;
    int32_t PlaceRingAt(float x, float y, float z, float yawDeg, float pitchDeg, float rollDeg) override;
    void SetZoom(float level) override;
    void ResetRing() override;
    void SetDisplayRotation(int32_t rotation) override;
    void SetVisibleNdcY(float center, float halfExtent) override;
    int32_t GetOrientation() const override { return mOrientation.load(); }
    // 标定工具:同步读取最近一帧 OnUpdate 缓存的相机姿态。out[7] = qx,qy,qz,qw,px,py,pz。
    // 返回 false 表示尚未跟踪稳定/未拿到姿态(JS 侧应处理 null)。
    bool GetLatestCamRawPose(float out[7]) override;
    bool GetLatestCamDispPose(float out[7]) override;

    // Da3 capture hand-off (request/poll). RequestCapture flips mCaptureRequested; the next render
    // frame fills mLastFrameRGBA + sets mFrameReady. TakeFrameRGBA moves the bytes out and clears
    // mFrameReady. Producer = GL thread (render lambda), consumer = ArkTS via NAPI.
    void RequestCapture() override;
    bool IsFrameReady() const override;
    bool TakeFrameRGBA(std::vector<uint8_t> &outRGBA, int &outW, int &outH) override;
    // 拍照(纯净帧):glReadPixels 在 wayfinder Render 之前完成,抓到的不含 AR 物体。
    void RequestCleanCapture() override;
    bool IsCleanFrameReady() const override;
    bool TakeCleanFrameRGBA(std::vector<uint8_t> &outRGBA, int &outW, int &outH) override;
    void GetRingState(float &distance, bool &ringPlaced, int32_t &finishState, bool &isTargetInView,
                      float &screenEdgeX, float &screenEdgeY, bool &isBehind, float &indicatorAngleDeg, float &ndcX,
                      float &ndcY, int32_t &huntPhase, float &yawDiffRad, float &pitchDiffRad, float &rollDiffRad,
                      bool &isAligned, bool &isLocked, float &targetYawDeg, float &targetPitchDeg,
                      float &targetRollDeg, bool &isAngleAligned, float &fillRatio,
                      bool &snapReady, float &snapHoldSec, float &cGraceSec, float &fillRatioRaw) override;

private:
    // Shared placement core. Two orthogonal toggles:
    //   useGivenOrientation = false → fresh random 6DoF target (PlaceRing demo path)
    //                          true → the supplied radians (PlaceRingWithOrientation / PlaceRingAt)
    //   useGivenPosition    = false → drop 1m ahead of the camera, base on the floor heuristic
    //                                 (legacy PlaceRing / PlaceRingWithOrientation behaviour)
    //                          true → camera-relative HORIZONTAL offset (Stage 12C PlaceRingAt):
    //                                 base.xz = camPos.xz + rightHoriz*relX + fwdHoriz*relZ
    //                                 base.y  = camPos.y  - kGroundDrop (still ground-snapped)
    //                                 Per-beacon ring/badge height = ringY (stored in mRingHeight,
    //                                 replaces the global kWayfinderTopHeight only for THIS beacon).
    int32_t PlaceBeaconInternal(bool useGivenOrientation, float yawRad, float pitchRad, float rollRad,
                                bool useGivenPosition, float relX, float relZ, float ringY);

    AREngine_ARSession *mArSession = nullptr;
    AREngine_ARFrame *mArFrame = nullptr;

    // Beacon anchor — created/reset/read only on the task-queue thread.
    AREngine_ARAnchor *mRingAnchor = nullptr;
    int32_t mNextId = 1;

    // Animation clock (task thread only): drives the spinner/ripple/breathing. mFrameHueTime tracks
    // mAnimTime but freezes once LOCKED so the alignment frame's hue stops cycling.
    float mAnimTime = 0.0f;
    float mFrameHueTime = 0.0f;
    std::chrono::steady_clock::time_point mLastFrameTime;
    bool mHasLastFrame = false;

    // 6DoF alignment target (task thread only): a random orientation generated at placeRing.
    float mTargetYaw = 0.0f;   // about world +Y
    float mTargetPitch = 0.0f; // about world +X
    float mTargetRoll = 0.0f;  // about world +Z (forward axis)

    // State-machine debounce timers (task thread only), seconds.
    float mToAligningTimer = 0.0f;
    float mToApproachingTimer = 0.0f;
    float mLockTimer = 0.0f;
    // 重构(2026-06-03)— 两阶段对齐:吸附触发(距离+填屏)与角度对准(yaw/pitch)分离。
    //   mPrevSnapReady     : 上一帧 snapReady(用于 fill 滞回 0.92↔0.88)
    //   mSnapHoldTimer     : snapReady 持续了多久(秒)。snapReady 为 true 时累加 dt,false 时清零。
    //                        ≥ kSnapHoldSec(1.5s)→ snapTriggered = true → phase 0→1 + 吸附特效。
    //   mWasAngleAligned   : 角度对准 yaw/pitch 5°↔7° 滞回。吸附后 HUD 阶段生效;LOCK timer 按此累加。
    bool mPrevSnapReady = false;
    float mSnapHoldTimer = 0.0f;
    bool mWasAngleAligned = false;
    // 修(2026-06-03 问题 2)— C grace:B→C 转换瞬间记下 mAnimTime,C→D 需要至少 kCGraceSec(1.5s)
    //   后才允许触发(防"吸附瞬间 angle 天然 <5° → D 一闪而过,HUD 没机会展示")。
    //   重置时机:仅在 snapped 重置时(C/D→B 或 *→A),不在 D→C 切换时重置(grace 已过)。
    float mCEnteredTime = -1.0f;

    // 灰盘/柱子 ↔ 对齐框 之间的时间渐变进度(task thread only)。每帧按 distance 是否 <30cm 推进:
    //   inside  → 朝 1 走(灰盘+柱子淡出,对齐框淡入)
    //   outside → 朝 0 走(反向)
    // 2.0s 走完全程,平滑切换替代原来的 huntPhase 硬切(灰盘瞬间消失/出现)。reset 时归零。
    float mBadgeFadeProgress = 0.0f;

    // 信标放置时刻(mAnimTime 秒)。-1 = 未放置/无动画。每次 PlaceBeaconInternal 成功放锚点后
    // 记录为当前 mAnimTime,renderer 据此算 animAge = mAnimTime - mBeaconPlacedAnimTime,驱动
    // 1.3s 放置序列(灰盘渐入 0.3s → 水滴下落 0.7s → 炫彩圈涌起 0.3s),≥1.3s 后稳态走
    // mBadgeFadeProgress 距离渐隐。ResetRing 时归 -1,下次 place 重新演。
    float mBeaconPlacedAnimTime = -1.0f;

    // Shared with the ArkTS poll (getRingState).
    std::atomic<bool> mReady{false};
    std::atomic<bool> mHasRing{false};
    std::atomic<bool> mCameraTracking{false};
    std::atomic<float> mDistance{99.0f};

    // Stage 11D 6DoF alignment challenge state.
    std::atomic<int32_t> mHuntPhase{0}; // 0=APPROACHING 1=ALIGNING 2=LOCKED
    std::atomic<float> mYawDiffRad{0.0f};
    std::atomic<float> mPitchDiffRad{0.0f};
    // Stage 12B-1: roll diff drives the on-screen banner only — not part of the alignment gate.
    std::atomic<float> mRollDiffRad{0.0f};
    // 重构(2026-06-03)— mIsAligned 现在 = (huntPhase == 1) = "吸附会话激活中"。
    //   触发条件:snapReady 持续 kSnapHoldSec(1.5s)→ phase 0→1 → mIsAligned true。
    //   持续条件:snapReady 失但还在 kDebounceSec 缓冲期内,mIsAligned 仍 true(防瞬间抖动让框秒变 pearl)。
    //   退出条件:snapReady 失持续 kDebounceSec → phase 1→0 → mIsAligned false。
    // 驱动:吸附特效(0→1 边缘触发)+ 灰盘 ↔ 框 渐变 + ArkTS onSnapTrigger。
    std::atomic<bool> mIsAligned{false};
    // 新增 — mIsSnapReady:snapReady 瞬时值(dist + fill,无 hold)。仅调试/UI 用,不驱动渲染。
    std::atomic<bool> mIsSnapReady{false};
    // 新增 — mSnapHoldSec:snapReady 当前持续秒数(0..1.5+)。调试浮层显示进度。
    std::atomic<float> mSnapHoldSec{0.0f};
    // 新增(问题 2 修)— mCGraceSec:进 C 后已过的秒数(snapped 才有意义,否则 0)。调试浮层用。
    std::atomic<float> mCGraceSec{0.0f};
    // 新增 — mIsAngleAligned:吸附后的角度对准(yaw/pitch < 5°↔7°),驱动 HUD 视觉 + LOCK timer。
    std::atomic<bool> mIsAngleAligned{false};
    std::atomic<bool> mIsLocked{false};
    // Degree mirrors of the target orientation, set at placement so the ArkTS poll can read them.
    std::atomic<float> mTargetYawDeg{0.0f};
    std::atomic<float> mTargetPitchDeg{0.0f};
    std::atomic<float> mTargetRollDeg{0.0f};
    // Physical orientation derived from camRoll snap (placement-time): 0=PORTRAIT, 1=LANDSCAPE_CW,
    // 2=LANDSCAPE_CCW. Read by ArkTS via getOrientation() NAPI to decide how to rotate the JPEG
    // before sending to da3 (so the generated reference image matches the user's physical framing).
    std::atomic<int32_t> mOrientation{0};

    // Calibration tool: pose snapshot updated each OnUpdate frame (after tracking check).
    // Both GetPose (sensor raw) and GetDisplayOrientedPose (UI-corrected) recorded so calib
    // tool can record both in meta.txt for offline mapping analysis.
    // Layout: [qx,qy,qz,qw, px,py,pz] = same as AR Engine pose raw[7]. Protected by mutex.
    std::mutex mCalibPoseMutex;
    bool mCalibPoseValid = false;
    float mCalibCamRawPose[7] = {0,0,0,1, 0,0,0};
    float mCalibCamDispPose[7] = {0,0,0,1, 0,0,0};

    // Camera pose snapshot taken at frame-capture time (when mCaptureRequested is drained).
    // PlaceBeaconInternal uses this instead of the live camera pose so the cloud-returned
    // relative displacement is applied to the SAME reference frame the cloud saw — preventing
    // beacon position drift caused by phone movement between capture and placement.
    // Layout: [qx,qy,qz,qw, px,py,pz] — GetDisplayOrientedPose raw[7] format.
    float mCaptureCamPose[7] = {0,0,0,1, 0,0,0};
    bool mHasCapturePose = false;
    std::chrono::steady_clock::time_point mCapturePoseTime{};

    // Off-screen target guidance (computed each tracked frame from the view/proj matrices, targeting
    // the beacon top). Default "in view" so guidance stays hidden until a beacon is placed.
    std::atomic<bool> mIsTargetInView{true};
    std::atomic<float> mScreenEdgeX{0.5f};
    std::atomic<float> mScreenEdgeY{0.5f};
    std::atomic<bool> mIsBehind{false};
    std::atomic<float> mIndicatorAngleDeg{0.0f};
    std::atomic<float> mNdcX{0.0f}; // projected ndc -> ArkTS screen-space guidance ray
    std::atomic<float> mNdcY{0.0f};

    uint64_t mWidth = 1080;
    uint64_t mHeight = 1920;
    AREngine_ARPoseType mDisplayRotation = ARENGINE_POSE_TYPE_IDENTITY;
    double mX = 0.0;
    double mY = 0.0;
    RingHuntRenderManager mRenderManager;
    TaskQueue mTaskQueue;
    bool mIsSurfaceChange = false;
    std::atomic<bool> isPaused = false;
    ConfigParams mConfigParam{};

    // Stage 12C: per-beacon ring/badge height (metres above the base). Default = the legacy
    // kWayfinderTopHeight = 0.9f so PlaceRing / PlaceRingWithOrientation render identically to
    // before. PlaceRingAt overrides this from its y parameter at placement time.
    std::atomic<float> mRingHeight{0.9f};

    // v13 digital zoom: 1.0 = native, 5.0 = 5x. OnUpdate maps this to glViewport so the entire
    // GL output (camera background quad + AR meshes) scales together at the framebuffer level;
    // AR tracking (SetDisplayGeometry) stays at native mWidth/mHeight so pose/alignment math is
    // unaffected. Clamped [1.0, 5.0] in SetZoom().
    std::atomic<float> mZoom{1.0f};

    // Phase 2 — 可见区 NDC y 边界(ArkTS push)。
    //   mVisibleNdcYCenter      = clip-space y shift,框 MVP 后乘 translate(0, center, 0, 1) → 框居中可见区
    //   mVisibleNdcYHalfExtent  = fillRatio 纵向归一(典型 0.666 for Mate 60;0 时 fall back NDC 全跨度)
    std::atomic<float> mVisibleNdcYCenter{0.0f};
    std::atomic<float> mVisibleNdcYHalfExtent{0.0f};
    // 实时 fillRatio(EMA 平滑后,已乘 mZoom)。OnUpdate 写;aligned 判据 + 调试日志读。
    std::atomic<float> mFillRatio{0.0f};
    // 修(问题 4)— 原始 fillRatio(未平滑,已乘 mZoom)。仅调试浮层显示,对比 smoothed 值。
    std::atomic<float> mFillRatioRaw{0.0f};
    // EMA 平滑累积值,跨帧持有。0 = 初始 / reset → 下次首帧用瞬时值初始化。
    float mFillRatioSmoothed = 0.0f;

    // Da3 capture state. mCaptureRequested set true by ArkTS, cleared after the next render fills
    // the buffer. mFrameReady gates ArkTS reads. mFrameMutex guards mLastFrameRGBA across the
    // GL-thread producer and the ArkTS-thread consumer.
    std::atomic<bool> mCaptureRequested{false};
    std::atomic<bool> mFrameReady{false};
    std::vector<uint8_t> mLastFrameRGBA;
    int mLastFrameW = 0;
    int mLastFrameH = 0;
    mutable std::mutex mFrameMutex;

    // Clean capture state(功能 6 拍照):同 mCaptureRequested 套路,但 glReadPixels 时机提前到
    // mBackgroundRenderer.Draw 之后、mWayfinderRenderer.Render 之前,抓到的是纯相机画面无 AR。
    std::atomic<bool> mCleanCaptureRequested{false};
    std::atomic<bool> mCleanFrameReady{false};
    std::vector<uint8_t> mLastCleanRGBA;
    int mLastCleanW = 0;
    int mLastCleanH = 0;
    mutable std::mutex mCleanFrameMutex;
};

} // namespace ARObject

#endif // RING_HUNT_AR_APPLICATION_H
