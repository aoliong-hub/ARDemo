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

// Unit tests for object_math.h (pure math; no AR/GL deps).
// Self-contained assertion harness (gtest not available for the ohos target). Cross-compiled
// for aarch64-ohos and executed on the device via `hdc shell` (same arch & code path as the app).
// Exit code 0 == all pass.

#include "object_math.h"
#include <cmath>
#include <cstdio>

using namespace ARObject;

static int g_failures = 0;
static int g_checks = 0;

static void ExpectNear(const char *what, float actual, float expected, float tol = 1e-4f)
{
    ++g_checks;
    if (std::fabs(actual - expected) > tol) {
        ++g_failures;
        std::printf("  FAIL %s: got %.6f, expected %.6f (tol %.6f)\n", what, actual, expected, tol);
    } else {
        std::printf("  ok   %s: %.6f\n", what, actual);
    }
}

static void ExpectTrue(const char *what, bool cond)
{
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL %s: expected true\n", what);
    } else {
        std::printf("  ok   %s\n", what);
    }
}

static void TestComputeForwardPoint()
{
    std::printf("[TEST] ComputeForwardPoint\n");
    // (a) camera at origin, identity orientation, d=1 -> (0,0,-1)
    {
        float camPos[3] = {0, 0, 0};
        float q[4] = {0, 0, 0, 1}; // identity (xyzw)
        float out[3];
        ComputeForwardPoint(camPos, q, 1.0f, out);
        ExpectNear("a.x", out[0], 0.0f);
        ExpectNear("a.y", out[1], 0.0f);
        ExpectNear("a.z", out[2], -1.0f);
    }
    // (b) yaw 90 deg about Y, d=1 -> (-1,0,0)
    {
        float camPos[3] = {0, 0, 0};
        float s = std::sin(static_cast<float>(M_PI) / 4.0f);
        float c = std::cos(static_cast<float>(M_PI) / 4.0f);
        float q[4] = {0, s, 0, c}; // Ry(90) xyzw
        float out[3];
        ComputeForwardPoint(camPos, q, 1.0f, out);
        ExpectNear("b.x", out[0], -1.0f);
        ExpectNear("b.y", out[1], 0.0f);
        ExpectNear("b.z", out[2], 0.0f);
    }
    // (c) camera at (1,2,3), pitch -30 deg (look down), d=2.
    //     forward = (0,-0.5,-0.866) -> point (1,1,1.268). Verify it is BELOW the camera (y down).
    {
        float camPos[3] = {1, 2, 3};
        float halfPitch = (-static_cast<float>(M_PI) / 6.0f) * 0.5f; // -30 deg about X
        float q[4] = {std::sin(halfPitch), 0, 0, std::cos(halfPitch)}; // Rx(-30) xyzw
        float out[3];
        ComputeForwardPoint(camPos, q, 2.0f, out);
        ExpectNear("c.x", out[0], 1.0f);
        ExpectNear("c.y", out[1], 1.0f); // 2 + (-0.5)*2
        ExpectNear("c.z", out[2], 1.26795f);
        // "looking down -> placed lower than camera": vertical delta is negative.
        ExpectTrue("c.below_camera (out.y < cam.y)", out[1] < camPos[1]);
    }
}

static void TestPackPoseRaw()
{
    std::printf("[TEST] PackPoseRaw\n");
    // (a) identity quat + zero trans
    {
        float q[4] = {0, 0, 0, 1};
        float t[3] = {0, 0, 0};
        float p[7];
        PackPoseRaw(q, t, QuatFormat::XYZW, p);
        ExpectNear("a.xyzw[0]", p[0], 0.0f);
        ExpectNear("a.xyzw[1]", p[1], 0.0f);
        ExpectNear("a.xyzw[2]", p[2], 0.0f);
        ExpectNear("a.xyzw[3]", p[3], 1.0f);
        ExpectNear("a.xyzw[4]", p[4], 0.0f);
        ExpectNear("a.xyzw[5]", p[5], 0.0f);
        ExpectNear("a.xyzw[6]", p[6], 0.0f);
        PackPoseRaw(q, t, QuatFormat::WXYZ, p);
        ExpectNear("a.wxyz[0]", p[0], 1.0f); // w first
        ExpectNear("a.wxyz[1]", p[1], 0.0f);
        ExpectNear("a.wxyz[2]", p[2], 0.0f);
        ExpectNear("a.wxyz[3]", p[3], 0.0f);
    }
    // (b) Ry(180) quat (0,1,0,0) + trans (1,2,3): verify component order + translation
    {
        float q[4] = {0, 1, 0, 0}; // xyzw
        float t[3] = {1, 2, 3};
        float p[7];
        PackPoseRaw(q, t, QuatFormat::XYZW, p);
        ExpectNear("b.xyzw[0]", p[0], 0.0f);
        ExpectNear("b.xyzw[1]", p[1], 1.0f);
        ExpectNear("b.xyzw[2]", p[2], 0.0f);
        ExpectNear("b.xyzw[3]", p[3], 0.0f);
        ExpectNear("b.xyzw.tx", p[4], 1.0f);
        ExpectNear("b.xyzw.ty", p[5], 2.0f);
        ExpectNear("b.xyzw.tz", p[6], 3.0f);
        PackPoseRaw(q, t, QuatFormat::WXYZ, p);
        ExpectNear("b.wxyz[0]", p[0], 0.0f); // w
        ExpectNear("b.wxyz[1]", p[1], 0.0f); // x
        ExpectNear("b.wxyz[2]", p[2], 1.0f); // y
        ExpectNear("b.wxyz[3]", p[3], 0.0f); // z
        ExpectNear("b.wxyz.tx", p[4], 1.0f);
    }
    // (c) non-normalized quat (0,2,0,0) -> normalized (0,1,0,0)
    {
        float q[4] = {0, 2, 0, 0};
        float t[3] = {0, 0, 0};
        float p[7];
        PackPoseRaw(q, t, QuatFormat::XYZW, p);
        ExpectNear("c.norm[0]", p[0], 0.0f);
        ExpectNear("c.norm[1]", p[1], 1.0f);
        ExpectNear("c.norm[2]", p[2], 0.0f);
        ExpectNear("c.norm[3]", p[3], 0.0f);
        float mag = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2] + p[3] * p[3]);
        ExpectNear("c.unit_magnitude", mag, 1.0f);
    }
}

// Apply RotY(yaw) to the model front (local -Z) and check it points at the camera.
// This is the convention-independent property the function must satisfy.
static void ExpectFacesCamera(const char *what, const float objPos[3], const float camPos[3])
{
    float yaw = ComputeYawToFaceCamera(objPos, camPos, 0.0f);
    // world front after RotY(yaw): (-sin yaw, 0, -cos yaw)
    float fx = -std::sin(yaw);
    float fz = -std::cos(yaw);
    float dx = camPos[0] - objPos[0];
    float dz = camPos[2] - objPos[2];
    float len = std::sqrt(dx * dx + dz * dz);
    float dot = (len > 1e-6f) ? (fx * (dx / len) + fz * (dz / len)) : 1.0f;
    // dot==1 means front aligns with object->camera direction.
    ExpectNear(what, dot, 1.0f, 1e-4f);
}

static void TestComputeYawToFaceCamera()
{
    std::printf("[TEST] ComputeYawToFaceCamera\n");
    float obj[3] = {0, 0, 0};
    // Primary (convention-independent) property: front faces the camera in all directions.
    {
        float front[3] = {0, 0, -1};
        ExpectFacesCamera("front (camZ<objZ)", obj, front);
        float back[3] = {0, 0, 1};
        ExpectFacesCamera("back (camZ>objZ)", obj, back);
        float right[3] = {1, 0, 0};
        ExpectFacesCamera("right (camX>objX)", obj, right);
        float left[3] = {-1, 0, 0};
        ExpectFacesCamera("left (camX<objX)", obj, left);
        float diag[3] = {1, 0, 1};
        ExpectFacesCamera("diag (+x,+z)", obj, diag);
        float diag2[3] = {-3, 0, 2};
        ExpectFacesCamera("diag2 (-x,+z)", obj, diag2);
    }
    // Raw yaw values (device-correct signs; note these differ from the brief's (c)/(d),
    // whose signs would mirror the rotation — see ExpectFacesCamera which is the real check).
    {
        float cam[3] = {0, 0, -1}; // front
        ExpectNear("yaw front=0", ComputeYawToFaceCamera(obj, cam, 0.0f), 0.0f);
    }
    {
        float cam[3] = {0, 0, 1}; // back
        ExpectNear("|yaw back|=pi", std::fabs(ComputeYawToFaceCamera(obj, cam, 0.0f)),
                   static_cast<float>(M_PI));
    }
    {
        float cam[3] = {1, 0, 0}; // right
        ExpectNear("yaw right=-pi/2", ComputeYawToFaceCamera(obj, cam, 0.0f),
                   -static_cast<float>(M_PI) / 2.0f);
    }
    {
        float cam[3] = {-1, 0, 0}; // left
        ExpectNear("yaw left=+pi/2", ComputeYawToFaceCamera(obj, cam, 0.0f),
                   static_cast<float>(M_PI) / 2.0f);
    }
}

static void TestComputeArrowDirection()
{
    std::printf("[TEST] ComputeArrowDirection\n");
    float dir[3];
    // (a) identity -> (0,0,-1)
    {
        float q[4] = {0, 0, 0, 1};
        ComputeArrowDirection(q, dir);
        ExpectNear("a.x", dir[0], 0.0f);
        ExpectNear("a.y", dir[1], 0.0f);
        ExpectNear("a.z", dir[2], -1.0f);
    }
    // (b) Ry(90) -> (-1,0,0)
    {
        float s = std::sin(static_cast<float>(M_PI) / 4.0f);
        float c = std::cos(static_cast<float>(M_PI) / 4.0f);
        float q[4] = {0, s, 0, c};
        ComputeArrowDirection(q, dir);
        ExpectNear("b.x", dir[0], -1.0f);
        ExpectNear("b.y", dir[1], 0.0f);
        ExpectNear("b.z", dir[2], 0.0f);
    }
    // (c) Rx(90) -> (0,1,0) (verified by hand)
    {
        float s = std::sin(static_cast<float>(M_PI) / 4.0f);
        float c = std::cos(static_cast<float>(M_PI) / 4.0f);
        float q[4] = {s, 0, 0, c};
        ComputeArrowDirection(q, dir);
        ExpectNear("c.x", dir[0], 0.0f);
        ExpectNear("c.y", dir[1], 1.0f);
        ExpectNear("c.z", dir[2], 0.0f);
    }
    // (d) Ry(45) -> (-sin45,0,-cos45)
    {
        float s = std::sin(static_cast<float>(M_PI) / 8.0f);
        float c = std::cos(static_cast<float>(M_PI) / 8.0f);
        float q[4] = {0, s, 0, c}; // 45 deg about Y
        ComputeArrowDirection(q, dir);
        ExpectNear("d.x", dir[0], -std::sin(static_cast<float>(M_PI) / 4.0f));
        ExpectNear("d.y", dir[1], 0.0f);
        ExpectNear("d.z", dir[2], -std::cos(static_cast<float>(M_PI) / 4.0f));
        float mag = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        ExpectNear("d.unit", mag, 1.0f);
    }
}

static void TestComputeAngleBetweenArrows()
{
    std::printf("[TEST] ComputeAngleBetweenArrows\n");
    float identity[4] = {0, 0, 0, 1};
    float ry90[4] = {0, std::sin(static_cast<float>(M_PI) / 4.0f), 0, std::cos(static_cast<float>(M_PI) / 4.0f)};
    float ry180[4] = {0, 1, 0, 0};
    float ry45[4] = {0, std::sin(static_cast<float>(M_PI) / 8.0f), 0, std::cos(static_cast<float>(M_PI) / 8.0f)};
    // (a) same -> 0
    ExpectNear("a.same", ComputeAngleBetweenArrows(identity, identity), 0.0f);
    // (b) opposite -> pi
    ExpectNear("b.opposite", ComputeAngleBetweenArrows(identity, ry180), static_cast<float>(M_PI));
    // (c) perpendicular -> pi/2
    ExpectNear("c.perp", ComputeAngleBetweenArrows(identity, ry90), static_cast<float>(M_PI) / 2.0f);
    // (d) 45 deg
    ExpectNear("d.45", ComputeAngleBetweenArrows(identity, ry45), static_cast<float>(M_PI) / 4.0f);
}

static void TestUpdateAlignState()
{
    std::printf("[TEST] UpdateAlignState (enter 2deg / exit 4deg)\n");
    const float deg = static_cast<float>(M_PI) / 180.0f;
    // NOT_ALIGNED + <2deg -> ALIGNED
    ExpectTrue("NA+1deg->A", UpdateAlignState(AlignState::NOT_ALIGNED, 1 * deg) == AlignState::ALIGNED);
    // NOT_ALIGNED + in band (3deg) -> stays NOT_ALIGNED
    ExpectTrue("NA+3deg->NA", UpdateAlignState(AlignState::NOT_ALIGNED, 3 * deg) == AlignState::NOT_ALIGNED);
    // NOT_ALIGNED + >4deg -> NOT_ALIGNED
    ExpectTrue("NA+5deg->NA", UpdateAlignState(AlignState::NOT_ALIGNED, 5 * deg) == AlignState::NOT_ALIGNED);
    // ALIGNED + in band (3deg) -> stays ALIGNED (hysteresis)
    ExpectTrue("A+3deg->A", UpdateAlignState(AlignState::ALIGNED, 3 * deg) == AlignState::ALIGNED);
    // ALIGNED + >4deg -> NOT_ALIGNED
    ExpectTrue("A+5deg->NA", UpdateAlignState(AlignState::ALIGNED, 5 * deg) == AlignState::NOT_ALIGNED);
    // ALIGNED + <2deg -> stays ALIGNED
    ExpectTrue("A+1deg->A", UpdateAlignState(AlignState::ALIGNED, 1 * deg) == AlignState::ALIGNED);
}

static void TestComputeRandomTargetDirection()
{
    std::printf("[TEST] ComputeRandomTargetDirection\n");
    float identity[4] = {0, 0, 0, 1};
    float dir[3];
    // (a) zero offset, identity cam -> forward (0,0,-1)
    {
        ComputeRandomTargetDirection(identity, 0.0f, 0.0f, dir);
        ExpectNear("a.x", dir[0], 0.0f);
        ExpectNear("a.y", dir[1], 0.0f);
        ExpectNear("a.z", dir[2], -1.0f);
    }
    // (b) pure yaw +90 -> (1,0,0)
    {
        ComputeRandomTargetDirection(identity, static_cast<float>(M_PI) / 2.0f, 0.0f, dir);
        ExpectNear("b.x", dir[0], 1.0f);
        ExpectNear("b.y", dir[1], 0.0f);
        ExpectNear("b.z", dir[2], 0.0f);
    }
    // (c) pure pitch +45 -> (0, sin45, -cos45)
    {
        ComputeRandomTargetDirection(identity, 0.0f, static_cast<float>(M_PI) / 4.0f, dir);
        ExpectNear("c.x", dir[0], 0.0f);
        ExpectNear("c.y", dir[1], std::sin(static_cast<float>(M_PI) / 4.0f));
        ExpectNear("c.z", dir[2], -std::cos(static_cast<float>(M_PI) / 4.0f));
    }
    // (d) composite yaw30/pitch20 with a rotated camera: verify the angle from cam-forward
    //     equals acos(cos(pitch)cos(yaw)) (i.e. dir stays inside the offset cone).
    {
        float ry90[4] = {0, std::sin(static_cast<float>(M_PI) / 4.0f), 0, std::cos(static_cast<float>(M_PI) / 4.0f)};
        float yaw = 30.0f * static_cast<float>(M_PI) / 180.0f;
        float pitch = 20.0f * static_cast<float>(M_PI) / 180.0f;
        ComputeRandomTargetDirection(ry90, yaw, pitch, dir);
        float camForward[3];
        ComputeArrowDirection(ry90, camForward);
        float dot = dir[0] * camForward[0] + dir[1] * camForward[1] + dir[2] * camForward[2];
        ExpectNear("d.cone_dot", dot, std::cos(pitch) * std::cos(yaw));
        float mag = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        ExpectNear("d.unit", mag, 1.0f);
    }
}

static void TestQuaternionAlignZNegTo()
{
    std::printf("[TEST] QuaternionAlignZNegTo (round-trip)\n");
    // For several directions, building the quaternion and re-deriving the arrow direction
    // should reproduce the input direction.
    float dirs[4][3] = {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {0.5f, 0.3f, -0.8f}};
    for (int i = 0; i < 4; ++i) {
        float q[4];
        QuaternionAlignZNegTo(dirs[i], q);
        float back[3];
        ComputeArrowDirection(q, back);
        // normalize input for comparison
        float n = std::sqrt(dirs[i][0] * dirs[i][0] + dirs[i][1] * dirs[i][1] + dirs[i][2] * dirs[i][2]);
        ExpectNear("rt.x", back[0], dirs[i][0] / n);
        ExpectNear("rt.y", back[1], dirs[i][1] / n);
        ExpectNear("rt.z", back[2], dirs[i][2] / n);
    }
}

static void TestCompute3DDistance()
{
    std::printf("[TEST] Compute3DDistance\n");
    float o[3] = {0, 0, 0};
    ExpectNear("identity", Compute3DDistance(o, o), 0.0f);
    float x[3] = {3, 0, 0};
    ExpectNear("single-axis", Compute3DDistance(o, x), 3.0f);
    float a[3] = {1, 2, 3};
    float b[3] = {4, 6, 15};
    ExpectNear("three-axis", Compute3DDistance(a, b), std::sqrt(9.0f + 16.0f + 144.0f)); // (3,4,12)->13
}

static void TestComputeRingTargetParams()
{
    std::printf("[TEST] ComputeRingTargetParams\n");
    const float deg = static_cast<float>(M_PI) / 180.0f;
    float identity[4] = {0, 0, 0, 1};
    float origin[3] = {0, 0, 0};
    RingTargetParams p;
    ComputeRingTargetParams(origin, identity, 12345u, p);
    // offsets within range
    ExpectTrue("yawOff in [-60,60]deg", p.yawOff >= -60 * deg && p.yawOff <= 60 * deg);
    ExpectTrue("pitchOff in [-30,30]deg", p.pitchOff >= -30 * deg && p.pitchOff <= 30 * deg);
    ExpectTrue("lateralOff in [-0.3,0.3]", p.lateralOff >= -0.3f && p.lateralOff <= 0.3f);
    ExpectTrue("heightOff in [-0.3,0.3]", p.heightOff >= -0.3f && p.heightOff <= 0.3f);
    // identity cam at origin: forward=(0,0,-1), right=(1,0,0), up=(0,1,0)
    // targetPos = (lateralOff, heightOff, -1)
    ExpectNear("targetPos.x=lat", p.targetPos[0], p.lateralOff);
    ExpectNear("targetPos.y=height", p.targetPos[1], p.heightOff);
    ExpectNear("targetPos.z=-1", p.targetPos[2], -1.0f);
    // translated camera (5,1,2), identity orientation -> basePos shifts accordingly
    float cam2[3] = {5, 1, 2};
    RingTargetParams p2;
    ComputeRingTargetParams(cam2, identity, 999u, p2);
    ExpectNear("p2.x=5+lat", p2.targetPos[0], 5.0f + p2.lateralOff);
    ExpectNear("p2.y=1+height", p2.targetPos[1], 1.0f + p2.heightOff);
    ExpectNear("p2.z=2-1", p2.targetPos[2], 1.0f);
}

static void TestComputeAlignmentLevel()
{
    std::printf("[TEST] ComputeAlignmentLevel\n");
    const float deg = static_cast<float>(M_PI) / 180.0f;
    // kDistOnTargetMeters = 0.15, kDistNearMeters = 0.60.
    ExpectTrue("close+aligned->GREEN", ComputeAlignmentLevel(0.10f, 2 * deg) == AlignLevel::GREEN);
    ExpectTrue("far+aligned->ORANGE", ComputeAlignmentLevel(0.50f, 2 * deg) == AlignLevel::ORANGE);
    ExpectTrue("near+offAngle->YELLOW", ComputeAlignmentLevel(0.40f, 20 * deg) == AlignLevel::YELLOW);
    ExpectTrue("far+offAngle->RED", ComputeAlignmentLevel(1.0f, 60 * deg) == AlignLevel::RED);
    ExpectTrue("close+offAngle->YELLOW", ComputeAlignmentLevel(0.10f, 20 * deg) == AlignLevel::YELLOW);
    ExpectTrue("veryFar+aligned->ORANGE", ComputeAlignmentLevel(1.0f, 2 * deg) == AlignLevel::ORANGE);
    ExpectTrue("boundary dist=0.15+aligned->ORANGE", ComputeAlignmentLevel(0.15f, 2 * deg) == AlignLevel::ORANGE);
    ExpectTrue("dist=0.70 offAngle->RED", ComputeAlignmentLevel(0.70f, 20 * deg) == AlignLevel::RED);
}

static void TestUpdateFinishState()
{
    std::printf("[TEST] UpdateFinishState\n");
    const float deg = static_cast<float>(M_PI) / 180.0f;
    // NOT_FINISHED + on-target (dist < 0.15), timer < 0.5 -> FINISHING
    {
        float t = 0.0f;
        FinishState s = UpdateFinishState(FinishState::NOT_FINISHED, t, 0.10f, 2 * deg, 0.3f);
        ExpectTrue("NF+onTarget(0.3s)->FINISHING", s == FinishState::FINISHING);
        ExpectNear("timer=0.3", t, 0.3f);
    }
    // FINISHING + on-target, timer crosses 0.5 -> FINISHED
    {
        float t = 0.3f;
        FinishState s = UpdateFinishState(FinishState::FINISHING, t, 0.10f, 2 * deg, 0.3f);
        ExpectTrue("FINISHING+onTarget(0.6s)->FINISHED", s == FinishState::FINISHED);
    }
    // FINISHING + off-target -> timer reset, NOT_FINISHED
    {
        float t = 0.4f;
        FinishState s = UpdateFinishState(FinishState::FINISHING, t, 1.0f, 2 * deg, 0.1f);
        ExpectTrue("FINISHING+offTarget->NF", s == FinishState::NOT_FINISHED);
        ExpectNear("timer reset", t, 0.0f);
    }
    // FINISHED stays FINISHED (latched)
    {
        float t = 0.0f;
        FinishState s = UpdateFinishState(FinishState::FINISHED, t, 1.0f, 90 * deg, 0.1f);
        ExpectTrue("FINISHED latched", s == FinishState::FINISHED);
    }
    // NOT_FINISHED + off-target -> NOT_FINISHED
    {
        float t = 0.0f;
        FinishState s = UpdateFinishState(FinishState::NOT_FINISHED, t, 1.0f, 2 * deg, 0.1f);
        ExpectTrue("NF+offTarget->NF", s == FinishState::NOT_FINISHED);
    }
}

static void TestComputeYawPitchDiff()
{
    std::printf("[TEST] ComputeYawPitchDiff\n");
    const float deg = static_cast<float>(M_PI) / 180.0f;
    float identity[4] = {0, 0, 0, 1};
    float yaw = 0.0f;
    float pitch = 0.0f;
    // (a) camera facing target: target_local = forward (0,0,-1) -> ringNormal = (0,0,1)
    {
        float ringNormal[3] = {0, 0, 1};
        ComputeYawPitchDiff(identity, ringNormal, yaw, pitch);
        ExpectNear("a.yaw=0", yaw, 0.0f);
        ExpectNear("a.pitch=0", pitch, 0.0f);
    }
    // (b) target to the right 30deg: target_local = (sin30,0,-cos30) -> ringNormal = -that
    {
        float c = std::cos(30 * deg);
        float s = std::sin(30 * deg);
        float ringNormal[3] = {-s, 0, c};
        ComputeYawPitchDiff(identity, ringNormal, yaw, pitch);
        ExpectNear("b.yaw=+30", yaw, 30 * deg);
        ExpectNear("b.pitch=0", pitch, 0.0f);
    }
    // (c) target to the left 30deg
    {
        float c = std::cos(30 * deg);
        float s = std::sin(30 * deg);
        float ringNormal[3] = {s, 0, c};
        ComputeYawPitchDiff(identity, ringNormal, yaw, pitch);
        ExpectNear("c.yaw=-30", yaw, -30 * deg);
        ExpectNear("c.pitch=0", pitch, 0.0f);
    }
    // (d) target above 30deg: target_local = (0,sin30,-cos30) -> ringNormal = -that
    {
        float c = std::cos(30 * deg);
        float s = std::sin(30 * deg);
        float ringNormal[3] = {0, -s, c};
        ComputeYawPitchDiff(identity, ringNormal, yaw, pitch);
        ExpectNear("d.yaw=0", yaw, 0.0f);
        ExpectNear("d.pitch=+30", pitch, 30 * deg);
    }
    // (e) target below
    {
        float c = std::cos(30 * deg);
        float s = std::sin(30 * deg);
        float ringNormal[3] = {0, s, c};
        ComputeYawPitchDiff(identity, ringNormal, yaw, pitch);
        ExpectNear("e.pitch=-30", pitch, -30 * deg);
    }
    // (f) right+up 45deg each: target_local = (1,1,-1)/sqrt3 -> ringNormal = -that
    {
        float inv = 1.0f / std::sqrt(3.0f);
        float ringNormal[3] = {-inv, -inv, inv};
        ComputeYawPitchDiff(identity, ringNormal, yaw, pitch);
        ExpectNear("f.yaw=45", yaw, 45 * deg);
        ExpectNear("f.pitch=45", pitch, 45 * deg);
    }
    // (g) non-identity camera (Ry90), target aligned with camera forward -> 0,0
    {
        float ry90[4] = {0, std::sin(static_cast<float>(M_PI) / 4.0f), 0, std::cos(static_cast<float>(M_PI) / 4.0f)};
        // camForward(Ry90) = (-1,0,0); target_world = camForward; ringNormal = -target_world = (1,0,0)
        float ringNormal[3] = {1, 0, 0};
        ComputeYawPitchDiff(ry90, ringNormal, yaw, pitch);
        ExpectNear("g.yaw=0", yaw, 0.0f);
        ExpectNear("g.pitch=0", pitch, 0.0f);
    }
}

// Column-major OpenGL perspective matrix (same convention as glm::perspective / the renderer).
static void MakePerspective(float fovyRad, float aspect, float nearZ, float farZ, float m[16])
{
    for (int i = 0; i < 16; ++i) {
        m[i] = 0.0f;
    }
    float f = 1.0f / std::tan(fovyRad * 0.5f);
    m[0] = f / aspect;                              // col0,row0
    m[5] = f;                                       // col1,row1
    m[10] = (farZ + nearZ) / (nearZ - farZ);        // col2,row2
    m[11] = -1.0f;                                  // col2,row3
    m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ); // col3,row2
}

static void TestComputeOffscreenGuidance()
{
    std::printf("[TEST] ComputeOffscreenGuidance\n");
    // Portrait phone framing: vertical fov 60deg, aspect 1080/1920, near 0.1, far 100.
    float proj[16];
    MakePerspective(60.0f * static_cast<float>(M_PI) / 180.0f, 1080.0f / 1920.0f, 0.1f, 100.0f, proj);
    // Camera at origin, identity orientation -> view = identity (eye space == world space).
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    // (1) dead ahead, 1m in front -> in view; edge values irrelevant.
    {
        float ring[3] = {0.0f, 0.0f, -1.0f};
        OffscreenGuidance g;
        ComputeOffscreenGuidance(view, proj, ring, g);
        ExpectTrue("1.in_view (dead ahead)", g.isInView);
        ExpectTrue("1.not_behind", !g.isBehind);
    }
    // (2) far to the right & 1m forward -> off the RIGHT edge, vertically centered, arrow points right.
    {
        float ring[3] = {2.0f, 0.0f, -1.0f};
        OffscreenGuidance g;
        ComputeOffscreenGuidance(view, proj, ring, g);
        ExpectTrue("2.not_in_view", !g.isInView);
        ExpectTrue("2.pinned_right_edge (x>0.9)", g.screenEdgeX > 0.9f);
        ExpectTrue("2.vertically_centered (0.4<y<0.6)", g.screenEdgeY > 0.4f && g.screenEdgeY < 0.6f);
        ExpectTrue("2.not_behind", !g.isBehind);
        ExpectTrue("2.arrow_points_right (45<deg<135)", g.indicatorAngleDeg > 45.0f && g.indicatorAngleDeg < 135.0f);
    }
    // (3) up & to the left -> off the TOP-LEFT, arrow points up-left (negative deg).
    {
        float ring[3] = {-2.0f, 2.0f, -1.0f};
        OffscreenGuidance g;
        ComputeOffscreenGuidance(view, proj, ring, g);
        ExpectTrue("3.not_in_view", !g.isInView);
        ExpectTrue("3.pinned_left_side (x<0.2)", g.screenEdgeX < 0.2f);
        ExpectTrue("3.upper_half (y<0.5)", g.screenEdgeY < 0.5f);
        ExpectTrue("3.arrow_up_left (-90<deg<0)", g.indicatorAngleDeg < 0.0f && g.indicatorAngleDeg > -90.0f);
    }
    // (4) directly behind the camera -> isBehind, never in view.
    {
        float ring[3] = {0.0f, 0.0f, 1.0f};
        OffscreenGuidance g;
        ComputeOffscreenGuidance(view, proj, ring, g);
        ExpectTrue("4.is_behind", g.isBehind);
        ExpectTrue("4.not_in_view", !g.isInView);
        ExpectTrue("4.not_in_front_so_x_irrelevant_but_defined", g.screenEdgeX >= 0.0f && g.screenEdgeX <= 1.0f);
    }
    // (5) inside the screen but close to the right edge (ndc.x ~ 0.85 < 0.9) -> STILL in view.
    {
        float zf = -1.0f;
        float wx = 0.85f * (-zf) / proj[0]; // worldX giving ndc.x = 0.85 at this depth
        float ring[3] = {wx, 0.0f, zf};
        OffscreenGuidance g;
        ComputeOffscreenGuidance(view, proj, ring, g);
        ExpectTrue("5.still_in_view (ndc.x=0.85)", g.isInView);
        ExpectTrue("5.not_behind", !g.isBehind);
    }
    // (6) just outside the right edge (ndc.x ~ 0.95 > 0.9) -> off-screen, pinned right.
    {
        float zf = -1.0f;
        float wx = 0.95f * (-zf) / proj[0];
        float ring[3] = {wx, 0.0f, zf};
        OffscreenGuidance g;
        ComputeOffscreenGuidance(view, proj, ring, g);
        ExpectTrue("6.not_in_view (ndc.x=0.95)", !g.isInView);
        ExpectTrue("6.pinned_right_edge (x>0.9)", g.screenEdgeX > 0.9f);
    }
}

int main()
{
    std::printf("==== object_math unit tests ====\n");
    TestComputeForwardPoint();
    TestPackPoseRaw();
    TestComputeYawToFaceCamera();
    TestComputeArrowDirection();
    TestComputeAngleBetweenArrows();
    TestUpdateAlignState();
    TestComputeRandomTargetDirection();
    TestQuaternionAlignZNegTo();
    TestCompute3DDistance();
    TestComputeRingTargetParams();
    TestComputeAlignmentLevel();
    TestUpdateFinishState();
    TestComputeYawPitchDiff();
    TestComputeOffscreenGuidance();
    std::printf("==== %d checks, %d failures ====\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("RESULT: ALL PASS\n");
        return 0;
    }
    std::printf("RESULT: FAILED\n");
    return 1;
}
