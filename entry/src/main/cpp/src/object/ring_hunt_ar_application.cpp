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
constexpr float kFrameAlignEnterRad = 0.0872665f; // 5 degrees: enter "aligned" (turn green)
constexpr float kFrameAlignExitRad = 0.1221730f;  // 7 degrees: leave "aligned" (hysteresis, turn red)
constexpr float kDebounceSec = 0.30f;        // hold time for APPROACHING<->ALIGNING transitions
constexpr float kLockSec = 5.00f;            // hold time aligned before LOCKED
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

        // 6DoF alignment-frame orientation, built with an EXPLICIT yaw->pitch->roll order (instead of
        // glm::quat(vec3)'s non-intuitive Y*X*Z) so external 6DoF inputs match expectations:
        //   yaw>0 -> frame faces the camera's right (+X), pitch>0 -> up, roll>0 -> clockwise (camera POV).
        // The same quaternion drives both the render and the alignment math below.
        glm::mat4 yawMat = glm::rotate(glm::mat4(1.0f), -mTargetYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 pitchMat = glm::rotate(glm::mat4(1.0f), mTargetPitch, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::mat4 rollMat = glm::rotate(glm::mat4(1.0f), mTargetRoll, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat targetQ = glm::quat_cast(yawMat * pitchMat * rollMat);

        RingCameraInfo cam;
        mRenderManager.OnDrawFrame(mArSession, mArFrame, mHasRing.load(), mRingAnchor, mAnimTime, color, lastDist,
                                   mHuntPhase.load(), targetQ, mFrameHueTime, mIsAligned.load(), dt,
                                   mRingHeight.load(), &cam);

        mCameraTracking.store(cam.tracking);
        if (!cam.tracking) {
            return;
        }
        if (!mReady.load()) {
            mReady.store(true);
            LOGI("[RingHunt] tracking ready.");
        }

        if (!mHasRing.load() || mRingAnchor == nullptr) {
            mDistance.store(99.0f);
            mIsTargetInView.store(true); // no beacon -> keep guidance hidden
            mIsBehind.store(false);
            mHuntPhase.store(0);
            mIsAligned.store(false);
            mIsLocked.store(false);
            mToAligningTimer = 0.0f;
            mToApproachingTimer = 0.0f;
            mLockTimer = 0.0f;
            mWasAligned = false;
            mFrameHueTime = mAnimTime;
            return;
        }

        // Beacon world position (translation of its anchor pose) -> distance to the camera.
        glm::vec3 ringPos(0.0f);
        {
            AREngine_ARPose *pose = nullptr;
            if (HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &pose) == ARENGINE_SUCCESS && pose != nullptr) {
                if (HMS_AREngine_ARAnchor_GetPose(mArSession, mRingAnchor, pose) == ARENGINE_SUCCESS) {
                    float m[16] = {0.0f};
                    HMS_AREngine_ARPose_GetMatrix(mArSession, pose, m, 16);
                    ringPos = glm::vec3(m[12], m[13], m[14]);
                }
                HMS_AREngine_ARPose_Destroy(pose);
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
        // Hysteresis: need <5deg to become aligned (green), but only >7deg breaks it (red) — so the
        // arrow doesn't flicker red/green right at the boundary.
        float thresh = mWasAligned ? kFrameAlignExitRad : kFrameAlignEnterRad;
        bool aligned = inRange && std::fabs(yawDiff) < thresh && std::fabs(pitchDiff) < thresh;
        mWasAligned = aligned;
        mIsAligned.store(aligned);

        int phase = mHuntPhase.load();
        if (phase == 0) { // APPROACHING
            mToAligningTimer = inRange ? (mToAligningTimer + dt) : 0.0f;
            if (mToAligningTimer >= kDebounceSec) {
                phase = 1;
                mToApproachingTimer = 0.0f;
                mLockTimer = 0.0f;
                LOGI("[RingHunt] -> ALIGNING");
            }
        } else if (phase == 1) { // ALIGNING
            mToApproachingTimer = (!inRange) ? (mToApproachingTimer + dt) : 0.0f;
            if (mToApproachingTimer >= kDebounceSec) {
                phase = 0;
                mToAligningTimer = 0.0f;
                mLockTimer = 0.0f;
                LOGI("[RingHunt] -> APPROACHING");
            } else {
                mLockTimer = aligned ? (mLockTimer + dt) : 0.0f;
                if (mLockTimer >= kLockSec) {
                    phase = 2;
                    LOGI("[RingHunt] -> LOCKED");
                }
            }
        }
        // phase == 2 (LOCKED) stays until reset.
        mHuntPhase.store(phase);
        mIsLocked.store(phase == 2);
        if (phase != 2) {
            mFrameHueTime = mAnimTime; // keep hue live until locked, then freeze at this value
        }

        // Off-screen guidance (droplet) only in APPROACHING: project the beacon ring (visible top
        // badge) to screen space. In ALIGNING/LOCKED the droplet is off (orientation is shown by
        // the co-planar disk + HUD). Same ringTop point as the distance / 15cm gate.
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
    mRingHeight.store(y);
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
        AREngine_ARCamera *cam = nullptr;
        CHECK(HMS_AREngine_ARFrame_AcquireCamera(mArSession, mArFrame, &cam));
        AREngine_ARPose *camPose = nullptr;
        CHECK(HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &camPose));
        CHECK(HMS_AREngine_ARCamera_GetPose(mArSession, cam, camPose));
        float raw[7] = {0.0f};
        HMS_AREngine_ARPose_GetPoseRaw(mArSession, camPose, raw, 7);
        HMS_AREngine_ARPose_Destroy(camPose);
        HMS_AREngine_ARCamera_Release(cam);

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
        // mTargetYaw is ABSOLUTE world yaw (relative + camYaw); pitch/roll are camera-independent.
        mTargetYaw = relYaw + camYaw;
        mTargetPitch = relPitch;
        mTargetRoll = relRoll;
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
        mIsLocked.store(false);
        mToAligningTimer = 0.0f;
        mToApproachingTimer = 0.0f;
        mLockTimer = 0.0f;
        mWasAligned = false;
        mFrameHueTime = mAnimTime;
        mHasRing.store(true);
        LOGI("[RingHunt] Beacon placed id=%{public}d pos=(%{public}f,%{public}f,%{public}f) "
             "yaw=%{public}f pitch=%{public}f roll=%{public}f",
             objectId, targetPos[0], targetPos[1], targetPos[2], mTargetYaw, mTargetPitch, mTargetRoll);
    });
    return objectId;
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
        mIsLocked.store(false);
        mYawDiffRad.store(0.0f);
        mPitchDiffRad.store(0.0f);
        mRollDiffRad.store(0.0f);
        mToAligningTimer = 0.0f;
        mToApproachingTimer = 0.0f;
        mLockTimer = 0.0f;
        mWasAligned = false;
        LOGI("[RingHunt] reset.");
    });
}

void RingHuntApp::GetRingState(float &distance, bool &ringPlaced, int32_t &finishState, bool &isTargetInView,
                               float &screenEdgeX, float &screenEdgeY, bool &isBehind, float &indicatorAngleDeg,
                               float &ndcX, float &ndcY, int32_t &huntPhase, float &yawDiffRad, float &pitchDiffRad,
                               float &rollDiffRad, bool &isAligned, bool &isLocked, float &targetYawDeg,
                               float &targetPitchDeg, float &targetRollDeg)
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
    isAligned = mIsAligned.load();
    isLocked = mIsLocked.load();
    targetYawDeg = mTargetYawDeg.load();
    targetPitchDeg = mTargetPitchDeg.load();
    targetRollDeg = mTargetRollDeg.load();
}

} // namespace ARObject
