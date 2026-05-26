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

// Pure math + constants for interface-style world placement.
// Header-only and dependency-free (plain float arrays, <cmath> only) so it is unit-testable
// on the host without any AR/GL dependency, and usable directly by the native module.

#ifndef OBJECT_MATH_H
#define OBJECT_MATH_H

#include <cmath>
#include <cstdint>
#include <random>

namespace ARObject {

// Maximum number of simultaneously placed objects (Addendum 5).
constexpr int kMaxObjects = 50;

// Intrinsic yaw offset (radians, about world +Y) so the AR_logo front faces the camera.
// CALIBRATED on-device 2026-05-25 (Stage 2 FRONT/BACK human gate, Addendum 2): with identity
// rotation the logo's front already faces the camera (gate answer = FRONT), so the offset is 0.
// Used by ComputeYawToFaceCamera (Stage 3). Stage 2 placement uses identity rotation.
constexpr float kLogoFrontYawOffset = 0.0f; // FRONT confirmed

// Quaternion component layout inside AR Engine poseRaw[0..3].
// Determined at runtime by ARObjectApp::ProbeQuaternionLayout() (Addendum 1) and passed
// into PackPoseRaw; this enum just names the two possibilities.
enum class QuatFormat { XYZW, WXYZ };

// Rotate vector v by unit quaternion q (component order xyzw). out = q * v * q^-1.
// t = 2*(q.xyz X v); out = v + q.w*t + q.xyz X t.
inline void RotateVectorByQuatXYZW(const float q[4], const float v[3], float out[3])
{
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float tx = 2.0f * (y * v[2] - z * v[1]);
    const float ty = 2.0f * (z * v[0] - x * v[2]);
    const float tz = 2.0f * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

// World point `distance` meters in front of the camera (along camera local -Z).
// camPos: camera world position; camQuat: camera world orientation (xyzw).
inline void ComputeForwardPoint(const float camPos[3], const float camQuat[4], float distance, float outPoint[3])
{
    const float forwardLocal[3] = {0.0f, 0.0f, -1.0f};
    float forwardWorld[3];
    RotateVectorByQuatXYZW(camQuat, forwardLocal, forwardWorld);
    outPoint[0] = camPos[0] + forwardWorld[0] * distance;
    outPoint[1] = camPos[1] + forwardWorld[1] * distance;
    outPoint[2] = camPos[2] + forwardWorld[2] * distance;
}

// Build a unit quaternion (xyzw) for a yaw rotation about world +Y.
inline void YawToQuaternionXYZW(float yaw, float outQuatXYZW[4])
{
    const float half = yaw * 0.5f;
    outQuatXYZW[0] = 0.0f;
    outQuatXYZW[1] = std::sin(half);
    outQuatXYZW[2] = 0.0f;
    outQuatXYZW[3] = std::cos(half);
}

// Pack a (canonical xyzw) quaternion + translation into AR Engine poseRaw[7], reordering the
// quaternion components according to `format`. The quaternion is normalized first.
inline void PackPoseRaw(const float quatXYZW[4], const float trans[3], QuatFormat format, float outPoseRaw[7])
{
    float qx = quatXYZW[0];
    float qy = quatXYZW[1];
    float qz = quatXYZW[2];
    float qw = quatXYZW[3];
    const float norm = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (norm > 1e-8f) {
        qx /= norm;
        qy /= norm;
        qz /= norm;
        qw /= norm;
    } else {
        qx = 0.0f;
        qy = 0.0f;
        qz = 0.0f;
        qw = 1.0f;
    }
    if (format == QuatFormat::XYZW) {
        outPoseRaw[0] = qx;
        outPoseRaw[1] = qy;
        outPoseRaw[2] = qz;
        outPoseRaw[3] = qw;
    } else { // WXYZ
        outPoseRaw[0] = qw;
        outPoseRaw[1] = qx;
        outPoseRaw[2] = qy;
        outPoseRaw[3] = qz;
    }
    outPoseRaw[4] = trans[0];
    outPoseRaw[5] = trans[1];
    outPoseRaw[6] = trans[2];
}

// Extract a canonical xyzw quaternion from an AR Engine poseRaw[7] given its layout.
inline void UnpackQuatXYZW(const float poseRaw[7], QuatFormat format, float outQuatXYZW[4])
{
    if (format == QuatFormat::XYZW) {
        outQuatXYZW[0] = poseRaw[0];
        outQuatXYZW[1] = poseRaw[1];
        outQuatXYZW[2] = poseRaw[2];
        outQuatXYZW[3] = poseRaw[3];
    } else { // WXYZ: poseRaw = (w, x, y, z)
        outQuatXYZW[0] = poseRaw[1];
        outQuatXYZW[1] = poseRaw[2];
        outQuatXYZW[2] = poseRaw[3];
        outQuatXYZW[3] = poseRaw[0];
    }
}

// ---- ARArrowAlign (alignment challenge) ----

// An arrow points along its local -Z. Returns the world-space direction (normalized) of the
// arrow given its orientation quaternion (xyzw).
inline void ComputeArrowDirection(const float quatXYZW[4], float outDir[3])
{
    const float forwardLocal[3] = {0.0f, 0.0f, -1.0f};
    RotateVectorByQuatXYZW(quatXYZW, forwardLocal, outDir);
    const float n = std::sqrt(outDir[0] * outDir[0] + outDir[1] * outDir[1] + outDir[2] * outDir[2]);
    if (n > 1e-8f) {
        outDir[0] /= n;
        outDir[1] /= n;
        outDir[2] /= n;
    }
}

// Angle (radians, in [0, pi]) between the directions of two arrows. dot is clamped to [-1,1]
// before acos to avoid NaN from floating-point overshoot.
inline float ComputeAngleBetweenArrows(const float quatA[4], const float quatB[4])
{
    float dirA[3];
    float dirB[3];
    ComputeArrowDirection(quatA, dirA);
    ComputeArrowDirection(quatB, dirB);
    float d = dirA[0] * dirB[0] + dirA[1] * dirB[1] + dirA[2] * dirB[2];
    if (d > 1.0f) {
        d = 1.0f;
    }
    if (d < -1.0f) {
        d = -1.0f;
    }
    return std::acos(d);
}

// Alignment state with hysteresis: enter ALIGNED below 2deg, leave above 4deg, else hold.
// (Stage 6: tightened from 4/6 to 2/4. These are gameplay thresholds, NOT Stage 2/3 calibration.)
enum class AlignState { NOT_ALIGNED, ALIGNED };
constexpr float kAlignEnterRad = 2.0f * 3.14159265358979323846f / 180.0f; // < 2 deg -> aligned
constexpr float kAlignExitRad = 4.0f * 3.14159265358979323846f / 180.0f;  // > 4 deg -> not aligned

inline AlignState UpdateAlignState(AlignState prev, float angleRad)
{
    if (prev == AlignState::NOT_ALIGNED) {
        return (angleRad < kAlignEnterRad) ? AlignState::ALIGNED : AlignState::NOT_ALIGNED;
    }
    return (angleRad > kAlignExitRad) ? AlignState::NOT_ALIGNED : AlignState::ALIGNED;
}

// Direction (world, normalized) obtained by offsetting the camera forward by yaw (about the
// camera's local up) and pitch (about the camera's local right). Offsets in radians.
// The randomness lives in the caller; this is the deterministic geometry it relies on.
inline void ComputeRandomTargetDirection(const float camQuatXYZW[4], float yawOffset, float pitchOffset,
                                         float outDir[3])
{
    float cp = std::cos(pitchOffset);
    float sp = std::sin(pitchOffset);
    float cy = std::cos(yawOffset);
    float sy = std::sin(yawOffset);
    // Local direction in camera space, offset from forward (-Z).
    float localDir[3] = {cp * sy, sp, -cp * cy};
    RotateVectorByQuatXYZW(camQuatXYZW, localDir, outDir);
    float n = std::sqrt(outDir[0] * outDir[0] + outDir[1] * outDir[1] + outDir[2] * outDir[2]);
    if (n > 1e-8f) {
        outDir[0] /= n;
        outDir[1] /= n;
        outDir[2] /= n;
    }
}

// Quaternion (xyzw) of the minimal rotation taking local -Z onto the given direction.
inline void QuaternionAlignZNegTo(const float dir[3], float outQuatXYZW[4])
{
    float bx = dir[0];
    float by = dir[1];
    float bz = dir[2];
    float bl = std::sqrt(bx * bx + by * by + bz * bz);
    if (bl > 1e-8f) {
        bx /= bl;
        by /= bl;
        bz /= bl;
    }
    float d = -bz; // dot((0,0,-1), b)
    if (d > 0.9999f) {
        outQuatXYZW[0] = 0.0f;
        outQuatXYZW[1] = 0.0f;
        outQuatXYZW[2] = 0.0f;
        outQuatXYZW[3] = 1.0f;
        return;
    }
    if (d < -0.9999f) { // 180 deg: pick +Y as the rotation axis
        outQuatXYZW[0] = 0.0f;
        outQuatXYZW[1] = 1.0f;
        outQuatXYZW[2] = 0.0f;
        outQuatXYZW[3] = 0.0f;
        return;
    }
    // axis = (0,0,-1) x b = (by, -bx, 0)
    float ax = by;
    float ay = -bx;
    float az = 0.0f;
    float al = std::sqrt(ax * ax + ay * ay + az * az);
    if (al > 1e-8f) {
        ax /= al;
        ay /= al;
        az /= al;
    }
    float angle = std::acos(d);
    float s = std::sin(angle * 0.5f);
    outQuatXYZW[0] = ax * s;
    outQuatXYZW[1] = ay * s;
    outQuatXYZW[2] = az * s;
    outQuatXYZW[3] = std::cos(angle * 0.5f);
}

// ---- ARRingHunt (ring alignment & hunt game) ----

// Euclidean distance between two world points.
inline float Compute3DDistance(const float pa[3], const float pb[3])
{
    float dx = pa[0] - pb[0];
    float dy = pa[1] - pb[1];
    float dz = pa[2] - pb[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Hamilton product of two xyzw quaternions: out represents (a then-> applied with) a*b,
// i.e. rotating by b first, then a.
inline void QuatMultiplyXYZW(const float a[4], const float b[4], float out[4])
{
    float ax = a[0];
    float ay = a[1];
    float az = a[2];
    float aw = a[3];
    float bx = b[0];
    float by = b[1];
    float bz = b[2];
    float bw = b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

// Random target ring parameters relative to the current camera pose. Position is ~1m ahead
// with lateral/height jitter; orientation is the camera orientation plus a yaw/pitch tilt.
// `seed` makes it unit-testable (caller passes std::random_device() at runtime).
struct RingTargetParams {
    float targetPos[3];
    float targetQuat[4];
    // Exposed random intermediates for testing.
    float yawOff;
    float pitchOff;
    float lateralOff;
    float heightOff;
};

inline void ComputeRingTargetParams(const float camPos[3], const float camQuat[4], uint32_t seed,
                                    RingTargetParams &out)
{
    constexpr float kDeg = 3.14159265358979323846f / 180.0f;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> yawD(-60.0f * kDeg, 60.0f * kDeg);
    std::uniform_real_distribution<float> pitchD(-30.0f * kDeg, 30.0f * kDeg);
    std::uniform_real_distribution<float> latD(-0.30f, 0.30f);
    std::uniform_real_distribution<float> heightD(-0.30f, 0.30f);
    out.yawOff = yawD(rng);
    out.pitchOff = pitchD(rng);
    out.lateralOff = latD(rng);
    out.heightOff = heightD(rng);

    float fwd[3];
    ComputeArrowDirection(camQuat, fwd); // camera -Z
    float right[3];
    {
        float r[3] = {1.0f, 0.0f, 0.0f};
        RotateVectorByQuatXYZW(camQuat, r, right); // camera +X
    }
    const float up[3] = {0.0f, 1.0f, 0.0f}; // world up
    for (int i = 0; i < 3; ++i) {
        out.targetPos[i] = camPos[i] + fwd[i] * 1.0f + right[i] * out.lateralOff + up[i] * out.heightOff;
    }

    float qYaw[4] = {0.0f, std::sin(out.yawOff * 0.5f), 0.0f, std::cos(out.yawOff * 0.5f)};
    float qPitch[4] = {std::sin(out.pitchOff * 0.5f), 0.0f, 0.0f, std::cos(out.pitchOff * 0.5f)};
    float delta[4];
    QuatMultiplyXYZW(qYaw, qPitch, delta);
    QuatMultiplyXYZW(camQuat, delta, out.targetQuat);
}

// Two-axis alignment feedback level.
enum class AlignLevel { RED, YELLOW, ORANGE, GREEN }; // 0,1,2,3
constexpr float kDistNearMeters = 0.60f;
constexpr float kDistOnTargetMeters = 0.15f; // Stage 8: tightened from 0.30 to 0.15
constexpr float kAngleNearRad = 30.0f * 3.14159265358979323846f / 180.0f;
constexpr float kAngleOnTargetRad = 5.0f * 3.14159265358979323846f / 180.0f;

inline AlignLevel ComputeAlignmentLevel(float distance, float angleRad)
{
    bool isDistOk = distance < kDistOnTargetMeters;
    bool isAngOk = angleRad < kAngleOnTargetRad;
    bool isDistNear = distance < kDistNearMeters;
    if (isDistOk && isAngOk) {
        return AlignLevel::GREEN;
    }
    if (isAngOk && !isDistOk) {
        return AlignLevel::ORANGE; // angle good, still need to move closer
    }
    if (isDistNear && !isAngOk) {
        return AlignLevel::YELLOW; // close, but angle off
    }
    return AlignLevel::RED;
}

// Stage 8: independent 2-axis feedback (replaces the single 4-level color).
inline bool IsDistanceOnTarget(float distance) { return distance < kDistOnTargetMeters; }
inline bool IsAngleOnTarget(float angleRad) { return angleRad < kAngleOnTargetRad; }

// Split the alignment error into yaw (left/right) and pitch (up/down) in the camera's local
// frame. target = -ringNormal ("look through the ring"). Positive yaw = target to the right,
// positive pitch = target above.
inline void ComputeYawPitchDiff(const float camQuat[4], const float ringNormal[3], float &yawDiffRad,
                                float &pitchDiffRad)
{
    float targetWorld[3] = {-ringNormal[0], -ringNormal[1], -ringNormal[2]};
    // local = R^T * targetWorld = rotate by the conjugate quaternion.
    float conj[4] = {-camQuat[0], -camQuat[1], -camQuat[2], camQuat[3]};
    float local[3];
    RotateVectorByQuatXYZW(conj, targetWorld, local);
    yawDiffRad = std::atan2(local[0], -local[2]);
    pitchDiffRad = std::atan2(local[1], -local[2]);
}

// Finish detection: both conditions met continuously for 0.5s -> FINISHED (latched).
enum class FinishState { NOT_FINISHED, FINISHING, FINISHED }; // 0,1,2
constexpr float kFinishHoldSec = 0.5f;

inline FinishState UpdateFinishState(FinishState prev, float &timerSec, float distance, float angleRad, float dtSec)
{
    if (prev == FinishState::FINISHED) {
        return FinishState::FINISHED; // latched until reset
    }
    bool onTarget = (distance < kDistOnTargetMeters) && (angleRad < kAngleOnTargetRad);
    if (!onTarget) {
        timerSec = 0.0f;
        return FinishState::NOT_FINISHED;
    }
    timerSec += dtSec;
    if (timerSec >= kFinishHoldSec) {
        return FinishState::FINISHED;
    }
    return FinishState::FINISHING;
}

// ---- ARRingHunt Stage 10: off-screen target guidance ----
//
// Decide whether the ring projects inside the visible screen (with a 10% margin) and, when it
// does not, where on the screen edge a guidance thumbnail should sit and which way its arrow
// should point. Everything is derived from the camera view & projection matrices + the ring's
// world position, so it is pure and host-testable (no AR/GL).
//
// view / proj: 16 floats, column-major (the glm::value_ptr / OpenGL convention used by the
// renderer). Eye space looks down local -Z, +X right, +Y up.
struct OffscreenGuidance {
    bool isInView;           // projects inside |ndc.x|<0.9 && |ndc.y|<0.9 AND in front (w>0)
    bool isBehind;           // ring is (nearly) directly behind the player (|horizontal yaw|>135deg)
    float screenEdgeX;       // 0..1 left..right ratio to pin the thumbnail
    float screenEdgeY;       // 0..1 top..bottom ratio (screen coords, y grows downward)
    float indicatorAngleDeg; // arrow rotation, clockwise from 12 o'clock, pointing at the target
};

// Multiply column-major 4x4 m by vec4 v -> r.
inline void Mat4MulVec4ColMajor(const float m[16], const float v[4], float r[4])
{
    for (int i = 0; i < 4; ++i) {
        r[i] = m[i] * v[0] + m[i + 4] * v[1] + m[i + 8] * v[2] + m[i + 12] * v[3];
    }
}

inline void ComputeOffscreenGuidance(const float view[16], const float proj[16], const float ringPos[3],
                                     OffscreenGuidance &out)
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kMargin = 0.9f; // 10% screen-edge buffer
    constexpr float kEps = 1e-6f;

    float world[4] = {ringPos[0], ringPos[1], ringPos[2], 1.0f};
    float eye[4];
    Mat4MulVec4ColMajor(view, world, eye); // world -> eye
    float clip[4];
    Mat4MulVec4ColMajor(proj, eye, clip); // eye -> clip

    float w = clip[3];
    bool inFront = (w > kEps);

    // Perspective divide. When the target is behind the camera (w<0) the ndc is mirrored.
    float ndcx = 0.0f;
    float ndcy = 0.0f;
    if (std::fabs(w) > kEps) {
        ndcx = clip[0] / w;
        ndcy = clip[1] / w;
    }

    out.isInView = inFront && std::fabs(ndcx) < kMargin && std::fabs(ndcy) < kMargin;

    // Horizontal yaw toward the ring in eye space: 0 dead ahead, +-180 directly behind.
    float yawToRing = std::atan2(eye[0], -eye[2]);
    out.isBehind = std::fabs(yawToRing) > (135.0f * kPi / 180.0f);

    // 2D screen direction toward the target (ndc space: x right, y up). Mirror when behind so the
    // thumbnail wraps to the correct side instead of the geometrically-flipped one.
    float dirX = inFront ? ndcx : -ndcx;
    float dirY = inFront ? ndcy : -ndcy;

    // Push the direction onto the [-1,1] screen box edge.
    float ax = std::fabs(dirX);
    float ay = std::fabs(dirY);
    float scale = (ax > ay) ? ax : ay;
    float edgeX = 0.0f;
    float edgeY = -1.0f; // degenerate (dead center but offscreen/behind) -> point straight down
    if (scale > kEps) {
        edgeX = dirX / scale;
        edgeY = dirY / scale;
    }

    // ndc edge -> screen ratio. Flip Y because ndc +1 is the top but screen Y grows downward.
    out.screenEdgeX = (edgeX + 1.0f) * 0.5f;
    out.screenEdgeY = (1.0f - edgeY) * 0.5f;

    // Arrow rotation: clockwise from 12 o'clock. Straight up (edgeY=+1) -> 0deg, right -> +90deg.
    out.indicatorAngleDeg = std::atan2(edgeX, edgeY) * (180.0f / kPi);
}

// Yaw (radians, about world +Y) so that the model's front (local -Z) points at the camera.
// Rendering applies RotY(yaw) (right-handed), so world front = RotY(yaw)*(0,0,-1) = (-sin,0,-cos).
// Requiring that to equal the horizontal object->camera direction (dx,dz) gives
// sin(yaw)=-dx/L, cos(yaw)=-dz/L  =>  yaw = atan2(-dx, -dz).  frontYawOffset (=0, calibrated) is
// added for the model's intrinsic front offset.
inline float ComputeYawToFaceCamera(const float objPos[3], const float camPos[3], float frontYawOffset)
{
    const float dx = camPos[0] - objPos[0];
    const float dz = camPos[2] - objPos[2];
    return std::atan2(-dx, -dz) + frontYawOffset;
}

} // namespace ARObject

#endif // OBJECT_MATH_H
