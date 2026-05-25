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
import { AlignmentState } from 'libentry.so';
import { Logger } from '../utils/Logger';

@Builder
export function ARArrowAlignBuilder() {
  ARArrowAlign();
}

@Component
struct ARArrowAlign {
  pageInfos: NavPathStack = new NavPathStack();
  private interval: number = -1;     // frame driver (update)
  private stateTimer: number = -1;   // 16fps alignment-state poll
  private prevAligned: boolean = false; // edge detection for vibration
  private xComponentId: string = 'ARArrowAlign';
  @State context: Context = this.getUIContext().getHostContext() as Context;
  private resMgr: resourceManager.ResourceManager = this.context.resourceManager;
  @State rotation: number = display.getDefaultDisplaySync().rotation;
  private idStr: string = systemDateTime.getTime(false).toString() + this.xComponentId;

  @State angleRad: number = Math.PI;
  @State isAligned: boolean = false;
  @State ready: boolean = false;
  @State targetPlaced: boolean = false;

  private degText(): string {
    return (this.angleRad * 180 / Math.PI).toFixed(1);
  }

  private progressValue(): number {
    let p: number = 1.0 - this.angleRad / Math.PI; // 0 rad -> 1, pi -> 0
    if (p < 0) {
      p = 0;
    }
    return p * 100;
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
            }, 33) // 30 fps frame driver
            this.stateTimer = setInterval(() => {
              let s: AlignmentState = arEngineDemo.getAlignmentState(this.idStr);
              this.angleRad = s.angleRad;
              this.isAligned = s.isAligned;
              this.ready = s.ready;
              this.targetPlaced = s.targetPlaced;
              // Vibrate once on the false -> true alignment edge.
              if (this.isAligned && !this.prevAligned) {
                this.triggerVibration();
              }
              this.prevAligned = this.isAligned;
            }, 62) // ~16 fps UI poll
          })
          .onDestroy(() => {
            Logger.info(`XComponent onDestroy ${this.idStr}.`);
            clearInterval(this.interval);
            clearInterval(this.stateTimer);
          })

        // Center crosshair = where the phone camera points. White normally, green when aligned.
        Stack() {
          Row()
            .width(48)
            .height(3)
            .backgroundColor(this.isAligned ? '#FF00E676' : '#CCFFFFFF')
          Column()
            .width(3)
            .height(48)
            .backgroundColor(this.isAligned ? '#FF00E676' : '#CCFFFFFF')
        }
        .id('crosshair')
        .alignRules({
          center: { anchor: '__container__', align: VerticalAlign.Center },
          middle: { anchor: '__container__', align: HorizontalAlign.Center }
        })

        // Bottom HUD: difference text + progress + two buttons.
        Column({ space: 10 }) {
          Text(`差距:${this.degText()}°`)
            .fontSize(20)
            .fontColor(Color.White)
            .backgroundColor('#80000000')
            .padding({ left: 10, right: 10, top: 4, bottom: 4 })
            .borderRadius(6)
          Progress({ value: this.progressValue(), total: 100, type: ProgressType.Linear })
            .width('86%')
            .color(Color.Green)
            .backgroundColor('#80AAAAAA')
          Row({ space: 10 }) {
            Button('放置目标')
              .layoutWeight(1)
              .enabled(this.ready)
              .onClick(() => {
                let id: number = arEngineDemo.placeTargetArrow(this.idStr);
                Logger.info(`[ArrowAlign] placeTargetArrow -> ${id}`);
                if (id < 0) {
                  try {
                    this.getUIContext().getPromptAction().showToast({ message: '请稳住手机等待跟踪稳定', duration: 1500 });
                  } catch (e) {
                    let err: BusinessError = e as BusinessError;
                    Logger.warn(`toast failed: ${err.message}`);
                  }
                }
              })
            Button('重置')
              .layoutWeight(1)
              .onClick(() => {
                arEngineDemo.resetArrowAlign(this.idStr);
                Logger.info(`[ArrowAlign] resetArrowAlign`);
              })
          }
          .width('92%')
        }
        .id('hud')
        .width('100%')
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
