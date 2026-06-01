/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Spacing } from '../styles/Theme';
import { TopBar } from './TopBar';
import { ZoomBar } from './ZoomBar';
import { AiBeaconButton } from './AiBeaconButton';
import { CameraBottomBar } from './CameraBottomBar';

// Stage 12A redo: HarmonyOS-native camera-style chrome. Single page (no mode picker — 水平/对象/
// 官方场景 have all been demoted to the hidden DevSelector). Layout:
//
//   ┌─────────────────────────────────────┐
//   │   TopBar  (dark glass capsule)      │   ← top
//   │                                     │
//   │     (AR preview + per-scene HUD)    │   ← middle: transparent pass-through
//   │                                     │
//   │   ┌──── ZoomBar ────┐  AiBeacon     │   ← zoom row: zoom strip + AI 机位 sphere
//   │                                     │
//   │   album   shutter   flip            │   ← bottom row
//   └─────────────────────────────────────┘
//
// onSphereTap is the camera-home primary action — host page calls placeRing on a press.
// onSettingsLongPress is the hidden dev entry (long-press the TopBar gear icon).
@Component
export struct CameraChrome {
  onSphereTap: () => void = (): void => {};
  onSettingsLongPress: () => void = (): void => {};
  // v13 zoom: host page (ARRingHuntCam) owns the live zoom level and the applyZoom entry point.
  // Chrome forwards both straight through to ZoomBar.
  @Prop currentZoom: number;
  onZoomChange: (z: number) => void = (z: number): void => {};
  // Stage 12B-1 panel-expanded mode: the reference panel is up, so the chrome compacts itself
  // (ZoomBar fades out, AI sphere shrinks + slides toward the right edge like the native camera's
  // 调色 position when in retouch mode, and the album/shutter/flip row scales to 0.75). All three
  // changes share a single 250ms ease-out so the chrome reads as one coordinated re-layout.
  @Prop panelExpanded: boolean = false;

  // The AI sphere shift when the panel is expanded. The expanded layout pulls it down-and-left:
  // smaller right shift (was 48 → 30) keeps it from drifting too far toward the edge, and the new
  // y shift (24vp) drops it closer to the shutter row so the cluster reads as one tucked-away
  // tool group instead of a floating dot above the shutter.
  private readonly kAiRightShift: number = 30;
  private readonly kAiDownShift: number = 24;

  build(): void {
    Column() {
      // Top — pinned to the top of the screen.
      TopBar({
        onSettingsLongPress: (): void => { this.onSettingsLongPress(); }
      })
      Blank()
      // Bottom block: zoom + AI beacon row, then the album/shutter/flip row. Paddings + spacing
      // are tight (8vp instead of the Theme's Spacing.M/L 16/24) so the whole bottom band
      // compresses ~18% — gives more viewport area for the AR preview.
      Column({ space: 4 }) {
        // Zoom + AI beacon. ZoomBar fades out when the panel is up (opacity, layout space kept so
        // the AI sphere's translation math stays consistent). The AI sphere scales down + slides
        // right via translate, matching the native camera's "compact" tool layout.
        Row({ space: Spacing.M }) {
          ZoomBar({
            currentZoom: this.currentZoom,
            onZoomChange: (z: number): void => { this.onZoomChange(z); }
          })
            .opacity(this.panelExpanded ? 0 : 1)
            .animation({ duration: 250, curve: Curve.EaseOut })
          AiBeaconButton({ onTap: (): void => { this.onSphereTap(); } })
            .scale({ x: this.panelExpanded ? 0.7 : 1.0, y: this.panelExpanded ? 0.7 : 1.0 })
            .translate({
              x: this.panelExpanded ? this.kAiRightShift : 0,
              y: this.panelExpanded ? this.kAiDownShift : 0
            })
            .animation({ duration: 250, curve: Curve.EaseOut })
        }
        .justifyContent(FlexAlign.Center)
        .width('100%')

        // Album/shutter/flip — scaled to 0.75 when panel is up so it tucks under the smaller AI
        // sphere row. Wrapped in a Column so .scale() applies cleanly (custom struct + animation).
        Column() {
          CameraBottomBar()
        }
        .scale({ x: this.panelExpanded ? 0.75 : 1.0, y: this.panelExpanded ? 0.75 : 1.0 })
        .animation({ duration: 250, curve: Curve.EaseOut })
      }
      .width('100%')
      // padding-bottom 12 (was 4) so triplet sits a bit higher in the bottom band, opening up
      // visible breathing room between the triplet bottom edge and the chevron arrow below.
      .padding({ top: 4, bottom: 12 })
    }
    .width('100%')
    .height('100%')
    .padding({ top: Spacing.S })
    .hitTestBehavior(HitTestMode.Transparent)
  }
}
