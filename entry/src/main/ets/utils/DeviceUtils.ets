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
import { systemParameter } from '@kit.BasicServicesKit';
import { deviceInfo, systemParameterEnhance } from '@kit.BasicServicesKit'
import { Logger } from './Logger'
import { NullUtil } from './NullUtil'
import { FoldPhoneTypeValue } from './Enum';

const TAG = 'DeviceUtil'

const FOLD_PRODUCT_TYPE = 'const.window.foldscreen.type';
const DEFAULTS_FOLD_TYPE = '0,0,0,0';

export class DeviceUtil {
  private static instance: DeviceUtil;
  private deviceType: string = ''
  private sFoldProductTypeValue: FoldPhoneTypeValue = FoldPhoneTypeValue.INVALID_VALUE;
  private static isVdeFold?: boolean = undefined;
  private static foldScreenProperty?: string = undefined;
  public static readonly FOLD_SCREEN_PRODUCT_PROPERTY = 'const.window.foldscreen.type';
  public static readonly SMALL_FOLD_SCREEN_VALUE_4: string = '4,';

  public getFoldProductType(): FoldPhoneTypeValue {
    if (this.sFoldProductTypeValue !== FoldPhoneTypeValue.INVALID_VALUE) {
      return this.sFoldProductTypeValue;
    }
    try {
      let productValue: string = systemParameterEnhance.getSync(FOLD_PRODUCT_TYPE, DEFAULTS_FOLD_TYPE);
      Logger.info(TAG, `productValue: ${productValue}`);
      const result: string[] = productValue?.split(',');
      if (result.length > 0) {
        this.sFoldProductTypeValue = Number.parseInt(result[0]);
        return this.sFoldProductTypeValue;
      }
    } catch (e) {
      Logger.error(TAG, `Get fold product type value failed, error code : ${e.code}`);
    }
    return FoldPhoneTypeValue.INVALID_VALUE;
  }

  public isSmallFoldProduct(): boolean {
    return FoldPhoneTypeValue.SMALL_FOLD === this.getFoldProductType() ||
      FoldPhoneTypeValue.EXPANDING_NEX_FORMS === this.getFoldProductType();
  }

  public static getInstance() {
    if (!DeviceUtil.instance) {
      DeviceUtil.instance = new DeviceUtil();
    }
    return DeviceUtil.instance;
  }

  static isFoldAble(): boolean {
    try {
      return display.isFoldable();
    } catch (err) {
      Logger.error(TAG, `display.isFoldable get res error, error code : ${err.code}`);
      return false;
    }
  }

  static isFoldDeviceMain(): boolean {
    try {
      return display.isFoldable() && display.getFoldDisplayMode() === display.FoldDisplayMode.FOLD_DISPLAY_MODE_MAIN;
    } catch (err) {
      Logger.error(TAG, `display.getFoldDisplayMode get res error, error code : ${err.code}`);
      return false;
    }
  }

  static isFoldDeviceFULL(): boolean {
    try {
      return display.isFoldable() && display.getFoldDisplayMode() === display.FoldDisplayMode.FOLD_DISPLAY_MODE_FULL;
    } catch (err) {
      Logger.error(TAG, `display.getFoldDisplayMode get res error, error code : ${err.code}`);
      return false;
    }
  }

  public isPuraXFolded(): boolean {
    try {
      return DeviceUtil.isPhonePuraX() && display.getFoldDisplayMode() === display.FoldDisplayMode.FOLD_DISPLAY_MODE_MAIN;
    } catch (err) {
      Logger.error(TAG, `display.getFoldDisplayMode get res error, error code : ${err.code}`);
      return false;
    }
  }

  public isPuraXFull(): boolean {
    try {
      return DeviceUtil.isPhonePuraX() && display.getFoldDisplayMode() === display.FoldDisplayMode.FOLD_DISPLAY_MODE_FULL;
    } catch (err) {
      Logger.error(TAG, `display.getFoldDisplayMode get res error, error code : ${err.code}`);
      return false;
    }
  }

  private static getFoldScreenProperty(): string {
    if (DeviceUtil.foldScreenProperty !== undefined) {
      return DeviceUtil.foldScreenProperty;
    }
    let tmpProperty: string = '';
    try {
      tmpProperty = systemParameter.getSync(DeviceUtil.FOLD_SCREEN_PRODUCT_PROPERTY);
    } catch (e) {
      Logger.error(TAG, 'getFoldScreenProperty error:' + e.code);
    }
    DeviceUtil.foldScreenProperty = tmpProperty ? tmpProperty : '';
    return tmpProperty;
  }

  public static isPhonePuraX(): boolean {
    if (DeviceUtil.isVdeFold !== undefined) {
      return DeviceUtil.isVdeFold;
    }
    let productProperty: string = DeviceUtil.getFoldScreenProperty();
    if (NullUtil.isStrNull(productProperty)) {
      return false;
    }
    Logger.info(TAG, `isPhoneVde productProperty = ${productProperty}`);
    DeviceUtil.isVdeFold = productProperty.startsWith(DeviceUtil.SMALL_FOLD_SCREEN_VALUE_4);
    return DeviceUtil.isVdeFold;
  }

  public getDeviceType(): string {
    if (NullUtil.isStrNull(this.deviceType)) {
      this.deviceType = deviceInfo.deviceType;
      Logger.info(TAG, `deviceType: ${this.deviceType}`);
    }
    return this.deviceType
  }

  public isPhone(): boolean {
    return this.getDeviceType() === 'phone';
  }

  public isPc(): boolean {
    return this.getDeviceType() === '2in1';
  }

  public isPad(): boolean {
    return this.getDeviceType() === 'tablet';
  }
}