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
  color: string; // 6-digit '#RRGGBB' locked at birth
}

@Builder
export function ARRingHuntBuilder() {
  ARRingHunt();
}

// Stage 11A: ARRingHunt is a Wayfinder beacon. The native layer draws the whole beacon (ground ring
// + ripple + light pillar + camera-facing badge with phone icon) on the placed anchor, tinted
// red->green by distance. The ArkTS UI: place/reset buttons + a live distance readout, plus the
// off-screen guidance overlay (edge-pinned droplet arrow + breathing halo + particle trail + a ↻
// glyph when the beacon is behind you) — it points at the beacon TOP whenever it's off-screen.
@Component
struct ARRingHunt {
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

  // Off-screen target guidance.
  @State isTargetInView: boolean = true;
  @State screenEdgeX: number = 0.5;
  @State screenEdgeY: number = 0.5;
  @State isBehind: boolean = false;
  @State indicatorAngleDeg: number = 0;
  @State ndcX: number = 0;
  @State ndcY: number = 0;
  @State guidanceMounted: boolean = false; // controls Visibility (kept true during fade-out)
  @State guidanceOpacity: number = 0;       // animated 0..1 for the 0.3s fade
  private guidanceShown: boolean = false;   // logical desired-visible state

  // The guidance overlay Stack's own size in vp (from its onAreaChange). This is the coordinate
  // frame .position() uses, so all guidance positioning scales by it.
  @State guidanceStackW: number = 0;
  @State guidanceStackH: number = 0;

  // Breathing halo (scale-pulsed) + a particle trail flowing center->droplet.
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

  private distText(): string {
    return this.distance.toFixed(2);
  }

  // ---- Off-screen guidance ----

  // The guidance overlay Stack's own size in vp (measured via its onAreaChange). Fallback values
  // cover the first frame before layout. This is the coordinate frame .position() resolves in.
  private getStackWidth(): number {
    return this.guidanceStackW > 0 ? this.guidanceStackW : 360;
  }

  private getStackHeight(): number {
    return this.guidanceStackH > 0 ? this.guidanceStackH : 800;
  }

  // Guidance only shows when the beacon is off-screen (you've turned away), where the beacon reads
  // red — so the droplet/halo/particles are fixed red.
  private guidanceColor(): string {
    return '#FFE63946';
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

  // Infinite alternating pulse 0.95 <-> 1.10. Gentle amplitude so the breathing ring never shrinks
  // below the droplet. Restarted on bucket change.
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

  // Particle color follows the (fixed red) guidance: 6-digit hex (Canvas uses globalAlpha to fade).
  private particleRgb(): string {
    return '#E63946';
  }

  // Emit one particle from screen center toward the droplet's current center.
  private spawnParticle(): void {
    // No particles when the target is behind you (a direction-to-target trail is meaningless).
    if (!this.guidanceShown || this.distance < 0.30 || this.isBehind) {
      return;
    }
    let p: Particle = {
      id: this.nextParticleId,
      birthMs: Date.now(),
      lifeMs: 1000,
      startX: this.getStackWidth() / 2, // stack center = the crosshair
      startY: this.getStackHeight() / 2,
      targetX: this.dropletCenterX(),
      targetY: this.dropletCenterY(),
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

  // Stop emitting. clearNow=true wipes in-flight particles immediately (< 0.30m / reset);
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

  // ---- Droplet position: screen-space ray from center -> projected target ----
  // Native NDC is square-normalized, while the phone screen is usually tall. Convert the projected
  // target into this overlay's real screen coordinates before deriving the ray angle; otherwise a
  // diagonal target on a portrait screen appears 10-20 degrees off.
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

  // Smallest positive t at which center + t*dir hits the 80vp inset rectangle.
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
      return this.getStackHeight() - 200; // behind: cluster center bottom-center, 200vp from bottom
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

  // Reconcile guidance visibility with a 0.3s opacity transition. Very close (< 0.30m, the beacon
  // is in your face) is suppressed to avoid near-clip projection jitter.
  private reconcileGuidance(): void {
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
        if (this.isBehind) {
          this.particles = []; // behind: drop any in-flight particles (none spawn either)
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
      // < 0.30m: wipe instantly (beacon is in your face). Re-entered view: let the trail drain.
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

        // Particle trail layer: a full-screen, non-interactive Canvas BELOW the droplet. Particles
        // stream from screen center to the droplet. Drawn imperatively at 60fps.
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

        // Off-screen target guidance: full-screen, non-interactive overlay that pins a red DROPLET
        // arrow (with a breathing halo + distance-feel glow) to the screen edge nearest the
        // off-screen beacon. Explicit TopStart anchor, so the cluster's .position() places its
        // top-left at exactly (getDropletX, getDropletY) in this Stack's own coordinate frame.
        Stack() {
          if (this.guidanceMounted) {
            // Unified cluster: halos + center content. Front -> droplet (points at target); behind
            // -> a ↻ glyph (same halos, just the center swaps). Position: edge vs bottom-center.
            Stack({ alignContent: Alignment.Center }) {
              // Bottom: distance-feel glow halo (strokeWidth/opacity by distance).
              Circle()
                .width(124)
                .height(124)
                .fillOpacity(0)
                .stroke(this.guidanceColor())
                .strokeWidth(this.glowThickness())
                .opacity(this.glowOpacity())
              // Middle: breathing halo — pulses 0.95<->1.10.
              Circle()
                .width(108)
                .height(108)
                .fillOpacity(0)
                .stroke(this.guidanceColor())
                .strokeWidth(3)
                .opacity(0.5)
                .scale({ x: this.breathScale, y: this.breathScale })
              // Center content:
              if (!this.isBehind) {
                // Droplet as an svg Image (viewBox 0..120) — Image scales/centers svg reliably,
                // unlike Shape+viewPort here. Centered by the cluster Stack; rotates about its own
                // center, in place, to the bearing toward the beacon.
                Image($r('app.media.droplet'))
                  .width(60)
                  .height(60)
                  .objectFit(ImageFit.Contain)
                  .rotate({ x: 0, y: 0, z: 1, angle: this.dropletRotationDeg(), centerX: '50%', centerY: '50%' })
              } else {
                // Behind: a rotate glyph in the halo center (no droplet, no caption).
                Text('↻')
                  .fontColor('#FFFFFF')
                  .fontSize(56)
                  .fontWeight(FontWeight.Bold)
              }
            }
            .width(160)
            .height(160)
            .opacity(this.guidanceOpacity)
            .position({ x: this.getDropletX(), y: this.getDropletY() })
          }
        }
        .id('guidance')
        .width('100%')
        .height('100%')
        .hitTestBehavior(HitTestMode.None)
        .alignContent(Alignment.TopStart)
        .alignRules({
          top: { anchor: '__container__', align: VerticalAlign.Top },
          left: { anchor: '__container__', align: HorizontalAlign.Start }
        })
        .onAreaChange((_o: Area, n: Area) => {
          this.guidanceStackW = n.width as number;
          this.guidanceStackH = n.height as number;
        })

        // Bottom HUD: live distance + place/reset buttons.
        Column({ space: 8 }) {
          if (this.ringPlaced) {
            Text(`距离 ${this.distText()}m`)
              .fontSize(18)
              .fontColor(Color.White)
              .backgroundColor('#80000000')
              .padding({ left: 10, right: 10, top: 4, bottom: 4 })
              .borderRadius(6)
          }
          Row({ space: 10 }) {
            Button('放置信标')
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
                this.distance = 99;
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
