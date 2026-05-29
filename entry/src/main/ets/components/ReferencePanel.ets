/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, FontSize } from '../styles/Theme';

// Stage 12B-1 (final): floating-island reference-shot card. The card has horizontal margins
// (passed from the host as horizontalMargin) so it doesn't kiss the screen edges, plus rounded
// corners + a soft drop shadow to read as a floating panel.
//
// In-card chevron lives at the TOP edge of the grey card surface (pointing DOWN — "tap to
// collapse"). When the card collapses, the host shows a SEPARATE standalone chevron at a fixed
// y near the screen bottom (pointing UP), and this whole component goes away.
//
// Z-order: rendered BEFORE the chrome in the host's Stack so the compressed chrome controls
// (album / shutter / flip in compact mode) float on top of the card surface.
@Component
export struct ReferencePanel {
  @Prop panelHeight: number = 0;
  @Prop cardExpanded: boolean = false;
  @Prop horizontalMargin: number = 12;
  onToggle: () => void = (): void => {};

  build(): void {
    // Outer container: full-width with horizontal padding = the card's left+right margin.
    // The inner Column IS the grey card surface — bg + rounded corners + shadow.
    Column() {
      Column() {
        // In-card chevron handle — top of the card.
        Row() {
          Image($r('app.media.chevron_down'))
            .width(36)
            .height(12)
            .objectFit(ImageFit.Contain)
        }
        .width('100%')
        .height(28)
        .justifyContent(FlexAlign.Center)
        .alignItems(VerticalAlign.Center)
        .onClick(() => { this.onToggle(); })

        // Reference-shot placeholder — fills the remaining card area.
        Stack({ alignContent: Alignment.Center }) {
          Text('参考构图')
            .fontSize(FontSize.hud)
            .fontColor(Palette.white60)
        }
        .layoutWeight(1)
        .width('100%')
      }
      .width('100%')
      .layoutWeight(1)
      .backgroundColor(this.cardExpanded ? '#FF2D2D2D' : '#00000000')
      .borderRadius(this.cardExpanded ? 22 : 0)
      .shadow({
        radius: 16,
        color: this.cardExpanded ? '#80000000' : '#00000000',
        offsetX: 0,
        offsetY: 6
      })
      .animation({ duration: 300, curve: Curve.EaseInOut })
    }
    .width('100%')
    .height(this.panelHeight)
    .padding({ left: this.horizontalMargin, right: this.horizontalMargin, top: 0, bottom: 0 })
    .clip(true)
  }
}
