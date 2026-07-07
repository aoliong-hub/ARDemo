/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, Spacing, Radius, FontSize } from '../styles/Theme';
import {
  getSkipNga, setSkipNga,
  getBackendType, setBackendType, getBackendLabel, allBackendTypes, BackendType
} from '../config/CloudConfig';

export interface DevSelectorParam {
  onEnterCalib: () => void;
  onCycleBackend: () => void;
  onTestHealth: () => void;
  backendLabel: string;
}

@Builder
export function DevSelectorBuilder() {
  DevSelector();
}

@Component
struct DevSelector {
  pageInfos: NavPathStack = new NavPathStack();
  // 回调由 ARRingHuntCam 通过 pushDestinationByName 的 param 传入
  private onEnterCalib: () => void = (): void => {};
  private onCycleBackend: () => void = (): void => {};
  private onTestHealth: () => void = (): void => {};

  @State skipNga: boolean = getSkipNga();
  @State backendLabel: string = getBackendLabel(getBackendType());

  @Builder settingRow(icon: string, title: string, subtitle: string, action: () => void): void {
    Row({ space: 12 }) {
      Text(icon)
        .fontSize(18)
        .width(28)
        .textAlign(TextAlign.Center)
      Column({ space: 3 }) {
        Text(title)
          .fontSize(14)
          .fontColor(Palette.white)
        Text(subtitle)
          .fontSize(11)
          .fontColor(Palette.white80)
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
      Text('›')
        .fontSize(20)
        .fontColor(Palette.white80)
    }
    .width('90%')
    .padding({ left: 16, right: 16, top: 14, bottom: 14 })
    .backgroundColor(Palette.glass)
    .borderRadius(Radius.M)
    .onClick(action)
  }

  build(): void {
    NavDestination() {
      Column({ space: Spacing.M }) {
        // ── 顶栏 ────────────────────────────────────────────────────
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

        // ── NGA 精调开关 ─────────────────────────────────────────────
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

        // ── 后端切换 ─────────────────────────────────────────────────
        this.settingRow('🔗', '后端切换', `当前：${this.backendLabel}`, () => {
          const types: BackendType[] = allBackendTypes();
          const idx: number = types.indexOf(getBackendType());
          const next: BackendType = types[(idx + 1) % types.length];
          setBackendType(next);
          this.backendLabel = getBackendLabel(next);
          this.onCycleBackend();
        })

        // ── 测试后端服务 ─────────────────────────────────────────────
        this.settingRow('🩺', '测试后端服务', '检测 generate / guide 健康状态', () => {
          this.onTestHealth();
          this.pageInfos.pop();
        })

        // ── 标定工具 ─────────────────────────────────────────────────
        this.settingRow('🛠', '标定工具', '采集相机内参标定数据', () => {
          this.onEnterCalib();
          this.pageInfos.pop();
        })
      }
      .width('100%')
      .height('100%')
      .backgroundColor('#FF101010')
      .alignItems(HorizontalAlign.Center)
    }
    .onReady((context: NavDestinationContext) => {
      this.pageInfos = context.pathStack;
      const param = context.pathInfo.param as DevSelectorParam | null;
      if (param !== null && param !== undefined) {
        this.onEnterCalib = param.onEnterCalib;
        this.onCycleBackend = param.onCycleBackend;
        this.onTestHealth = param.onTestHealth;
        this.backendLabel = param.backendLabel;
      }
    })
    .hideTitleBar(true)
    .hideBackButton(true)
    .hideToolBar(true)
  }
}
