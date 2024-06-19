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
import resourceManager from '@ohos.resourceManager';
import { router } from '@kit.ArkUI';
import abilityAccessCtrl, { PermissionRequestResult } from '@ohos.abilityAccessCtrl';
import { BusinessError } from '@ohos.base';

@Entry
@Component
struct Selector {
  private resMgr: resourceManager.ResourceManager = getContext(this).resourceManager;

  aboutToAppear() {
    Logger.debug('aboutToAppear');
    abilityAccessCtrl.createAtManager().requestPermissionsFromUser(getContext(this), [
      'ohos.permission.CAMERA'
    ]).then((data: PermissionRequestResult) => {
      let grantStatus: Array<number> = data.authResults;
      let length: number = grantStatus.length;
      for (let i = 0; i < length; i++) {
        if (grantStatus[i] === 0) {
          Logger.info('requestPermission success');
        } else {
          // 用户拒绝授权，提示用户必须授权才能访问当前页面的功能，并引导用户到系统设置中打开相应的权限
          Logger.error('requestPermission user rejected')
          router.back();
          return;
        }
      }
      Logger.info('requestPermission success');
    }).catch((err: BusinessError) => {
      Logger.error("Failed to request permissions from user. Code is ${err.code}, message is ${err.message}")
    })
  }

  build() {
    Column() {
      Button('ArWorld', { type: ButtonType.Normal, stateEffect: true })
        .borderRadius(8)
        .width('50%')
        .height('5%')
        .onClick(()=>{
          router.pushUrl({'url':'pages/ArWorld'})
        })
    }
    .justifyContent(FlexAlign.SpaceEvenly)
    .width('100%')
    .height('100%')
  }
}
