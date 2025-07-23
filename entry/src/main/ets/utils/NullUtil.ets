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
export class NullUtil {

  public static isObjNull(obj: Object | undefined | null): boolean {
    if (obj === undefined || obj === null) {
      return true;
    } else {
      return false;
    }
  }

  public static isStrNull(str: string | undefined | null): boolean {
    if (NullUtil.isObjNull(str) || str === '') {
      return true;
    } else {
      return false;
    }
  }
}