/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, Spacing, FontSize } from '../styles/Theme';

// HarmonyOS-native camera-style zoom strip (Stage 12A redo). W / 1x / 2x / 4x / 10x in a single
// dark glass capsule, the selected level wrapped in a small white pill. Tap = swap selection
// (visual only — real zoom is out of scope; see reports/zoom-research.md for why AR Engine offers
// no runtime zoom API). Default selection: 1x.
@Component
export struct ZoomBar {
  // The host can override the initial selection, but typically the bar manages its own state.
  @State private selected: string = '1x';
  private readonly levels: string[] = ['W', '1x', '2x', '4x', '10x'];

  // Selected cell: white filled circle with black text. Unselected: translucent white text on glass.
  @Builder
  cell(level: string): void {
    if (this.selected === level) {
      Text(level)
        .fontSize(FontSize.hud - 1)
        .fontColor('#FF000000')
        .fontWeight(FontWeight.Bold)
        .width(34)
        .height(34)
        .textAlign(TextAlign.Center)
        .backgroundColor(Palette.white)
        .borderRadius(17)
        .onClick(() => { this.selected = level; })
    } else {
      Text(level)
        .fontSize(FontSize.hud - 1)
        .fontColor(Palette.white80)
        .width(34)
        .height(34)
        .textAlign(TextAlign.Center)
        .onClick(() => { this.selected = level; })
    }
  }

  build(): void {
    Row({ space: Spacing.XS }) {
      ForEach(this.levels, (level: string) => {
        this.cell(level);
      }, (level: string) => level)
    }
    .padding({ left: Spacing.S, right: Spacing.S, top: 4, bottom: 4 })
    .backgroundColor(Palette.glass)
    .borderRadius(22)
  }
}
