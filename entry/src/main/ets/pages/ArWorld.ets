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
import { display } from '@kit.ArkUI';

@Entry
@Component
struct ArWorld {
  private xcomponentId = 'ArWorld';
  private panOption: PanGestureOptions = new PanGestureOptions({ direction: PanDirection.All });
  private resMgr: resourceManager.ResourceManager = getContext(this).resourceManager;
  private interval: number = -1;
  private isUpdate: boolean = true;

  aboutToAppear() {
    Logger.debug('aboutToAppear ' + this.xcomponentId);
    arEngineDemo.init(this.resMgr);
    arEngineDemo.start(this.xcomponentId);
    display.on("foldStatusChange", (foldStatus: display.FoldStatus) => {
      Logger.info('foldStatusChange display on ' + foldStatus);
      if (foldStatus === display.FoldStatus.FOLD_STATUS_EXPANDED
        || foldStatus === display.FoldStatus.FOLD_STATUS_FOLDED) {
        arEngineDemo.stop(this.xcomponentId);
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

  build() {
    Column() {
      XComponent({ id: this.xcomponentId, type: 'surface', libraryname: 'entry' })
        .onLoad(() => {
          Logger.debug('XComponent onLoad ' + this.xcomponentId);
          this.interval = setInterval(() => {
            if (this.isUpdate) {
              arEngineDemo.update(this.xcomponentId);
            }
          }, 33);
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
  }
}
