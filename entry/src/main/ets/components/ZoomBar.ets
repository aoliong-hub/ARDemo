/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, Spacing, FontSize } from '../styles/Theme';

// v13 digital-zoom strip: 1x / 2x / 5x. Highlighted stop = closest to currentZoom (argmin
// |stop - currentZoom|), so e.g. continuous pinch to 3.9x highlights "5x". Tap a stop → invoke
// onZoomChange callback so the host page can drive applyZoom (which writes mZoom in C++ +
// shows the central indicator).
@Component
export struct ZoomBar {
  @Prop currentZoom: number;
  onZoomChange: (z: number) => void = (z: number): void => {};
  private readonly stops: number[] = [1.0, 2.0, 5.0];

  private selectedStop(): number {
    let best: number = this.stops[0];
    let bestDist: number = Math.abs(best - this.currentZoom);
    for (let i = 1; i < this.stops.length; i = i + 1) {
      let s: number = this.stops[i];
      let d: number = Math.abs(s - this.currentZoom);
      if (d < bestDist) {
        bestDist = d;
        best = s;
      }
    }
    return best;
  }

  @Builder
  cell(stop: number): void {
    if (stop === this.selectedStop()) {
      Text(stop.toString() + 'x')
        .fontSize(FontSize.hud - 1)
        .fontColor('#FF000000')
        .fontWeight(FontWeight.Bold)
        .width(34)
        .height(34)
        .textAlign(TextAlign.Center)
        .backgroundColor(Palette.white)
        .borderRadius(17)
        .onClick(() => { this.onZoomChange(stop); })
    } else {
      Text(stop.toString() + 'x')
        .fontSize(FontSize.hud - 1)
        .fontColor(Palette.white80)
        .width(34)
        .height(34)
        .textAlign(TextAlign.Center)
        .onClick(() => { this.onZoomChange(stop); })
    }
  }

  build(): void {
    Row({ space: Spacing.XS }) {
      ForEach(this.stops, (stop: number) => {
        this.cell(stop);
      }, (stop: number) => stop.toString())
    }
    .padding({ left: Spacing.S, right: Spacing.S, top: 4, bottom: 4 })
    .backgroundColor(Palette.glass)
    .borderRadius(22)
  }
}
