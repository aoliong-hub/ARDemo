/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, Spacing, FontSize } from '../styles/Theme';

// HarmonyOS-native camera-style top bar — minimalist version (12B-1 final). The ✦ / ⚡ / ◔
// placeholder icons were removed per spec; only the ⚙ remains as the entry to DevSelector.
// Tap = open DevSelector directly; long-press retained for backward compat.
@Component
export struct TopBar {
  onSettingsLongPress: () => void = (): void => {};

  build(): void {
    Row() {
      Blank()
      // Gear: tap or long-press → DevSelector.
      Text('⚙')
        .fontSize(FontSize.title + 2)
        .fontColor(Palette.white)
        .width(36)
        .height(36)
        .textAlign(TextAlign.Center)
        .onClick(() => { this.onSettingsLongPress(); })
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
