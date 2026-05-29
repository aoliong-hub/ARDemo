/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { Palette, Spacing, Radius, FontSize } from '../styles/Theme';

// Generic glass capsule. Used by the top-bar aspect-ratio toggle and any read-only HUD label.
// Tap is optional — if onTap is left as the no-op default, the pill renders as a static badge.
@Component
export struct Pill {
  @Prop text: string = '';
  @Prop active: boolean = false;
  // Default to a no-op so the parent can leave this off for static labels.
  onTap: () => void = () => {};

  build(): void {
    Text(this.text)
      .fontSize(FontSize.hud)
      .fontColor(this.active ? Palette.white : Palette.white80)
      .fontWeight(this.active ? FontWeight.Medium : FontWeight.Regular)
      .backgroundColor(Palette.glass)
      .padding({ left: Spacing.M, right: Spacing.M, top: Spacing.S - 2, bottom: Spacing.S - 2 })
      .borderRadius(Radius.capsule)
      .border({
        width: this.active ? 1 : 0,
        color: Palette.white50,
        radius: Radius.capsule
      })
      .onClick(() => { this.onTap(); })
  }
}
