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
              if (s.finishState === 2 && this.prevFinishState !== 2) {
                this.triggerVibration();
              }
              this.prevFinishState = s.finishState;
              this.finishState = s.finishState;
            }, 62)
          })
          .onDestroy(() => {
            Logger.info(`XComponent onDestroy ${this.idStr}.`);
            clearInterval(this.interval);
            clearInterval(this.stateTimer);
          })

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
