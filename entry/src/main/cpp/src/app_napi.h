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

#ifndef APP_NAPI_H_
#define APP_NAPI_H_

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <cstdint>
#include <string>
#include <vector>

class AppNapi {
public:
    using ParamType = enum { DEPTH_RENDER_MODE = 0, ROTATION, SEMANTICDENSEMODE };
    using ConfigParams = struct {
        bool depthRenderMode;
        int32_t rotation;
        int32_t semanticDenseMode;
    };

public:
    explicit AppNapi(std::string &id) : id_(id){};
    virtual ~AppNapi() = default;

    // XComponent Callback
    virtual void OnSurfaceCreated(OH_NativeXComponent *component, void *window){};
    virtual void OnSurfaceChanged(OH_NativeXComponent *component, void *window){};
    virtual void OnSurfaceDestroyed(OH_NativeXComponent *component, void *window){};
    virtual void DispatchTouchEvent(OH_NativeXComponent *component, void *window){};
    virtual void DispatchMouseEvent(OH_NativeXComponent *component, void *window){};

    // Lifecycle method on the service side.
    virtual void OnStart(const ConfigParams &params){};
    virtual void OnPause(){};
    virtual void OnResume(){};
    virtual void OnUpdate(){};
    virtual void OnStop(){};
    virtual std::string GetDistance() { return ""; }
    virtual int32_t InitImage(size_t bufferLen, uint32_t width, uint32_t height, uint8_t *buffer) { return 0; }
    virtual void SetPath(std::string){};
    virtual void SaveImageDataBaseToLocal(std::string){};
    virtual uint32_t getImageCount() { return 0; };
    virtual std::string GetVolume() {return "";};

    // Interface-style object placement (ARObject scene). Default stubs return failure;
    // ARObjectApp overrides them in Stage 2/3. (-1 / false / 0 == "not handled".)
    virtual int32_t PlaceObjectAtWorld(float x, float y, float z, const std::string &modelId) { return -1; }
    virtual int32_t PlaceObjectInFrontOfCamera(float distance, const std::string &modelId) { return -1; }
    virtual bool RemoveObject(int32_t objectId) { return false; }
    virtual int32_t ClearAllObjects() { return 0; }

    // Arrow alignment game (ARArrowAlign scene). Default stubs; ArrowAlignApp overrides.
    virtual int32_t PlaceTargetArrow() { return -1; }
    virtual void ResetArrowAlign() {}
    virtual void GetAlignmentState(float &angleRad, bool &aligned, bool &ready, bool &targetPlaced)
    {
        angleRad = 0.0f;
        aligned = false;
        ready = false;
        targetPlaced = false;
    }

    // Ring hunt game (ARRingHunt scene). Default stubs; RingHuntApp overrides.
    virtual int32_t PlaceRing() { return -1; }
    // Stage 11D interface extension: place a beacon with an externally-supplied 6DoF target (degrees,
    // relative to the camera forward at placement). Default unimplemented.
    virtual int32_t PlaceRingWithOrientation(float yawDeg, float pitchDeg, float rollDeg)
    {
        (void)yawDeg;
        (void)pitchDeg;
        (void)rollDeg;
        return -1;
    }
    // Stage 12C: place a beacon at a CAMERA-RELATIVE HORIZONTAL position with a per-beacon ring
    // height. AR Engine world origin is not pinned to session start, so the contract is camera-
    // relative at call time. Beacon BASE is ground-snapped (same heuristic as PlaceRing); the
    // visible top ring/badge sits at height y — that's the user's aim point.
    //   x = right    (metres, horizontal — perpendicular to user's heading)
    //   y = up       (metres, world vertical — height of the ring/badge above the ground base)
    //   z = forward  (metres, horizontal — user's heading at call time)
    virtual int32_t PlaceRingAt(float x, float y, float z, float yawDeg, float pitchDeg, float rollDeg)
    {
        (void)x;
        (void)y;
        (void)z;
        (void)yawDeg;
        (void)pitchDeg;
        (void)rollDeg;
        return -1;
    }
    // v13 digital zoom: 1.0 native, up to 5.0. Scales the GL viewport at render time so the
    // entire frame (camera background + AR overlays) is uniformly enlarged. AR tracking is not
    // affected. Default stub no-ops.
    virtual void SetZoom(float level) { (void)level; }
    virtual void ResetRing() {}

    // Forward the display rotation (0/1/2/3 from display.getDefaultDisplaySync().rotation) so the
    // AR session can re-call SetDisplayGeometry. Default stub no-op for scenes that don't care.
    virtual void SetDisplayRotation(int32_t rotation) { (void)rotation; }

    // Report physical phone orientation derived from camRoll snap (computed at placement time):
    // 0 = PORTRAIT, 1 = LANDSCAPE_CW (camRoll≤-45°), 2 = LANDSCAPE_CCW (camRoll≥+45°).
    // ArkTS reads this to decide whether to rotate the captured JPEG before sending to da3.
    virtual int32_t GetOrientation() const { return 0; }

    // Da3 capture hand-off. Default stubs are no-ops / "not ready" so non-RingHunt scenes do not
    // need to opt in. RequestCapture flips a flag the render path drains on its next frame;
    // IsFrameReady reports whether a captured RGBA buffer is waiting; TakeFrameRGBA moves the bytes
    // out (and clears the ready flag).
    virtual void RequestCapture() {}
    virtual bool IsFrameReady() const { return false; }
    virtual bool TakeFrameRGBA(std::vector<uint8_t> &outRGBA, int &outW, int &outH)
    {
        (void)outRGBA;
        (void)outW;
        (void)outH;
        return false;
    }
    // The Wayfinder beacon exposes its distance, placement, the off-screen guidance fields, and
    // (Stage 11D) the 6DoF alignment challenge: huntPhase (0=APPROACHING 1=ALIGNING 2=LOCKED),
    // the yaw/pitch diff to the alignment frame, and instantaneous/held alignment flags.
    virtual void GetRingState(float &distance, bool &ringPlaced, int32_t &finishState, bool &isTargetInView,
                              float &screenEdgeX, float &screenEdgeY, bool &isBehind, float &indicatorAngleDeg,
                              float &ndcX, float &ndcY, int32_t &huntPhase, float &yawDiffRad, float &pitchDiffRad,
                              float &rollDiffRad, bool &isAligned, bool &isLocked, float &targetYawDeg,
                              float &targetPitchDeg, float &targetRollDeg)
    {
        distance = 0.0f;
        ringPlaced = false;
        finishState = 0;
        isTargetInView = true;
        screenEdgeX = 0.5f;
        screenEdgeY = 0.5f;
        isBehind = false;
        indicatorAngleDeg = 0.0f;
        ndcX = 0.0f;
        ndcY = 0.0f;
        huntPhase = 0;
        yawDiffRad = 0.0f;
        pitchDiffRad = 0.0f;
        rollDiffRad = 0.0f;
        isAligned = false;
        isLocked = false;
        targetYawDeg = 0.0f;
        targetPitchDeg = 0.0f;
        targetRollDeg = 0.0f;
    }

public:
    std::string id_;
};

#endif // APP_NAPI_H_
