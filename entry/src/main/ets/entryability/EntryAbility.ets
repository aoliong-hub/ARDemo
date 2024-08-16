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

import UIAbility from '@ohos.app.ability.UIAbility';
import { Logger } from '../utils/Logger';
import window from '@ohos.window';

export default class EntryAbility extends UIAbility {
  onCreate() {
    Logger.info('Ability onCreate');
  }

  onDestroy() {
    Logger.info('Ability onDestroy');
  }

  onWindowStageCreate(windowStage: window.WindowStage) {
    Logger.info(`request Permissions success!`)
    windowStage.loadContent('pages/Selector', (err, data) => {
      if (err.code) {
        Logger.error('Failed to load the content. Cause: ' + JSON.stringify(err));
        return;
      }
      Logger.info('Succeeded in loading the content. Data: ' + JSON.stringify(data));
    });
    windowStage.getMainWindowSync().setWindowKeepScreenOn(true, (err) => {
      if (err.code) {
        Logger.info(`setWindowKeepScreenOn fail, erro:` + JSON.stringify(err));
        return;
      }
      Logger.info(`setWindowKeepScreenOn success!`);
    });
  }

  onWindowStageDestroy() {
    // Main window is destroyed, release UI related resources
    Logger.info('Ability onWindowStageDestroy');
  }

  onForeground() {
    // Ability has brought to foreground
    Logger.info('Ability onForeground');
  }

  onBackground() {
    // Ability has back to background
    Logger.info('Ability onBackground');
  }
};