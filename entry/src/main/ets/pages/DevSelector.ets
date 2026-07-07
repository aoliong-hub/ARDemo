/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, Spacing, Radius, FontSize } from '../styles/Theme';
import { getSkipNga, setSkipNga } from '../config/CloudConfig';

@Builder
export function DevSelectorBuilder() {
  DevSelector();
}

@Component
struct DevSelector {
  pageInfos: NavPathStack = new NavPathStack();
  @State skipNga: boolean = getSkipNga();

  build(): void {
    NavDestination() {
      Column({ space: Spacing.M }) {
        // ── 顶栏：返回按钮 + 标题 ────────────────────────────────────
        Row() {
          Text('‹')
            .fontSize(32)
            .fontColor(Palette.white)
            .width(44)
            .height(44)
            .textAlign(TextAlign.Center)
            .onClick(() => { this.pageInfos.pop(); })
          Text('设置')
            .fontSize(FontSize.title + 2)
            .fontColor(Palette.white)
            .fontWeight(FontWeight.Bold)
            .layoutWeight(1)
        }
        .width('90%')
        .margin({ top: Spacing.L, bottom: Spacing.M })

        // ── NGA 精调开关 ──────────────────────────────────────────────
        Row() {
          Column({ space: 4 }) {
            Text('NGA 精调对齐')
              .fontSize(14)
              .fontColor(Palette.white)
            Text(this.skipNga ? '关闭 — 锁定后直接拍照' : '开启 — 锁定后进入像素级精调')
              .fontSize(11)
              .fontColor(this.skipNga ? '#FFFF8866' : '#FF88CC88')
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)

          Toggle({ type: ToggleType.Switch, isOn: !this.skipNga })
            .onChange((isOn: boolean) => {
              this.skipNga = !isOn;
              setSkipNga(!isOn);
            })
        }
        .width('90%')
        .padding({ left: 16, right: 16, top: 12, bottom: 12 })
        .backgroundColor(Palette.glass)
        .borderRadius(Radius.M)
      }
      .width('100%')
      .height('100%')
      .backgroundColor('#FF101010')
      .alignItems(HorizontalAlign.Center)
    }
    .onReady((context: NavDestinationContext) => {
      this.pageInfos = context.pathStack;
    })
    .hideTitleBar(true)
    .hideBackButton(true)
    .hideToolBar(true)
  }
}

