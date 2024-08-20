/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

import { Logger } from '../utils/Logger';
import arEngineDemo from 'libentry.so'
import { resourceManager } from '@kit.LocalizationKit';
import { display, promptAction } from '@kit.ArkUI';
import { vibrator } from '@kit.SensorServiceKit';

@Entry
@Component
struct ArWorld {
  private xcomponentId = 'ArWorld';
  private resMgr: resourceManager.ResourceManager = getContext(this).resourceManager;
  private interval: number = -1;
  private isUpdate: boolean = true;
  @State numberOfPlans: number = 0;
  private currentMillisecond = 0;
  @State showPage: boolean = true;

  aboutToAppear() {
    Logger.debug('aboutToAppear ' + this.xcomponentId);
    arEngineDemo.init(this.resMgr);
    arEngineDemo.start(this.xcomponentId);
    //适配折叠屏
    display.on("foldStatusChange", (foldStatus: display.FoldStatus) => {
      Logger.info('foldStatusChange display on ' + foldStatus);
      if (foldStatus === display.FoldStatus.FOLD_STATUS_HALF_FOLDED) {
        this.showPage = false;
        arEngineDemo.stop(this.xcomponentId);
      }
      if (foldStatus === display.FoldStatus.FOLD_STATUS_EXPANDED
        || foldStatus === display.FoldStatus.FOLD_STATUS_FOLDED) {
        this.showPage = true;
        arEngineDemo.init(this.resMgr);
        arEngineDemo.start(this.xcomponentId);
        arEngineDemo.show(this.xcomponentId);
      }
    })
  }

  aboutToDisappear() {
    Logger.debug('aboutToDisappear ' + this.xcomponentId);
    arEngineDemo.stop(this.xcomponentId);
  }

  onPageShow() {
    this.isUpdate = true;
    Logger.debug('onPageShow ' + this.xcomponentId);
    arEngineDemo.show(this.xcomponentId);
  }

  onPageHide() {
    Logger.debug('onPageHide ' + this.xcomponentId);
    this.isUpdate = false;
    arEngineDemo.hide(this.xcomponentId);
  }

  messageNotification() {
    promptAction.showToast({
      message: '当前特征点较少，无法识别平面',
      bottom: 300
    })
    try {
      vibrator.startVibration({
        type: 'time',
        duration: 1000,
      }, {
        id: 0,
        usage: 'alarm'
      })
    } catch (err) {
      Logger.debug('errCode: ' + err.code + ' ,msg: ' + err.message);
    }
  }

  planeNum() {
    if (this.numberOfPlans < 1) {
      //平面数小于1
      let tempMillisecond: number = new Date().getTime();
      //首次进入 时间赋值
      if (this.currentMillisecond == 0) {
        this.currentMillisecond = tempMillisecond;
        return;
      }
      //超过10s 给用户弹窗
      if (tempMillisecond - this.currentMillisecond > 10000) {
        this.messageNotification();
        this.currentMillisecond = 0;
      }
    } else {
      this.currentMillisecond = 0;
    }
  }

  build() {
    if (this.showPage) {
      Column() {
        XComponent({ id: this.xcomponentId, type: XComponentType.SURFACE, libraryname: 'entry' })
          .onLoad(() => {
            Logger.debug('XComponent onLoad ' + this.xcomponentId);
            this.interval = setInterval(() => {
              if (this.isUpdate) {
                // 调用Native的update，更新AR Engine每一帧的计算结果
                this.numberOfPlans = arEngineDemo.update(this.xcomponentId);
                this.planeNum()
              }
            }, 33); // 控制帧率为30fps（每33毫秒刷新一帧）。
          })
          .width('100%')
          .height('100%')
          .onDestroy(() => {
            Logger.debug('XComponent onDestroy ' + this.xcomponentId);
            clearInterval(this.interval);
          })
          .backgroundColor(Color.White)
      }
      .justifyContent(FlexAlign.SpaceAround)
      .alignItems(HorizontalAlign.Center)
      .backgroundColor(Color.White)
      .borderRadius(24)
      .width('100%')
      .height('100%')
    } else {
      Stack().align(Alignment.Center).height('100%').width('100%').backgroundColor(Color.Black)
    }
  }
}


