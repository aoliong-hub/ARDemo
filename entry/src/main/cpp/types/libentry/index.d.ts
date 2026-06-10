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

import { resourceManager } from '@kit.LocalizationKit';

export const start: (id: string, params: Int32Array) => void;
export const show: (id: string) => void;
export const hide: (id: string) => void;
export const update: (id: string) => number;
export const stop: (id: string) => void;
export const init: (resmgr: resourceManager.ResourceManager) => void;
export const getDistance: (id: string) => string;
export const initImage: (id: string, width: number, height: number, buffer: ArrayBuffer) => number;
export const setPath: (id: string, path: string) => void;
export const saveImageDataBaseToLocal: (id: string, path: string) => void;
export const getImageCount: (id: string) => number;
export const getVolume: (id: string) => string;

// ARObject interface-style placement. Returns a monotonic objectId (>=0 on success, -1 on failure).
export const placeObjectAtWorld: (id: string, x: number, y: number, z: number, modelId?: string) => number;
export const placeObjectInFrontOfCamera: (id: string, distance: number, modelId?: string) => number;
export const removeObject: (id: string, objectId: number) => boolean;
export const clearAllObjects: (id: string) => number;

// ARArrowAlign alignment game.
export interface AlignmentState {
  angleRad: number;
  isAligned: boolean;
  ready: boolean;
  targetPlaced: boolean;
}
export const placeTargetArrow: (id: string) => number;
export const resetArrowAlign: (id: string) => void;
export const getAlignmentState: (id: string) => AlignmentState;

// ARRingHunt Wayfinder beacon (Stage 11A). Reports the camera->beacon-top distance, whether a
// beacon is placed, a finish state (always 0 in 11A; the state machine returns in 11B), and the
// off-screen target guidance (project the beacon top to screen space to drive the droplet arrow).
export interface RingState {
  distance: number;     // metres from the camera to the beacon top
  ringPlaced: boolean;  // a beacon is currently placed
  finishState: number;  // 0=NOT 1=FINISHING 2=FINISHED (always 0 in Stage 11A)
  // Off-screen target guidance (targets the beacon top).
  isTargetInView: boolean;    // beacon top projects inside the visible screen
  screenEdgeX: number;        // 0..1 left..right ratio to pin the edge thumbnail
  screenEdgeY: number;        // 0..1 top..bottom ratio (screen coords, y down)
  isBehind: boolean;          // beacon is directly behind the player (|yaw| > 135deg)
  indicatorAngleDeg: number;  // arrow rotation, clockwise from 12 o'clock, toward the target
  ndcX: number;               // raw projected ndc.x (ArkTS derives the screen-space guidance ray)
  ndcY: number;               // raw projected ndc.y
  // Stage 11D: 6DoF alignment challenge.
  huntPhase: number;          // 0=APPROACHING 1=ALIGNING(snapped) 2=LOCKED
  yawDiffRad: number;         // yaw difference to the alignment frame normal
  pitchDiffRad: number;       // pitch difference to the alignment frame normal
  rollDiffRad: number;        // Stage 12B-1: roll difference (banner only; not part of align gate)
  // 重构(2026-06-03)— isAligned 现在指 "snapReady"(吸附触发):dist<30cm + fillRatio∈[0.85↔0.90, 1.10],
  // 不含角度。0→1 边缘 = 吸附特效触发(膜 + 闪 + 青绿 + 震动)。
  isAligned: boolean;
  isLocked: boolean;          // angle held kLockSec -> LOCKED(吸附后角度对准 2s)
  // Stage 11D interface extension: current 6DoF target orientation (degrees), for either placement path.
  targetYawDeg: number;
  targetPitchDeg: number;
  targetRollDeg: number;
  // 重构 — 角度对准(吸附后 HUD 精调用):yaw + pitch < 5°↔7° 滞回。HUD 视觉(L 括 + dot glow)+ LOCK timer 用。
  isAngleAligned: boolean;
  // 重构 — 实时 fillRatio(框 4 角 NDC 占可见区比例 × zoom)。调试/调参用。
  fillRatio: number;
  // 重构(2026-06-03)— 1.5s 持续门:snapReady 瞬时状态 + 当前持续秒数。
  // UI 不直接依赖(吸附触发由 isAligned=phase==1 边缘驱动);调试浮层显示进度。
  snapReady: boolean;
  snapHoldSec: number;
  // 修(2026-06-03 问题 2)— C grace:进 C 后已过秒数。C→D 需要 ≥ 1.5s。调试浮层显示。
  cGraceSec: number;
  // 修(2026-06-03 问题 4)— 原始 fillRatio(未 EMA 平滑)。调试浮层 fillR vs fillS 对比。
  fillRatioRaw: number;
}
export const placeRing: (id: string) => number;
// Place a beacon with an externally-supplied 6DoF target orientation (degrees, relative to the
// camera forward at placement). Clamped to yaw +/-180, pitch +/-90, roll +/-180. Returns objectId
// (>=0) or -1 on failure (not ready / bad args).
export const placeRingWithOrientation: (id: string, yawDeg: number, pitchDeg: number, rollDeg: number) => number;
// Stage 12C: place a beacon at a CAMERA-RELATIVE horizontal position with a per-beacon ring
// height. Base ground-snapped (same heuristic as placeRing); the visible ring/badge sits at y.
//   x = right    (metres, horizontal — perpendicular to user's heading)
//   y = up       (metres, world vertical — height of the ring above the ground base)
//   z = forward  (metres, horizontal — user's heading at call time)
//   yawDeg/pitchDeg/rollDeg: same clamping as placeRingWithOrientation.
// Returns objectId (>=0) or -1 on failure (not ready / bad args).
export const placeRingAt: (id: string, x: number, y: number, z: number,
  yawDeg: number, pitchDeg: number, rollDeg: number) => number;
// Place a beacon at an ABSOLUTE WORLD position with the beacon's own quaternion.
// Bypasses camera-relative reconstruction — no world→local→world round-trip.
export const placeRingAtWorld: (id: string, worldX: number, worldY: number, worldZ: number,
  quatX: number, quatY: number, quatZ: number, quatW: number) => number;
// v13: digital zoom — set GL viewport scale [1.0, 5.0]. 1.0 = native; clamped silently.
export const setZoom: (id: string, level: number) => void;
// Phase 2 — ArkTS push 可见区 NDC y 边界(从布局算)。
//   centerY     = (vis_top_ndc + vis_bot_ndc) / 2,正典型(底黑条比顶厚 → 可见中心高于 FB 中心)
//   halfExtent  = (vis_top_ndc - vis_bot_ndc) / 2,典型 ≈ 0.666
// Renderer 用 centerY 做 clip-space y shift,fillRatio 判据用 halfExtent 做纵向归一。
// 跨机型自适应,每次 onAreaChange / 初始化时 push 一次。
export const setVisibleNdcY: (id: string, centerY: number, halfExtent: number) => void;
// ArkTS 监听 display.on('change') 后回喂 rotation(display.getDefaultDisplaySync().rotation: 0/1/2/3)。
// native 端更新 mDisplayRotation 并重调 SetDisplayGeometry,让 AR Engine 知晓新方向。
export const setDisplayRotation: (id: string, rotation: number) => void;
// 物理手机朝向(每帧从 cam.viewMat 推算的 camRoll snap 结果)。
// 0 = PORTRAIT, 1 = LANDSCAPE_CW(顺时针横), 2 = LANDSCAPE_CCW(逆时针横)
// ArkTS 抓帧后读这个,决定是否旋转 JPEG 再发 da3。
export const getOrientation: (id: string) => number;
// 标定工具:同步读取最近一帧的相机姿态。返回 null 表示未跟踪稳定。
// p 7 元素:旋转四元数 (qx,qy,qz,qw) + 位置 (px,py,pz)。世界坐标系。
export interface CamPose {
  qx: number; qy: number; qz: number; qw: number;
  px: number; py: number; pz: number;
}
export const getLatestCamRawPose: (id: string) => CamPose | null;
export const getLatestCamDispPose: (id: string) => CamPose | null;
export const resetRing: (id: string) => void;
export const getRingState: (id: string) => RingState;

// Da3 capture (request/poll). captureFrame flips a request flag; the next render fills the buffer.
// Poll isFrameReady every ~50ms then takeFrameRGBA — moves the bytes out (clearing readiness),
// returns {buffer, width, height} with origin = top-left (renderer Y-flipped for ArkTS consumers).
// Returns null if no frame is ready.
export interface CapturedFrame {
  buffer: ArrayBuffer;
  width: number;
  height: number;
}
export const captureFrame: (id: string) => void;
export const isFrameReady: (id: string) => boolean;
export const takeFrameRGBA: (id: string) => CapturedFrame | null;
export const takeFrameRGBAScaled: (id: string, maxDim: number) => CapturedFrame | null;

// 拍照纯净帧(功能 6):glReadPixels 在 wayfinder Render 之前,抓到的不含信标/炫彩圈/对齐框等
// AR 物体,只有纯相机背景画面。同一组 request/poll/take 套路,共用 CapturedFrame 类型。
export const captureCleanFrame: (id: string) => void;
export const isCleanFrameReady: (id: string) => boolean;
export const takeCleanFrameRGBA: (id: string) => CapturedFrame | null;
