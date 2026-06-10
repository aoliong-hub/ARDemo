/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { display, window, PathShape } from '@kit.ArkUI';
import { photoAccessHelper } from '@kit.MediaLibraryKit';
import { common } from '@kit.AbilityKit';
import { media } from '@kit.MediaKit';
import { audio } from '@kit.AudioKit';
import { resourceManager } from '@kit.LocalizationKit';
import { systemDateTime } from '@kit.BasicServicesKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { vibrator } from '@kit.SensorServiceKit';
import { image } from '@kit.ImageKit';
import { fileIo, fileUri } from '@kit.CoreFileKit';
import { util } from '@kit.ArkTS';
import arEngineDemo from 'libentry.so';
import { RingState, CapturedFrame, CamPose } from 'libentry.so';
import { Logger } from '../utils/Logger';
import { Palette, FontSize, Spacing, Radius } from '../styles/Theme';
import { CameraChrome } from '../components/CameraChrome';
import { ReferencePanel, RefImageHolder } from '../components/ReferencePanel';
import { generate as da3Generate, getGuide as da3GetGuide, packPixelMapToJpeg, parseGuideString, GuideData as Da3GuideData } from '../services/Da3Service';
import { WsGuideClient, WsPoseResult, BeaconWorld, extractGuideField } from '../services/Da3WsService';
import { getCloudConfig } from '../config/CloudConfig';
import { getBackendType, setBackendType, getBackendLabel, allBackendTypes, initBackendType, BackendType, getUseBeaconWorld } from '../config/CloudConfig';

// v13 (c): da3 机位服务返回的 guideData — 全 string,由 applyGuide 转 number 并喂给
// placeRingAt + applyZoom。语义跟 placeRingAt 一致:x=右(米)/ y=圆环离地高度(米)/ z=前(米),
// pitch/yaw/roll 度,zoom 变焦倍率。
interface GuideData {
  x: string;
  y: string;
  z: string;
  pitch: string;
  yaw: string;
  roll: string;
  zoom: string;
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
  @State guideUpdateCount: number = 0;

  // 6DoF alignment.
  @State huntPhase: number = 0; // 0=APPROACHING 1=ALIGNING 2=LOCKED
  @State yawDiffRad: number = 0;
  @State pitchDiffRad: number = 0;
  @State rollDiffRad: number = 0;
  // 重构(2026-06-03)— isAligned 现在 = (huntPhase == 1) = "吸附会话激活"。
  //   触发链:snapReady(瞬时) → 持续 1.5s → snapTriggered → phase 0→1 → isAligned 0→1 → 吸附特效 + snappedOnce
  //   修旧 bug:之前 isAligned=snapReady 比 phase 早 0.3s,ArkTS 的 phase==1 守门永远 miss → HUD 不出
  @State isAligned: boolean = false;
  @State isAngleAligned: boolean = false;
  @State fillRatio: number = 0;     // EMA 平滑后,过头提示 (>1.0) 用
  @State isLocked: boolean = false;
  @State inDeadzone: boolean = false;
  private prevLocked: boolean = false;
  // 进 30cm 边缘检测:huntPhase 上一帧值。0→1 切换时触发一次短震(第一阶段完成,进对齐阶段)。
  private prevHuntPhase: number = 0;
  // Phase 3 ④ — 吸附 + 两阶段 HUD 时序:
  //   snappedOnce: 本次进 30cm 是否已经吸附过(isAligned 0→1 边缘 → true,phase 1→0 重置 false)。
  //   hudOpacity: AlignHUD(L 括 + 炫彩点)的 opacity,吸附后 200ms 延迟淡入,LOCKED 或走出时 200ms 淡出。
  //   prevIsAligned: 上一帧 isAligned 值,用于检测 0→1 边缘触发 snap。
  //   hudShowTimer: 200ms 延迟 setTimeout 句柄(用户在延迟期间走开时要 clear 掉,避免乱出 HUD)。
  @State private snappedOnce: boolean = false;
  @State private hudOpacity: number = 0;
  private prevIsAligned: boolean = false;
  private hudShowTimer: number = -1;
  // LOCKED banner 显示控制:进 LOCKED 时 opacity=1,2s 后 animateTo 淡出到 0。reset/离开 LOCKED
  // 清掉计时器和 opacity。再次进 LOCKED 计时重置(重新显示 2s)。
  @State lockedBannerOpacity: number = 0;
  private lockedBannerTimer: number = -1;
  // 拍照快门黑闪:点快门时短暂全屏黑覆盖,类似系统相机的拍照反馈。150ms 总时长。
  @State shutterFlashOpacity: number = 0;

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

  // v13 digital zoom. Pinch on the viewport overlay drives continuous 1.0–5.0 via zoomBase × scale;
  // ZoomBar buttons jump to 1/2/5x. applyZoom is the single entry: clamps, writes mZoom in C++
  // via setZoom NAPI, pulses the central "Nx" indicator (auto-hides 900ms after the last input).
  @State zoom: number = 1.0;
  @State zoomIndicatorVisible: boolean = false;
  private readonly zoomStops: number[] = [1.0, 2.0, 5.0];
  private zoomBase: number = 1.0;
  private zoomHideTimer: number = -1;

  // v13 (d) — Da3 two-stage flow state. refHolder.image = generated target PixelMap (rendered in
  // ReferencePanel); refLoading drives the spinner; targetJpeg caches the JPEG bytes for step2.
  // Two flags (busyGenerate / busyGuide) are reentrancy guards so multiple taps don't pile up
  // overlapping requests.
  @State refHolder: RefImageHolder = new RefImageHolder();
  @State refLoading: boolean = false;
  private targetJpeg?: ArrayBuffer;
  private busyGenerate: boolean = false;
  private busyGuide: boolean = false;
  // v13 (e) — 连续机位引导循环（WebSocket）。持续抽帧上传直到对齐。
  private guideLoopRunning: boolean = false;
  private guideLoopFrameCount: number = 0;
  private wsClient?: WsGuideClient;
  // 会话级 task_id — 每次点炫彩球重生成,generate + getGuide 共用,服务端靠这个把
  // "先前生成的目标图"和"现在算机位"两次请求关联起来。da3 demo 对齐:UuidUtil.getUuid()。
  private sessionTaskId: string = '';
  // CALIB 调试面板 — 暴露最近一次 applyGuide 的"云端原值"和"实际送进 placeRingAt 的数字",
  // 演示时一眼对照信标实际位置/姿态,校准 da3 坐标系/符号约定。
  // 物理朝向(每 62ms 从 native 轮询):0=PORTRAIT, 1=LANDSCAPE_CW, 2=LANDSCAPE_CCW, 3=FLIP(预留)
  // 驱动炫彩球 / ReferencePanel 文字+chevron / ZoomBar 倍率数字 原位自转,让横拿/倒置时视觉正。
  @State orientation: number = 0;

  // 后端切换:da3 / vggt-omega。点一下切下一个。
  @State backendType: BackendType = getBackendType();

  // ===== Scale 校准引导 =====
  @State private scaleCalStep: number = 0;
  @State private scaleCalOpacity: number = 0;
  @State private scaleCalText: string = '';
  private scaleCalOrigin: CamPose | null = null;
  private scaleCalTimerMoveId: number = -1;
  private scaleCalTimerNudgeId: number = -1;
  private scaleCalCompleteTimerId: number = -1;

  // ===== 标定工具状态(完全旁路) =====
  // 标定模式总开关。true 时显示标定 overlay,其它正常 UI 不受影响。
  @State calibMode: boolean = false;
  // 当前步骤:0 = 采基准 A,1-10 = 采 10 个动作的 B
  @State calibStep: number = 0;
  // 当前手机朝向标签(用户手动指定):'portrait' | 'landscape'
  @State calibOrientationLabel: string = 'portrait';
  // 已完成会话编号(每完成一轮 10 步后递增,用于文件命名分组)
  @State calibGroupIdx: number = 1;
  // 状态文字(idle / capturing / done / running-da3 / error 等)
  @State calibStatus: string = '等待开始';
  // 上次采集的 A 帧 jpeg + 姿态(用于送 da3 + 写文件)
  private calibA_jpeg?: ArrayBuffer;
  private calibA_raw?: string;    // 姿态 raw 字符串(用于 meta)
  private calibA_disp?: string;
  private calibA_ori?: number;
  private calibA_ts?: number;


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

  // Phase 2 — 把"可见区 NDC y 边界"算出来 push 给 native。
  //   render 帧 y 映射到 NDC:y=0(FB 顶)→ ndc=+1;y=screenHvp×density(FB 底)→ ndc=-1。
  //   可见区上下边在 vp:vp_top=viewportTop;vp_bot=viewportTop+viewportH。
  //   用 vp 比例 → NDC y(不用 density,直接 vp/screenHvp 算分数,等价同除以 density)。
  // Native 用:center → renderer clip-space y shift(框居中可见区);halfExtent → fillRatio 判据归一。
  // 每次 onAreaChange 后调用(布局变化自适应跨机型 / 折叠屏 / 系统 UI 变化)。
  private pushVisibleNdcY(): void {
    const sh: number = Math.max(1, this.screenHvp);
    const topFrac: number = this.viewportTop / sh;
    const bottomFrac: number = (this.viewportTop + this.viewportH()) / sh;
    const topNdc: number = 1 - 2 * topFrac;
    const bottomNdc: number = 1 - 2 * bottomFrac;
    const centerY: number = (topNdc + bottomNdc) / 2;
    const halfExtent: number = (topNdc - bottomNdc) / 2;
    try {
      arEngineDemo.setVisibleNdcY(this.idStr, centerY, halfExtent);
      Logger.info(`ARDA3-LAYOUT setVisibleNdcY center=${centerY.toFixed(4)} halfExt=${halfExtent.toFixed(4)} ` +
                  `sh=${sh}vp viewportTop=${this.viewportTop} viewportH=${this.viewportH()}`);
    } catch (e) {
      const err: BusinessError = e as BusinessError;
      Logger.warn(`ARDA3-LAYOUT setVisibleNdcY FAIL: code=${err.code} msg=${err.message}`);
    }
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

  // AI sphere tap → enter AI mode (chrome compacts) AND open the card. Called from onBeaconTap
  // when step1 (capture + Da3 generate) begins.
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

  // 准星 dot 的 translate 按 native 报告的物理朝向旋转坐标。屏幕坐标系是竖布局,横拿时头歪 90°
  // → 屏右=视角下,会让 yaw/pitch 串轴。按朝向把屏 (x,y) 转换到"用户视角 (x,y)"。
  // 方向(ori===1 用 (y,-x) 还是 (-y,x))不能纯推理确定,首版按 LANDSCAPE_CW=(y,-x) 装,
  // 实测反了再翻 LANDSCAPE_CW 和 LANDSCAPE_CCW 的符号。
  private dotTranslate(): TranslateOptions {
    const xv: number = this.dotOffsetX();
    const yv: number = this.dotOffsetY();
    const ori: number = arEngineDemo.getOrientation(this.idStr);
    const out: TranslateOptions = { x: xv, y: yv };
    if (ori === 1) {        // LANDSCAPE_CW
      out.x = yv;
      out.y = -xv;
    } else if (ori === 2) { // LANDSCAPE_CCW
      out.x = -yv;
      out.y = xv;
    }
    return out;
  }

  // Stage 12C-redesign batch 1 fix — bracket / crosshair line builder. Off-aim = single white60
  // (pearl colors wash out at hairline thickness anyway). Aligned = real pearl pink→purple→blue
  // linearGradient so the L brackets AND the center "+" both shimmer together as the user pins
  // the dot in the gate. Solid colors on aligned looked uniform-purple in batch 1; switching to
  // gradient is the actual fix the brief asked for.
  @Builder
  private bracketLine(w: number, h: number, tx: number, ty: number): void {
    // 重构(2026-06-03)— HUD 视觉的 "in gate" 反馈用 angleAligned(yaw+pitch 5°↔7°),不再用 isAligned
    // (现 isAligned 指 snapReady,框填满就 true,但角度可能还没对)。HUD 引导的是角度,所以视觉用角度门。
    if (this.isAngleAligned) {
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

  // 清 AR 场景 + 流程状态(不含 da3 会话/参考图,供 onBeaconTap 和 applyGuide 共用)。
  // 关键:arEngineDemo.resetRing 会让 native 端 detach+release 上次的 ringAnchor(信标/灰盘/
  // 炫彩圈/对齐框这些视觉效果都是从 mRingAnchor 当前 pose 渲染出来的,anchor 一去全消失),并把
  // mHasRing/mHuntPhase/mDistance 等 atomic 状态归零。ets 端这里再手动把对应 @State 归零是为了
  // 当前帧立即更新 UI,不等下一个 62ms stateTimer tick。
  private resetSceneOnly(): void {
    arEngineDemo.resetRing(this.idStr);
    this.ringPlaced = false;
    this.huntPhase = 0;
    this.distance = 99;
    this.isTargetInView = true;
    this.screenEdgeX = 0.5;
    this.screenEdgeY = 0.5;
    this.isBehind = false;
    this.indicatorAngleDeg = 0;
    this.ndcX = 0;
    this.ndcY = 0;
    this.yawDiffRad = 0;
    this.pitchDiffRad = 0;
    this.rollDiffRad = 0;
    this.isAligned = false;
    this.isAngleAligned = false;
    this.fillRatio = 0;
    this.isLocked = false;
    this.prevLocked = false;
    this.prevHuntPhase = 0;
    // Phase 3 ④ — 重置 snap + HUD 状态(beacon reset 时立即清,不走动画)。
    this.snappedOnce = false;
    this.prevIsAligned = false;
    this.clearHudShowTimer();
    this.hudOpacity = 0;
    this.hideLockedBanner();
    this.resetGuidance();
    this.stopScaleCalGuidance();
  }

  // v13 (c) — guideData → AR end-to-end glue. Parses the 7 string fields, validates, then routes
  // to the existing placeRingAt + applyZoom paths. Designed to be the SINGLE entry the da3
  // pose-calc backend (or any other producer) calls: reset prior ring state → place → open AI
  // panel, plus a zoom write at the end.
  private applyGuide(g: GuideData): void {
    let x: number = Number(g.x);
    let y: number = Number(g.y);
    let z: number = Number(g.z);
    let yaw: number = Number(g.yaw);
    // 实测:da3"俯拍"经 parse 为正,但 placeRingAt 渲染时正 pitch = 仰(L177 pitchMat 约定),
    // 直接透传方向反。取负让 "俯拍 N°" 真正朝下倾。camPitch 补偿在 native 端保留。
    let pitch: number = -Number(g.pitch);
    let roll: number = Number(g.roll);
    let zoom: number = Number(g.zoom);
    if (Number.isNaN(x) || Number.isNaN(y) || Number.isNaN(z) ||
        Number.isNaN(yaw) || Number.isNaN(pitch) || Number.isNaN(roll)) {
      Logger.warn(`[applyGuide] NaN in pose fields, skip: x=${g.x} y=${g.y} z=${g.z} yaw=${g.yaw} pitch=${g.pitch} roll=${g.roll}`);
      return;
    }
    const posDist: number = Math.sqrt(x * x + y * y + z * z);
    const angDist: number = Math.abs(yaw) + Math.abs(pitch) + Math.abs(roll);
    if (posDist < 0.001 && angDist < 0.01) {
      Logger.warn(`[applyGuide] zero guide (pos=${posDist} ang=${angDist}), skip`);
      return;
    }
    // 清场景(含 native 锚点 detach+release)+ 流程状态。da3 会话/参考图不动 —— applyGuide
    // 是同一会话内的 step2,sessionTaskId 和 targetJpeg 后续可能还要用。
    this.resetSceneOnly();
    // === 符号约定(扶正图标定铁证,横竖屏一致)===
    //   da3 输出 = 从当前 B 走到目标 A 的位移向量,用户视角(相机本地):
    //     x:+ =用户右  y:+ =用户上(垂直 delta)  z:+ =用户前
    //     yaw:+ =右转  pitch:+ =俯  roll:+ =左滚
    //   placeRingAt 同语义:x/y/z=用户右/上/前(米),yaw=+右转,pitch=+仰(L177 约定)。
    //   ∴ x/y/z/yaw/roll 直传;pitch 反向(da3+俯 vs placeRingAt+仰)→ 取负。
    //   roll:native 会丢弃(用 camRoll 自动 snap 横竖),传啥都无效。
    //   横竖屏统一:captureJpeg 按 ori 旋转扶正后送 da3,标定工具同套旋转
    //   (10 步 × 3 朝向标定主导轴+符号完全一致),故此处不做 orientation 分支。
    let id: number = arEngineDemo.placeRingAt(this.idStr, x, y, z, yaw, pitch, roll);
    Logger.info(`[applyGuide] placeRingAt(x=${x} y=${y} z=${z} yaw=${yaw} pitch=${pitch} roll=${roll}) -> ${id}, zoom=${zoom}`);
    if (id < 0) {
      this.toast('请稳住手机等待跟踪稳定');
      return;
    }
    this.ringPlaced = true;
    this.guideUpdateCount++;
    // 注意:不调 enterAiModeAndOpenCard()。applyGuide 是 step2(点参考图)的产物,onTargetTap
    // 已主动收起卡片栏让出取景空间;此处再展开会和功能②冲突。panelExpanded(AI 模式)在
    // onBeaconTap 时已设好,这里无需重设。卡片保持 onTargetTap 收起的状态,用户点 chevron 可
    // 手动展开看参考图。
    // 相机焦距暂不自动调整(da3 zoom 先不生效)。ZoomBar / 捏合手动变焦不受影响。
    // if (!Number.isNaN(zoom) && zoom > 0) {
    //   this.applyZoom(Math.min(5.0, Math.max(1.0, zoom)));
    // }
  }

  private applyGuideFromGuideString(pose: WsPoseResult): void {
    const gs: Record<string, ESObject> | undefined = pose.guide_string;
    if (gs === undefined) {
      Logger.warn('ARDA3-WS loop: pose without guide_string');
      return;
    }
    const rawGs: Da3GuideData = {
      x:     extractGuideField(gs, 'x', '0'),
      y:     extractGuideField(gs, 'y', '0'),
      z:     extractGuideField(gs, 'z', '0'),
      pitch: extractGuideField(gs, 'pitch', '0'),
      yaw:   extractGuideField(gs, 'yaw', '0'),
      roll:  extractGuideField(gs, 'roll', '0'),
      zoom:  extractGuideField(gs, 'zoom', '1'),
    };
    const parsed: Da3GuideData = parseGuideString(rawGs);
    Logger.info(`ARDA3-WS loop frame ${this.guideLoopFrameCount}: raw=${JSON.stringify(rawGs)} parsed=${JSON.stringify(parsed)}`);
    this.applyGuide(parsed as GuideData);
  }

  // AI 机位 action: place a wayfinder beacon (random 6DoF target). Repeated taps cause native to
  // replace the prior beacon, so we also wipe the local UI state to keep guidance in sync.
  // v13 single zoom entry: clamp [1, 5], push to native (setZoom NAPI → mZoom atomic → next
  // OnUpdate's glViewport), show/refresh the central "Nx" indicator. Called by both the pinch
  // gesture (continuous) and ZoomBar (button taps).
  private applyZoom(z: number): void {
    let nz: number = Math.min(5.0, Math.max(1.0, z));
    this.zoom = nz;
    arEngineDemo.setZoom(this.idStr, nz);
    this.zoomIndicatorVisible = true;
    if (this.zoomHideTimer >= 0) {
      clearTimeout(this.zoomHideTimer);
    }
    this.zoomHideTimer = setTimeout(() => {
      this.zoomIndicatorVisible = false;
      this.zoomHideTimer = -1;
    }, 900);
  }

  // v13 (d) — request a single frame from native, poll isFrameReady every 50ms (cap 1s), then
  // wrap the RGBA bytes in a PixelMap and pack JPEG q92. Persists a copy under filesDir for
  // post-hoc verification ("hdc file recv files/cap_*.jpg .").
  // Phase 3(2026-06-02)统一 crop:把抓的完整渲染帧(1256×2526,1:2)裁成用户实际看到的可见区
  // (1256×1680,3:4),去掉顶 147px + 底 700px(ArkUI 黑条遮罩位置)。所有出图(da3 generate /
  // da3 getGuide / 拍照存图库 / 标定工具)都走这一层,保证发出去的图 = 用户看到的画面。
  //
  // ★ 必须在 pm.rotate(扶正)之前调用!理由:可见区 strip 在"App XComponent 竖屏坐标系"里是固定
  //   的(顶 147 + 底 700),不随物理朝向变。先 crop(始终同一对边)再 rotate 扶正,数学最简
  //   单也不会算错。crop+rotate 后,各朝向产生的可见区相对最终图像的边:
  //     ori=0 (PORTRAIT,不旋转):       裁 raw 顶 147 + 底 700 → 1256×1680 输出
  //     ori=1 (LANDSCAPE_CW,旋 90°):    底带 700 → 输出 LEFT,顶带 147 → 输出 RIGHT,输出 1680×1256
  //     ori=2 (LANDSCAPE_CCW,旋 270°):  顶带 147 → 输出 LEFT,底带 700 → 输出 RIGHT,输出 1680×1256
  //     ori=3 (FLIP 180°,旋 180°):     底带 700 → 输出 TOP,顶带 147 → 输出 BOTTOM,输出 1256×1680
  //   ↑ 所有朝向最终图都是 3:4 的可见区内容(da3/相册看到的就是用户看到的)。
  //
  // 边界比例从 ArkUI 布局推:viewportTop / viewportH 都是 vp;screenHvp 是 XComponent 的 vp 高;
  // 把"vp 比例"乘以 rawH(px)就拿到 px 坐标。不依赖具体 density,只用比例,跨机型自适应。
  private async cropToVisibleRegion(pm: image.PixelMap): Promise<void> {
    try {
      const info: image.ImageInfo = await pm.getImageInfo();
      const rawW: number = info.size.width;
      const rawH: number = info.size.height;
      const screenH_vp: number = Math.max(1, this.screenHvp);
      const visTop_px: number = Math.round((this.viewportTop / screenH_vp) * rawH);
      const visH_px: number = Math.round((this.viewportH() / screenH_vp) * rawH);
      if (visH_px <= 0 || visTop_px < 0 || visTop_px + visH_px > rawH) {
        Logger.warn(`ARDA3-CROP region invalid top=${visTop_px} h=${visH_px} raw=${rawW}x${rawH} — skip`);
        return;
      }
      await pm.crop({ x: 0, y: visTop_px, size: { width: rawW, height: visH_px } });
      Logger.info(`ARDA3-CROP visible y=${visTop_px} h=${visH_px} → ${rawW}x${visH_px} (raw ${rawW}x${rawH})`);
    } catch (e) {
      const err: BusinessError = e as BusinessError;
      Logger.warn(`ARDA3-CROP failed code=${err.code} msg=${err.message}`);
    }
  }

  private captureJpeg(): Promise<ArrayBuffer | null> {
    return new Promise<ArrayBuffer | null>((resolve: (v: ArrayBuffer | null) => void) => {
      // 排空上一次残留的旧帧,保证后续 isFrameReady 轮询等到的是本次 captureFrame 触发的新帧
      arEngineDemo.takeFrameRGBA(this.idStr);
      arEngineDemo.captureFrame(this.idStr);
      let tries: number = 0;
      let reFired: boolean = false;
      const maxTries: number = 60;   // 50ms * 60 = 3s 总等待
      const reFireAt: number = 20;   // 1s 仍无帧 → 重发一次 captureFrame(覆盖 surface 抖动 / tracking 暂掉)
      const timerId: number = setInterval(() => {
        tries = tries + 1;
        if (!arEngineDemo.isFrameReady(this.idStr)) {
          if (tries === reFireAt && !reFired) {
            reFired = true;
            Logger.warn(`ARDA3-CAP no frame after 1s — re-firing captureFrame (try ${tries}/${maxTries})`);
            arEngineDemo.captureFrame(this.idStr);
          }
          if (tries >= maxTries) {
            clearInterval(timerId);
            Logger.error(`ARDA3-CAP timeout (no frame after 3s, retry=${reFired})`);
            resolve(null);
          }
          return;
        }
        clearInterval(timerId);
        const cap: CapturedFrame | null = arEngineDemo.takeFrameRGBA(this.idStr);
        if (cap === null) {
          Logger.error('ARDA3-CAP takeFrameRGBA returned null');
          resolve(null);
          return;
        }
        Logger.info(`ARDA3-CAP RGBA taken w=${cap.width} h=${cap.height} bytes=${cap.buffer.byteLength}`);
        const opts: image.InitializationOptions = {
          size: { width: cap.width, height: cap.height },
          srcPixelFormat: image.PixelMapFormat.RGBA_8888,
          pixelFormat: image.PixelMapFormat.RGBA_8888,
          editable: true
        };
        image.createPixelMap(cap.buffer, opts).then(async (pm: image.PixelMap) => {
          // 先裁可见区再扶正(与 captureCleanJpeg / captureJpegForCalib 一致),
          // 去掉顶+底黑条,让云侧收到的图 = 用户实际看到的画面。
          await this.cropToVisibleRegion(pm);
          // 按 native 报告的物理朝向把图旋正,让 da3 收到的图和用户视觉构图一致。
          // 0=PORTRAIT(不转), 1=LANDSCAPE_CW(顺时针横拿,把图逆时针 90° → da3 看到横画布), 2=LANDSCAPE_CCW(反向)。
          const ori: number = arEngineDemo.getOrientation(this.idStr);
          try {
            // 实测对调:存档图证实 LANDSCAPE_CW 用 rotate(270) 会得到倒置图 → 改 90;
            // LANDSCAPE_CCW 对称翻成 270。HarmonyOS pm.rotate(angle) 的实际旋转方向跟之前推理反。
            if (ori === 1) {
              await pm.rotate(90);
              Logger.info('ARDA3-CAP rotate 90 (LANDSCAPE_CW)');
            } else if (ori === 2) {
              await pm.rotate(270);
              Logger.info('ARDA3-CAP rotate 270 (LANDSCAPE_CCW)');
            } else {
              Logger.info('ARDA3-CAP no rotate (PORTRAIT)');
            }
          } catch (e) {
            const err: BusinessError = e as BusinessError;
            Logger.error(`ARDA3-CAP rotate failed code=${err.code} msg=${err.message}`);
          }
          // 云侧推理不需要全分辨率,缩到 ~512px 减少上传流量
          try {
            const imgInfo: image.ImageInfo = await pm.getImageInfo();
            const sx: number = 512 / imgInfo.size.width;
            const sy: number = 512 / imgInfo.size.height;
            const s: number = Math.min(sx, sy);
            await pm.scale(s, s);
            Logger.info(`ARDA3-CAP scaled ${s.toFixed(3)} → ~512px`);
          } catch (e) {
            Logger.warn(`ARDA3-CAP scale failed: ${(e as BusinessError).message}`);
          }
          packPixelMapToJpeg(pm).then((jpeg: ArrayBuffer | null) => {
            pm.release().catch((e: BusinessError) => { Logger.warn(`ARDA3-CAP pm.release: ${e.message}`); });
            if (jpeg === null) {
              resolve(null);
              return;
            }
            Logger.info(`ARDA3-CAP jpeg ready bytes=${jpeg.byteLength}`);
            try {
              const path: string = `${this.context.filesDir}/cap_${systemDateTime.getTime(false)}.jpg`;
              const f: fileIo.File = fileIo.openSync(path,
                fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
              fileIo.write(f.fd, jpeg).then(() => {
                fileIo.closeSync(f.fd);
                Logger.info(`ARDA3-CAP jpeg saved path=${path}`);
              }).catch((e: BusinessError) => {
                Logger.warn(`ARDA3-CAP save write failed: ${e.message}`);
                try { fileIo.closeSync(f.fd); } catch (_e) {}
              });
            } catch (e) {
              const err: BusinessError = e as BusinessError;
              Logger.warn(`ARDA3-CAP save failed code=${err.code} msg=${err.message}`);
            }
            resolve(jpeg);
          });
        }).catch((e: BusinessError) => {
          Logger.error(`ARDA3-CAP createPixelMap failed code=${e.code} msg=${e.message}`);
          resolve(null);
        });
      }, 50);
    });
  }

  // v13 (d/e) — step1: AI sphere tap.
  //   da3:         HTTP 两段式 → capture+generate → 显示目标图 → 等用户点目标图触发 step2
  //   vggt-omega:  WebSocket 一站式 → runGuideLoop 连续抽帧+引导
  private async onBeaconTap(): Promise<void> {
    const isWs: boolean = getBackendType() === 'vggt-omega';
    if (isWs) {
      // ── vggt-omega: 混合模式 — 手机端 DA3 生图 + 服务端 3D 定位 ──
      if (this.busyGuide) {
        Logger.warn('ARDA3-WS beacon tap ignored (busy)');
        return;
      }
      this.busyGuide = true;
      this.stopGuideLoop();
      this.resetSceneOnly();
      this.refHolder.image = undefined;
      this.targetJpeg = undefined;
      this.sessionTaskId = util.generateRandomUUID();
      Logger.info(`ARDA3-WS new session: ${this.sessionTaskId}`);
      this.guideUpdateCount = 0;
      this.enterAiModeAndOpenCard();
      this.refLoading = true;
      if (this.zoom !== 1.0) {
        this.applyZoom(1.0);
        await new Promise<void>((r: () => void) => { setTimeout(r, 150); });
      }

      // Step 1: DA3 生图
      Logger.info('ARDA3-WS beacon tap → DA3 generate target image');
      this.toast('正在生成目标图…');
      const jpeg: ArrayBuffer | null = await this.captureJpeg();
      if (jpeg === null) {
        this.refLoading = false;
        this.busyGuide = false;
        this.toast('抓帧失败');
        Logger.error('ARDA3-WS step1 capture fail');
        return;
      }
      const target: image.PixelMap | null = await da3Generate(jpeg, this.sessionTaskId, this.context.filesDir);
      if (target === null) {
        this.refLoading = false;
        this.busyGuide = false;
        this.toast('生成失败');
        Logger.error('ARDA3-WS step1 DA3 generate fail');
        return;
      }
      this.refHolder.image = target;
      this.refLoading = false;
      const targetJpeg: ArrayBuffer | null = await packPixelMapToJpeg(target);
      if (targetJpeg === null) {
        this.busyGuide = false;
        Logger.error('ARDA3-WS step1 target re-pack to jpeg failed');
        this.toast('图像编码失败');
        return;
      }
      this.targetJpeg = targetJpeg;
      Logger.info(`ARDA3-WS step1 done, target shown, targetJpegBytes=${targetJpeg.byteLength}`);

      // Step 2: WebSocket 引导（带目标图）
      Logger.info('ARDA3-WS beacon tap → starting WebSocket guide loop');
      this.toast('连接云侧…');
      this.startScaleCalGuidance();
      this.runGuideLoop();
    } else {
      // ── da3: HTTP 两段式 ──
      if (this.busyGenerate) {
        Logger.warn('ARDA3-FLOW step1 ignored (busy)');
        return;
      }
      this.busyGenerate = true;
      try {
        this.resetSceneOnly();
        this.refHolder.image = undefined;
        this.targetJpeg = undefined;
        this.sessionTaskId = util.generateRandomUUID();
        Logger.info(`ARDA3-FLOW new sessionTaskId=${this.sessionTaskId}`);
        Logger.info('ARDA3-FLOW step1 beacon tap → capture+generate');
        this.enterAiModeAndOpenCard();
        this.refLoading = true;
        if (this.zoom !== 1.0) {
          this.applyZoom(1.0);
          await new Promise<void>((r: () => void) => { setTimeout(r, 150); });
        }
        const jpeg: ArrayBuffer | null = await this.captureJpeg();
        if (jpeg === null) {
          this.refLoading = false;
          this.toast('抓帧失败');
          Logger.error('ARDA3-FLOW step1 capture fail');
          return;
        }
        const target: image.PixelMap | null = await da3Generate(jpeg, this.sessionTaskId, this.context.filesDir);
        this.refLoading = false;
        if (target === null) {
          this.toast('生成失败');
          Logger.error('ARDA3-FLOW step1 generate fail');
          return;
        }
        this.refHolder.image = target;
        const targetJpeg: ArrayBuffer | null = await packPixelMapToJpeg(target);
        if (targetJpeg === null) {
          Logger.error('ARDA3-FLOW step1 target re-pack to jpeg failed');
        } else {
          this.targetJpeg = targetJpeg;
          Logger.info(`ARDA3-FLOW step1 done, target shown, targetJpegBytes=${targetJpeg.byteLength}`);
        }
      } finally {
        this.busyGenerate = false;
      }
    }
  }

  // v13 (d) — step2: target image tap. Capture current frame again, send (target, current) to
  // Da3 getGuide, route the result through applyGuide. Reentrancy-guarded.
  private async onTargetTap(): Promise<void> {
    if (this.busyGuide) {
      Logger.warn('ARDA3-FLOW step2 ignored (busy)');
      return;
    }
    if (this.targetJpeg === undefined) {
      this.toast('请先生成目标图');
      Logger.warn('ARDA3-FLOW step2 no targetJpeg cached');
      return;
    }
    this.busyGuide = true;
    try {
      if (this.cardExpanded) {
        this.toggleCard();
      }
      Logger.info('ARDA3-FLOW step2 target tap → capture+getGuide');
      this.toast('正在计算机位…');
      if (this.zoom !== 1.0) {
        this.applyZoom(1.0);
        await new Promise<void>((resolve: () => void) => { setTimeout(resolve, 150); });
      }
      const current: ArrayBuffer | null = await this.captureJpeg();
      if (current === null) {
        this.toast('抓帧失败');
        Logger.error('ARDA3-FLOW step2 capture fail');
        return;
      }
      const guide: Da3GuideData | null = await da3GetGuide(this.targetJpeg as ArrayBuffer, current, this.sessionTaskId, this.context.filesDir);
      if (guide === null) {
        this.toast('机位计算失败');
        Logger.error('ARDA3-FLOW step2 getGuide fail');
        return;
      }
      Logger.info(`ARDA3-FLOW step2 guide=${JSON.stringify(guide)} → applyGuide`);
      this.applyGuide(guide as GuideData);
    } finally {
      this.busyGuide = false;
    }
  }

  // Debug: 从 rawfile 加载固定 JPEG 图片,替代摄像头抽帧。
  private async loadDebugImage(): Promise<ArrayBuffer | null> {
    try {
      const ctx: common.UIAbilityContext = this.context as common.UIAbilityContext;
      const cfg = getCloudConfig();
      const raw: Uint8Array = await ctx.resourceManager.getRawFileContent(cfg.debugFixedImageName);
      Logger.info(`ARDA3-DEBUG fixed image loaded: ${cfg.debugFixedImageName} (${raw.byteLength}B)`);
      return raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength) as ArrayBuffer;
    } catch (e) {
      Logger.error(`ARDA3-DEBUG load failed: ${(e as BusinessError).message}`);
      return null;
    }
  }

  // v13 (e) — WebSocket 连续引导主循环。
  //   1. 连接 ws://host/ws/infer，等服务端发 "ready"
  //   2. 每 intervalMs 抽一帧 → 通过 WebSocket 二进制帧头发送
  //   3. 云侧滑动窗口攒够帧后触发 VGGT batch 推理 → 返回 pose + guide_string
  //   4. 收到 pose → parse guide_string → applyGuide → 检测 isLocked
  //   5. LOCKED 或超时 → 关 WebSocket → 收尾
  private async runGuideLoop(): Promise<void> {
    this.guideLoopRunning = true;
    this.guideLoopFrameCount = 0;

    const client: WsGuideClient = new WsGuideClient();
    this.wsClient = client;

    // 状态闭包 —— WebSocket 回调与 setInterval 共享
    let stopped: boolean = false;
    let frameTimer: number = -1;

    const cleanup = (): void => {
      stopped = true;
      this.guideLoopRunning = false;
      if (frameTimer >= 0) {
        clearInterval(frameTimer);
        frameTimer = -1;
      }
      client.close();
      this.busyGuide = false;
      this.wsClient = undefined;
      this.stopScaleCalGuidance();
    };

    // ── 连接 ──
    const ok: boolean = await client.connect();
    if (!ok) {
      Logger.error('ARDA3-WS loop: connect failed');
      this.toast('连接云侧失败');
      this.busyGuide = false;
      this.guideLoopRunning = false;
      return;
    }

    // ── 等 "ready" 后开始抽帧发送 ──
    const cfg = getCloudConfig();
    const maxFrames: number = cfg.wsGuideMaxFrames;
    const intervalMs: number = cfg.wsGuideFrameIntervalMs;
    const useDebugImage: boolean = cfg.debugUseFixedImage;

    // 注册回调：状态 / pose / 关闭 / 目标图
    let imageReceived: boolean = false;
    let poseReceived: boolean = false;
    let captureInProgress: boolean = false;  // 防重入：一次只允许一个 captureJpeg 在跑

    client.setCallbacks(
      // onStatus — 等服务端就绪后：先发目标图，再开始抽帧
      (status: string) => {
        Logger.info(`ARDA3-WS loop: status=${status}`);
        if (status === 'ready') {
          // 先发客户端生成的目标图
          if (this.targetJpeg !== undefined) {
            const sent: boolean = client.sendTargetImage(this.targetJpeg as ArrayBuffer);
            Logger.info(`ARDA3-WS loop: sent target image to server, ok=${sent}`);
          } else {
            Logger.warn('ARDA3-WS loop: no target image to send (targetJpeg is undefined)');
          }

          frameTimer = setInterval(async (): Promise<void> => {
            if (stopped || !this.guideLoopRunning) {
              clearInterval(frameTimer);
              cleanup();
              return;
            }
            // 已对齐 → 立即停止抽帧
            if (this.isLocked) {
              Logger.info(`ARDA3-WS loop: LOCKED detected, stopping`);
              this.toast('对齐成功!');
              cleanup();
              return;
            }
            // 上一帧还在抽 → 跳过本次，等下一拍
            if (captureInProgress) {
              return;
            }

            this.guideLoopFrameCount++;
            if (this.guideLoopFrameCount > maxFrames) {
              Logger.info(`ARDA3-WS loop: max frames ${maxFrames} reached`);
              this.toast('机位引导超时，请重试');
              cleanup();
              return;
            }

            captureInProgress = true;
            let current: ArrayBuffer | null = null;
            if (useDebugImage) {
              current = await this.loadDebugImage();
            } else {
              current = await this.captureJpeg();
            }
            captureInProgress = false;
            if (current === null) {
              Logger.warn(`ARDA3-WS loop frame ${this.guideLoopFrameCount}: capture failed`);
              return;
            }

            Logger.info(`ARDA3-WS loop frame ${this.guideLoopFrameCount}/${maxFrames}: sending (${current.byteLength}B)`);
            // 优先发 0x04（带 DisplayOriented pose）；tracking 未就绪时降级发 0x01（云侧 scale=1.0）。
            // 必须用 DispPose（GetDisplayOrientedPose）而非 RawPose（GetPose），因为 JPEG 是显示方向的，
            // 云侧 VGGT 相机坐标系与显示方向对齐，用 RawPose 会导致 beacon 偏移旋转~90°。
            const camPose: CamPose | null = arEngineDemo.getLatestCamDispPose(this.idStr);
            if (camPose !== null) {
              client.sendFrameWithPose(current,
                camPose.px, camPose.py, camPose.pz,
                camPose.qx, camPose.qy, camPose.qz, camPose.qw);
            } else {
              client.sendFrame(current);
            }
          }, intervalMs);
        }
      },
      // onPose — 云侧推理完成，带回 guide_string（可选 beacon_world）
      (pose: WsPoseResult): void => {
        if (stopped) {
          return;
        }
        if (!poseReceived) {
          poseReceived = true;
          this.refLoading = false;
          Logger.info('ARDA3-WS loop: first pose received, server ready');
        }
        this.inDeadzone = pose.in_deadzone === true;

        // ── beacon_world 路径：直接传世界坐标给 C++，不做 world→local→world 往返 ──
        const bw: BeaconWorld | undefined = pose.beacon_world;
        if (getUseBeaconWorld() && bw !== undefined &&
            bw.position.length >= 3 && bw.quaternion.length >= 4) {
          Logger.info(`ARDA3-WS beacon_world: worldPos=[${bw.position.map((v: number): string => v.toFixed(4)).join(',')}] ` +
            `quat=[${bw.quaternion.map((v: number): string => v.toFixed(4)).join(',')}]`);
          this.resetSceneOnly();
          let id: number = arEngineDemo.placeRingAtWorld(this.idStr,
            bw.position[0], bw.position[1], bw.position[2],
            bw.quaternion[0], bw.quaternion[1], bw.quaternion[2], bw.quaternion[3]);
          Logger.info(`[applyGuide] placeRingAtWorld -> ${id}`);
          if (id < 0) {
            this.toast('请稳住手机等待跟踪稳定');
          } else {
            this.ringPlaced = true;
            this.guideUpdateCount++;
          }
        } else {
          // ── guide_string 路径（原有逻辑 / beacon_world 不存在时回退） ──
          Logger.info(`ARDA3-WS beacon_world skipped: useFlag=${getUseBeaconWorld()} bw=${bw !== undefined} ` +
            `posLen=${bw !== undefined ? bw.position.length : -1} quatLen=${bw !== undefined ? bw.quaternion.length : -1}`);
          this.applyGuideFromGuideString(pose);
        }

        setTimeout((): void => {
          if (this.isLocked) {
            Logger.info(`ARDA3-WS loop: LOCKED at frame ${this.guideLoopFrameCount}`);
            this.toast('对齐成功!');
            cleanup();
          }
        }, 150);
      },
      // onClosed — 连接断开时收尾
      (): void => {
        Logger.info('ARDA3-WS loop: connection closed');
        cleanup();
      },
      // onImage — 云侧 Vision API 生成的目标图（首帧触发，type=0x03 二进制消息）
      (jpegBytes: ArrayBuffer): void => {
        if (imageReceived) {
          return;
        }
        imageReceived = true;
        Logger.info(`ARDA3-WS loop: target image received (${jpegBytes.byteLength}B)`);
        try {
          const ts: number = systemDateTime.getTime(false);
          const path: string = `${this.context.filesDir}/target_${ts}.jpg`;
          const f: fileIo.File = fileIo.openSync(path,
            fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
          fileIo.writeSync(f.fd, jpegBytes);
          fileIo.closeSync(f.fd);
          Logger.info(`ARDA3-WS loop: target image saved path=${path} bytes=${jpegBytes.byteLength}`);
        } catch (e) {
          const err: BusinessError = e as BusinessError;
          Logger.warn(`ARDA3-WS loop: target image save failed: ${err.message}`);
        }
        // 解码 JPEG → PixelMap
        try {
          const src: image.ImageSource = image.createImageSource(jpegBytes);
          const decodeOpts: image.DecodingOptions = {
            editable: true,
            desiredPixelFormat: image.PixelMapFormat.RGBA_8888
          };
          src.createPixelMap(decodeOpts).then((pm: image.PixelMap) => {
            this.refHolder.image = pm;
            src.release();
            Logger.info(`ARDA3-WS loop: target image displayed (${pm.getImageInfoSync().size.width}x${pm.getImageInfoSync().size.height})`);
          }).catch((e: BusinessError) => {
            Logger.error(`ARDA3-WS loop: target image decode failed: ${e.message}`);
            src.release();
          });
        } catch (e) {
          Logger.error(`ARDA3-WS loop: createImageSource failed: ${(e as BusinessError).message}`);
        }
      }
    );
  }

  // v13 (e) — 停止连续引导循环（炫彩球重开新会话时调用）。
  private stopGuideLoop(): void {
    if (this.guideLoopRunning) {
      Logger.info('ARDA3-WS loop: stopping by user request');
      this.guideLoopRunning = false;
      if (this.wsClient) {
        this.wsClient.close();
        this.wsClient = undefined;
      }
      this.busyGuide = false;
    }
  }

  // 拍照纯净帧抓取(无 AR 物体):captureCleanFrame 触发 → poll isCleanFrameReady → takeCleanFrameRGBA。
  // 流程几乎同 captureJpeg(da3 用),区别:用 clean 系列 NAPI,拿到的 framebuffer 在 wayfinder
  // 渲染前 glReadPixels,所以不含信标/炫彩圈/对齐框等。其余 createPixelMap + 按 orientation 旋转
  // 扶正 + packJpeg 完全一致。
  private captureCleanJpeg(): Promise<ArrayBuffer | null> {
    return new Promise<ArrayBuffer | null>((resolve: (v: ArrayBuffer | null) => void) => {
      arEngineDemo.captureCleanFrame(this.idStr);
      let tries: number = 0;
      const maxTries: number = 60;
      const timerId: number = setInterval(() => {
        tries++;
        if (!arEngineDemo.isCleanFrameReady(this.idStr)) {
          if (tries >= maxTries) {
            clearInterval(timerId);
            Logger.error('ARDA3-CAPTURE clean frame timeout (3s)');
            resolve(null);
          }
          return;
        }
        clearInterval(timerId);
        const cap: CapturedFrame | null = arEngineDemo.takeCleanFrameRGBA(this.idStr);
        if (cap === null) {
          Logger.error('ARDA3-CAPTURE takeCleanFrameRGBA returned null');
          resolve(null);
          return;
        }
        Logger.info(`ARDA3-CAPTURE clean RGBA taken ${cap.width}x${cap.height} bytes=${cap.buffer.byteLength}`);
        const opts: image.InitializationOptions = {
          size: { width: cap.width, height: cap.height },
          srcPixelFormat: image.PixelMapFormat.RGBA_8888,
          pixelFormat: image.PixelMapFormat.RGBA_8888,
          editable: true
        };
        image.createPixelMap(cap.buffer, opts).then(async (pm: image.PixelMap) => {
          // Phase 3 — 先裁可见区再扶正(照片 = 用户看到的)。详见 cropToVisibleRegion。
          await this.cropToVisibleRegion(pm);
          const ori: number = arEngineDemo.getOrientation(this.idStr);
          try {
            if (ori === 1) { await pm.rotate(90); }
            else if (ori === 2) { await pm.rotate(270); }
            else if (ori === 3) { await pm.rotate(180); }
          } catch (e) {
            const err: BusinessError = e as BusinessError;
            Logger.error(`ARDA3-CAPTURE rotate failed code=${err.code} msg=${err.message}`);
          }
          packPixelMapToJpeg(pm).then((jpeg: ArrayBuffer | null) => {
            pm.release().catch((e: BusinessError) => { Logger.warn(`pm.release: ${e.message}`); });
            resolve(jpeg);
          });
        }).catch((e: BusinessError) => {
          Logger.error(`ARDA3-CAPTURE createPixelMap failed code=${e.code} msg=${e.message}`);
          resolve(null);
        });
      }, 50);
    });
  }

  // 存图库(免权限路径):
  //   1) JPEG 字节先写到 app 沙箱 filesDir/photo_<ts>.jpg
  //   2) 拼 file:// 沙箱 uri
  //   3) photoAccessHelper.showAssetsCreationDialog 弹系统确认窗,用户点保存 → 入库
  // 这条路径不需 WRITE_IMAGEVIDEO(WRITE_IMAGEVIDEO 是 system_grant 第三方拿不到);每次拍照
  // 会弹一次"是否保存到图库",用户授权 = 入库。第一次拍后系统可能记忆选择(各机型表现不同)。
  private async savePhotoToGallery(jpeg: ArrayBuffer): Promise<boolean> {
    let stage: string = 'init';
    try {
      stage = 'sandbox-write';
      const ts: number = systemDateTime.getTime(false);
      const sandboxPath: string = `${this.context.filesDir}/photo_${ts}.jpg`;
      const f: fileIo.File = fileIo.openSync(sandboxPath,
        fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
      fileIo.writeSync(f.fd, jpeg);
      fileIo.closeSync(f.fd);
      Logger.info(`ARDA3-CAPTURE sandbox written path=${sandboxPath} bytes=${jpeg.byteLength}`);

      stage = 'sandbox-uri';
      const sandboxUri: string = fileUri.getUriFromPath(sandboxPath);

      stage = 'showAssetsCreationDialog';
      const ctx: common.UIAbilityContext = this.context as common.UIAbilityContext;
      const helper: photoAccessHelper.PhotoAccessHelper = photoAccessHelper.getPhotoAccessHelper(ctx);
      const config: photoAccessHelper.PhotoCreationConfig = {
        title: `AR_${ts}`,
        fileNameExtension: 'jpg',
        photoType: photoAccessHelper.PhotoType.IMAGE
      };
      const galleryUris: string[] = await helper.showAssetsCreationDialog([sandboxUri], [config]);
      Logger.info(`ARDA3-CAPTURE showAssetsCreationDialog returned ${galleryUris.length} uri(s)`);
      if (galleryUris.length === 0) {
        Logger.warn('ARDA3-CAPTURE user cancelled gallery save dialog');
        return false;
      }

      // showAssetsCreationDialog 返回的是相册中【预创建的空 asset slot uri】,必须把 JPEG 字节
      // 写入这个 uri 才算真正存图。否则相册里会显示一个空 asset(用户看不到图)。
      stage = 'gallery-write';
      const galleryUri: string = galleryUris[0];
      const gf: fileIo.File = fileIo.openSync(galleryUri, fileIo.OpenMode.WRITE_ONLY);
      fileIo.writeSync(gf.fd, jpeg);
      fileIo.closeSync(gf.fd);
      Logger.info(`ARDA3-CAPTURE saved to gallery uri=${galleryUri} bytes=${jpeg.byteLength}`);
      return true;
    } catch (e) {
      const err: BusinessError = e as BusinessError;
      Logger.error(`ARDA3-CAPTURE save FAIL at stage=${stage} code=${err.code} msg=${err.message}`);
      return false;
    }
  }

  // 底部大白圈快门 onClick:tiny 震 + 咔嚓音(fire-and-forget,不阻塞)→ 抓纯净帧 → 存图库。
  // 重入锁:连续点击时,前一次未完成的直接 ignore(避免多个 poll 抢帧导致"抓帧失败")。
  // native 端 tracking 丢失时会自动重试下一帧(re-arm),所以 3s poll 期间瞬时跟踪丢失不会失败。
  private async onCapturePhoto(): Promise<void> {
    if (this.busyCapture) {
      Logger.warn('ARDA3-CAPTURE shutter ignored (busy)');
      this.toast('正在处理...');
      return;
    }
    this.busyCapture = true;
    try {
      Logger.info('ARDA3-CAPTURE shutter tapped');
      this.triggerTinyVibration();
      this.playShutterSound();
      this.triggerShutterFlash();
      const jpeg: ArrayBuffer | null = await this.captureCleanJpeg();
      if (jpeg === null) {
        Logger.error('ARDA3-CAPTURE captureCleanJpeg returned null');
        this.toast('抓帧失败');
        return;
      }
      const saved: boolean = await this.savePhotoToGallery(jpeg);
      if (saved) {
        this.toast('已保存到相册');
        // ★ 拍照成功后(2026-06-03)全场景重置:走完引导流程到 D 拍照后,清空回初始态。
        //   清:信标锚点 / 灰盘 / 炫彩框 / HUD / snap 状态;da3 会话(sessionTaskId / refImage / targetJpeg);
        //   ★机位引导图卡片(2026-06-03 追加):滑下隐藏 + 退 AI 模式,回初始干净态。
        //   不清:zoom(用户主动控制的)。
        this.resetSceneOnly();
        this.refHolder.image = undefined;
        this.targetJpeg = undefined;
        this.sessionTaskId = '';
        // 卡片滑下隐藏:动画 panelHeight → 0,然后退出 AI 模式(chrome 恢复全尺寸)。
        // 复用现有 animatePanelHeightTo(0) 动画,300ms ease-out,和 chevron 收起一致。
        this.animatePanelHeightTo(0);
        this.cardExpanded = false;
        this.panelExpanded = false;
        Logger.info('ARDA3-CAPTURE photo saved → full reset (scene + da3 session + card)');
      } else {
        this.toast('保存失败,查日志');
      }
    } finally {
      this.busyCapture = false;
    }
  }

  // 拍照 SoundPool(快门音)。lazy init,失败/资源缺失静默,不影响拍照本身。
  private shutterSoundPool: media.SoundPool | undefined = undefined;
  private shutterSoundId: number = -1;
  private shutterSoundLoading: boolean = false;
  // 拍照重入锁:连续快速点击时,前一次还在抓帧/存图,后续点击直接忽略,避免多个 captureCleanJpeg
  // 同时 poll 互相抢同一帧导致"抓帧失败"。
  private busyCapture: boolean = false;

  // LOCKED banner:显示 → 2s 后开始 500ms 淡出。再次调用会清旧计时器,重新显示 2s。
  private showLockedBanner(): void {
    if (this.lockedBannerTimer >= 0) {
      clearTimeout(this.lockedBannerTimer);
      this.lockedBannerTimer = -1;
    }
    this.lockedBannerOpacity = 1;
    this.lockedBannerTimer = setTimeout(() => {
      animateTo({ duration: 500, curve: Curve.EaseOut }, () => {
        this.lockedBannerOpacity = 0;
      });
      this.lockedBannerTimer = -1;
    }, 2000);
  }

  // 即时隐藏 banner(reset 或离开 LOCKED 时调)。清计时器 + opacity=0,不走动画。
  private hideLockedBanner(): void {
    if (this.lockedBannerTimer >= 0) {
      clearTimeout(this.lockedBannerTimer);
      this.lockedBannerTimer = -1;
    }
    this.lockedBannerOpacity = 0;
  }

  // ── Scale 校准引导 ──────────────────────────────────────────────
  private startScaleCalGuidance(): void {
    this.stopScaleCalGuidance();
    this.scaleCalOrigin = arEngineDemo.getLatestCamDispPose(this.idStr) ?? null;
    this.scaleCalStep = 1;
    this.scaleCalText = '请将手机对准目标';
    animateTo({ duration: 300, curve: Curve.EaseOut }, () => {
      this.scaleCalOpacity = 1;
    });
    this.scaleCalTimerMoveId = setTimeout(() => {
      this.scaleCalTimerMoveId = -1;
      this.advanceScaleCalToMove();
    }, 700);
  }

  private advanceScaleCalToMove(): void {
    if (this.scaleCalStep !== 1) {
      return;
    }
    this.scaleCalStep = 2;
    this.scaleCalText = '保持对准，慢慢向旁边走一小步';
    if (this.scaleCalOrigin == null) {
      this.scaleCalOrigin = arEngineDemo.getLatestCamDispPose(this.idStr) ?? null;
    }
    this.scaleCalTimerNudgeId = setTimeout(() => {
      this.scaleCalTimerNudgeId = -1;
      this.advanceScaleCalToNudge();
    }, 5000);
  }

  private advanceScaleCalToNudge(): void {
    if (this.scaleCalStep !== 2) {
      return;
    }
    this.scaleCalStep = 4;
    this.scaleCalText = '再走远一点';
  }

  private completeScaleCalGuidance(): void {
    if (this.scaleCalStep === 0 || this.scaleCalStep === 3) {
      return;
    }
    this.clearScaleCalTimers();
    this.scaleCalStep = 3;
    this.scaleCalText = '校准完成，请稍候';
    this.scaleCalCompleteTimerId = setTimeout(() => {
      this.scaleCalCompleteTimerId = -1;
      animateTo({ duration: 600, curve: Curve.EaseOut }, () => {
        this.scaleCalOpacity = 0;
      });
      setTimeout(() => {
        this.scaleCalStep = 0;
      }, 600);
    }, 1500);
  }

  private stopScaleCalGuidance(): void {
    this.clearScaleCalTimers();
    this.scaleCalStep = 0;
    this.scaleCalOpacity = 0;
    this.scaleCalOrigin = null;
  }

  private clearScaleCalTimers(): void {
    if (this.scaleCalTimerMoveId >= 0) {
      clearTimeout(this.scaleCalTimerMoveId);
      this.scaleCalTimerMoveId = -1;
    }
    if (this.scaleCalTimerNudgeId >= 0) {
      clearTimeout(this.scaleCalTimerNudgeId);
      this.scaleCalTimerNudgeId = -1;
    }
    if (this.scaleCalCompleteTimerId >= 0) {
      clearTimeout(this.scaleCalCompleteTimerId);
      this.scaleCalCompleteTimerId = -1;
    }
  }

  // Phase 3 ④ — 吸附触发:三重门 aligned 首次 0→1 瞬间,触发震动 + 200ms 后淡入 HUD。
  //   native 端框已经在自己跑 snap 弹性动画(lockProgress 弹簧 + pulseScale 衰减) — ArkTS 这边只
  //   管 (1) 震动反馈;(2) snappedOnce 标记;(3) 延迟 200ms 让 snap 弹性先演完,再淡入 HUD(精调阶段)。
  private onSnapTrigger(): void {
    if (this.snappedOnce) {
      return; // 同一轮 ALIGNING 期只 snap 一次,后续 aligned 抖动不重复
    }
    this.snappedOnce = true;
    this.triggerShortVibration(); // 80ms 中震 — 吸附里程碑,比拍照轻震(30ms)更明显
    this.clearHudShowTimer();
    // Snap FX(2026-06-03)— 0.9s 庆祝特效播完之后再淡入 HUD(炫彩膜 + 闪白 + 涌出取景框 + 消失)。
    // 之前 200ms 是给"简单弹性 snap + 颜色变化"留时间;现在升级为 4 阶段大特效(总 0.9s),HUD 延迟同步抬到 900ms。
    this.hudShowTimer = setTimeout(() => {
      animateTo({ duration: 200, curve: Curve.EaseOut }, () => {
        this.hudOpacity = 1;
      });
      this.hudShowTimer = -1;
    }, 900);
  }

  // Phase 3 ④ — 走出 30cm(phase 1→0)或进 LOCKED(phase 1→2):平滑淡出 HUD。
  //   走出 30cm 时:snappedOnce 清零(下一轮进 30cm 重新走"接近→吸附"流程);
  //   LOCKED 时:不清 snappedOnce(用户已对齐成功,banner 上来),HUD 直接淡出。
  private fadeHudOut(resetSnapped: boolean): void {
    this.clearHudShowTimer();
    if (resetSnapped) {
      this.snappedOnce = false;
    }
    if (this.hudOpacity > 0.01) {
      animateTo({ duration: 200, curve: Curve.EaseOut }, () => {
        this.hudOpacity = 0;
      });
    } else {
      this.hudOpacity = 0;
    }
  }

  private clearHudShowTimer(): void {
    if (this.hudShowTimer >= 0) {
      clearTimeout(this.hudShowTimer);
      this.hudShowTimer = -1;
    }
  }

  // 拍照黑闪:opacity 立即拉到 1(覆盖屏幕)→ 80ms 后开始 150ms 淡出。总 230ms 视觉反馈。
  // 纯 UI 层动画,不影响 native glReadPixels(早在闪之前就抓完了)。
  private triggerShutterFlash(): void {
    this.shutterFlashOpacity = 1;
    setTimeout(() => {
      animateTo({ duration: 150, curve: Curve.EaseOut }, () => {
        this.shutterFlashOpacity = 0;
      });
    }, 80);
  }

  // 30ms 极短震 — 拍照触觉反馈,比 triggerShortVibration(80ms) 更轻,纯按键回弹感。
  private triggerTinyVibration(): void {
    try {
      vibrator.startVibration({ type: 'time', duration: 30 }, { usage: 'notification' })
        .catch((err: BusinessError) => {
          Logger.warn(`tiny vibrate failed: code=${err.code} msg=${err.message}`);
        });
    } catch (e) {
      const err: BusinessError = e as BusinessError;
      Logger.warn(`tiny vibrate exception: ${err.message}`);
    }
  }

  // SoundPool 懒加载(第一次拍照时初始化),返回是否就绪。失败一次就放弃(不阻塞拍照)。
  private async ensureShutterSoundReady(): Promise<boolean> {
    if (this.shutterSoundPool !== undefined && this.shutterSoundId >= 0) {
      return true;
    }
    if (this.shutterSoundLoading) {
      return false;
    }
    this.shutterSoundLoading = true;
    try {
      const rendererInfo: audio.AudioRendererInfo = {
        usage: audio.StreamUsage.STREAM_USAGE_NOTIFICATION,
        rendererFlags: 0
      };
      const pool: media.SoundPool = await media.createSoundPool(1, rendererInfo);
      // 监听 load 完成,拿到可播 soundId(media.SoundPool.load 返回的是初步 id,但要 loadComplete
      // 事件触发后才能 play;这里用一次性 Promise 等事件)。
      const loaded: Promise<number> = new Promise<number>((resolve, reject) => {
        pool.on('loadComplete', (id: number) => { resolve(id); });
        // 容错:5s 内没触发 loadComplete 就当失败。
        setTimeout(() => { reject(new Error('SoundPool loadComplete timeout 5s')); }, 5000);
      });
      const ctx: common.UIAbilityContext = this.context as common.UIAbilityContext;
      const rawFd: resourceManager.RawFileDescriptor = await ctx.resourceManager.getRawFd('shutter_click.wav');
      await pool.load(rawFd.fd, rawFd.offset, rawFd.length);
      const soundId: number = await loaded;
      this.shutterSoundPool = pool;
      this.shutterSoundId = soundId;
      Logger.info(`ARDA3-CAPTURE shutter SoundPool ready soundId=${soundId}`);
      return true;
    } catch (e) {
      const err: BusinessError = e as BusinessError;
      Logger.warn(`ARDA3-CAPTURE shutter SoundPool init failed code=${err.code} msg=${err.message}`);
      this.shutterSoundPool = undefined;
      this.shutterSoundId = -1;
      return false;
    } finally {
      this.shutterSoundLoading = false;
    }
  }

  // 播放快门音 — 不 await,fire-and-forget。SoundPool 没就绪就静默跳过。
  private playShutterSound(): void {
    this.ensureShutterSoundReady().then((ready: boolean) => {
      if (!ready || this.shutterSoundPool === undefined || this.shutterSoundId < 0) {
        return;
      }
      try {
        const playParams: media.PlayParameters = {
          loop: 0,
          rate: 1.0,
          leftVolume: 1.0,
          rightVolume: 1.0,
          priority: 0
        };
        this.shutterSoundPool.play(this.shutterSoundId, playParams)
          .catch((e: BusinessError) => { Logger.warn(`shutter sound play failed: ${e.message}`); });
      } catch (e) {
        const err: BusinessError = e as BusinessError;
        Logger.warn(`shutter sound play exception: ${err.message}`);
      }
    }).catch(() => { /* ensure 已 swallow */ });
  }

  // 80ms 短震 — 用于阶段切换的触觉反馈(进 30cm phase 0→1,以及 LOCKED phase 1→2)。
  private triggerShortVibration(): void {
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

  // 切换后端:da3 ↔ vggt-omega。点一下小标签即切换,下次 generate/getGuide 走新 URL。
  private cycleBackend(): void {
    const types: BackendType[] = allBackendTypes();
    const idx: number = types.indexOf(this.backendType);
    const next: BackendType = types[(idx + 1) % types.length];
    setBackendType(next, this.context);
    this.backendType = next;
    Logger.info(`[ARRingHuntCam] backend switched to ${next} (${getBackendLabel(next)})`);
    this.toast(`后端: ${getBackendLabel(next)}`);
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

  // ===== 标定工具(完全旁路,不动主流程) =====

  // 11 步描述(0 = 基准,1-10 = 动作)。
  private readonly kCalibSteps: string[] = [
    '基准位置(任意稳定位置)',
    '纯右移 50cm',
    '纯左移 50cm',
    '纯前进 50cm',
    '纯后退 50cm',
    '纯上抬 30cm',
    '纯下降 30cm',
    '回基准位置,纯右转 30°',
    '回基准位置,纯左转 30°',
    '回基准位置,纯俯 30°(镜头朝下)',
    '回基准位置,纯仰 30°(镜头朝上)',
  ];

  // 把 CamPose 序列化成可读字符串。
  private calibFmtPose(p: CamPose | null): string {
    if (p === null) {
      return 'NULL(tracking not ready)';
    }
    return `qx=${p.qx.toFixed(6)} qy=${p.qy.toFixed(6)} qz=${p.qz.toFixed(6)} qw=${p.qw.toFixed(6)} ` +
           `px=${p.px.toFixed(6)} py=${p.py.toFixed(6)} pz=${p.pz.toFixed(6)}`;
  }

  // 标定抓帧:按 orientation 旋转(与正常 captureJpeg 一致),扶正后送 da3。
  // 这样标定喂给 da3 的字节和线上 applyGuide 路径喂的完全一致,得到的 x/y/z/yaw/pitch
  // 才能直接套到正常流程,不受"原始横躺图 vs 扶正图"差异污染。
  private captureJpegForCalib(): Promise<ArrayBuffer | null> {
    return new Promise<ArrayBuffer | null>((resolve: (v: ArrayBuffer | null) => void) => {
      arEngineDemo.takeFrameRGBA(this.idStr);
      arEngineDemo.captureFrame(this.idStr);
      let tries: number = 0;
      const maxTries: number = 60;
      const timerId: number = setInterval(() => {
        tries++;
        if (!arEngineDemo.isFrameReady(this.idStr)) {
          if (tries >= maxTries) {
            clearInterval(timerId);
            Logger.error('ARDA3-CALIBTOOL capture timeout');
            resolve(null);
          }
          return;
        }
        clearInterval(timerId);
        const cap: CapturedFrame | null = arEngineDemo.takeFrameRGBA(this.idStr);
        if (cap === null) {
          resolve(null);
          return;
        }
        const opts: image.InitializationOptions = {
          size: { width: cap.width, height: cap.height },
          srcPixelFormat: image.PixelMapFormat.RGBA_8888,
          pixelFormat: image.PixelMapFormat.RGBA_8888,
          editable: true
        };
        image.createPixelMap(cap.buffer, opts).then(async (pm: image.PixelMap) => {
          // Phase 3 — 与正常 captureJpeg 完全同一套裁切+旋转,标定 ↔ 线上路径喂 da3 的图字节一致。
          await this.cropToVisibleRegion(pm);
          // 与正常 captureJpeg 完全同一套旋转策略(实测对调:CW=90,CCW=270;FLIP=180)。
          const ori: number = arEngineDemo.getOrientation(this.idStr);
          try {
            if (ori === 1) {
              await pm.rotate(90);
              Logger.info('ARDA3-CALIBTOOL rotate 90 (LANDSCAPE_CW)');
            } else if (ori === 2) {
              await pm.rotate(270);
              Logger.info('ARDA3-CALIBTOOL rotate 270 (LANDSCAPE_CCW)');
            } else if (ori === 3) {
              await pm.rotate(180);
              Logger.info('ARDA3-CALIBTOOL rotate 180 (FLIP)');
            } else {
              Logger.info('ARDA3-CALIBTOOL no rotate (PORTRAIT)');
            }
          } catch (e) {
            const err: BusinessError = e as BusinessError;
            Logger.error(`ARDA3-CALIBTOOL rotate failed code=${err.code} msg=${err.message}`);
          }
          packPixelMapToJpeg(pm).then((jpeg: ArrayBuffer | null) => {
            pm.release().catch((e: BusinessError) => { Logger.warn(`pm.release: ${e.message}`); });
            resolve(jpeg);
          });
        }).catch((e: BusinessError) => {
          Logger.error(`ARDA3-CALIBTOOL createPixelMap failed: ${e.message}`);
          resolve(null);
        });
      }, 50);
    });
  }

  // 把字节流写到标定子目录(filesDir/da3_calib/),目录不存在自动建。
  private calibSaveBytes(name: string, bytes: ArrayBuffer): void {
    try {
      const dir: string = this.context.filesDir + '/da3_calib';
      if (!fileIo.accessSync(dir)) {
        fileIo.mkdirSync(dir);
      }
      const path: string = dir + '/' + name;
      const f: fileIo.File = fileIo.openSync(path,
        fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
      fileIo.writeSync(f.fd, bytes);
      fileIo.closeSync(f.fd);
      Logger.info(`ARDA3-CALIBTOOL saved ${path} bytes=${bytes.byteLength}`);
    } catch (e) {
      const err: BusinessError = e as BusinessError;
      Logger.warn(`ARDA3-CALIBTOOL save FAIL ${name}: ${err.message}`);
    }
  }

  private calibSaveText(name: string, text: string): void {
    try {
      const dir: string = this.context.filesDir + '/da3_calib';
      if (!fileIo.accessSync(dir)) {
        fileIo.mkdirSync(dir);
      }
      const path: string = dir + '/' + name;
      const f: fileIo.File = fileIo.openSync(path,
        fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
      const enc: util.TextEncoder = new util.TextEncoder('utf-8');
      const bytes: Uint8Array = enc.encodeInto(text);
      const ab: ArrayBuffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
      fileIo.writeSync(f.fd, ab);
      fileIo.closeSync(f.fd);
      Logger.info(`ARDA3-CALIBTOOL saved ${path} text len=${text.length}`);
    } catch (e) {
      const err: BusinessError = e as BusinessError;
      Logger.warn(`ARDA3-CALIBTOOL save FAIL ${name}: ${err.message}`);
    }
  }

  // 采基准 A:抓帧 + 记录姿态 + 写文件,作为后续 10 步的固定 target。
  private async calibCaptureA(): Promise<void> {
    this.calibStatus = '采基准 A 中...';
    const jpeg: ArrayBuffer | null = await this.captureJpegForCalib();
    if (jpeg === null) {
      this.calibStatus = '采 A 失败:抓帧超时';
      return;
    }
    const rawP: CamPose | null = arEngineDemo.getLatestCamRawPose(this.idStr);
    const dispP: CamPose | null = arEngineDemo.getLatestCamDispPose(this.idStr);
    const ts: number = systemDateTime.getTime(false);
    this.calibA_jpeg = jpeg;
    this.calibA_raw = this.calibFmtPose(rawP);
    this.calibA_disp = this.calibFmtPose(dispP);
    this.calibA_ori = arEngineDemo.getOrientation(this.idStr);
    this.calibA_ts = ts;
    // 立即存档 A(同步存,后续每步 B 都引用这个 A)
    const fnameJpeg: string = `calib_${this.calibOrientationLabel}_g${this.calibGroupIdx}_A_ts${ts}.jpg`;
    this.calibSaveBytes(fnameJpeg, jpeg);
    this.calibStatus = `基准 A 已采集(ts=${ts},jpeg=${jpeg.byteLength}B,ori=${this.calibA_ori})。下一步:执行 step1 动作`;
    this.calibStep = 1;  // 自动进 step1
    Logger.info(`ARDA3-CALIBTOOL captured A group=${this.calibGroupIdx} ori=${this.calibOrientationLabel}`);
  }

  // 采当前 B + 调 da3 getGuide(target=A, current=B)+ 写文件。
  private async calibCaptureBandRun(): Promise<void> {
    if (this.calibA_jpeg === undefined) {
      this.calibStatus = '请先采基准 A';
      return;
    }
    const step: number = this.calibStep;
    if (step < 1 || step > 10) {
      this.calibStatus = '所有动作已完成。按【完成会话】写 group 汇总';
      return;
    }
    this.calibStatus = `采 step${step} B 中...`;
    const jpegB: ArrayBuffer | null = await this.captureJpegForCalib();
    if (jpegB === null) {
      this.calibStatus = `采 step${step} B 失败:抓帧超时`;
      return;
    }
    const rawP: CamPose | null = arEngineDemo.getLatestCamRawPose(this.idStr);
    const dispP: CamPose | null = arEngineDemo.getLatestCamDispPose(this.idStr);
    const tsB: number = systemDateTime.getTime(false);
    const oriB: number = arEngineDemo.getOrientation(this.idStr);
    const fnameJpeg: string = `calib_${this.calibOrientationLabel}_g${this.calibGroupIdx}_s${step}_B_ts${tsB}.jpg`;
    this.calibSaveBytes(fnameJpeg, jpegB);
    this.calibStatus = `step${step} B 已采集,调 da3 中...`;
    // 调 da3 getGuide(复用现有 Da3Service,新会话 task_id)
    const calibTaskId: string = util.generateRandomUUID();
    let da3Raw: string = 'N/A';
    try {
      const guide: Da3GuideData | null = await da3GetGuide(
        this.calibA_jpeg as ArrayBuffer, jpegB, calibTaskId, this.context.filesDir);
      if (guide !== null) {
        da3Raw = JSON.stringify(guide);
      } else {
        da3Raw = 'NULL(getGuide 失败)';
      }
    } catch (e) {
      da3Raw = `EXCEPTION: ${(e as Error).message}`;
    }
    // 写 meta
    const fnameMeta: string = `calib_${this.calibOrientationLabel}_g${this.calibGroupIdx}_s${step}_meta.txt`;
    const meta: string =
      `step: ${step}/10\n` +
      `action: ${this.kCalibSteps[step]}\n` +
      `orientation_label: ${this.calibOrientationLabel}\n` +
      `group: ${this.calibGroupIdx}\n` +
      `--- A (target / 基准) ---\n` +
      `ts: ${this.calibA_ts}\n` +
      `orientation_code: ${this.calibA_ori}\n` +
      `poseA_getPose:           ${this.calibA_raw}\n` +
      `poseA_getDisplayOriented:${this.calibA_disp}\n` +
      `--- B (current / 当前帧) ---\n` +
      `ts: ${tsB}\n` +
      `orientation_code: ${oriB}\n` +
      `poseB_getPose:           ${this.calibFmtPose(rawP)}\n` +
      `poseB_getDisplayOriented:${this.calibFmtPose(dispP)}\n` +
      `--- da3 getGuide(target=A, current=B)---\n` +
      `da3_taskid: ${calibTaskId}\n` +
      `da3_raw: ${da3Raw}\n`;
    this.calibSaveText(fnameMeta, meta);
    this.calibStatus = `step${step} 完成。da3=${da3Raw}`;
    Logger.info(`ARDA3-CALIBTOOL step${step} done da3=${da3Raw}`);
    // 进下一步
    if (step < 10) {
      this.calibStep = step + 1;
    } else {
      this.calibStatus += '\n会话完成!可【新会话】(group++)或【切换横竖】重测';
    }
  }

  private calibStartNewGroup(): void {
    this.calibGroupIdx += 1;
    this.calibStep = 0;
    this.calibA_jpeg = undefined;
    this.calibStatus = `新会话 group ${this.calibGroupIdx}。下一步:采基准 A`;
  }

  private calibToggleOrientation(): void {
    this.calibOrientationLabel = this.calibOrientationLabel === 'portrait' ? 'landscape' : 'portrait';
    this.calibStartNewGroup();
    this.calibStatus = `已切到 ${this.calibOrientationLabel}。新会话 group ${this.calibGroupIdx}。先采 A`;
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
              // huntPhase 0→1 边缘 = 进 30cm 第一阶段完成 → 短震一下 + 卡片自动弹出展开
              // (对齐时方便看参考图比对构图)。仅在收起状态下展开,避免冗余 toggle。
              if (s.huntPhase === 1 && this.prevHuntPhase === 0) {
                this.triggerShortVibration();
                if (!this.cardExpanded) {
                  this.toggleCard();
                }
              }
              // huntPhase 1→0 边缘 = 走出 30cm → 卡片自动收起(回到找信标状态,让出取景空间)。
              // 仅在展开状态下收起。中间用户手动点 chevron 收/展不受影响,30cm 区间内自由。
              // Phase 3 ④ — 同时清吸附状态 + 淡出 HUD(回到 pearl 自由流彩状态,下次进 30cm 重新走"接近→吸附")。
              if (s.huntPhase === 0 && this.prevHuntPhase === 1) {
                if (this.cardExpanded) {
                  this.toggleCard();
                }
                this.fadeHudOut(true);  // resetSnapped=true,允许下次重新 snap
              }
              // LOCKED banner 边缘:进 2 (C→D) → 显示 2s 后淡出;离开 2 → 立即隐藏(为下次进 D 准备)。
              // 注:phase 1→2(C→D)只动 banner,不动 HUD(用户可能 D→C 再回,HUD 要在 D/C 都可见,
              // 用户在 D 状态仍然能看炫彩点判断角度;角度 > 10° 才真正解锁回 B,由 isAligned 1→0 处理 HUD 淡出)。
              if (s.huntPhase === 2 && this.prevHuntPhase !== 2) {
                this.showLockedBanner();
                // ★ 不调 fadeHudOut(false) — D 状态保留 HUD,用户角度回 5°-10° (D→C) 时 HUD 还在
              } else if (s.huntPhase !== 2 && this.prevHuntPhase === 2) {
                this.hideLockedBanner();
              }
              this.prevHuntPhase = s.huntPhase;
              this.huntPhase = s.huntPhase;
              this.yawDiffRad = s.yawDiffRad;
              this.pitchDiffRad = s.pitchDiffRad;
              this.rollDiffRad = s.rollDiffRad;
              // 重构(2026-06-03 状态机 A/B/C/D)— isAligned 现在 = snapped(sticky 标志):
              //   B→C 边缘 0→1 = snapReady 持续 1.5s 后 snap 触发。震动 + 900ms 后淡入 HUD。
              //   C/D→B 边缘 1→0 = 角度 > 10° 解锁(或走出 30cm)。HUD 立刻淡出,snappedOnce 重置。
              //   D↔C 之间(phase 变 1↔2)不影响 isAligned。
              if (s.isAligned && !this.prevIsAligned) {
                this.onSnapTrigger();
              }
              // ★ 问题 2 修(2026-06-03):C/D→B 角度解锁,需要立刻 HUD 淡出 + snappedOnce 重置。
              //   原 fadeHudOut 只在 phase 1→0 触发,但 C→B 时 phase 还在 1(没出 30cm),所以漏触发。
              if (!s.isAligned && this.prevIsAligned) {
                this.fadeHudOut(true); // resetSnapped=true,允许下次 fill 持续 1.5s 后重新 snap
              }
              this.prevIsAligned = s.isAligned;
              this.isAligned = s.isAligned;
              this.isAngleAligned = s.isAngleAligned;
              this.fillRatio = s.fillRatio;
              this.isLocked = s.isLocked;
              if (this.isLocked && !this.prevLocked) {
                this.triggerShortVibration();
              }
              this.prevLocked = this.isLocked;
              this.reconcileGuidance();
              // 物理朝向轮询:变化时打 log + 触发依赖控件重渲染(rotate angle)。
              const newOri: number = arEngineDemo.getOrientation(this.idStr);
              if (newOri !== this.orientation) {
                this.orientation = newOri;
              }
              // ── Scale 校准引导:位移检测 ──
              if (this.scaleCalStep === 2 || this.scaleCalStep === 4) {
                const calPose: CamPose | null = arEngineDemo.getLatestCamDispPose(this.idStr) ?? null;
                if (calPose != null) {
                  if (this.scaleCalOrigin == null) {
                    this.scaleCalOrigin = calPose;
                  } else {
                    const dx: number = calPose.px - this.scaleCalOrigin.px;
                    const dy: number = calPose.py - this.scaleCalOrigin.py;
                    const dz: number = calPose.pz - this.scaleCalOrigin.pz;
                    if (Math.sqrt(dx * dx + dy * dy + dz * dz) >= 0.30) {
                      this.completeScaleCalGuidance();
                    }
                  }
                }
              }
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
            if (this.guidanceMounted && !this.isBehind && this.zoom < 1.05) {
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
                Text(`距离 ${this.distText()} m  #${this.guideUpdateCount}${this.inDeadzone ? '  ◉位置锁定' : ''}`)
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

          // 重构(2026-06-03)— "过头"提示:bbox fillRatio > 1.0 = 框超出取景框(用户走太近)。
          // 引导用户后退,直到框回到可见区内(fill ≤ 1.0,在吸附区间 [0.90, 1.0])。
          // LOCKED 后不显示(已对齐完成)。位置紧贴距离 pill 下方(距顶 76vp ≈ pill 高 + 间距)。
          if (this.ringPlaced && this.fillRatio > 1.0 && this.huntPhase !== 2) {
            Row() {
              Text('请退后一点 (取景框已被超出)')
                .fontSize(FontSize.hud)
                .fontColor('#FFFFCC66')
                .fontWeight(FontWeight.Medium)
                .backgroundColor(Palette.glass)
                .padding({ left: Spacing.M, right: Spacing.M, top: Spacing.S, bottom: Spacing.S })
                .borderRadius(Radius.M)
            }
            .id('overshootHint')
            .width('100%')
            .justifyContent(FlexAlign.Center)
            .position({ x: 0, y: 76 })
            .hitTestBehavior(HitTestMode.None)
          }

          // 修(2026-06-03)— "拿反"提示:dist<30cm 但 yaw > 90° = 用户朝向反了(看框背面)。
          // 引导用户转向目标。条件:在 30cm 内 + |yaw| > 90° + 没 LOCKED。
          // 配合 native:snapReady 的 facingOk 守门 + ComputeFillRatio 的几何根治(dot<0→fill=0),
          // 让"拿反不再死循环吸附"。提示位置在过头提示下方(y=120,叠不上时分开)。
          if (this.ringPlaced && this.distance < 0.30 && Math.abs(this.yawDiffRad) > Math.PI / 2 && this.huntPhase !== 2) {
            Row() {
              Text('请转向目标方向')
                .fontSize(FontSize.hud)
                .fontColor('#FFFF9966')
                .fontWeight(FontWeight.Medium)
                .backgroundColor(Palette.glass)
                .padding({ left: Spacing.M, right: Spacing.M, top: Spacing.S, bottom: Spacing.S })
                .borderRadius(Radius.M)
            }
            .id('facingHint')
            .width('100%')
            .justifyContent(FlexAlign.Center)
            .position({ x: 0, y: 120 })
            .hitTestBehavior(HitTestMode.None)
          }

          // Stage 12C-redesign — ALIGNING HUD: 4 L-shaped corner brackets at the corners of an
          // 80vp focus box (the camera-style "you're framed here" mark), a faint 120vp reference
          // ring around them, and an iridescent center dot that translates with yaw/pitch diff.
          //   Off-aim    → brackets white60, dot has soft white glow.
          //   ±5° aligned → brackets switch to iridescent purple, dot glow brightens.
          // L bracket math: each corner = 1 horizontal 12×1.5 + 1 vertical 1.5×12 line whose
          // centers land at (±34, ±40) / (±40, ±34) on the 80×80 box — measured from the Stack
          // center via translate().
          // Phase 3 ④ — 两阶段时序:HUD 不再绑 huntPhase===1,而是绑 hudOpacity(吸附后 200ms 延迟淡入
          // 1,走出 30cm / 进 LOCKED 时 200ms 淡出 0)。视觉本身不改,只改"何时出"。
          if (this.hudOpacity > 0.01) {
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
                .shadow({ radius: this.isAngleAligned ? 12 : 6,
                          color: this.isAngleAligned ? '#CCFFFFFF' : '#80FFFFFF',
                          offsetX: 0, offsetY: 0 })
                .translate(this.dotTranslate())
            }
            .id('alignHud')
            .opacity(this.hudOpacity)
            .hitTestBehavior(HitTestMode.None)
          }

          // LOCKED banner:pearl 两行字玻璃胶囊。进 LOCKED 时显示 2s 后渐变淡出(showLockedBanner
          // 驱动 lockedBannerOpacity 1→0)。opacity≈0 时跳过渲染省 GPU。
          if (this.huntPhase === 2 && this.lockedBannerOpacity > 0.01) {
            Column({ space: 4 }) {
              Text('对齐成功')
                .fontSize(22)
                .fontColor(this.iridescentFlow(this.flowT))
                .fontWeight(FontWeight.Bold)
                .textShadow({ radius: 6, color: this.iridescentFlow(this.flowT) })
              Text('可点底部快门')
                .fontSize(14)
                .fontColor(Palette.white80)
            }
            .alignItems(HorizontalAlign.Center)
            .backgroundColor(Palette.glass)
            .padding({ left: Spacing.L, right: Spacing.L, top: Spacing.M, bottom: Spacing.M })
            .borderRadius(Radius.M)
            .opacity(this.lockedBannerOpacity)
            .hitTestBehavior(HitTestMode.None)
            .id('lockedBanner')
          }

          // Scale 校准引导:玻璃胶囊提示用户移动以完成 scale 校准。
          if (this.scaleCalStep > 0) {
            Text(this.scaleCalText)
              .fontSize(16)
              .fontColor(Color.White)
              .fontWeight(FontWeight.Medium)
              .backgroundColor(Palette.glass)
              .padding({ left: 20, right: 20, top: 10, bottom: 10 })
              .borderRadius(Radius.M)
              .opacity(this.scaleCalOpacity)
              .offset({ y: -80 })
              .hitTestBehavior(HitTestMode.None)
          }

          // v13 central zoom-value indicator. Pulses on every pinch / ZoomBar tap, auto-hides
          // 900ms after last input. Sits low in the viewport (offset from center +200vp) so it
          // floats just above the ZoomBar / shutter row in the chrome below — out of the way of
          // the AR composition the user is framing.
          if (this.zoomIndicatorVisible) {
            Text((Math.abs(this.zoom - Math.round(this.zoom)) < 0.05
                  ? Math.round(this.zoom).toString()
                  : this.zoom.toFixed(1)) + 'x')
              .fontSize(24)
              .fontColor(Color.White)
              .fontWeight(FontWeight.Bold)
              .textShadow({ radius: 8, color: 'rgba(0,0,0,0.5)' })
              .offset({ x: 0, y: 200 })
              .hitTestBehavior(HitTestMode.None)
          }
        }
        .width('100%')
        .height(this.viewportH())
        .position({ x: 0, y: this.viewportTop })
        .clip(true)
        .hitTestBehavior(HitTestMode.Transparent)
        // v13 pinch zoom. zoomBase captured at onActionStart; each onActionUpdate multiplies it by
        // e.scale and routes through applyZoom (clamp + native setZoom + central indicator pulse).
        .gesture(
          PinchGesture()
            .onActionStart(() => { this.zoomBase = this.zoom; })
            .onActionUpdate((e: GestureEvent) => { this.applyZoom(this.zoomBase * e.scale); })
        )

        // (3) Top black bar — opaque mask from page top down to viewportTop.
        Column()
          .width('100%')
          .height(this.viewportTop)
          .backgroundColor('#FF000000')
          .position({ x: 0, y: 0 })
          .hitTestBehavior(HitTestMode.None)

        // ===== 标定工具入口(屏右上角,完全旁路) =====
        Text('🛠 标定')
          .fontSize(11)
          .fontColor(Palette.white)
          .backgroundColor('#A0FF6600')
          .borderRadius(8)
          .padding({ left: 8, right: 8, top: 4, bottom: 4 })
          .position({ x: 8, y: 8 })
          .onClick(() => {
            this.calibMode = true;
            this.calibStatus = `已进入标定模式 - ${this.calibOrientationLabel} - group ${this.calibGroupIdx} - 请采 A`;
          })

        // ===== 后端切换入口(标定按钮右边) =====
        Text(getBackendLabel(this.backendType))
          .fontSize(11)
          .fontColor(Palette.white)
          .backgroundColor(this.backendType === 'da3' ? '#A0336699' : '#A0996633')
          .borderRadius(8)
          .padding({ left: 8, right: 8, top: 4, bottom: 4 })
          .position({ x: 80, y: 8 })
          .onClick(() => { this.cycleBackend(); })

        // ===== 标定 overlay(条件渲染,屏幕居中)=====
        if (this.calibMode) {
          Column({ space: 14 }) {
            // 标题:朝向 + group 进度
            Text(`标定 · ${this.calibOrientationLabel} · group ${this.calibGroupIdx}`)
              .fontSize(13).fontColor('#FFFFCC00').fontWeight(FontWeight.Bold)
            // 进度 + A 状态
            Text(this.calibA_jpeg !== undefined
                 ? `✓ 基准 A 已采(无需再采)   step ${this.calibStep} / 10`
                 : `✗ 请先点【采基准 A】   step ${this.calibStep} / 10`)
              .fontSize(12)
              .fontColor(this.calibA_jpeg !== undefined ? '#FF66FF66' : '#FFFFAA66')
            // 动作提示(大字)
            Text(`📍 ${this.kCalibSteps[this.calibStep]}`)
              .fontSize(18).fontColor(Palette.white).fontWeight(FontWeight.Bold)
              .textAlign(TextAlign.Center).width('100%')
            // 状态反馈
            Text(this.calibStatus)
              .fontSize(11).fontColor('#FFCCCCCC').textAlign(TextAlign.Center).width('100%')
            // 主操作按钮(大,带视觉权重区分)
            Row({ space: 12 }) {
              Text(this.calibA_jpeg !== undefined ? '✓ A 已采' : '采基准 A')
                .fontSize(14).fontColor(Palette.white)
                .backgroundColor(this.calibA_jpeg !== undefined ? '#60004400' : '#FF009900')
                .borderRadius(10).padding({ left: 14, right: 14, top: 10, bottom: 10 })
                .onClick(() => { this.calibCaptureA(); })
              Text('采当前 B + da3')
                .fontSize(14).fontColor(Palette.white).fontWeight(FontWeight.Bold)
                .backgroundColor('#FF0066CC')
                .borderRadius(10).padding({ left: 14, right: 14, top: 10, bottom: 10 })
                .onClick(() => { this.calibCaptureBandRun(); })
            }
            // 次操作按钮(小)
            Row({ space: 8 }) {
              Text('切换横竖')
                .fontSize(11).fontColor(Palette.white).backgroundColor('#A0660099')
                .borderRadius(8).padding({ left: 10, right: 10, top: 6, bottom: 6 })
                .onClick(() => { this.calibToggleOrientation(); })
              Text('新会话')
                .fontSize(11).fontColor(Palette.white).backgroundColor('#A0996600')
                .borderRadius(8).padding({ left: 10, right: 10, top: 6, bottom: 6 })
                .onClick(() => { this.calibStartNewGroup(); })
              Text('退出标定')
                .fontSize(11).fontColor(Palette.white).backgroundColor('#A0990000')
                .borderRadius(8).padding({ left: 10, right: 10, top: 6, bottom: 6 })
                .onClick(() => { this.calibMode = false; })
            }
          }
          .padding(16)
          .backgroundColor('#E8000000')
          .borderRadius(12)
          .alignItems(HorizontalAlign.Center)
          .width('86%')
          .position({ x: '7%', y: '30%' })
        }

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
            refHolder: this.refHolder,
            loading: this.refLoading,
            orientation: this.orientation,
            onToggle: (): void => { this.toggleCard(); },
            onImageTap: (): void => { this.onTargetTap(); }
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
            orientation: this.orientation,
            onSphereTap: (): void => { this.onBeaconTap(); },
            onSettingsLongPress: (): void => { this.openDevSelector(); },
            onShutterTap: (): void => { this.onCapturePhoto(); },
            currentZoom: this.zoom,
            onZoomChange: (z: number): void => { this.applyZoom(z); }
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

        // 拍照黑闪 — 最顶层全屏黑色,opacity 由 triggerShutterFlash 驱动 1→淡出 0(总 230ms)。
        // hitTestBehavior None 让闪现期间下层 UI 仍可点击;opacity≈0 时跳过渲染。
        if (this.shutterFlashOpacity > 0.01) {
          Column()
            .id('shutterFlash')
            .width('100%')
            .height('100%')
            .backgroundColor('#FF000000')
            .opacity(this.shutterFlashOpacity)
            .hitTestBehavior(HitTestMode.None)
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
        // Phase 2 — 布局变化时 push 可见区 NDC y 给 native(框居中 + fillRatio 判据)。
        this.pushVisibleNdcY();
      })
    }
    .expandSafeArea([SafeAreaType.SYSTEM], [SafeAreaEdge.BOTTOM])
    .onAppear(() => {
      arEngineDemo.init(this.resMgr);
      let config: Int32Array = new Int32Array([1, this.rotation]);
      arEngineDemo.start(this.idStr, config);
      // 产品最终要求:UI 锁竖屏,AR 感知物理横竖(用 camRoll snap)。
      // 离开页面 onWillDisappear 还原 UNSPECIFIED。
      try {
        window.getLastWindow(this.context, (err: BusinessError, win: window.Window) => {
          if (err.code) {
            return;
          }
          win.setPreferredOrientation(window.Orientation.PORTRAIT, (_err2: BusinessError) => {});
        });
      } catch (_e) {
      }
      // 锁竖后 display.on('change') 理论上不会触发(系统不再转屏)。但万一仍触发,
      // 不要把 rotation 喂进 native——保持 mDisplayRotation=0,让 GetDisplayOrientedPose
      // 始终按"竖屏"校正,camRoll 才会真实反映物理姿态(横拿→±90°)。
      try {
        display.on('change', (id: number) => {
          const r: number = display.getDefaultDisplaySync().rotation;
          // DISABLED: 锁竖屏下保持 native mDisplayRotation=0,让 camRoll 暴露物理横竖。
          // if (r !== this.rotation) {
          //   this.rotation = r;
          //   arEngineDemo.setDisplayRotation(this.idStr, r);
          // }
        });
      } catch (_e) {
      }
    })
    .onWillDisappear(() => {
      try {
        display.off('change');
      } catch (_e) {
      }
      // 还原朝向 UNSPECIFIED,其他页面不受 AUTO_ROTATION 污染。
      try {
        window.getLastWindow(this.context, (err: BusinessError, win: window.Window) => {
          if (err.code) {
            return;
          }
          win.setPreferredOrientation(window.Orientation.UNSPECIFIED, () => {});
        });
      } catch (_e) {
      }
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
      // 恢复上次持久化的后端选择（preferences 异步读，完成后 UI 自动刷新）
      initBackendType(this.context).then(() => {
        this.backendType = getBackendType();
      });
    })
    .backgroundColor(Color.Black)
    .hideTitleBar(true)
    .hideBackButton(true)
    .hideToolBar(true)
  }
}
