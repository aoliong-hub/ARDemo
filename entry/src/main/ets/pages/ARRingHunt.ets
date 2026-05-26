/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { display } from '@kit.ArkUI';
import { resourceManager } from '@kit.LocalizationKit';
import { systemDateTime } from '@kit.BasicServicesKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { vibrator } from '@kit.SensorServiceKit';
import arEngineDemo from 'libentry.so';
import { RingState } from 'libentry.so';
import { Logger } from '../utils/Logger';

// A single trail particle flying from screen center to the droplet, fading as it goes.
interface Particle {
  id: number;
  birthMs: number;
  lifeMs: number;
  startX: number;
  startY: number;
  targetX: number;
  targetY: number;
  color: string; // 6-digit '#RRGGBB' locked at birth (follows the ring color)
}

@Builder
export function ARRingHuntBuilder() {
  ARRingHunt();
}

@Component
struct ARRingHunt {
  pageInfos: NavPathStack = new NavPathStack();
  private interval: number = -1;
  private stateTimer: number = -1;
  private prevFinishState: number = 0;
  private xComponentId: string = 'ARRingHunt';
  @State context: Context = this.getUIContext().getHostContext() as Context;
  private resMgr: resourceManager.ResourceManager = this.context.resourceManager;
  @State rotation: number = display.getDefaultDisplaySync().rotation;
  private idStr: string = systemDateTime.getTime(false).toString() + this.xComponentId;

  @State distance: number = 99;
  @State angleRad: number = Math.PI;
  @State yawDiffRad: number = 0;
  @State pitchDiffRad: number = 0;
  @State finishState: number = 0;
  @State foundSec: number = 0;
  @State ringPlaced: boolean = false;

  // Stage 10: off-screen target guidance.
  @State ringColor: string = 'red';
  @State isTargetInView: boolean = true;
  @State screenEdgeX: number = 0.5;
  @State screenEdgeY: number = 0.5;
  @State isBehind: boolean = false;
  @State indicatorAngleDeg: number = 0;
  @State guidanceMounted: boolean = false; // controls Visibility (kept true during fade-out)
  @State guidanceOpacity: number = 0;       // animated 0..1 for the 0.3s fade
  private guidanceShown: boolean = false;   // logical desired-visible state

  // Stage 10 UX upgrade: breathing halo (scale-pulsed) + a particle trail flowing center->droplet.
  @State breathScale: number = 1.0;
  private breathDurationMs: number = 0;     // current breathing period; restart on distance-bucket change
  private distanceBucket: number = -1;      // 0:<1m 1:<3m 2:>=3m, drives breath+spawn speed

  // Particle trail. Drawn imperatively on a Canvas at ~60fps, so `particles` is a plain field
  // (no @State) to avoid re-running build() every frame.
  private canvasSettings: RenderingContextSettings = new RenderingContextSettings(true);
  private canvasCtx: CanvasRenderingContext2D = new CanvasRenderingContext2D(this.canvasSettings);
  private canvasReady: boolean = false;
  private particles: Particle[] = [];
  private nextParticleId: number = 0;
  private spawnTimer: number = -1;
  private renderTimer: number = -1;

  private clamp(v: number, lo: number, hi: number): number {
    if (v < lo) {
      return lo;
    }
    if (v > hi) {
      return hi;
    }
    return v;
  }

  private isProximityMode(): boolean {
    return this.ringPlaced && this.distance < 0.15;
  }

  private isAligned(): boolean {
    // "Aligned" (green) only counts once you are close (proximity) AND within angle. A distant
    // lucky angle keeps the reticle white. (Per user feedback.)
    return this.isProximityMode() && this.angleRad < (5 * Math.PI / 180);
  }

  private reticleColor(): string {
    return this.isAligned() ? '#FF00E676' : '#CCFFFFFF';
  }

  private aimOffsetX(): number {
    let yawDeg: number = this.yawDiffRad * 180 / Math.PI;
    return this.clamp(yawDeg / 30.0, -1, 1) * 120;
  }

  private aimOffsetY(): number {
    // ArkUI translate Y is positive downward; target above (pitch > 0) -> aim circle up -> negate.
    let pitchDeg: number = this.pitchDiffRad * 180 / Math.PI;
    return -this.clamp(pitchDeg / 30.0, -1, 1) * 120;
  }

  private distText(): string {
    return this.distance.toFixed(2);
  }

  private angleText(): string {
    return (this.angleRad * 180 / Math.PI).toFixed(1);
  }

  private distProgress(): number {
    return (1.0 - Math.min(this.distance / 2.0, 1.0)) * 100;
  }

  private angleProgress(): number {
    return (1.0 - Math.min(this.angleRad / Math.PI, 1.0)) * 100;
  }

  private triggerVibration(): void {
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

  // ---- Stage 10: off-screen guidance ----

  private dispWidthVp(): number {
    return this.getUIContext().px2vp(display.getDefaultDisplaySync().width);
  }

  private dispHeightVp(): number {
    return this.getUIContext().px2vp(display.getDefaultDisplaySync().height);
  }

  // Guidance follows the RING color (distance feedback): green when on target, else red.
  private guidanceColor(): string {
    return this.ringColor === 'green' ? '#FF00E676' : '#FFE63946';
  }

  // Distance-feel halo: near = thick+saturated, far = thin+dim.
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

  // Distance bucket drives both the breathing period and the particle spawn rate (closer = faster).
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

  // Infinite alternating pulse 0.95 <-> 1.10. Gentle amplitude so the breathing ring never
  // shrinks below the ~92vp droplet (a wider pulse made the tip poke out). Restarted on bucket change.
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

  // A duration-0 animation cancels the running infinite pulse and parks the scale at rest.
  private stopBreathing(): void {
    this.breathDurationMs = 0;
    this.getUIContext().animateTo({ duration: 0 }, () => {
      this.breathScale = 1.0;
    });
  }

  // Spawn cadence: closer = denser stream.
  private spawnIntervalMs(): number {
    if (this.distance < 1.0) {
      return 50;
    }
    if (this.distance < 3.0) {
      return 150;
    }
    return 300;
  }

  // Particle color follows the ring: 6-digit hex (Canvas uses globalAlpha for the fade).
  private particleRgb(): string {
    return this.ringColor === 'green' ? '#00E676' : '#E63946';
  }

  // Emit one particle from screen center toward the droplet's current center.
  private spawnParticle(): void {
    if (!this.guidanceShown || this.distance < 0.30) {
      return;
    }
    let p: Particle = {
      id: this.nextParticleId,
      birthMs: Date.now(),
      lifeMs: 1000,
      startX: this.dispWidthVp() / 2,
      startY: this.dispHeightVp() / 2,
      targetX: this.guidanceCenterX(),
      targetY: this.guidanceCenterY(),
      color: this.particleRgb()
    };
    this.nextParticleId = this.nextParticleId + 1;
    this.particles.push(p);
  }

  // ~60fps draw loop: lerp each particle center->target, fade alpha/size, drop the dead ones.
  // Self-terminates (and clears the canvas) once spawning has stopped and no particles remain.
  private renderParticles(): void {
    if (!this.canvasReady) {
      return;
    }
    let ctx: CanvasRenderingContext2D = this.canvasCtx;
    let w: number = this.dispWidthVp();
    let h: number = this.dispHeightVp();
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

  // Stop emitting. clearNow=true wipes in-flight particles immediately (< 0.30m / FOUND);
  // otherwise the render loop drains the remaining particles, then self-stops.
  private stopParticles(clearNow: boolean): void {
    if (this.spawnTimer !== -1) {
      clearInterval(this.spawnTimer);
      this.spawnTimer = -1;
    }
    if (clearNow) {
      this.particles = [];
    }
  }

  // Center of the droplet (the 95vp halo cluster shares it). This is also the particle target.
  // Directly-behind -> bottom center, raised enough to fit the ↻ + caption below; otherwise the
  // screen-edge point nearest the target, clamped so the 95vp halo stays on screen (47.5 margin).
  private guidanceCenterX(): number {
    let w: number = this.dispWidthVp();
    if (this.isBehind) {
      return w / 2;
    }
    return this.clamp(this.screenEdgeX * w, 80, w - 80);
  }

  private guidanceCenterY(): number {
    let h: number = this.dispHeightVp();
    if (this.isBehind) {
      return h - 240; // cluster top-left at h-320, +80 to its center
    }
    return this.clamp(this.screenEdgeY * h, 80, h - 80);
  }

  // Top-left of the 160vp cluster (and, when behind, of the wrapping Column).
  private clusterX(): number {
    return this.guidanceCenterX() - 80;
  }

  private clusterY(): number {
    return this.guidanceCenterY() - 80;
  }

  // Droplet tip points up at 0deg; rotate to the target bearing (12 o'clock = 0, clockwise).
  // Directly-behind -> point straight down (180), paired with the turn-around cue below it.
  private dropletRotationDeg(): number {
    return this.isBehind ? 180 : this.indicatorAngleDeg;
  }

  // Reconcile guidance visibility with a 0.3s opacity transition. FOUND hides immediately;
  // very close (< 0.30m, ring in your face) is suppressed to avoid near-clip projection jitter.
  private reconcileGuidance(): void {
    if (this.finishState === 2) {
      if (this.guidanceMounted || this.guidanceShown) {
        this.guidanceShown = false;
        this.guidanceOpacity = 0;
        this.guidanceMounted = false;
        this.stopBreathing();
        this.stopParticles(true);
      }
      return;
    }
    let shouldShow: boolean =
      this.ringPlaced && !this.isTargetInView && this.distance >= 0.30;
    if (shouldShow === this.guidanceShown) {
      // Already in the right state: keep breathing/particle speed in sync with distance.
      if (shouldShow) {
        let bucket: number = this.distBucket();
        if (bucket !== this.distanceBucket) {
          this.distanceBucket = bucket;
          this.startBreathing();
          this.restartSpawnTimer();
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
      // < 0.30m: wipe instantly (ring is in your face). Re-entered view: let the trail drain.
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
    this.ringColor = 'red';
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
      RelativeContainer() {
        XComponent({ id: this.idStr, type: XComponentType.SURFACE, libraryname: 'entry' })
          .id('xc')
          .width('100%')
          .height('100%')
          .alignRules({
            center: { anchor: '__container__', align: VerticalAlign.Center },
            middle: { anchor: '__container__', align: HorizontalAlign.Center }
          })
          .onLoad(() => {
            Logger.info(`XComponent onLoad ${this.idStr}.`);
            this.interval = setInterval(() => {
              arEngineDemo.update(this.idStr);
            }, 33)
            this.stateTimer = setInterval(() => {
              // Freeze UI once FOUND (locked until reset).
              if (this.finishState === 2) {
                return;
              }
              let s: RingState = arEngineDemo.getRingState(this.idStr);
              this.distance = s.distance;
              this.angleRad = s.angleRad;
              this.yawDiffRad = s.yawDiffRad;
              this.pitchDiffRad = s.pitchDiffRad;
              this.foundSec = s.foundSec;
              this.ringColor = s.ringColor;
              this.isTargetInView = s.isTargetInView;
              this.screenEdgeX = s.screenEdgeX;
              this.screenEdgeY = s.screenEdgeY;
              this.isBehind = s.isBehind;
              this.indicatorAngleDeg = s.indicatorAngleDeg;
              if (s.finishState === 2 && this.prevFinishState !== 2) {
                this.triggerVibration();
              }
              this.prevFinishState = s.finishState;
              this.finishState = s.finishState;
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

        // Particle trail layer (UX upgrade): a full-screen, non-interactive Canvas BELOW the
        // droplet. Particles stream from screen center to the droplet. Drawn imperatively at 60fps.
        Canvas(this.canvasCtx)
          .id('particles')
          .width('100%')
          .height('100%')
          .hitTestBehavior(HitTestMode.None)
          .onReady(() => {
            this.canvasReady = true;
          })
          .alignRules({
            top: { anchor: '__container__', align: VerticalAlign.Top },
            left: { anchor: '__container__', align: HorizontalAlign.Start }
          })

        // Off-screen target guidance (Stage 10 + UX upgrade). Full-screen, non-interactive overlay
        // that pins a ring-colored DROPLET arrow (with a breathing halo + distance-feel glow) to
        // the screen edge nearest the off-screen ring. Placed under the HUD so the buttons stay
        // clickable. Mounted only while guiding; opacity is animated for the 0.3s fade.
        if (this.guidanceMounted) {
          Stack() {
            // A single Column holds the droplet cluster and (when behind) the turn-around cue, so
            // the whole group is positioned/centered as one. The cluster's 4 elements are centered
            // on each other by the inner Stack(Center) — droplet is the main body, halos surround.
            Column({ space: 8 }) {
              Stack({ alignContent: Alignment.Center }) {
                // Bottom: distance-feel glow halo (strokeWidth/opacity by distance). Thin accent
                // just outside the breathing ring; the ~92vp droplet always sits inside it.
                Circle()
                  .width(124)
                  .height(124)
                  .fillOpacity(0)
                  .stroke(this.guidanceColor())
                  .strokeWidth(this.glowThickness())
                  .opacity(this.glowOpacity())
                // Middle: breathing halo — ring-colored stroke pulsing 0.95<->1.10 (103<->119vp),
                // always larger than the droplet so the tip never pokes out.
                Circle()
                  .width(108)
                  .height(108)
                  .fillOpacity(0)
                  .stroke(this.guidanceColor())
                  .strokeWidth(3)
                  .opacity(0.5)
                  .scale({ x: this.breathScale, y: this.breathScale })
                // Top: the droplet (main body, ~120vp). Wrapped in a Shape so its viewPort (0..120)
                // scales SVG-style to a true 120vp box on any density (Path.commands are px, NOT
                // scaled by width/height). Geometric center (60,60); tip up at 0deg, rotated to bearing.
                Shape() {
                  Path()
                    .commands('M 60 14 C 38 14 21 38 21 55 C 21 82 38 99 60 106 C 82 99 99 82 99 55 C 99 38 82 14 60 14 Z')
                    .fill(this.guidanceColor())
                    .stroke('#FFFFFF')
                    .strokeWidth(2.5)
                }
                .viewPort({ x: 0, y: 0, width: 120, height: 120 })
                .width(120)
                .height(120)
                .rotate({ x: 0, y: 0, z: 1, angle: this.dropletRotationDeg(), centerX: '50%', centerY: '50%' })
              }
              .width(160)
              .height(160)
              // Behind: rotate glyph + caption, stacked under the bottom-center droplet.
              if (this.isBehind) {
                Text('↻')
                  .fontColor('#FFFFFF')
                  .fontSize(48)
                Text('向后转')
                  .fontColor('#FFFFFF')
                  .fontSize(20)
                  .backgroundColor('#00000080')
                  .padding({ left: 12, right: 12, top: 4, bottom: 4 })
                  .borderRadius(8)
              }
            }
            .width(160)
            .alignItems(HorizontalAlign.Center)
            .position({ x: this.clusterX(), y: this.clusterY() })
          }
          .id('guidance')
          .width('100%')
          .height('100%')
          .opacity(this.guidanceOpacity)
          .hitTestBehavior(HitTestMode.Transparent)
          .alignRules({
            top: { anchor: '__container__', align: VerticalAlign.Top },
            left: { anchor: '__container__', align: HorizontalAlign.Start }
          })
        }

        // Center crosshair (always shown). White / green by angle.
        Stack() {
          Row().width(48).height(3).backgroundColor(this.reticleColor())
          Column().width(3).height(48).backgroundColor(this.reticleColor())
        }
        .id('crosshair')
        .alignRules({
          center: { anchor: '__container__', align: VerticalAlign.Center },
          middle: { anchor: '__container__', align: HorizontalAlign.Center }
        })

        // 2D dual-ring aiming HUD, only in proximity mode (distance < 30cm).
        if (this.isProximityMode() && this.finishState !== 2) {
          Stack({ alignContent: Alignment.Center }) {
            // Fixed middle ring.
            Circle({ width: 80, height: 80 })
              .fillOpacity(0)
              .stroke(this.reticleColor())
              .strokeWidth(3)
              .opacity(0.85)
            // Aim ring, offset by yaw/pitch.
            Circle({ width: 50, height: 50 })
              .fillOpacity(0)
              .stroke(this.reticleColor())
              .strokeWidth(3)
              .translate({ x: this.aimOffsetX(), y: this.aimOffsetY() })
          }
          .id('dualRing')
          .width(300)
          .height(300)
          .alignRules({
            center: { anchor: '__container__', align: VerticalAlign.Center },
            middle: { anchor: '__container__', align: HorizontalAlign.Center }
          })
        }

        // FOUND banner.
        if (this.finishState === 2) {
          Column() {
            Text('FOUND')
              .fontSize(64)
              .fontColor(Color.White)
              .fontWeight(FontWeight.Bold)
            Text(`in ${this.foundSec.toFixed(1)}s`)
              .fontSize(32)
              .fontColor(Color.White)
          }
          .id('foundBanner')
          .padding({ left: 24, right: 24, top: 16, bottom: 16 })
          .backgroundColor('#A0000000')
          .borderRadius(12)
          .alignRules({
            center: { anchor: '__container__', align: VerticalAlign.Center },
            middle: { anchor: '__container__', align: HorizontalAlign.Center }
          })
        }

        // Bottom HUD.
        Column({ space: 8 }) {
          Progress({ value: this.distProgress(), total: 100, type: ProgressType.Linear })
            .width('100%')
            .height(6)
            .color('#2196F3') // blue = distance
            .backgroundColor('#80AAAAAA')
          Progress({ value: this.angleProgress(), total: 100, type: ProgressType.Linear })
            .width('100%')
            .height(6)
            .color('#9C27B0') // purple = angle
            .backgroundColor('#80AAAAAA')
            .visibility(this.isProximityMode() ? Visibility.None : Visibility.Visible)
          Text(`距离 ${this.distText()}m  |  角度 ${this.angleText()}°`)
            .fontSize(18)
            .fontColor(Color.White)
            .backgroundColor('#80000000')
            .padding({ left: 10, right: 10, top: 4, bottom: 4 })
            .borderRadius(6)
          Row({ space: 10 }) {
            Button('放置圆环')
              .layoutWeight(1)
              .onClick(() => {
                let id: number = arEngineDemo.placeRing(this.idStr);
                Logger.info(`[RingHunt] placeRing -> ${id}`);
                if (id < 0) {
                  try {
                    this.getUIContext().getPromptAction().showToast({ message: '请稳住手机等待跟踪稳定', duration: 1500 });
                  } catch (e) {
                    let err: BusinessError = e as BusinessError;
                    Logger.warn(`toast failed: ${err.message}`);
                  }
                } else {
                  this.ringPlaced = true;
                }
              })
            Button('重置')
              .layoutWeight(1)
              .onClick(() => {
                arEngineDemo.resetRing(this.idStr);
                this.prevFinishState = 0;
                this.finishState = 0;
                this.distance = 99;
                this.angleRad = Math.PI;
                this.ringPlaced = false;
                this.resetGuidance();
                Logger.info(`[RingHunt] resetRing`);
              })
          }
          .width('100%')
        }
        .id('hud')
        .width('92%')
        .alignRules({
          bottom: { anchor: '__container__', align: VerticalAlign.Bottom },
          middle: { anchor: '__container__', align: HorizontalAlign.Center }
        })
        .margin({ bottom: 40 })
      }
    }
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
    })
    .onHidden(() => {
      arEngineDemo.hide(this.idStr);
    })
    .onReady((context: NavDestinationContext) => {
      this.pageInfos = context.pathStack;
    })
    .hideTitleBar(true)
    .hideBackButton(true)
    .hideToolBar(true)
  }
}
