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

#include "napi_manager.h"
#include "arrowalign/arrow_align_ar_application.h"
#include "depth/depth_ar_application.h"
#include "image/image_ar_application.h"
#include "mesh/mesh_ar_application.h"
#include "object/object_ar_application.h"
#include "object/ring_hunt_ar_application.h"
#include "semanticdense/semanticdense_ar_application.h"
#include "utils/log.h"
#include "world/world_ar_application.h"
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

enum class ContextType {
    APP_LIFECYCLE = 0,
    JS_PAGE_LIFECYCLE,
};

NapiManager NapiManager::manager_;

namespace {
void ClearPendingException(napi_env env)
{
    bool isPending = false;
    if (napi_is_exception_pending(env, &isPending) == napi_ok && isPending) {
        napi_value ignored = nullptr;
        napi_get_and_clear_last_exception(env, &ignored);
    }
}
} // namespace

void NapiManager::OnSurfaceCreatedCB(OH_NativeXComponent *component, void *window)
{
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

void NapiManager::OnSurfaceChangedCB(OH_NativeXComponent *component, void *window)
{
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

void NapiManager::OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window)
{
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
    NapiManager::GetInstance()->Release(id);
}

void NapiManager::DispatchTouchEventCB(OH_NativeXComponent *component, void *window)
{
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

void NapiManager::DispatchMouseEventCB(OH_NativeXComponent *component, void *window)
{
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
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;

    bool hasXComponent = false;
    status = napi_has_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &hasXComponent);
    if (status != napi_ok || !hasXComponent) {
        ClearPendingException(env);
        return true;
    }

    status = napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance);
    if (status != napi_ok || exportInstance == nullptr) {
        ClearPendingException(env);
        LOGE("NapiManager::Export fail 0.");
        return true;
    }

    status = napi_unwrap(env, exportInstance, reinterpret_cast<void **>(&nativeXComponent));
    if (status != napi_ok || nativeXComponent == nullptr) {
        ClearPendingException(env);
        LOGE("NapiManager::Export fail 1: %{public}d.", status);
        return true;
    }

    ret = OH_NativeXComponent_GetXComponentId(nativeXComponent, idStr, &idSize);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        LOGE("NapiManager::Export fail 2.");
        return true;
    }

    std::string id(idStr);
    NapiManager *context = NapiManager::GetInstance();
    if (context == nullptr) {
        LOGE("NapiManager::Export fail 3.");
        return true;
    }

    context->SetNativeXComponent(id, nativeXComponent);
    context->GetApp(id);

    return true;
}

void NapiManager::SetNativeXComponent(std::string &id, OH_NativeXComponent *nativeXComponent)
{
    LOGD("NapiManager::SetNativeXComponent");
    if (nativeXComponentMap_.find(id) == nativeXComponentMap_.end() || nativeXComponentMap_[id] != nativeXComponent) {
        nativeXComponentMap_[id] = nativeXComponent;
        OH_NativeXComponent_RegisterCallback(nativeXComponent, &callback_);
        OH_NativeXComponent_RegisterMouseEventCallback(nativeXComponent, &mouseEventcallback_);
    }
}

AppNapi *NapiManager::GetApp(std::string &id)
{
    if (appNapiMap_.find(id) == appNapiMap_.end()) {
        AppNapi *instance = CreateApp(id);
        appNapiMap_[id] = instance;
        return instance;
    } else {
        return appNapiMap_[id];
    }
}

void NapiManager::Release(const std::string &id)
{
    if (appNapiMap_.find(id) != appNapiMap_.end()) {
        delete appNapiMap_[id];
        appNapiMap_.erase(id);
    }
}

napi_value NapiManager::NapiOnPageAppear(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiOnPageAppear");

    size_t argc = 2;
    napi_value args[2] = {nullptr};

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    std::string id(idStr);

    // Get the incoming array typedarray to generate input_buffer
    napi_typedarray_type type; // data type
    napi_value input_buffer;
    size_t byte_offset; // data offset
    size_t length;
    napi_get_typedarray_info(env, args[1], &type, &length, NULL, &input_buffer, &byte_offset);

    AppNapi::ConfigParams params{};
    if (type == napi_int32_array) {
        // Get array data
        void *data;
        size_t byte_length;
        napi_get_arraybuffer_info(env, input_buffer, &data, &byte_length);
        int32_t *data_bytes = (int32_t *)(data);
        int32_t num = length / sizeof(int32_t);
        for (int32_t i = 0; i + 1 < num; i += 2) {
            int32_t key = *(data_bytes + i);
            int32_t value = *(data_bytes + i + 1);
            if (key == AppNapi::DEPTH_RENDER_MODE) {
                params.depthRenderMode = value;
            } else if (key == AppNapi::ROTATION) {
                params.rotation = value;
            } else if (key == AppNapi::SEMANTICDENSEMODE) {
                params.semanticDenseMode = value;
            }
        }
    }
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->OnStart(params);

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
    // (per-frame; no debug log to avoid flooding hilog)
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);

    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->OnUpdate();
    if (id == std::string("ARWorld")) {
        napi_value res;
        napi_create_int32(env, ARWorld::WorldRenderManager::mPlaneCount, &res);
        return res;
    }

    return nullptr;
}

napi_value NapiManager::NapiOnPageDisappear(napi_env env, napi_callback_info info)
{
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

napi_value NapiManager::NapiGetDistance(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiGetCameraPose");

    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);

    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    std::string distance = app->GetDistance();

    std::ostringstream distanceString;
    distanceString << std::setiosflags(std::ios::fixed) << std::setiosflags(std::ios::right) << std::setprecision(4)
                   << distance << std::endl;

    napi_value ret = nullptr;
    std::string result = distanceString.str();
    napi_create_string_utf8(env, result.c_str(), result.length(), &ret);

    return ret;
}

napi_value NapiManager::NapiSaveImageDataBaseToLocal(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiSaveImageDataBaseToLocal");

    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_value context = nullptr;
    napi_get_cb_info(env, info, &argc, args, &context, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);

    char path[128] = {};
    size_t path_size = 0;
    napi_get_value_string_utf8(env, args[1], path, OH_XCOMPONENT_ID_LEN_MAX + 1, &path_size);
    LOGD("NapiManager::NapiSaveImageDataBaseToLocal path=%{public}s.", path);
    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->SaveImageDataBaseToLocal(path);

    return nullptr;
}

napi_value NapiManager::NapiGetImageCount(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiGetImageCount");

    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_value context = nullptr;
    napi_get_cb_info(env, info, &argc, args, &context, nullptr);
    
    // get idStr
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    
    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    uint32_t imageCount = app->getImageCount();
    
    napi_value result = nullptr;
    napi_create_int32(env, imageCount, &result);
    return result;
}

napi_value NapiManager::NapiGetVolume(napi_env env, napi_callback_info info) {
    LOGD("NapiManager::NapiGetVolume");

    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);

    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    std::string volume = app->GetVolume();
    LOGE("NapiManager::NapiGetVolume volume is:%{public}s", volume.c_str());

    std::ostringstream distanceString;
    distanceString << setiosflags(ios::fixed) << setiosflags(ios::right) << setprecision(4)  <<volume << std::endl;

    napi_value ret = nullptr;
    string result = distanceString.str();
    napi_create_string_utf8(env, result.c_str(), result.length(), &ret);

    return ret;
}

napi_value NapiManager::NapiSetPath(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiSetPath");

    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_value context = nullptr;
    napi_get_cb_info(env, info, &argc, args, &context, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);

    char path[128] = {};
    size_t path_size = 0;
    napi_get_value_string_utf8(env, args[1], path, OH_XCOMPONENT_ID_LEN_MAX + 1, &path_size);
    LOGD("NapiManager::NapiSetPath path=%{public}s.", path);
    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->SetPath(path);
    return nullptr;
}

napi_value NapiManager::NapiInitImage(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiInitImage");

    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_value context = nullptr;
    napi_get_cb_info(env, info, &argc, args, &context, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);

    int32_t width = 0;
    napi_get_value_int32(env, args[1], &width);

    int32_t height = 0;
    napi_get_value_int32(env, args[2], &height);

    void *data;
    size_t byte_length;
    napi_get_arraybuffer_info(env, args[3], &data, &byte_length);

    uint8_t *data_bytes = (uint8_t *)(data);

    std::string id(idStr);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    int32_t ret = app->InitImage(byte_length, width, height, data_bytes);
    napi_value result = nullptr;
    napi_create_int32(env, ret, &result);
    return result;
}

// ---- ARObject interface-style placement ----
// Helper: read an optional modelId argument (defaults to "AR_logo").
static std::string ReadModelIdArg(napi_env env, napi_value arg)
{
    if (arg == nullptr) {
        return std::string("AR_logo");
    }
    napi_valuetype valueType = napi_undefined;
    napi_typeof(env, arg, &valueType);
    if (valueType != napi_string) {
        return std::string("AR_logo");
    }
    char modelId[64] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, arg, modelId, sizeof(modelId), &resultSize);
    if (resultSize == 0) {
        return std::string("AR_logo");
    }
    return std::string(modelId);
}

napi_value NapiManager::NapiPlaceObjectAtWorld(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiPlaceObjectAtWorld");
    size_t argc = 5;
    napi_value args[5] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    std::string id(idStr);

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    napi_get_value_double(env, args[1], &x);
    napi_get_value_double(env, args[2], &y);
    napi_get_value_double(env, args[3], &z);
    std::string modelId = ReadModelIdArg(env, argc > 4 ? args[4] : nullptr);

    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    int32_t objectId = app->PlaceObjectAtWorld(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                                               modelId);
    napi_value result = nullptr;
    napi_create_int32(env, objectId, &result);
    return result;
}

napi_value NapiManager::NapiPlaceObjectInFrontOfCamera(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiPlaceObjectInFrontOfCamera");
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    std::string id(idStr);

    double distance = 0.0;
    napi_get_value_double(env, args[1], &distance);
    std::string modelId = ReadModelIdArg(env, argc > 2 ? args[2] : nullptr);

    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    int32_t objectId = app->PlaceObjectInFrontOfCamera(static_cast<float>(distance), modelId);
    napi_value result = nullptr;
    napi_create_int32(env, objectId, &result);
    return result;
}

napi_value NapiManager::NapiRemoveObject(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiRemoveObject");
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    std::string id(idStr);

    int32_t objectId = -1;
    napi_get_value_int32(env, args[1], &objectId);

    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    bool removed = app->RemoveObject(objectId);
    napi_value result = nullptr;
    napi_get_boolean(env, removed, &result);
    return result;
}

napi_value NapiManager::NapiClearAllObjects(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiClearAllObjects");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, args[0], idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    std::string id(idStr);

    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    int32_t cleared = app->ClearAllObjects();
    napi_value result = nullptr;
    napi_create_int32(env, cleared, &result);
    return result;
}

// ---- ARArrowAlign alignment game ----
static std::string ReadIdArg(napi_env env, napi_value arg)
{
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t resultSize = 0;
    napi_get_value_string_utf8(env, arg, idStr, OH_XCOMPONENT_ID_LEN_MAX + 1, &resultSize);
    return std::string(idStr);
}

napi_value NapiManager::NapiPlaceTargetArrow(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiPlaceTargetArrow");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string id = ReadIdArg(env, args[0]);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    int32_t objectId = app->PlaceTargetArrow();
    napi_value result = nullptr;
    napi_create_int32(env, objectId, &result);
    return result;
}

napi_value NapiManager::NapiResetArrowAlign(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiResetArrowAlign");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string id = ReadIdArg(env, args[0]);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->ResetArrowAlign();
    return nullptr;
}

napi_value NapiManager::NapiGetAlignmentState(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string id = ReadIdArg(env, args[0]);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);

    float angleRad = 0.0f;
    bool aligned = false;
    bool ready = false;
    bool targetPlaced = false;
    app->GetAlignmentState(angleRad, aligned, ready, targetPlaced);

    napi_value result = nullptr;
    napi_create_object(env, &result);
    napi_value vAngle = nullptr;
    napi_value vAligned = nullptr;
    napi_value vReady = nullptr;
    napi_value vTarget = nullptr;
    napi_create_double(env, static_cast<double>(angleRad), &vAngle);
    napi_get_boolean(env, aligned, &vAligned);
    napi_get_boolean(env, ready, &vReady);
    napi_get_boolean(env, targetPlaced, &vTarget);
    napi_set_named_property(env, result, "angleRad", vAngle);
    napi_set_named_property(env, result, "isAligned", vAligned);
    napi_set_named_property(env, result, "ready", vReady);
    napi_set_named_property(env, result, "targetPlaced", vTarget);
    return result;
}

// ---- ARRingHunt ----
napi_value NapiManager::NapiPlaceRing(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiPlaceRing");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string id = ReadIdArg(env, args[0]);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    int32_t objectId = app->PlaceRing();
    napi_value result = nullptr;
    napi_create_int32(env, objectId, &result);
    return result;
}

napi_value NapiManager::NapiResetRing(napi_env env, napi_callback_info info)
{
    LOGD("NapiManager::NapiResetRing");
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string id = ReadIdArg(env, args[0]);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);
    app->ResetRing();
    return nullptr;
}

napi_value NapiManager::NapiGetRingState(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string id = ReadIdArg(env, args[0]);
    AppNapi *app = NapiManager::GetInstance()->GetApp(id);

    float distance = 0.0f;
    bool ringPlaced = false;
    int32_t finishState = 0;
    bool isTargetInView = true;
    float screenEdgeX = 0.5f;
    float screenEdgeY = 0.5f;
    bool isBehind = false;
    float indicatorAngleDeg = 0.0f;
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    int32_t huntPhase = 0;
    float yawDiffRad = 0.0f;
    float pitchDiffRad = 0.0f;
    bool isAligned = false;
    bool isLocked = false;
    bool isViewingFromBack = false;
    app->GetRingState(distance, ringPlaced, finishState, isTargetInView, screenEdgeX, screenEdgeY, isBehind,
                      indicatorAngleDeg, ndcX, ndcY, huntPhase, yawDiffRad, pitchDiffRad, isAligned, isLocked,
                      isViewingFromBack);

    napi_value result = nullptr;
    napi_create_object(env, &result);
    napi_value vDist = nullptr;
    napi_value vPlaced = nullptr;
    napi_value vFinish = nullptr;
    napi_value vInView = nullptr;
    napi_value vEdgeX = nullptr;
    napi_value vEdgeY = nullptr;
    napi_value vBehind = nullptr;
    napi_value vIndicator = nullptr;
    napi_value vNdcX = nullptr;
    napi_value vNdcY = nullptr;
    napi_value vHuntPhase = nullptr;
    napi_value vYaw = nullptr;
    napi_value vPitch = nullptr;
    napi_value vAligned = nullptr;
    napi_value vLocked = nullptr;
    napi_value vViewBack = nullptr;
    napi_create_double(env, static_cast<double>(distance), &vDist);
    napi_get_boolean(env, ringPlaced, &vPlaced);
    napi_create_int32(env, finishState, &vFinish);
    napi_get_boolean(env, isTargetInView, &vInView);
    napi_create_double(env, static_cast<double>(screenEdgeX), &vEdgeX);
    napi_create_double(env, static_cast<double>(screenEdgeY), &vEdgeY);
    napi_get_boolean(env, isBehind, &vBehind);
    napi_create_double(env, static_cast<double>(indicatorAngleDeg), &vIndicator);
    napi_create_double(env, static_cast<double>(ndcX), &vNdcX);
    napi_create_double(env, static_cast<double>(ndcY), &vNdcY);
    napi_create_int32(env, huntPhase, &vHuntPhase);
    napi_create_double(env, static_cast<double>(yawDiffRad), &vYaw);
    napi_create_double(env, static_cast<double>(pitchDiffRad), &vPitch);
    napi_get_boolean(env, isAligned, &vAligned);
    napi_get_boolean(env, isLocked, &vLocked);
    napi_get_boolean(env, isViewingFromBack, &vViewBack);
    napi_set_named_property(env, result, "distance", vDist);
    napi_set_named_property(env, result, "ringPlaced", vPlaced);
    napi_set_named_property(env, result, "finishState", vFinish);
    napi_set_named_property(env, result, "isTargetInView", vInView);
    napi_set_named_property(env, result, "screenEdgeX", vEdgeX);
    napi_set_named_property(env, result, "screenEdgeY", vEdgeY);
    napi_set_named_property(env, result, "isBehind", vBehind);
    napi_set_named_property(env, result, "indicatorAngleDeg", vIndicator);
    napi_set_named_property(env, result, "ndcX", vNdcX);
    napi_set_named_property(env, result, "ndcY", vNdcY);
    napi_set_named_property(env, result, "huntPhase", vHuntPhase);
    napi_set_named_property(env, result, "yawDiffRad", vYaw);
    napi_set_named_property(env, result, "pitchDiffRad", vPitch);
    napi_set_named_property(env, result, "isAligned", vAligned);
    napi_set_named_property(env, result, "isLocked", vLocked);
    napi_set_named_property(env, result, "isViewingFromBack", vViewBack);
    return result;
}

// Create a service implementation class based on the service ID.
AppNapi *NapiManager::CreateApp(std::string &id)
{
    std::string scene;
    auto currentTime = std::chrono::system_clock::now();
    auto currentiTime_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(currentTime);
    auto valueMs = currentiTime_ms.time_since_epoch().count();
    int len = std::to_string(valueMs).length();

    if(id.length() >= len) {
        scene = id.substr(len);
        LOGE("NapiManager::Before CreateApp is:%{public}s", scene.c_str());
    }
    
    if (scene == std::string("ARWorld")) {
        return new ARWorld::ARWorldApp(scene);
    }
    if (scene == std::string("ARDepth")) {
        return new ARDepth::ARDepthApp(scene);
    }
    if (scene == std::string("ARMesh")) {
        return new ARMesh::ARMeshApp(scene);
    }
    if (scene == std::string("ARImage")) {
        return new ARImage::ARImageApp(scene);
    }
    if (scene == std::string("ARSemanticDense")) {
        return new ARSemanticDense::ArSemanticDenseApp(scene);
    }
    if (scene == std::string("ARObject")) {
        return new ARObject::ARObjectApp(scene);
    }
    if (scene == std::string("ARArrowAlign")) {
        return new ArrowAlign::ArrowAlignApp(scene);
    }
    if (scene == std::string("ARRingHunt")) {
        return new ARObject::RingHuntApp(scene);
    }
    return nullptr;
}
