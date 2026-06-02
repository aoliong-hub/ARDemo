/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { image } from '@kit.ImageKit';
import { Palette, FontSize } from '../styles/Theme';

// Stage 12B-1 (final): floating-island reference-shot card. The card has horizontal margins
// (passed from the host as horizontalMargin) so it doesn't kiss the screen edges, plus rounded
// corners + a soft drop shadow to read as a floating panel.
//
// v13 (d): the card body now renders one of three states:
//   loading=true       → centered LoadingProgress + "生成中…" (Da3 generate in flight)
//   refImage defined   → that PixelMap, tappable (triggers onImageTap → Da3 getGuide stage)
//   else (default)     → the original "参考构图" placeholder text
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
  // v13 (d) — three-state body.
  @ObjectLink @Watch('onRefChanged') refHolder: RefImageHolder;
  @Prop loading: boolean = false;
  // 物理朝向。驱动 chevron / 三处文字 / LoadingProgress 原位自转。
  // 参考图 Image 本轮暂不动(下轮单独加 rotate+scale 适配)。
  @Prop orientation: number = 0;
  onToggle: () => void = (): void => {};
  onImageTap: () => void = (): void => {};

  private rotateDeg(): number {
    if (this.orientation === 1) return -90;
    if (this.orientation === 2) return 90;
    if (this.orientation === 3) return 180;
    return 0;
  }

  // loading 态:LoadingProgress 永远居中固定,只有 Text("生成中…")相对中心 offset 到
  // 图标的"用户视角下方"。offset + rotate 都加 animation,平滑过渡。
  // GAP = 文字相对图标中心的距离(图标半径 28 + 间隙 6 = 34vp,文字紧挨图标)。
  // ArkUI offset:+y = 向下(标准屏幕坐标),所以 PORTRAIT 用 y=+34 让文字在图标下方。
  private readonly kLoadingTextGap: number = 34;

  // CW/CCW 的 x 符号:对调后 CW→x 正(屏幕右=用户视角下) / CCW→x 负(屏幕左=用户视角下)。
  private loadingTextOffsetX(): number {
    if (this.orientation === 1) return this.kLoadingTextGap;
    if (this.orientation === 2) return -this.kLoadingTextGap;
    return 0;
  }
  private loadingTextOffsetY(): number {
    if (this.orientation === 0) return this.kLoadingTextGap;   // PORTRAIT: 文字屏幕下方(y 正)
    if (this.orientation === 3) return -this.kLoadingTextGap;  // FLIP: 文字屏幕上方(y 负)
    return 0;
  }

  // Tracks whether refHolder.image has flipped since last render — used only to force a UI
  // refresh through @ObjectLink (ArkUI auto-rerenders when the watched ref changes).
  private onRefChanged(_propName: string): void { /* no-op; @ObjectLink drives the redraw */ }

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
            // 不转:chevron 是相对卡片的"折叠把手",方向感固定(▼=收起),不该跟 orientation 转。
        }
        .width('100%')
        .height(28)
        .justifyContent(FlexAlign.Center)
        .alignItems(VerticalAlign.Center)
        .onClick(() => { this.onToggle(); })

        // Body — fills the remaining card area. Three-state.
        Stack({ alignContent: Alignment.Center }) {
          if (this.loading) {
            // Stack 居中:LoadingProgress 永远在中心(图标圆形对称,不动不转),
            // 文字相对中心 offset 到图标的"用户视角下方"。offset + rotate 都加 animation,
            // 朝向切换时文字平滑滑动+旋转过去,图标位置完全不变。
            Stack({ alignContent: Alignment.Center }) {
              LoadingProgress()
                .width(56)
                .height(56)
                .color(Palette.white)
              Text('生成中…')
                .fontSize(FontSize.hud)
                .fontColor(Palette.white60)
                .rotate({ x: 0, y: 0, z: 1, angle: this.rotateDeg(), centerX: '50%', centerY: '50%' })
                .offset({ x: this.loadingTextOffsetX(), y: this.loadingTextOffsetY() })
                .animation({ duration: 300, curve: Curve.EaseInOut })
            }
            .width('100%')
            .height('100%')
          } else if (this.refHolder.image !== undefined) {
            // Target image + tap-to-guide hint pinned to the bottom.
            Image(this.refHolder.image)
              .objectFit(ImageFit.Contain)
              .width('100%')
              .height('100%')
              .onClick(() => { this.onImageTap(); })
            Column() {
              Text('点击生成机位引导')
                .fontSize(FontSize.hud - 2)
                .fontColor(Palette.white80)
                .backgroundColor('#80000000')
                .padding({ left: 10, right: 10, top: 4, bottom: 4 })
                .borderRadius(10)
              // 不转:这条 hint 跟下方参考图 Image(触发按钮)同一对,用户接受横拿时它躺着。
            }
            .width('100%')
            .height('100%')
            .justifyContent(FlexAlign.End)
            .alignItems(HorizontalAlign.Center)
            .padding({ bottom: 12 })
            .hitTestBehavior(HitTestMode.Transparent)
          } else {
            // Default placeholder.
            Text('参考构图')
              .fontSize(FontSize.hud)
              .fontColor(Palette.white60)
              .rotate({ x: 0, y: 0, z: 1, angle: this.rotateDeg(), centerX: '50%', centerY: '50%' })
            .animation({ duration: 300, curve: Curve.EaseInOut })
          }
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

// PixelMap can't sit directly on a @Prop in a way ArkUI re-renders cleanly when it flips between
// undefined and a fresh PixelMap, so we route it through this @Observed holder + @ObjectLink in
// the panel. Host page mutates holder.image; panel re-renders.
@Observed
export class RefImageHolder {
  image?: image.PixelMap;
}
