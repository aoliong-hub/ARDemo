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

#ifndef NAPI_MANAGER_H
#define NAPI_MANAGER_H

#include "app_napi.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <napi/native_api.h>
#include <string>
#include <unordered_map>

class NapiManager {
public:
    NapiManager();
    ~NapiManager() {}

    static NapiManager *GetInstance() { return &NapiManager::manager_; }

    void SetNativeXComponent(std::string &id, OH_NativeXComponent *nativeXComponent);
    AppNapi *GetApp(std::string &id);

    // JS Page Lifecycle
    static napi_value NapiOnPageAppear(napi_env env, napi_callback_info info);
    static napi_value NapiOnPageShow(napi_env env, napi_callback_info info);
    static napi_value NapiOnPageHide(napi_env env, napi_callback_info info);
    static napi_value NapiOnPageUpdate(napi_env env, napi_callback_info info);
    static napi_value NapiOnPageDisappear(napi_env env, napi_callback_info info);
    static napi_value NapiGetDistance(napi_env env, napi_callback_info info);
    static napi_value NapiInitImage(napi_env env, napi_callback_info info);
    static napi_value NapiSetPath(napi_env env, napi_callback_info info);
    static napi_value NapiSaveImageDataBaseToLocal(napi_env env, napi_callback_info info);
    static napi_value NapiGetImageCount(napi_env env, napi_callback_info info);
    static napi_value NapiGetVolume(napi_env env, napi_callback_info info);

    // ARObject interface-style placement
    static napi_value NapiPlaceObjectAtWorld(napi_env env, napi_callback_info info);
    static napi_value NapiPlaceObjectInFrontOfCamera(napi_env env, napi_callback_info info);
    static napi_value NapiRemoveObject(napi_env env, napi_callback_info info);
    static napi_value NapiClearAllObjects(napi_env env, napi_callback_info info);

    // ARArrowAlign alignment game
    static napi_value NapiPlaceTargetArrow(napi_env env, napi_callback_info info);
    static napi_value NapiResetArrowAlign(napi_env env, napi_callback_info info);
    static napi_value NapiGetAlignmentState(napi_env env, napi_callback_info info);

    // ARRingHunt ring game
    static napi_value NapiPlaceRing(napi_env env, napi_callback_info info);
    static napi_value NapiPlaceRingWithOrientation(napi_env env, napi_callback_info info);
    static napi_value NapiPlaceRingAt(napi_env env, napi_callback_info info);
    static napi_value NapiSetZoom(napi_env env, napi_callback_info info);
    static napi_value NapiSetDisplayRotation(napi_env env, napi_callback_info info);
    static napi_value NapiGetOrientation(napi_env env, napi_callback_info info);
    static napi_value NapiGetLatestCamRawPose(napi_env env, napi_callback_info info);
    static napi_value NapiGetLatestCamDispPose(napi_env env, napi_callback_info info);
    static napi_value NapiResetRing(napi_env env, napi_callback_info info);
    static napi_value NapiGetRingState(napi_env env, napi_callback_info info);

    // Da3 frame capture (request/poll). captureFrame flips a flag; isFrameReady polls; takeFrameRGBA
    // moves the RGBA bytes + dims out as an Object {buffer:ArrayBuffer, width, height} | null.
    static napi_value NapiCaptureFrame(napi_env env, napi_callback_info info);
    static napi_value NapiIsFrameReady(napi_env env, napi_callback_info info);
    static napi_value NapiTakeFrameRGBA(napi_env env, napi_callback_info info);
    // 拍照纯净帧(功能 6):同上,但 glReadPixels 在 wayfinder Render 之前完成,抓到的不含 AR 物体。
    static napi_value NapiCaptureCleanFrame(napi_env env, napi_callback_info info);
    static napi_value NapiIsCleanFrameReady(napi_env env, napi_callback_info info);
    static napi_value NapiTakeCleanFrameRGBA(napi_env env, napi_callback_info info);

    // Napi export
    bool Export(napi_env env, napi_value exports);

private:
    // XComponent Callback
    AppNapi *CreateApp(std::string &id);
    void Release(const std::string &id);

    static void OnSurfaceCreatedCB(OH_NativeXComponent *component, void *window);
    static void OnSurfaceChangedCB(OH_NativeXComponent *component, void *window);
    static void OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window);
    static void DispatchTouchEventCB(OH_NativeXComponent *component, void *window);
    static void DispatchMouseEventCB(OH_NativeXComponent *component, void *window);

    static NapiManager manager_;

    std::string id_;
    std::unordered_map<std::string, OH_NativeXComponent *> nativeXComponentMap_;
    std::unordered_map<std::string, AppNapi *> appNapiMap_;

    OH_NativeXComponent_Callback callback_;
    OH_NativeXComponent_MouseEvent_Callback mouseEventcallback_;
};

#endif // NAPI_MANAGER_H
