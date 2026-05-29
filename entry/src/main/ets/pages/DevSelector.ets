/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { BusinessError } from '@kit.BasicServicesKit';
import { Logger } from '../utils/Logger';
import { Palette, Spacing, Radius, FontSize } from '../styles/Theme';

@Builder
export function DevSelectorBuilder() {
  DevSelector();
}

// Hidden dev-only entry — reached by long-pressing the gear icon in CameraChrome's TopBar for 1s.
// Production users won't discover this. Lets us still hit the 5 official sample scenes (ARWorld /
// ARMesh / ARDepth / ARImage / ARSemanticDense) and the 3 dev variants (ARObject / ARArrowAlign /
// ARRingHunt) that pre-date the Stage 12 camera-style chrome.
@Component
struct DevSelector {
  pageInfos: NavPathStack = new NavPathStack();
  // Stage 12A redo: only the camera home (ARRingHuntCam) remains as a Cam page; 水平 / 对象 fall
  // back to the dev pages here. Plus the 5 official sample scenes for regression testing.
  private readonly scenes: string[] = [
    'ARRingHuntCam',
    'ARRingHunt', 'ARArrowAlign', 'ARObject',
    'ARWorld', 'ARMesh', 'ARDepth', 'ARImage', 'ARSemanticDense'
  ];

  build(): void {
    NavDestination() {
      Column({ space: Spacing.S }) {
        Text('Dev Selector')
          .fontSize(FontSize.title + 2)
          .fontColor(Palette.white)
          .fontWeight(FontWeight.Bold)
          .margin({ top: Spacing.L, bottom: Spacing.M })
        ForEach(this.scenes, (name: string) => {
          Button(name, { type: ButtonType.Normal, stateEffect: true })
            .borderRadius(Radius.M)
            .width('80%')
            .height(44)
            .backgroundColor(Palette.glass)
            .fontColor(Palette.white)
            .onClick(() => {
              this.pageInfos.clear();
              this.pageInfos.pushDestinationByName(name, null).catch((err: BusinessError) => {
                Logger.error(`pushDestinationByName(${name}) failed: ${err.code} ${err.message}`);
              });
            })
        }, (name: string) => name)
      }
      .width('100%')
      .height('100%')
      .backgroundColor('#FF101010')
      .justifyContent(FlexAlign.Start)
    }
    .onReady((context: NavDestinationContext) => {
      this.pageInfos = context.pathStack;
    })
    .hideTitleBar(true)
    .hideBackButton(true)
    .hideToolBar(true)
  }
}
