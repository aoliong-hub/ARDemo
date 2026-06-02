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
  // 物理朝向(0/1/2/3),驱动炫彩球绕自身中心旋转,横拿/倒置时视觉正。
  @Prop orientation: number = 0;
  onTap: () => void = (): void => {};

  @State private pressed: boolean = false;
  @State private breath: number = 1.0;

  // 朝向→角度映射(首版,实测反了调符号):
  // 0=PORTRAIT→0°, 1=CW→-90°, 2=CCW→+90°, 3=FLIP→180°。
  private rotateDeg(): number {
    if (this.orientation === 1) return -90;
    if (this.orientation === 2) return 90;
    if (this.orientation === 3) return 180;
    return 0;
  }

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
      .rotate({ x: 0, y: 0, z: 1, angle: this.rotateDeg(), centerX: '50%', centerY: '50%' })
      // 复合动画:scale 用 140ms(按压反馈快),rotate 跟随 orientation 变时也用同一 animation;
      // 改成 300ms EaseInOut 让横竖切换更顺滑(原 140ms 太短)。
      .animation({ duration: 300, curve: Curve.EaseInOut })
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
