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

#include <cstdint>
#include <string>
#include <cstdio>

#include "utils/log.h"
#include "napi_manager.h"
#include "world/world_ar_application.h"

enum class ContextType {
    APP_LIFECYCLE = 0,
    JS_PAGE_LIFECYCLE,
};

NapiManager NapiManager::manager_;

void NapiManager::OnSurfaceCreatedCB(OH_NativeXComponent *component, void *window) {
    LOGD("OnSurfaceCreatedCB");
    int32_t ret;
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    ret = OH_NativeXComponent_GetXComponentId(component, idStr, &idSize);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return;
    }

    std::string id(idStr);
    auto app = NapiManager::GetInstance()->GetApp(id);
    app->OnSurfaceCreated(component, window);
}

void NapiManager::OnSurfaceChangedCB(OH_NativeXComponent *component, void *window) {
    int32_t ret;
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    ret = OH_NativeXComponent_GetXComponentId(component, idStr, &idSize);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return;
    }

    std::string id(idStr);
    auto app = NapiManager::GetInstance()->GetApp(id);
    app->OnSurfaceChanged(component, window);
}

void NapiManager::OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window) {
    int32_t ret;
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    ret = OH_NativeXComponent_GetXComponentId(component, idStr, &idSize);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return;
    }

    std::string id(idStr);
    auto app = NapiManager::GetInstance()->GetApp(id);
    app->OnSurfaceDestroyed(component, window);
}

void NapiManager::DispatchTouchEventCB(OH_NativeXComponent *component, void *window) {
    LOGD("DispatchTouchEventCB");
    int32_t ret;
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    ret = OH_NativeXComponent_GetXComponentId(component, idStr, &idSize);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return;
    }
    std::string id(idStr);
    auto app = NapiManager::GetInstance()->GetApp(id);
    app->DispatchTouchEvent(component, window);
}

void NapiManager::DispatchMouseEventCB(OH_NativeXComponent *component, void *window) {
    LOGD("DispatchMouseEventCB");
    int32_t ret;
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    ret = OH_NativeXComponent_GetXComponentId(component, idStr, &idSize);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return;
    }
    std::string id(idStr);
    auto app = NapiManager::GetInstance()->GetApp(id);
    app->DispatchMouseEvent(component, window);
}

NapiManager::NapiManager()
{
    callback_.OnSurfaceCreated = NapiManager::OnSurfaceCreatedCB;
    callback_.OnSurfaceChanged = NapiManager::OnSurfaceChangedCB;
    callback_.OnSurfaceDestroyed = NapiManager::OnSurfaceDestroyedCB;
    callback_.DispatchTouchEvent = NapiManager::DispatchTouchEventCB;
    mouseEventcallback_.DispatchMouseEvent = NapiManager::DispatchMouseEventCB;
}

bool NapiManager::Export(napi_env env, napi_value exports)
{
    napi_status status;
    napi_value exportInstance = nullptr;
    OH_NativeXComponent *nativeXComponent = nullptr;
    int32_t ret;
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = { };
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;

    status = napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance);
    if (status != napi_ok) {
        LOGE("NapiManager::Export fail 0");
        return false;
    }

    status = napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&nativeXComponent));
    if (status != napi_ok) {
        LOGE("NapiManager::Export fail 1: %{public}d", status);
        return false;
    }

    ret = OH_NativeXComponent_GetXComponentId(nativeXComponent, idStr, &idSize);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        LOGE("NapiManager::Export fail 2");
        return false;
    }

    std::string id(idStr);
    NapiManager* context = NapiManager::GetInstance();
    if (context == nullptr) {
        LOGE("NapiManager::Export fail 3");
        return false;
    }
    
    context->SetNativeXComponent(id, nativeXComponent);
    auto app = context->GetApp(id);
    
    return true;
}

void NapiManager::SetNativeXComponent(std::string& id, OH_NativeXComponent* nativeXComponent)
{
    LOGD("NapiManager::SetNativeXComponent");
    if (nativeXComponentMap_.find(id) == nativeXComponentMap_.end() || nativeXComponentMap_[id] != nativeXComponent) {
        nativeXComponentMap_[id] = nativeXComponent;
        OH_NativeXComponent_RegisterCallback(nativeXComponent, &callback_);
        OH_NativeXComponent_RegisterMouseEventCallback(nativeXComponent, &mouseEventcallback_);
    }
}

OH_NativeXComponent* NapiManager::GetNativeXComponent(std::string& id)
{
    if (nativeXComponentMap_.find(id) == nativeXComponentMap_.end()) {
        return nullptr;
    } else {
        return nativeXComponentMap_[id];
    }
}

AppNapi* NapiManager::GetApp(std::string& id)
{
    if (appNapiMap_.find(id) == appNapiMap_.end()) {
        AppNapi* instance = CreateApp(id);
        appNapiMap_[id] = instance;
        return instance;
    } else {
        return appNapiMap_[id];
    }
}

napi_value NapiManager::NapiOnPageAppear(napi_env env, napi_callback_info info) 
{
    LOGD("NapiManager::NapiOnPageAppear");

    size_t argc = 1;
    napi_value args[1] = {nullptr};

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->OnStart();

    return nullptr;
}

napi_value NapiManager::NapiOnPageShow(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiOnPageShow");

    size_t argc = 1;
    napi_value args[1] = {nullptr};

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->OnResume();
    
    return nullptr;
}

napi_value NapiManager::NapiOnPageHide(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiOnPageHide");

    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    
    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->OnPause();
    
    return nullptr;
}

napi_value NapiManager::NapiOnPageUpdate(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiOnPageUpdate");

    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);

    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->OnUpdate();

    return nullptr;
}

napi_value NapiManager::NapiOnPageDisappear(napi_env env, napi_callback_info info) {
    LOGD("NapiManager::NapiOnPageDisappear");

    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);

    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->OnStop();

    return nullptr;
}

AppNapi *NapiManager::CreateApp(std::string &id) {
    if (id == std::string("ArWorld")) {
        return new ArWorld::ArWorldApp(id);
    }
    abort();
    return nullptr;
}
