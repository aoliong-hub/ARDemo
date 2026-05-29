/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, Spacing, Radius, FontSize } from '../styles/Theme';
import { BusinessError } from '@kit.BasicServicesKit';
import { Logger } from '../utils/Logger';

// HarmonyOS-native camera-style top bar — minimalist version (12B-1 final). The ✦ / ⚡ / ◔
// placeholder icons were removed per spec; only the ⚙ remains because it's the long-press
// entry point to the hidden DevSelector. Tap = "设置开发中" toast; long-press 1s = dev entry.
@Component
export struct TopBar {
  onSettingsLongPress: () => void = (): void => {};

  private toast(msg: string): void {
    try {
      this.getUIContext().getPromptAction().showToast({ message: msg, duration: 1500 });
    } catch (e) {
      let err: BusinessError = e as BusinessError;
      Logger.warn(`toast failed: ${err.message}`);
    }
  }

  build(): void {
    Row() {
      Blank()
      // Gear: tap = settings placeholder; long-press 1s = dev entry to DevSelector.
      Text('⚙')
        .fontSize(FontSize.title + 2)
        .fontColor(Palette.white)
        .width(36)
        .height(36)
        .textAlign(TextAlign.Center)
        .onClick(() => { this.toast('设置开发中'); })
        .gesture(
          LongPressGesture({ repeat: false, duration: 1000 })
            .onAction(() => { this.onSettingsLongPress(); })
        )
    }
    .width('92%')
    .height(44)
    .padding({ left: Spacing.M, right: Spacing.M })
  }
}
