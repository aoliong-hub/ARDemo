/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
import { router } from '@kit.ArkUI';
import { abilityAccessCtrl, PermissionRequestResult } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { promptAction } from '@kit.ArkUI';

function showDialog(context: Context)
{
  try {
    promptAction.showDialog({
      isModal: true,
      title: $r('app.string.tips'),
      message: $r('app.string.permission_desc'),
      buttons: [
        {
          text: $r('app.string.back'),
          color: '#000000'
        }
      ],
    }).then(data => {
        if (data.index === 0) {
          let applicationContext = context.getApplicationContext();
          applicationContext.killAllProcesses();
        }
        console.info('showDialog success, click button: ' + data.index);
      })
      .catch((err:Error) => {
        console.info('showDialog error: ' + err);
      })
  } catch (error) {
    let message = (error as BusinessError).message;
    let code = (error as BusinessError).code;
    console.error(`showDialog args error code is ${code}, message is ${message}`);
  };
}
@Entry
@Component
struct Selector {
  private hasPermission: boolean = false;

  aboutToAppear() {
    Logger.debug('aboutToAppear');
    abilityAccessCtrl.createAtManager().requestPermissionsFromUser(getContext(this), [
      'ohos.permission.CAMERA'
    ]).then((data: PermissionRequestResult) => {
      let grantStatus: Array<number> = data.authResults;
      let length: number = grantStatus.length;
      for (let i = 0; i < length; i++) {
        if (grantStatus[i] === 0) {
          this.hasPermission = true;
          Logger.info('requestPermission success');
        } else {
          // If the user refuses to grant the authorization, display a message indicating that user authorization is required, and direct the user to set the permission in system Settings.
          this.hasPermission = false;
          showDialog(getContext(this));
          Logger.error('requestPermission user rejected')
          return;
        }
      }
      Logger.info('requestPermission success');
    }).catch((err: BusinessError) => {
      Logger.error(`Failed to request permissions from user. Code is ${err.code}, message is ${err.message}`)
    })
  }

  build() {
    Column() {
      Button('ArWorld', { type: ButtonType.Normal, stateEffect: true })
        .borderRadius(8)
        .width('50%')
        .height('5%')
        .onClick(()=>{
          if (!this.hasPermission) {
            showDialog(getContext(this))
            return
          }
          router.pushUrl({'url':'pages/ArWorld'})
        })
    }
    .justifyContent(FlexAlign.SpaceEvenly)
    .width('100%')
    .height('100%')
  }
}
