/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

import { abilityAccessCtrl, PermissionRequestResult } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { Logger } from '../utils/Logger';
import { requestPermissionOnSetting } from '../utils/Utils';
import { Palette, FontSize, Spacing } from '../styles/Theme';

// Stage 12A: Selector is now a one-shot route forwarder. App start -> request CAMERA permission
// -> auto-push to the default camera mode (ARRingHuntCam = 机位). The user never sees this page's
// own content — the next NavDestination overlays it immediately. Kept as the Navigation root so:
//  (a) permission can be requested before AR Engine init runs in any scene page,
//  (b) popping back from a scene returns here cleanly (Cam pages do clear+push so this is rare),
//  (c) the dev-only DevSelector is still reachable via long-press on the gear icon in CameraChrome.
@Entry
@Component
struct Selector {
  pageInfos: NavPathStack = new NavPathStack();
  @State private didAutoPush: boolean = false;
  @State private permissionDenied: boolean = false;
  @State context: Context = this.getUIContext().getHostContext() as Context;

  private pushDefault(): void {
    if (this.didAutoPush) {
      return;
    }
    this.didAutoPush = true;
    this.pageInfos.clear();
    this.pageInfos.pushDestinationByName('ARRingHuntCam', null).catch((err: BusinessError) => {
      Logger.error(`Failed to push ARRingHuntCam: ${err.code} ${err.message}`);
      // If push fails, allow another attempt (e.g., manual retry).
      this.didAutoPush = false;
    });
  }

  private async ensurePermissionAndPush(): Promise<void> {
    try {
      let data: PermissionRequestResult = await abilityAccessCtrl.createAtManager()
        .requestPermissionsFromUser(this.context, ['ohos.permission.CAMERA']);
      let grantStatus: number[] = data.authResults;
      if (grantStatus[0] === 0) {
        Logger.info('CAMERA permission granted -> auto-push to ARRingHuntCam');
        this.pushDefault();
      } else {
        Logger.warn('CAMERA permission not granted via prompt — falling back to settings flow');
        let granted: boolean = await requestPermissionOnSetting(this.context);
        if (granted) {
          this.pushDefault();
        } else {
          this.permissionDenied = true;
        }
      }
    } catch (e) {
      let err: BusinessError = e as BusinessError;
      Logger.error(`Permission request failed: ${err.code} ${err.message}`);
      this.permissionDenied = true;
    }
  }

  build(): void {
    Navigation(this.pageInfos) {
      // Minimal splash: a black backdrop with a quiet hint. User normally only sees this for a
      // beat before the Cam page overlays it. If permission was denied, surface a retry tap.
      Column({ space: Spacing.M }) {
        Text(this.permissionDenied ? '需要相机权限' : '加载中…')
          .fontSize(FontSize.title)
          .fontColor(Palette.white80)
        if (this.permissionDenied) {
          Text('点此重新申请')
            .fontSize(FontSize.hud)
            .fontColor(Palette.white60)
            .backgroundColor(Palette.glass)
            .padding({ left: Spacing.M, right: Spacing.M, top: Spacing.S, bottom: Spacing.S })
            .borderRadius(20)
            .onClick(() => {
              this.permissionDenied = false;
              this.ensurePermissionAndPush();
            })
        }
      }
      .width('100%')
      .height('100%')
      .justifyContent(FlexAlign.Center)
      .backgroundColor('#FF000000')
    }
    .onAppear(() => {
      this.ensurePermissionAndPush();
    })
    .mode(NavigationMode.Stack)
    .hideTitleBar(true)
    .hideBackButton(true)
    .hideToolBar(true)
  }
}
