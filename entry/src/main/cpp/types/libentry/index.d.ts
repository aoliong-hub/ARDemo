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

// ARRingHunt ring game. Stage 8: 2-axis feedback (ringColor by distance, arrowColor by angle)
// + yaw/pitch split for the 2D aiming HUD. The old single 4-level `level` is removed.
export interface RingState {
  distance: number;
  angleRad: number;
  yawDiffRad: number;
  pitchDiffRad: number;
  ringColor: string;   // "red" | "green" (distance)
  arrowColor: string;  // "red" | "green" (angle)
  finishState: number; // 0=NOT 1=FINISHING 2=FINISHED
  foundSec: number;
}
export const placeRing: (id: string) => number;
export const resetRing: (id: string) => void;
export const getRingState: (id: string) => RingState;
