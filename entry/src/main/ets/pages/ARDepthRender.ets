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

import { deviceInfo } from '@kit.BasicServicesKit';
import { resourceManager } from '@kit.LocalizationKit';
import arEngineDemo from 'libentry.so';

@Builder
export function ARDepthRenderBuilder() {
  ARDepthRender();
}

@Component
struct ARDepthRender {
  pageInfos: NavPathStack = new NavPathStack();
  private interval: number = -1;
  private isUpdate: boolean = true;
  private params: number = 0;
  private xComponentId = 'ARDepth';
  @State context: Context = this.getUIContext().getHostContext() as Context;
  private resMgr: resourceManager.ResourceManager = this.context.resourceManager;
  @State distance: string = '';
  @State rotation: number = deviceInfo.deviceType === 'tablet' ? 3 : 0;

  build(): void {
    NavDestination() {
      RelativeContainer() {
        XComponent({ id: this.xComponentId, type: XComponentType.SURFACE, libraryname: 'entry' })
          .width('100%')
          .height('100%')
          .alignRules({
            center: { anchor: '__container__', align: VerticalAlign.Center },
            middle: { anchor: '__container__', align: HorizontalAlign.Center }
          })
          .onLoad(() => {
            this.interval = setInterval(() => {
              if (this.isUpdate) {
                // Call the update Native API to update the calculation result of each frame by AR Engine.
                arEngineDemo.update(this.xComponentId);
                this.distance = arEngineDemo.getDistance(this.xComponentId);
              }
            }, 33) // Set the frame rate to 30 fps (with the frame refreshed every 33 ms).
          })
          .onDestroy(() => {
            clearInterval(this.interval);
          })

        Text(this.distance)
          .fontColor(Color.Red)
          .fontSize(18)
          .textAlign(TextAlign.Center)
          .alignRules({
            bottom: { anchor: '__container__', align: VerticalAlign.Bottom },
            middle: { anchor: '__container__', align: HorizontalAlign.Center }
          })
      }
    }
    .onAppear(() => {
      arEngineDemo.init(this.resMgr);
      let config: Int32Array = new Int32Array([0, this.params, 1, this.rotation]);
      arEngineDemo.start(this.xComponentId, config);
    })
    .onWillDisappear(() => {
      arEngineDemo.stop(this.xComponentId);
    })
    .onShown(() => {
      this.isUpdate = true;
      arEngineDemo.show(this.xComponentId);
    })
    .onHidden(() => {
      this.isUpdate = false;
      arEngineDemo.hide(this.xComponentId);
    })
    .onReady((context: NavDestinationContext) => {
      this.pageInfos = context.pathStack;
      this.params = context.pathInfo.param as number;
    })
    .hideTitleBar(true)
    .hideBackButton(true)
    .hideToolBar(true)
  }
}
