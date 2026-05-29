/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

// Stage 12A camera-style design system: a single source of truth for chrome colors / spacing
// / type / corner-radii so the new home page, mode picker, top bar, sphere button and any HUD
// pulled forward from the dev scenes pull from one palette.
export class Palette {
  // Brand
  static readonly primaryGreen: string = '#FF00E676';   // aligned / locked feedback
  static readonly alertRed: string = '#FFE63946';       // wayfinder beacon / off-screen droplet

  // Glass chrome (translucent black backgrounds for capsules and HUD pills)
  static readonly glass: string = '#80000000';          // standard glass pill background
  static readonly glassStrong: string = '#A0000000';    // emphasis pill (banner)
  static readonly glassDim: string = '#40000000';       // subtle wash

  // Whites (translucent strokes / text variants)
  static readonly white80: string = '#CCFFFFFF';
  static readonly white60: string = '#99FFFFFF';
  static readonly white50: string = '#80FFFFFF';
  static readonly white40: string = '#66FFFFFF';
  static readonly white30: string = '#4DFFFFFF';
  static readonly white20: string = '#33FFFFFF';
  static readonly white10: string = '#1AFFFFFF';
  static readonly white: string = '#FFFFFFFF';

  // Sphere button gradient stops (iridescent blue->purple->pink->orange).
  static readonly sphereHi: string = '#FFFFFFFF';
  static readonly sphereWarm: string = '#FFFFB78A';
  static readonly spherePink: string = '#FFFF7DB9';
  static readonly spherePurple: string = '#FF7B6CFB';
  static readonly sphereBlue: string = '#FF3F60E8';
}

// Spacing (vp)
export class Spacing {
  static readonly XS: number = 4;
  static readonly S: number = 8;
  static readonly M: number = 16;
  static readonly L: number = 24;
  static readonly XL: number = 32;
}

// Corner radii (vp). "capsule" is the magic number for pill rounding regardless of height.
export class Radius {
  static readonly S: number = 6;
  static readonly M: number = 12;
  static readonly L: number = 18;
  static readonly capsule: number = 999;
}

// Font sizes (fp)
export class FontSize {
  static readonly hud: number = 14;
  static readonly mode: number = 16;
  static readonly modeSelected: number = 16;
  static readonly title: number = 18;
  static readonly banner: number = 48;
}
