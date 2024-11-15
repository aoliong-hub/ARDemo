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
    // Applicable to foldable screens.
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
      message: $r('app.string.alert_desc'),
      bottom: 300
    })
  }

  planeNum() {
    if (this.numberOfPlans < 1) {
      // The number of planes is less than 1.
      let tempMillisecond: number = new Date().getTime();
      // Assign a value to the time when the feature is started for the first time.
      if (this.currentMillisecond == 0) {
        this.currentMillisecond = tempMillisecond;
        return;
      }
      // Display a pop-up window if the plane fails to be recognized within 10 seconds.
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
                // Call the update Native API to update the calculation result of each frame by AR Engine.
                this.numberOfPlans = arEngineDemo.update(this.xcomponentId);
                this.planeNum()
              }
            }, 33); // Set the frame rate to 30 fps (with the frame refreshed every 33 ms).
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


