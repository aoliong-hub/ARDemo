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
  huntPhase: number;          // 0=APPROACHING 1=ALIGNING 2=LOCKED
  yawDiffRad: number;         // yaw difference to the alignment frame normal
  pitchDiffRad: number;       // pitch difference to the alignment frame normal
  isAligned: boolean;         // instantaneous: within 5deg yaw+pitch and <30cm
  isLocked: boolean;          // alignment held 0.5s -> LOCKED
}
export const placeRing: (id: string) => number;
export const resetRing: (id: string) => void;
export const getRingState: (id: string) => RingState;
