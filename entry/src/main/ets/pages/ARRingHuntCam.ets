/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { display } from '@kit.ArkUI';
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

// Trail particle flying from screen center to the off-screen droplet, fading as it goes.
interface Particle {
  id: number;
  birthMs: number;
  lifeMs: number;
  startX: number;
  startY: number;
  targetX: number;
  targetY: number;
  color: string;
}

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
  @State breathScale: number = 1.0;
  private breathDurationMs: number = 0;
  private distanceBucket: number = -1;

  // Particle trail.
  private canvasSettings: RenderingContextSettings = new RenderingContextSettings(true);
  private canvasCtx: CanvasRenderingContext2D = new CanvasRenderingContext2D(this.canvasSettings);
  private canvasReady: boolean = false;
  private particles: Particle[] = [];
  private nextParticleId: number = 0;
  private spawnTimer: number = -1;
  private renderTimer: number = -1;

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
  private readonly kMaxOffset: number = 200;

  private dotOffsetX(): number {
    let yawDeg: number = this.clamp(this.yawDiffRad * 180 / Math.PI, -30, 30);
    return (yawDeg / 30) * this.kMaxOffset;
  }

  private dotOffsetY(): number {
    let pitchDeg: number = this.clamp(this.pitchDiffRad * 180 / Math.PI, -30, 30);
    return (pitchDeg / 30) * this.kMaxOffset;
  }

  private dotColor(): string {
    let ox: number = this.dotOffsetX();
    let oy: number = this.dotOffsetY();
    let d: number = Math.sqrt(ox * ox + oy * oy);
    let t: number = this.clamp(1 - d / this.kMaxOffset, 0, 1);
    let r: number = Math.round(255 + (76 - 255) * t);
    let g: number = Math.round(76 + (255 - 76) * t);
    let b: number = Math.round(76 + (102 - 76) * t);
    return `rgb(${r},${g},${b})`;
  }

  private toast(msg: string): void {
    try {
      this.getUIContext().getPromptAction().showToast({ message: msg, duration: 1500 });
    } catch (e) {
      let err: BusinessError = e as BusinessError;
      Logger.warn(`toast failed: ${err.message}`);
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

  private guidanceColor(): string {
    return Palette.alertRed;
  }

  private glowThickness(): number {
    if (this.distance < 1.0) {
      return 4;
    }
    if (this.distance < 3.0) {
      return 2;
    }
    return 1;
  }

  private glowOpacity(): number {
    if (this.distance < 1.0) {
      return 0.9;
    }
    if (this.distance < 3.0) {
      return 0.6;
    }
    return 0.3;
  }

  private distBucket(): number {
    if (this.distance < 1.0) {
      return 0;
    }
    if (this.distance < 3.0) {
      return 1;
    }
    return 2;
  }

  private breathDurationFor(bucket: number): number {
    if (bucket === 0) {
      return 800;
    }
    if (bucket === 1) {
      return 1500;
    }
    return 2000;
  }

  private startBreathing(): void {
    this.breathDurationMs = this.breathDurationFor(this.distBucket());
    this.breathScale = 0.95;
    this.getUIContext().animateTo({
      duration: this.breathDurationMs,
      curve: Curve.EaseInOut,
      iterations: -1,
      playMode: PlayMode.Alternate
    }, () => {
      this.breathScale = 1.10;
    });
  }

  private stopBreathing(): void {
    this.breathDurationMs = 0;
    this.getUIContext().animateTo({ duration: 0 }, () => {
      this.breathScale = 1.0;
    });
  }

  private spawnIntervalMs(): number {
    if (this.distance < 1.0) {
      return 50;
    }
    if (this.distance < 3.0) {
      return 150;
    }
    return 300;
  }

  private particleRgb(): string {
    return '#E63946';
  }

  private spawnParticle(): void {
    if (!this.guidanceShown || this.distance < 0.30 || this.isBehind) {
      return;
    }
    let p: Particle = {
      id: this.nextParticleId,
      birthMs: Date.now(),
      lifeMs: 1000,
      startX: this.getStackWidth() / 2,
      startY: this.getStackHeight() / 2,
      targetX: this.dropletCenterX(),
      targetY: this.dropletCenterY(),
      color: this.particleRgb()
    };
    this.nextParticleId = this.nextParticleId + 1;
    this.particles.push(p);
  }

  private renderParticles(): void {
    if (!this.canvasReady) {
      return;
    }
    let ctx: CanvasRenderingContext2D = this.canvasCtx;
    let w: number = this.getStackWidth();
    let h: number = this.getStackHeight();
    ctx.clearRect(0, 0, w, h);
    let now: number = Date.now();
    let alive: Particle[] = [];
    for (let p of this.particles) {
      let t: number = (now - p.birthMs) / p.lifeMs;
      if (t >= 1) {
        continue;
      }
      let x: number = p.startX + (p.targetX - p.startX) * t;
      let y: number = p.startY + (p.targetY - p.startY) * t;
      ctx.globalAlpha = 1 - t * t;
      ctx.fillStyle = p.color;
      ctx.beginPath();
      ctx.arc(x, y, 4 - t * 2, 0, Math.PI * 2);
      ctx.fill();
      alive.push(p);
    }
    ctx.globalAlpha = 1;
    this.particles = alive;
    if (this.spawnTimer === -1 && alive.length === 0 && this.renderTimer !== -1) {
      clearInterval(this.renderTimer);
      this.renderTimer = -1;
    }
  }

  private restartSpawnTimer(): void {
    if (this.spawnTimer !== -1) {
      clearInterval(this.spawnTimer);
    }
    this.spawnTimer = setInterval(() => {
      this.spawnParticle();
    }, this.spawnIntervalMs());
  }

  private startParticles(): void {
    this.restartSpawnTimer();
    if (this.renderTimer === -1) {
      this.renderTimer = setInterval(() => {
        this.renderParticles();
      }, 16);
    }
  }

  private stopParticles(clearNow: boolean): void {
    if (this.spawnTimer !== -1) {
      clearInterval(this.spawnTimer);
      this.spawnTimer = -1;
    }
    if (clearNow) {
      this.particles = [];
    }
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
    if (dx > 0.001) {
      t = Math.min(t, (this.getStackWidth() - 80 - cx) / dx);
    }
    if (dx < -0.001) {
      t = Math.min(t, (80 - cx) / dx);
    }
    if (dy > 0.001) {
      t = Math.min(t, (this.getStackHeight() - 80 - cy) / dy);
    }
    if (dy < -0.001) {
      t = Math.min(t, (80 - cy) / dy);
    }
    if (t === Infinity || t < 0) {
      t = 0;
    }
    return t;
  }

  private rayCenterX(): number {
    if (this.isBehind) {
      return this.getStackWidth() / 2;
    }
    return this.getStackWidth() / 2 + this.rayTMin() * this.rayDirX();
  }

  private rayCenterY(): number {
    if (this.isBehind) {
      return this.getStackHeight() - 200;
    }
    return this.getStackHeight() / 2 + this.rayTMin() * this.rayDirY();
  }

  private getDropletX(): number {
    return this.clamp(this.rayCenterX() - 80, 0, this.getStackWidth() - 160);
  }

  private getDropletY(): number {
    return this.clamp(this.rayCenterY() - 80, 0, this.getStackHeight() - 160);
  }

  private dropletCenterX(): number {
    return this.getDropletX() + 80;
  }

  private dropletCenterY(): number {
    return this.getDropletY() + 80;
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
      if (shouldShow) {
        let bucket: number = this.distBucket();
        if (bucket !== this.distanceBucket) {
          this.distanceBucket = bucket;
          this.startBreathing();
          this.restartSpawnTimer();
        }
        if (this.isBehind) {
          this.particles = [];
        }
      }
      return;
    }
    this.guidanceShown = shouldShow;
    if (shouldShow) {
      this.guidanceMounted = true;
      this.distanceBucket = this.distBucket();
      this.startBreathing();
      this.startParticles();
      this.getUIContext().animateTo({ duration: 300, curve: Curve.EaseOut }, () => {
        this.guidanceOpacity = 1;
      });
    } else {
      this.stopBreathing();
      this.stopParticles(this.distance < 0.30);
      this.distanceBucket = -1;
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
    this.distanceBucket = -1;
    this.stopBreathing();
    this.stopParticles(true);
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
            if (this.spawnTimer !== -1) {
              clearInterval(this.spawnTimer);
              this.spawnTimer = -1;
            }
            if (this.renderTimer !== -1) {
              clearInterval(this.renderTimer);
              this.renderTimer = -1;
            }
          })

        // (2) Viewport overlay — sized to the 4:3 rect, positioned at viewportTop. Every guidance
        // element scopes its math to this Stack (via onAreaChange + this.guidanceStackW/H), so
        // "off-screen droplet at the edge" naturally means "edge of viewport", not edge of screen.
        // clip(true) is a safety net for any per-frame jitter that could put a pixel outside.
        Stack({ alignContent: Alignment.Center }) {

          // Particle trail (drawn imperatively at ~60fps inside the viewport).
          Canvas(this.canvasCtx)
            .id('particles')
            .width('100%')
            .height('100%')
            .hitTestBehavior(HitTestMode.None)
            .onReady(() => { this.canvasReady = true; })

          // Off-screen droplet guidance. Same ray/edge math as the dev page, but the Stack is now
          // viewport-sized, so getDropletX/Y land on the viewport border (not the screen border).
          Stack() {
            if (this.guidanceMounted) {
              Stack({ alignContent: Alignment.Center }) {
                Circle()
                  .width(124).height(124)
                  .fillOpacity(0)
                  .stroke(this.guidanceColor())
                  .strokeWidth(this.glowThickness())
                  .opacity(this.glowOpacity())
                Circle()
                  .width(108).height(108)
                  .fillOpacity(0)
                  .stroke(this.guidanceColor())
                  .strokeWidth(3)
                  .opacity(0.5)
                  .scale({ x: this.breathScale, y: this.breathScale })
                if (!this.isBehind) {
                  Image($r('app.media.droplet'))
                    .width(60).height(60)
                    .objectFit(ImageFit.Contain)
                    .rotate({ x: 0, y: 0, z: 1, angle: this.dropletRotationDeg(), centerX: '50%', centerY: '50%' })
                } else {
                  Text('↻')
                    .fontColor(Palette.white)
                    .fontSize(56)
                    .fontWeight(FontWeight.Bold)
                }
              }
              .width(160).height(160)
              .opacity(this.guidanceOpacity)
              .position({ x: this.getDropletX(), y: this.getDropletY() })
            }
          }
          .id('guidance')
          .width('100%').height('100%')
          .hitTestBehavior(HitTestMode.None)
          .alignContent(Alignment.TopStart)
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

          // ALIGNING HUD: crosshair + reference ring + aiming dot — centered on viewport center
          // (which is now the visual center, not the screen center). Dot reach (kMaxOffset = 200)
          // comfortably fits inside the viewport at default phone widths.
          if (this.huntPhase === 1) {
            Stack({ alignContent: Alignment.Center }) {
              Row().width(56).height(3).backgroundColor('#E6FFFFFF')
              Column().width(3).height(56).backgroundColor('#E6FFFFFF')
              Circle()
                .width(this.kMaxOffset * 2).height(this.kMaxOffset * 2)
                .fillOpacity(0)
                .stroke(Palette.white40)
                .strokeWidth(2)
              Circle()
                .width(28).height(28)
                .fill(this.dotColor())
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
