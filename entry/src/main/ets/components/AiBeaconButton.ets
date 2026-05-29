/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

// AI 机位 action button — the camera-home primary CTA. Renders only the iridescent sphere asset
// (no text label) at the supplied diameter, with a gentle infinite breathing scale and a press
// bounce. Tap = onTap (host page wires it to placeRing). Designed to sit inline with the ZoomBar
// in CameraChrome; Row's default vertical centering puts the sphere center on the zoom-strip
// horizontal axis automatically.
@Component
export struct AiBeaconButton {
  // ~56vp matches the native camera "调色" affordance the design references.
  @Prop diameter: number = 56;
  onTap: () => void = (): void => {};

  @State private pressed: boolean = false;
  @State private breath: number = 1.0;

  aboutToAppear(): void {
    // 2s breathing 0.96 <-> 1.04 — keeps the sphere alive without dominating the frame.
    this.getUIContext().animateTo({
      duration: 2000,
      curve: Curve.EaseInOut,
      iterations: -1,
      playMode: PlayMode.Alternate
    }, () => {
      this.breath = 1.04;
    });
  }

  private scaleNow(): number { return (this.pressed ? 0.9 : 1.0) * this.breath; }

  build(): void {
    // Plain Image (no Column/Text wrapper) so the sphere is the entire button — Row alignItems
    // defaults to VerticalAlign.Center, so it naturally co-aligns with the ZoomBar in the parent.
    Image($r('app.media.ai_beacon'))
      .width(this.diameter)
      .height(this.diameter)
      .objectFit(ImageFit.Contain)
      .scale({ x: this.scaleNow(), y: this.scaleNow() })
      .animation({ duration: 140, curve: Curve.EaseOut })
      .onTouch((e: TouchEvent) => {
        if (e.type === TouchType.Down) {
          this.pressed = true;
        } else if (e.type === TouchType.Up || e.type === TouchType.Cancel) {
          this.pressed = false;
        }
      })
      .onClick(() => { this.onTap(); })
  }
}
