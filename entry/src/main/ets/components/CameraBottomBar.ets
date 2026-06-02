/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, Spacing } from '../styles/Theme';
import { BusinessError } from '@kit.BasicServicesKit';
import { Logger } from '../utils/Logger';

// HarmonyOS-native camera bottom row (Stage 12A redo). Three items, evenly spaced:
//   - left:  album thumbnail (placeholder grey circle)
//   - center: shutter — solid white disc inside a slightly bigger white ring (the native look),
//             with a press bounce + toast "拍照功能开发中"
//   - right: flip-camera glyph (placeholder)
// All actions are visual-only for Stage 12A and toast.
@Component
export struct CameraBottomBar {
  @State private shutterPressed: boolean = false;
  // 物理朝向。驱动左右两侧的 album / flip 原位自转(快门中间对称圆,不转)。
  @Prop orientation: number = 0;
  // 快门点击回调 — host 接管拍照流程(纯净帧抓取 + 存图库 + 短震 + 咔嚓音)。默认空函数兜底,
  // host 不传时回退到 toast 提示(老占位行为)。
  onShutterTap: () => void = (): void => {};

  private rotateDeg(): number {
    if (this.orientation === 1) return -90;
    if (this.orientation === 2) return 90;
    if (this.orientation === 3) return 180;
    return 0;
  }

  private toast(msg: string): void {
    try {
      this.getUIContext().getPromptAction().showToast({ message: msg, duration: 1500 });
    } catch (e) {
      let err: BusinessError = e as BusinessError;
      Logger.warn(`toast failed: ${err.message}`);
    }
  }

  @Builder
  album(): void {
    Stack({ alignContent: Alignment.Center }) {
      // Plain grey disc as a placeholder thumbnail.
      Circle({ width: 48, height: 48 })
        .fill(Palette.white20)
      Circle({ width: 48, height: 48 })
        .fillOpacity(0)
        .stroke(Palette.white60)
        .strokeWidth(1.5)
    }
    .width(56)
    .height(56)
    .rotate({ x: 0, y: 0, z: 1, angle: this.rotateDeg(), centerX: '50%', centerY: '50%' })
    .animation({ duration: 300, curve: Curve.EaseInOut })
    .onClick(() => { this.toast('相册开发中'); })
  }

  @Builder
  shutter(): void {
    Stack({ alignContent: Alignment.Center }) {
      // Outer ring — shrunk 20% (76 → 60vp) per the 12B-1 polish so the shutter no longer
      // dominates the compact bottom band. Inner disc proportionally 48vp.
      Circle({ width: 60, height: 60 })
        .fillOpacity(0)
        .stroke(Palette.white)
        .strokeWidth(3)
      Circle({ width: 48, height: 48 })
        .fill(Palette.white)
    }
    .width(60)
    .height(60)
    .scale({ x: this.shutterPressed ? 0.9 : 1.0, y: this.shutterPressed ? 0.9 : 1.0 })
    .animation({ duration: 120, curve: Curve.EaseOut })
    .onTouch((e: TouchEvent) => {
      if (e.type === TouchType.Down) {
        this.shutterPressed = true;
      } else if (e.type === TouchType.Up || e.type === TouchType.Cancel) {
        this.shutterPressed = false;
      }
    })
    .onClick(() => { this.onShutterTap(); })
  }

  @Builder
  flip(): void {
    // Stage 12C-batch3: replaced the placeholder Circle(glass) + Text('⇋') glyph with the
    // designed PNG icon (ic_flip_camera.png — white camera + circular swap arrows on a baked
    // dark disc). The disc + edge ring live inside the PNG, so the wrapping Stack no longer
    // draws its own Circle background to avoid a double-disc.
    Stack({ alignContent: Alignment.Center }) {
      Image($r('app.media.ic_flip_camera'))
        .width(56)
        .height(56)
        .objectFit(ImageFit.Contain)
    }
    .width(56)
    .height(56)
    .rotate({ x: 0, y: 0, z: 1, angle: this.rotateDeg(), centerX: '50%', centerY: '50%' })
    .animation({ duration: 300, curve: Curve.EaseInOut })
    .onClick(() => { this.toast('翻转开发中'); })
  }

  build(): void {
    // SpaceBetween + wider row pushes album to the left edge and flip to the right edge with the
    // shutter naturally centered (since album + flip are the same size, shutter sits at the row's
    // midpoint). Reads spread-out like the native camera, not crowded in the middle.
    Row() {
      this.album()
      this.shutter()
      this.flip()
    }
    .width('92%')
    .justifyContent(FlexAlign.SpaceBetween)
    .alignItems(VerticalAlign.Center)
  }
}
