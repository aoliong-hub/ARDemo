/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { display, PathShape } from '@kit.ArkUI';
import { resourceManager } from '@kit.LocalizationKit';
import { systemDateTime } from '@kit.BasicServicesKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { vibrator } from '@kit.SensorServiceKit';
import arEngineDemo from 'libentry.so';
import { RingState } from 'libentry.so';
import { Logger } from '../utils/Logger';
import { Palette, FontSize, Spacing, Radius } from '../styles/Theme';
import { CameraChrome } from '../components/CameraChrome';
import { ReferencePanel } from '../components/ReferencePanel';

@Builder
export function ARRingHuntCamBuilder() {
  ARRingHuntCam();
}

// Stage 12A "机位" camera mode — the default home. Wraps the full ARRingHunt experience (Wayfinder
// beacon + 6DoF alignment + off-screen guidance + LOCKED banner) in CameraChrome, with a single
// iridescent sphere button replacing the dev page's place / reset / 6DoF test buttons. Sphere
// semantics: press once = placeRing (random 6DoF target); press again = resetRing then placeRing.
//
// The native scene is shared with the dev ARRingHunt page (idStr suffix stays "ARRingHunt" so
// NapiManager::CreateApp dispatches RingHuntApp). The dev page is preserved separately for the
// long-press dev entry on Selector.
@Component
struct ARRingHuntCam {
  pageInfos: NavPathStack = new NavPathStack();
  private interval: number = -1;
  private stateTimer: number = -1;
  private xComponentId: string = 'ARRingHunt';
  @State context: Context = this.getUIContext().getHostContext() as Context;
  private resMgr: resourceManager.ResourceManager = this.context.resourceManager;
  @State rotation: number = display.getDefaultDisplaySync().rotation;
  private idStr: string = systemDateTime.getTime(false).toString() + this.xComponentId;

  @State distance: number = 99;
  @State ringPlaced: boolean = false;

  // 6DoF alignment.
  @State huntPhase: number = 0; // 0=APPROACHING 1=ALIGNING 2=LOCKED
  @State yawDiffRad: number = 0;
  @State pitchDiffRad: number = 0;
  @State rollDiffRad: number = 0;
  @State isAligned: boolean = false;
  @State isLocked: boolean = false;
  private prevLocked: boolean = false;

  // Off-screen target guidance.
  @State isTargetInView: boolean = true;
  @State screenEdgeX: number = 0.5;
  @State screenEdgeY: number = 0.5;
  @State isBehind: boolean = false;
  @State indicatorAngleDeg: number = 0;
  @State ndcX: number = 0;
  @State ndcY: number = 0;
  @State guidanceMounted: boolean = false;
  @State guidanceOpacity: number = 0;
  private guidanceShown: boolean = false;
  @State guidanceStackW: number = 0;
  @State guidanceStackH: number = 0;
  // Iridescent-dot breathing: applied to BOTH the off-screen guidance dot and the alignment-HUD
  // center dot. Fixed 2s alternate, 0.92↔1.08 — no longer distance-modulated (kept constant for a
  // calm, "alive" pulse that doesn't compete for attention with the hue gradient).
  @State breathScale: number = 1.0;
  // Iridescent hue flow [0,1] for the two Text glyphs (▲ off-screen direction, ⟳ turn-around).
  // Driven by animateTo (Linear, 2.4s, Alternate). Text doesn't support gradient fill on this
  // device, so the multi-hue read is delivered by sweeping fontColor along the pearl spectrum
  // instead. ▲ and ⟳ share the same flowT and so flow in sync.
  @State flowT: number = 0;

  // Mode-switch fade.
  @State private fadeOpacity: number = 1.0;


  // Stage 12B-1 bottom reference-shot panel. Two INDEPENDENT states drive the layout:
  //   panelExpanded — are we in "AI 机位" mode at all (compact chrome, panel visible).
  //                   Flips true on AI sphere tap; never flips false again in this stage.
  //   cardExpanded  — within AI mode, is the grey card open (true) or collapsed (false)?
  //                   Flips on chevron tap. Toggling cardExpanded does NOT change panelExpanded —
  //                   so the chrome stays compact and the chevron handle stays docked at the
  //                   bottom of the screen even when the card collapses.
  // panelHeight is the actual animated panel height; it's derived from both booleans:
  //   !panelExpanded                 → 0
  //   panelExpanded && cardExpanded  → panelExpandedHeight() (full grey card visible)
  //   panelExpanded && !cardExpanded → kPanelCollapsedHandleHeight (just the chevron handle)
  @State private panelHeight: number = 0;
  @State private panelExpanded: boolean = false;
  @State private cardExpanded: boolean = false;
  // Bottom reserved band — 48vp at the screen bottom that's ALWAYS clear of chrome controls
  // (even in the initial state). Two purposes:
  //   1. Pre-reserves the spot for the absolute-positioned chevron handle so when the handle
  //      appears in collapsed-card state it doesn't push the triplet/AI/zoom upward.
  //   2. Keeps the initial-state and collapsed-state triplet positions IDENTICAL — the chrome
  //      bottom-padding is `panelHeight + kBottomReservedHeight`, and panelHeight is 0 in both
  //      states. Only the chevron handle's visibility differs.
  // Chevron Row height — absolute when collapsed; in-card at top when expanded.
  private readonly kBottomReservedHeight: number = 28;
  // Gap from the standalone chevron's bottom to the safe-area bottom (= top of the system nav
  // indicator zone). Small but non-zero so the chevron doesn't visually touch the gesture pill.
  private readonly kChevronToBottomAnchorGap: number = 8;
  // =============================================================================================
  // 12B-1 BOTTOM LAYOUT — single source of truth. All bottom-y positions derive from THESE
  // constants. Do NOT add ad-hoc positioning numbers elsewhere.
  //
  // Invariants:
  //   1. viewportBottom = const in ALL 3 states (viewport never resizes)
  //   2. card.top = viewportBottom + kGCardTopFromVp  — always 12vp dark gap from viewport
  //   3. card.bottom = screenHvp - kGCardBottomFromNav — always 8vp above gesture pill
  //   4. chrome block position in initial state == collapsed state (chromeUplift = 0 in both)
  //   5. expanded: chrome block bottom = viewportBottom - kGBlockBottomFromVpExpanded → chrome
  //      floats just above viewport line, never overlapping the card (12vp dark gap between)
  // =============================================================================================
  private readonly kChromeBlockH: number = 136;          // 4 + 56 + 4 + 60 + 12 (CameraChrome's bottom Column layout)
  private readonly kGCardTopFromVp: number = 12;         // card top edge gap below viewportBottom
  private readonly kGCardBottomFromNav: number = -8;     // negative = card bottom extends INTO nav zone (system gesture pill floats over card)
  private readonly kGChevronBottomFromNav: number = 8;   // standalone chevron bottom gap above gesture pill
  private readonly kGChevronTopFromBlock: number = 12;   // standalone chevron top gap below collapsed-chrome block bottom
  private readonly kGBlockBottomFromVpExpanded: number = 8;  // expanded chrome block bottom gap above viewportBottom
  private readonly kCardHorizontalMargin: number = 12;   // card left + right margin (floating-island look)

  // chromeUplift animates 0 (collapsed/initial) → chromeUpliftTotal (expanded), tied to
  // cardExpanded toggle via animateTo. Drives chromeBottomPadding upward when card opens.
  @State private chromeUplift: number = 0;


  // Stage 12A viewport rect — a HarmonyOS-native portrait 9:16 (i.e., "tall 16:9") camera viewport
  // with opaque black bars top + bottom. The AR engine still renders full-screen at the GL layer;
  // the black bars are ArkUI masks pinned over the XComponent, leaving only the strip visible. All
  // AR-guidance overlays (off-screen droplet, alignment HUD, distance pill, LOCKED banner,
  // particle trail) are scoped to a Stack sized to this rect, so guidance positioned at the "edge"
  // naturally hugs the viewport edge instead of leaking into the black bars.
  //
  // Sizing rule: ideal viewportH = screenW * 16/9 (portrait 9:16). On most phones the page is too
  // short for that ideal + the top icons + the bottom zoom/triplet, so we clamp to whatever's
  // available after reserving kBottomBandMin for the bottom-control band:
  //   viewportH = min(ideal, screenH - viewportTop - kBottomBandMin)
  // The result is "as close to 16:9 as the device allows, with controls always fitting".
  @State private screenWvp: number = 360;
  @State private screenHvp: number = 800;
  // Top band ≈ 60vp covers the TopBar capsule (44vp) with the chrome's 8vp top padding, plus a
  // tiny slack so the bar isn't kissing the viewport line.
  private readonly viewportTop: number = 42;
  // Bottom band — compressed ~27% total from the original layout (278 → 204vp visual band).
  // chrome block is now 144vp (4 + 56 + 4 + 76 + 4) and chevron-band stays 24vp; with 8vp slack
  // → 176vp safe-area band, +28vp nav reserve = 204vp visual band. The all-4vp paddings
  // distribute the latest compression evenly across the chrome's three vertical gaps.
  private readonly kBottomBandMin: number = 200;

  private viewportH(): number {
    // Plan B-FINAL: viewport is FIXED — doesn't depend on panelHeight. AR preview area stays
    // the same size in initial / collapsed / expanded states. The card lives entirely in the
    // bottom black band below viewportBottom; the chrome cluster floats on top of viewport.
    let ideal: number = this.screenWvp * 16 / 9;
    let available: number = this.screenHvp - this.viewportTop - this.kBottomBandMin;
    return Math.min(ideal, Math.max(0, available));
  }
  private viewportBottom(): number { return this.viewportTop + this.viewportH(); }
  // Bottom black bar covers viewportBottom to screenHvp (safe-area bottom). The nav-zone area
  // below screenHvp is filled by NavDestination's BLACK BACKGROUND (set explicitly below) —
  // not by extending this bar. Cleaner separation: bar = viewport mask, NavDestination = root.
  private bottomBarH(): number {
    return Math.max(0, this.screenHvp - this.viewportBottom());
  }

  private clamp(v: number, lo: number, hi: number): number {
    if (v < lo) {
      return lo;
    }
    if (v > hi) {
      return hi;
    }
    return v;
  }

  private distText(): string {
    return this.distance.toFixed(2);
  }

  // Stage 12B-1 banner helpers: format a signed radian diff as an integer degree with a leading
  // sign ("+23°" / "-7°"). Used in the second line of the top distance pill alongside the absolute
  // distance reading.
  private formatDiff(rad: number): string {
    let deg: number = rad * 180 / Math.PI;
    let rounded: number = Math.round(deg);
    let sign: string = rounded >= 0 ? '+' : '-';
    return `${sign}${Math.abs(rounded)}°`;
  }

  // ---- BOTTOM LAYOUT — derived positions (single source of truth) ----

  // Card bottom edge — fixed in expanded state (always 8vp above gesture pill).
  private cardBottomY(): number { return this.screenHvp - this.kGCardBottomFromNav; }
  // Card top edge — fixed in expanded state (always 12vp below viewportBottom).
  private cardTopY(): number { return this.viewportBottom() + this.kGCardTopFromVp; }
  // Card height when fully expanded (derived; e.g., 713.7 - 523.7 = 190 on Pura).
  private cardHeight(): number { return Math.max(0, this.cardBottomY() - this.cardTopY()); }

  // Standalone chevron Row position (28vp tall) — ANCHORED TO viewportBottom (per user spec).
  // Fixed 172vp offset below viewportBottom so the chevron follows viewportBottom 1:1 when the
  // viewport bounds change. 172 puts chevron y_bottom right at the safe-area bottom edge.
  private readonly kChevronStandaloneOffsetFromVp: number = 172;
  private chevronStandaloneY(): number {
    return this.viewportBottom() + this.kChevronStandaloneOffsetFromVp;
  }

  // Chrome block bottom y in COLLAPSED / INITIAL state — sits 12vp above the standalone chevron.
  private chromeBlockBottomCollapsedY(): number {
    return this.chevronStandaloneY() - this.kGChevronTopFromBlock;
  }
  // Chrome block bottom y in EXPANDED state — floats 8vp above viewportBottom (on AR preview).
  private chromeBlockBottomExpandedY(): number {
    return this.viewportBottom() - this.kGBlockBottomFromVpExpanded;
  }
  // Vertical distance chrome moves UP when transitioning collapsed → expanded.
  private chromeUpliftTotal(): number {
    return this.chromeBlockBottomCollapsedY() - this.chromeBlockBottomExpandedY();
  }

  // chromeBottomPadding = collapsed value + animated uplift. Animated via this.chromeUplift.
  private chromeBottomPadding(): number {
    return (this.screenHvp - this.chromeBlockBottomCollapsedY()) + this.chromeUplift;
  }

  // Card extends fully (190vp) when expanded.
  private panelExpandedHeight(): number {
    return this.cardHeight();
  }

  // Card y_top — animated via panelHeight (0 → cardHeight). Anchored at cardBottomY.
  private cardY(): number {
    return this.cardBottomY() - this.panelHeight;
  }

  // Animate BOTH panelHeight (card slide) AND chromeUplift (chrome lift up to AR) together.
  // Single animateTo block keeps them in lock-step for the entire 300ms transition.
  private animatePanelTo(targetHeight: number, targetUplift: number): void {
    if (this.panelHeight === targetHeight && this.chromeUplift === targetUplift) {
      return;
    }
    this.getUIContext().animateTo({ duration: 300, curve: Curve.EaseOut }, () => {
      this.panelHeight = targetHeight;
      this.chromeUplift = targetUplift;
    });
  }

  // Legacy single-value method kept for any leftover callers; routes to animatePanelTo.
  private animatePanelHeightTo(target: number): void {
    this.animatePanelTo(target, target > 0 ? this.chromeUpliftTotal() : 0);
  }

  // AI sphere tap → enter AI mode (chrome compacts) AND open the card. Called from placeBeacon
  // after a successful native placement.
  private enterAiModeAndOpenCard(): void {
    this.panelExpanded = true;
    this.cardExpanded = true;
    this.animatePanelHeightTo(this.panelExpandedHeight());
  }

  // Chevron tap → toggle card open/closed WITHOUT exiting AI mode. The chevron is now an
  // ABSOLUTE element sandwiched between the chrome triplet and the card; collapsing the card
  // animates panelHeight to 0 (card invisible) while the chevron stays put.
  private toggleCard(): void {
    if (!this.panelExpanded) {
      return; // chevron is only interactive while in AI mode
    }
    this.cardExpanded = !this.cardExpanded;
    this.animatePanelHeightTo(this.cardExpanded ? this.panelExpandedHeight() : 0);
  }

  // ---- 6DoF aiming HUD: dot offset driven by yaw/pitch diff (max at 30deg) ----
  // Stage 12C-redesign: kMaxOffset shrunk 200→90 so the iridescent center dot reaches just past
  // the L-corner bracket box (80vp side) at the 30° edge of the aim envelope. The dot still uses
  // the same yaw/pitch ratio (deg/30 × kMaxOffset).
  private readonly kMaxOffset: number = 90;

  private dotOffsetX(): number {
    let yawDeg: number = this.clamp(this.yawDiffRad * 180 / Math.PI, -30, 30);
    return (yawDeg / 30) * this.kMaxOffset;
  }

  private dotOffsetY(): number {
    let pitchDeg: number = this.clamp(this.pitchDiffRad * 180 / Math.PI, -30, 30);
    return (pitchDeg / 30) * this.kMaxOffset;
  }

  // Stage 12C-redesign batch 1 fix — bracket / crosshair line builder. Off-aim = single white60
  // (pearl colors wash out at hairline thickness anyway). Aligned = real pearl pink→purple→blue
  // linearGradient so the L brackets AND the center "+" both shimmer together as the user pins
  // the dot in the gate. Solid colors on aligned looked uniform-purple in batch 1; switching to
  // gradient is the actual fix the brief asked for.
  @Builder
  private bracketLine(w: number, h: number, tx: number, ty: number): void {
    if (this.isAligned) {
      Column()
        .width(w).height(h)
        .linearGradient({
          angle: 45,
          colors: [
            [Palette.iridescentOrange, 0.0],
            [Palette.iridescentPink, 0.33],
            [Palette.iridescentPurple, 0.66],
            [Palette.iridescentBlue, 1.0]
          ]
        })
        .shadow({ radius: 4, color: '#80FFFFFF', offsetX: 0, offsetY: 0 })
        .translate({ x: tx, y: ty })
    } else {
      Column()
        .width(w).height(h)
        .backgroundColor(Palette.white60)
        .translate({ x: tx, y: ty })
    }
  }

  private toast(msg: string): void {
    try {
      this.getUIContext().getPromptAction().showToast({ message: msg, duration: 1500 });
    } catch (e) {
      let err: BusinessError = e as BusinessError;
      Logger.warn(`toast failed: ${err.message}`);
    }
  }

  // Stage 12C dev path: place a beacon at a CAMERA-RELATIVE position + 6DoF target via placeRingAt.
  //   x = right (horizontal, perpendicular to user's heading)
  //   y = up    (height of the ring/badge above the ground base — what the user aims at)
  //   z = forward (horizontal, user's heading at call time)
  //   yaw/pitch/roll degrees: yaw composes with the camera's current world yaw (so 0 = facing
  //   along the user's gaze, back of the frame toward the user); pitch/roll are absolute world.
  private placeBeaconAt(x: number, y: number, z: number, yawDeg: number, pitchDeg: number, rollDeg: number,
                        label: string): void {
    if (this.ringPlaced) {
      this.distance = 99;
      this.huntPhase = 0;
      this.isAligned = false;
      this.isLocked = false;
      this.prevLocked = false;
      this.resetGuidance();
    }
    let id: number = arEngineDemo.placeRingAt(this.idStr, x, y, z, yawDeg, pitchDeg, rollDeg);
    Logger.info(`[ARRingHuntCam] placeRingAt(${label}: x=${x} y=${y} z=${z} yaw=${yawDeg} pitch=${pitchDeg} roll=${rollDeg}) -> ${id}`);
    if (id < 0) {
      this.toast('请稳住手机等待跟踪稳定');
    } else {
      this.ringPlaced = true;
      this.enterAiModeAndOpenCard();
    }
  }

  // AI 机位 action: place a wayfinder beacon (random 6DoF target). Repeated taps cause native to
  // replace the prior beacon, so we also wipe the local UI state to keep guidance in sync.
  private placeBeacon(): void {
    if (this.ringPlaced) {
      // Tear down stale guidance overlays first so the new placement reads fresh.
      this.distance = 99;
      this.huntPhase = 0;
      this.isAligned = false;
      this.isLocked = false;
      this.prevLocked = false;
      this.resetGuidance();
    }
    let id: number = arEngineDemo.placeRing(this.idStr);
    Logger.info(`[ARRingHuntCam] placeRing -> ${id}`);
    if (id < 0) {
      this.toast('请稳住手机等待跟踪稳定');
    } else {
      this.ringPlaced = true;
      // Stage 12B-1: successful placement opens the reference panel.
      this.enterAiModeAndOpenCard();
    }
  }

  private triggerLockVibration(): void {
    try {
      vibrator.startVibration({ type: 'time', duration: 80 }, { usage: 'notification' })
        .catch((err: BusinessError) => {
          Logger.warn(`vibrate failed: code=${err.code} msg=${err.message}`);
        });
    } catch (e) {
      let err: BusinessError = e as BusinessError;
      Logger.warn(`vibrate exception: ${err.message}`);
    }
  }

  private openDevSelector(): void {
    Logger.info(`[ARRingHuntCam] long-press gear -> DevSelector`);
    this.pageInfos.clear();
    this.pageInfos.pushDestinationByName('DevSelector', null).catch((err: BusinessError) => {
      Logger.error(`pushDestinationByName(DevSelector) failed: ${err.code} ${err.message}`);
    });
  }

  // ---- Off-screen guidance (identical to ARRingHunt dev page) ----

  private getStackWidth(): number {
    return this.guidanceStackW > 0 ? this.guidanceStackW : 360;
  }

  private getStackHeight(): number {
    return this.guidanceStackH > 0 ? this.guidanceStackH : 800;
  }

  // Stage 12C-redesign: breathing is now a single fixed loop (0.92↔1.08, 2s alternate) shared by
  // the iridescent dots in both the ALIGNING HUD and the off-screen guidance. Distance-modulated
  // glow/halo/particle helpers (guidanceColor / glowThickness / glowOpacity / distBucket /
  // breathDurationFor / particle pipeline) all dropped — the new minimal accent doesn't need
  // distance feedback (the banner already shows the live distance).
  private startBreathing(): void {
    this.breathScale = 0.92;
    this.getUIContext().animateTo({
      duration: 2000,
      curve: Curve.EaseInOut,
      iterations: -1,
      playMode: PlayMode.Alternate
    }, () => {
      this.breathScale = 1.08;
    });
  }

  private stopBreathing(): void {
    this.getUIContext().animateTo({ duration: 0 }, () => {
      this.breathScale = 1.0;
    });
  }

  // Pearl iridescent spectrum (orange → pink → purple → blue, image-#3 reference). Driven by
  // @State flowT via animateTo; sampling iridescentFlow(flowT) at any moment gives a fontColor
  // that walks the spectrum smoothly. Used for ▲ and ⟳ which can't take a real gradient fill.
  private toHex2(v: number): string {
    const s = Math.round(Math.max(0, Math.min(255, v))).toString(16);
    return s.length < 2 ? '0' + s : s;
  }

  private iridescentFlow(t: number): string {
    // Saturated pearl-ish spectrum, closed loop (last stop = first) so Normal-mode (one-way)
    // animation walks the spectrum without a jump back at the wrap. Pearl-soft hues looked dead
    // on AR preview; pushing saturation + lightness brings the color back as the icon's lead.
    const stops: number[][] = [
      [253, 222, 193],  // #FDDEC1 warm pearl
      [244, 210, 209],  // #F4D2D1 pearl pink
      [214, 189, 255],  // #D6BDFF pearl purple
      [199, 214, 255],  // #C7D6FF pearl blue
      [253, 222, 193]   // wrap back to warm (close loop for Normal-mode play)
    ];
    const n: number = stops.length - 1;
    const x: number = Math.max(0, Math.min(1, t)) * n;
    const i: number = Math.min(Math.floor(x), n - 1);
    const f: number = x - i;
    const a: number[] = stops[i];
    const b: number[] = stops[i + 1];
    const r: number = a[0] + (b[0] - a[0]) * f;
    const g: number = a[1] + (b[1] - a[1]) * f;
    const bl: number = a[2] + (b[2] - a[2]) * f;
    return '#' + this.toHex2(r) + this.toHex2(g) + this.toHex2(bl);
  }

  private startIridescentFlow(): void {
    this.flowT = 0;
    this.getUIContext().animateTo({
      duration: 1600,
      curve: Curve.Linear,
      iterations: -1,
      playMode: PlayMode.Normal
    }, () => {
      this.flowT = 1;
    });
  }

  private shouldFlipProjectedNdc(): boolean {
    let edgeX: number = this.screenEdgeX - 0.5;
    let edgeY: number = 0.5 - this.screenEdgeY;
    return edgeX * this.ndcX + edgeY * this.ndcY < 0;
  }

  private projectedNdcX(): number {
    return this.shouldFlipProjectedNdc() ? -this.ndcX : this.ndcX;
  }

  private projectedNdcY(): number {
    return this.shouldFlipProjectedNdc() ? -this.ndcY : this.ndcY;
  }

  private targetVectorX(): number {
    return this.projectedNdcX() * this.getStackWidth() * 0.5;
  }

  private targetVectorY(): number {
    return -this.projectedNdcY() * this.getStackHeight() * 0.5;
  }

  private rayDirX(): number {
    if (this.isBehind) {
      return 0;
    }
    let vx: number = this.targetVectorX();
    let vy: number = this.targetVectorY();
    let len: number = Math.sqrt(vx * vx + vy * vy);
    if (len < 0.001) {
      return this.screenEdgeX >= 0.5 ? 1 : -1;
    }
    return vx / len;
  }

  private rayDirY(): number {
    if (this.isBehind) {
      return 1;
    }
    let vx: number = this.targetVectorX();
    let vy: number = this.targetVectorY();
    let len: number = Math.sqrt(vx * vx + vy * vy);
    if (len < 0.001) {
      return this.screenEdgeY >= 0.5 ? 1 : -1;
    }
    return vy / len;
  }

  private rayTMin(): number {
    let cx: number = this.getStackWidth() / 2;
    let cy: number = this.getStackHeight() / 2;
    let dx: number = this.rayDirX();
    let dy: number = this.rayDirY();
    let t: number = Infinity;
    // Edge margin matches the new 96vp guidance container's half-width (48vp) so the indicator
    // lands flush against the viewport edge without clipping the outer ring or the triangle.
    if (dx > 0.001) {
      t = Math.min(t, (this.getStackWidth() - 48 - cx) / dx);
    }
    if (dx < -0.001) {
      t = Math.min(t, (48 - cx) / dx);
    }
    if (dy > 0.001) {
      t = Math.min(t, (this.getStackHeight() - 48 - cy) / dy);
    }
    if (dy < -0.001) {
      t = Math.min(t, (48 - cy) / dy);
    }
    if (t === Infinity || t < 0) {
      t = 0;
    }
    return t;
  }

  private rayCenterX(): number {
    return this.getStackWidth() / 2 + this.rayTMin() * this.rayDirX();
  }

  private rayCenterY(): number {
    return this.getStackHeight() / 2 + this.rayTMin() * this.rayDirY();
  }

  private getDropletX(): number {
    return this.clamp(this.rayCenterX() - 48, 0, this.getStackWidth() - 96);
  }

  private getDropletY(): number {
    return this.clamp(this.rayCenterY() - 48, 0, this.getStackHeight() - 96);
  }

  private dropletRotationDeg(): number {
    if (this.isBehind) {
      return 180;
    }
    return Math.atan2(this.rayDirX(), -this.rayDirY()) * 180 / Math.PI;
  }

  private reconcileGuidance(): void {
    let shouldShow: boolean =
      this.huntPhase === 0 && this.ringPlaced && !this.isTargetInView && this.distance >= 0.30;
    if (shouldShow === this.guidanceShown) {
      return;
    }
    this.guidanceShown = shouldShow;
    if (shouldShow) {
      this.guidanceMounted = true;
      this.startBreathing();
      this.startIridescentFlow();
      this.getUIContext().animateTo({ duration: 300, curve: Curve.EaseOut }, () => {
        this.guidanceOpacity = 1;
      });
    } else {
      this.stopBreathing();
      this.getUIContext().animateTo({
        duration: 300,
        curve: Curve.EaseOut,
        onFinish: () => {
          if (!this.guidanceShown) {
            this.guidanceMounted = false;
          }
        }
      }, () => {
        this.guidanceOpacity = 0;
      });
    }
  }

  private resetGuidance(): void {
    this.isTargetInView = true;
    this.isBehind = false;
    this.screenEdgeX = 0.5;
    this.screenEdgeY = 0.5;
    this.indicatorAngleDeg = 0;
    this.guidanceShown = false;
    this.guidanceMounted = false;
    this.guidanceOpacity = 0;
    this.stopBreathing();
  }

  build(): void {
    NavDestination() {
      // Root z-stack (back→front): [AR preview] → [viewport guidance overlay] → [top black bar]
      // → [bottom black bar] → [chrome controls]. The viewport overlay is sized to the 4:3 rect
      // and positioned at (0, viewportTop) so all guidance lives strictly inside the viewport;
      // the black bars are opaque masks that hide any GL preview leaking outside the viewport.
      Stack({ alignContent: Alignment.TopStart }) {

        // (1) AR preview — full-screen XComponent (AR Engine still renders the whole display).
        XComponent({ id: this.idStr, type: XComponentType.SURFACE, libraryname: 'entry' })
          .id('xcRingCam')
          .width('100%')
          .height('100%')
          .onLoad(() => {
            Logger.info(`XComponent onLoad ${this.idStr}.`);
            this.interval = setInterval(() => {
              arEngineDemo.update(this.idStr);
            }, 33)
            this.stateTimer = setInterval(() => {
              let s: RingState = arEngineDemo.getRingState(this.idStr);
              this.distance = s.distance;
              this.ringPlaced = s.ringPlaced;
              this.isTargetInView = s.isTargetInView;
              this.screenEdgeX = s.screenEdgeX;
              this.screenEdgeY = s.screenEdgeY;
              this.isBehind = s.isBehind;
              this.indicatorAngleDeg = s.indicatorAngleDeg;
              this.ndcX = s.ndcX;
              this.ndcY = s.ndcY;
              this.huntPhase = s.huntPhase;
              this.yawDiffRad = s.yawDiffRad;
              this.pitchDiffRad = s.pitchDiffRad;
              this.rollDiffRad = s.rollDiffRad;
              this.isAligned = s.isAligned;
              this.isLocked = s.isLocked;
              if (this.isLocked && !this.prevLocked) {
                this.triggerLockVibration();
              }
              this.prevLocked = this.isLocked;
              this.reconcileGuidance();
            }, 62)
          })
          .onDestroy(() => {
            Logger.info(`XComponent onDestroy ${this.idStr}.`);
            clearInterval(this.interval);
            clearInterval(this.stateTimer);
          })

        // (2) Viewport overlay — sized to the 4:3 rect, positioned at viewportTop. Every guidance
        // element scopes its math to this Stack (via onAreaChange + this.guidanceStackW/H), so
        // "off-screen droplet at the edge" naturally means "edge of viewport", not edge of screen.
        // clip(true) is a safety net for any per-frame jitter that could put a pixel outside.
        Stack({ alignContent: Alignment.Center }) {

          // Stage 12C-redesign batch 1 (fix) — full multi-color gradient. Every accent uses a
          // pearl pink → purple → blue linearGradient as its fill, with a mask defining the shape
          // (ring outline, triangle, arc + arrowhead). Single-color iridescent strokes from the
          // first attempt looked uniformly tinted; gradient + mask gives the real "holographic"
          // multi-hue read across each element.
          //   !isBehind → 24vp gradient ring (mask = Circle stroke) + 10vp gradient dot + 10vp
          //               gradient triangle (mask = Path). Rotates by dropletRotationDeg.
          //   isBehind  → drawn 60vp arc + arrowhead (mask = Path) gradient-filled; centered on
          //               the viewport with a "转向目标" label below.
          Stack({ alignContent: Alignment.TopStart }) {
            if (this.guidanceMounted && !this.isBehind) {
              // Concentric ring is now a baked iridescent PNG sequence (guide_ring_*) instead of
              // Circle.stroke + inner Column gradient — same reason as the triangle: glyph/Shape
              // can't carry real multi-hue iridescence on this device. The ring stays STATIC at
              // viewport center; only the direction triangle (inner Stack) rotates by
              // dropletRotationDeg, so the ring no longer wobbles with the target bearing. Outer
              // Stack still owns scale (breath), opacity (fade), and position (ray-edge anchor).
              Stack({ alignContent: Alignment.Center }) {
                ImageAnimator()
                  .images([
                    { src: $r('app.media.guide_ring_0') }, { src: $r('app.media.guide_ring_1') },
                    { src: $r('app.media.guide_ring_2') }, { src: $r('app.media.guide_ring_3') },
                    { src: $r('app.media.guide_ring_4') }, { src: $r('app.media.guide_ring_5') },
                    { src: $r('app.media.guide_ring_6') }, { src: $r('app.media.guide_ring_7') },
                    { src: $r('app.media.guide_ring_8') }, { src: $r('app.media.guide_ring_9') },
                    { src: $r('app.media.guide_ring_10') }, { src: $r('app.media.guide_ring_11') },
                    { src: $r('app.media.guide_ring_12') }, { src: $r('app.media.guide_ring_13') },
                    { src: $r('app.media.guide_ring_14') }, { src: $r('app.media.guide_ring_15') },
                    { src: $r('app.media.guide_ring_16') }, { src: $r('app.media.guide_ring_17') }
                  ])
                  .duration(2000)
                  .iterations(-1)
                  .state(AnimationStatus.Running)
                  .width(27).height(27)

                // Triangle wrapped in its own 96×96 Stack that rotates by dropletRotationDeg.
                // The triangle sits 27vp above this inner Stack's center via offset; rotation
                // then orbits it around the ring to point at the target.
                Stack({ alignContent: Alignment.Center }) {
                  ImageAnimator()
                    .images([
                      { src: $r('app.media.guide_tri_0') }, { src: $r('app.media.guide_tri_1') },
                      { src: $r('app.media.guide_tri_2') }, { src: $r('app.media.guide_tri_3') },
                      { src: $r('app.media.guide_tri_4') }, { src: $r('app.media.guide_tri_5') },
                      { src: $r('app.media.guide_tri_6') }, { src: $r('app.media.guide_tri_7') },
                      { src: $r('app.media.guide_tri_8') }, { src: $r('app.media.guide_tri_9') },
                      { src: $r('app.media.guide_tri_10') }, { src: $r('app.media.guide_tri_11') },
                      { src: $r('app.media.guide_tri_12') }, { src: $r('app.media.guide_tri_13') },
                      { src: $r('app.media.guide_tri_14') }, { src: $r('app.media.guide_tri_15') },
                      { src: $r('app.media.guide_tri_16') }, { src: $r('app.media.guide_tri_17') }
                    ])
                    .duration(1600)
                    .iterations(-1)
                    .state(AnimationStatus.Running)
                    .width(24).height(24)
                    .offset({ x: 0, y: -22 })
                }
                .width(96).height(96)
                .rotate({ x: 0, y: 0, z: 1, angle: this.dropletRotationDeg(),
                          centerX: '50%', centerY: '50%' })
              }
              .width(96).height(96)
              .scale({ x: this.breathScale, y: this.breathScale, centerX: '50%', centerY: '50%' })
              .opacity(this.guidanceOpacity)
              .position({ x: this.getDropletX(), y: this.getDropletY() })
            }

            if (this.guidanceMounted && this.isBehind) {
              // Centered turn-around indicator — drawn arc (NOT a Unicode glyph; the ⟳ char
              // brought its own outline that looked like an extra frame). 270° arc split into 4
              // Path segments (67.5° each) in orange / pink / purple / blue, plus the V-arrowhead
              // joined to the last (blue) segment. Center (50,50) radius 36 inside 100×100
              // viewport. Endpoints: 67.5° → (63.78, 83.26), 135° → (24.54, 75.46), 202.5° →
              // (16.74, 36.22), 270° → (50, 14). Arrowhead V at top pointing right.
              //
              // Centering via Flex (Column direction, both axes Center) which actually centers
              // its children inside its bounds — earlier Stack({alignContent: Center}) reads kept
              // pinning to top-left in this nesting context. No .shadow on Shape: that cast a
              // rectangular halo on the 100×100 bbox which read as a "frame" around the icon.
              // Direct children in Stack({alignContent:Center}) ended up scattered (icon top-left,
              // text below center) because each child centers independently and the .offset() on
              // the Text didn't pull the icon along. Solution: group icon + text inside ONE Column
              // (alignItems Center), and let the Stack center that single Column as a tight unit
              // — same nesting pattern that's now reliably working in block 1.
              // Turn-around icon — Text('⟳') instead of Shape+Path arcs. Same device quirk: the
              // 4 stroked arc Paths rendered as a tiny cluster in the top-left of the 100×100
              // Shape because Path coords aren't scaled to component size. Text glyph at
              // fontSize 48 fills its 56×56 box reliably, centered inside the wrapping Column.
              // (If ⟳ U+27F3 is missing from the system font, swap to ↻ U+21BB.)
              Stack({ alignContent: Alignment.Center }) {
                Column() {
                  Text('⟳')
                    .fontSize(48)
                    .fontColor(this.iridescentFlow(this.flowT))
                    .textAlign(TextAlign.Center)
                    .width(56).height(56)
                    .lineHeight(56)
                    .textShadow({ radius: 10, color: this.iridescentFlow(this.flowT) })

                  Text('转向目标')
                    .fontSize(14)
                    .fontColor(Palette.white60)
                    .margin({ top: 8 })
                }
                .alignItems(HorizontalAlign.Center)
              }
              .width(this.getStackWidth())
              .height(this.getStackHeight())
              .opacity(this.guidanceOpacity)
              .hitTestBehavior(HitTestMode.None)
            }
          }
          .id('guidance')
          .width('100%').height('100%')
          .hitTestBehavior(HitTestMode.None)
          .onAreaChange((_o: Area, n: Area) => {
            this.guidanceStackW = n.width as number;
            this.guidanceStackH = n.height as number;
          })

          // Stage 12B-1 banner — two lines inside one glass capsule. Line 1 = absolute distance.
          // Line 2 = signed yaw/pitch/roll diffs in integer degrees (e.g. "偏航 +23° 俯仰 -12°
          // 翻滚 +5°") — updated live by the same 62ms state poll that drives the alignment HUD.
          if (this.ringPlaced && this.huntPhase !== 2) {
            Row() {
              Column({ space: 2 }) {
                Text(`距离 ${this.distText()} m`)
                  .fontSize(FontSize.hud)
                  .fontColor(Palette.white)
                Text(`偏航 ${this.formatDiff(this.yawDiffRad)}  俯仰 ${this.formatDiff(this.pitchDiffRad)}  翻滚 ${this.formatDiff(this.rollDiffRad)}`)
                  .fontSize(FontSize.hud - 1)
                  .fontColor(Palette.white80)
              }
              .alignItems(HorizontalAlign.Center)
              .backgroundColor(Palette.glass)
              .padding({ left: Spacing.M, right: Spacing.M, top: Spacing.S, bottom: Spacing.S })
              .borderRadius(Radius.M)
            }
            .id('distancePill')
            .width('100%')
            .justifyContent(FlexAlign.Center)
            .position({ x: 0, y: Spacing.M })
            .hitTestBehavior(HitTestMode.None)
          }

          // Stage 12C-redesign — ALIGNING HUD: 4 L-shaped corner brackets at the corners of an
          // 80vp focus box (the camera-style "you're framed here" mark), a faint 120vp reference
          // ring around them, and an iridescent center dot that translates with yaw/pitch diff.
          // Replaces the old 56vp slab crosshair + 400vp outer ring + solid red/green dot.
          //   Off-aim    → brackets white60, dot has soft white glow.
          //   ±5° aligned → brackets switch to iridescent purple, dot glow brightens.
          // L bracket math: each corner = 1 horizontal 12×1.5 + 1 vertical 1.5×12 line whose
          // centers land at (±34, ±40) / (±40, ±34) on the 80×80 box — measured from the Stack
          // center via translate().
          if (this.huntPhase === 1) {
            Stack({ alignContent: Alignment.Center }) {
              // Faint outer reference ring (1vp hairline, 30% white) — gives a context circle
              // without competing with the brackets or dot.
              Circle()
                .width(120).height(120)
                .fillOpacity(0)
                .stroke(Palette.white30)
                .strokeWidth(1)

              // 4 L corner brackets — each line via bracketLine() builder; off-aim = white60,
              // aligned = pearl pink→purple→blue gradient + soft glow.
              this.bracketLine(12, 1.5, -34, -40)
              this.bracketLine(1.5, 12, -40, -34)
              this.bracketLine(12, 1.5, 34, -40)
              this.bracketLine(1.5, 12, 40, -34)
              this.bracketLine(12, 1.5, -34, 40)
              this.bracketLine(1.5, 12, -40, 34)
              this.bracketLine(12, 1.5, 34, 40)
              this.bracketLine(1.5, 12, 40, 34)

              // Center crosshair "+" — same gradient logic as the corner brackets so they all
              // shimmer together in the aligned state.
              this.bracketLine(14, 1.5, 0, 0)
              this.bracketLine(1.5, 14, 0, 0)

              // Iridescent center dot — 4-stop pearl gradient (orange / pink / purple / blue).
              // Glow brightens when aligned so the "in the gate" feedback reads instantly.
              Column()
                .width(16).height(16)
                .borderRadius(8)
                .linearGradient({
                  angle: 45,
                  colors: [
                    [Palette.iridescentOrange, 0.0],
                    [Palette.iridescentPink, 0.33],
                    [Palette.iridescentPurple, 0.66],
                    [Palette.iridescentBlue, 1.0]
                  ]
                })
                .shadow({ radius: this.isAligned ? 12 : 6,
                          color: this.isAligned ? '#CCFFFFFF' : '#80FFFFFF',
                          offsetX: 0, offsetY: 0 })
                .translate({ x: this.dotOffsetX(), y: this.dotOffsetY() })
            }
            .id('alignHud')
            .hitTestBehavior(HitTestMode.None)
          }

          // LOCKED banner — centered on viewport center.
          if (this.huntPhase === 2) {
            Text('对齐成功!')
              .fontSize(FontSize.banner)
              .fontColor(Palette.primaryGreen)
              .fontWeight(FontWeight.Bold)
              .backgroundColor(Palette.glassStrong)
              .padding({ left: Spacing.L + 4, right: Spacing.L + 4, top: Spacing.M, bottom: Spacing.M })
              .borderRadius(14)
              .id('lockedBanner')
          }
        }
        .width('100%')
        .height(this.viewportH())
        .position({ x: 0, y: this.viewportTop })
        .clip(true)
        .hitTestBehavior(HitTestMode.Transparent)

        // (3) Top black bar — opaque mask from page top down to viewportTop.
        Column()
          .width('100%')
          .height(this.viewportTop)
          .backgroundColor('#FF000000')
          .position({ x: 0, y: 0 })
          .hitTestBehavior(HitTestMode.None)

        // Stage 12C dev chips — placeRingAt test buttons at top-left of the viewport. Row 1 tests
        // position (x=right, y=ringHeight, z=forward); row 2 tests orientation (yaw composes with
        // camera yaw — 0 = facing user's gaze, back of frame toward user; pitch/roll absolute).
        // Tiny, semi-transparent; verifies the contract on every build. Remove only when the 3D
        // pose-calc pipeline replaces them.
        Column({ space: 4 }) {
          Row({ space: 6 }) {
            Text('前2高1.2')
              .fontSize(11)
              .fontColor(Palette.white)
              .backgroundColor('#80000000')
              .borderRadius(8)
              .padding({ left: 8, right: 8, top: 4, bottom: 4 })
              .onClick(() => { this.placeBeaconAt(0, 1.2, 2, 0, 0, 0, '前2高1.2'); })
            Text('前2高0.5')
              .fontSize(11)
              .fontColor(Palette.white)
              .backgroundColor('#80000000')
              .borderRadius(8)
              .padding({ left: 8, right: 8, top: 4, bottom: 4 })
              .onClick(() => { this.placeBeaconAt(0, 0.5, 2, 0, 0, 0, '前2高0.5'); })
            Text('右1前1高1.2')
              .fontSize(11)
              .fontColor(Palette.white)
              .backgroundColor('#80000000')
              .borderRadius(8)
              .padding({ left: 8, right: 8, top: 4, bottom: 4 })
              .onClick(() => { this.placeBeaconAt(1, 1.2, 1, 0, 0, 0, '右1前1高1.2'); })
          }
          Row({ space: 6 }) {
            Text('前2转右30')
              .fontSize(11)
              .fontColor(Palette.white)
              .backgroundColor('#80000000')
              .borderRadius(8)
              .padding({ left: 8, right: 8, top: 4, bottom: 4 })
              .onClick(() => { this.placeBeaconAt(0, 1.2, 2, 30, 0, 0, '前2转右30'); })
            Text('前2俯30')
              .fontSize(11)
              .fontColor(Palette.white)
              .backgroundColor('#80000000')
              .borderRadius(8)
              .padding({ left: 8, right: 8, top: 4, bottom: 4 })
              .onClick(() => { this.placeBeaconAt(0, 1.2, 2, 0, -30, 0, '前2俯30'); })
            Text('前2斜45')
              .fontSize(11)
              .fontColor(Palette.white)
              .backgroundColor('#80000000')
              .borderRadius(8)
              .padding({ left: 8, right: 8, top: 4, bottom: 4 })
              .onClick(() => { this.placeBeaconAt(0, 1.2, 2, 0, 0, 45, '前2斜45'); })
          }
        }
        .alignItems(HorizontalAlign.Start)
        .position({ x: 12, y: this.viewportTop + 8 })

        // (4) Bottom black bar — opaque mask from viewportBottom down to page bottom.
        Column()
          .width('100%')
          .height(this.bottomBarH())
          .backgroundColor('#FF000000')
          .position({ x: 0, y: this.viewportBottom() })
          .hitTestBehavior(HitTestMode.None)

        // (5) Chrome overlay on top of the black bars. TopBar naturally lands in the top band
        // (chrome's Column has TopBar first, with 8vp top padding); the zoom + AI + shutter cluster
        // anchors to the bottom of the chrome Column → lands in the bottom band. When the
        // reference panel slides up, the wrapper's bottom-padding grows by panelHeight so the
        // bottom cluster lifts in sync (per 12B-1 spec: shutter sits above the panel).
        // ReferencePanel — floating-island card with horizontal margin + rounded corners + shadow.
        // Rendered BEFORE chrome so chrome controls (in compact mode) float ON TOP of the card
        // surface. Card y_top sits 12vp below viewportBottom; y_bottom sits 8vp above bottomAnchor.
        if (this.cardExpanded || this.panelHeight > 0) {
          ReferencePanel({
            panelHeight: this.panelHeight,
            cardExpanded: this.cardExpanded,
            horizontalMargin: this.kCardHorizontalMargin,
            onToggle: (): void => { this.toggleCard(); }
          })
            .width('100%')
            .position({ x: 0, y: this.cardY() })
        }

        // Chrome — rendered AFTER ReferencePanel so it sits ON TOP in z-order. Compact mode
        // (zoom hidden / AI sphere shrunk + shifted right / triplet 0.75) floats over the card.
        Column() {
          CameraChrome({
            // Chrome compact mode tracks cardExpanded — when collapsed via chevron, chrome
            // restores to its full-size layout.
            panelExpanded: this.cardExpanded,
            onSphereTap: (): void => { this.placeBeacon(); },
            onSettingsLongPress: (): void => { this.openDevSelector(); }
          })
        }
        .id('chromeRingCam')
        .width('100%')
        .height('100%')
        // Constant bottom-padding (independent of panelHeight). Chrome doesn't shift when the
        // card expands — only the card overlay grows.
        .padding({ bottom: this.chromeBottomPadding() })
        .hitTestBehavior(HitTestMode.Transparent)

        // STANDALONE chevron Row (collapsed-state-finished only). Hidden during animation so
        // it doesn't visually overlap with the shrinking/growing card.
        if (this.panelExpanded && !this.cardExpanded && this.panelHeight === 0) {
          Row() {
            Image($r('app.media.chevron_down'))
              .width(36)
              .height(12)
              .objectFit(ImageFit.Contain)
              .rotate({ angle: 180 })  // ∧
          }
          .id('chevronHandleStandalone')
          .position({ x: 0, y: this.chevronStandaloneY() })
          .width('100%')
          .height(this.kBottomReservedHeight)
          .justifyContent(FlexAlign.Center)
          .alignItems(VerticalAlign.Center)
          .onClick(() => { this.toggleCard(); })
        }
      }
      .width('100%')
      .height('100%')
      .opacity(this.fadeOpacity)
      .onAreaChange((_o: Area, n: Area) => {
        // The root Stack equals the page area (safe-area-corrected). Capturing here gives accurate
        // viewport math regardless of device size or notch insets.
        this.screenWvp = n.width as number;
        this.screenHvp = n.height as number;
      })
    }
    .expandSafeArea([SafeAreaType.SYSTEM], [SafeAreaEdge.BOTTOM])
    .onAppear(() => {
      arEngineDemo.init(this.resMgr);
      let config: Int32Array = new Int32Array([1, this.rotation]);
      arEngineDemo.start(this.idStr, config);
    })
    .onWillDisappear(() => {
      arEngineDemo.stop(this.idStr);
    })
    .onShown(() => {
      arEngineDemo.show(this.idStr);
      this.getUIContext().animateTo({ duration: 200, curve: Curve.EaseIn }, () => {
        this.fadeOpacity = 1.0;
      });
    })
    .onHidden(() => {
      arEngineDemo.hide(this.idStr);
    })
    .onReady((context: NavDestinationContext) => {
      this.pageInfos = context.pathStack;
    })
    .backgroundColor(Color.Black)
    .hideTitleBar(true)
    .hideBackButton(true)
    .hideToolBar(true)
  }
}
