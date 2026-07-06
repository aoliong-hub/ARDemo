/**
 * NAPI 桥接层 — 将 NGA C++ 引擎暴露给 ArkTS 层
 *
 * 导出4个函数:
 *   ngaInitFramework(width, height)   → 初始化引擎
 *   ngaSetReference(data, w, h)       → 设置参考帧
 *   ngaProcessFrame(data, w, h)       → 处理当前帧 → 返回JSON
 *   ngaDestroyFramework()             → 销毁引擎
 */

#include <napi/native_api.h>
#include <hilog/log.h>
#include <string>
#include "nga_core.h"

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x3200
#define LOG_TAG "NGA_NAPI"

// ============================================================================
// 辅助函数：将 NgaResult 序列化为 JSON 字符串
// ============================================================================

static std::string ResultToJson(const NgaResult& result) {
    std::ostringstream json;
    json << "{";

    // success
    json << "\"success\":" << (result.success ? "true" : "false") << ",";

    // error_reason
    json << "\"error_reason\":\"" << result.error_reason << "\",";

    // alignment_method
    json << "\"alignment_method\":\"" << result.alignment_method << "\",";

    // is_aligned
    json << "\"is_aligned\":" << (result.is_aligned ? "true" : "false") << ",";

    // tracking_lost
    json << "\"tracking_lost\":" << (result.tracking_lost ? "true" : "false") << ",";

    // match_count
    json << "\"match_count\":" << result.match_count << ",";

    // dof
    json << "\"dof\":{";
    json << "\"dX_px\":" << result.dof.dX_px << ",";
    json << "\"dY_px\":" << result.dof.dY_px << ",";
    json << "\"dRoll_deg\":" << result.dof.dRoll_deg << ",";
    json << "\"dPitch_warp\":" << result.dof.dPitch_warp << ",";
    json << "\"dYaw_warp\":" << result.dof.dYaw_warp << ",";
    json << "\"scale_factor\":" << result.dof.scale_factor << ",";
    json << "\"perspective_ratio\":" << result.dof.perspective_ratio << ",";
    json << "\"is_pure_zoom\":" << (result.dof.is_pure_zoom ? "true" : "false");
    json << "},";

    // agent_commands
    json << "\"agent_commands\":[";
    for (size_t i = 0; i < result.commands.size(); i++) {
        if (i > 0) json << ",";
        // 转义双引号
        std::string cmd = result.commands[i];
        size_t pos = 0;
        while ((pos = cmd.find('"', pos)) != std::string::npos) {
            cmd.replace(pos, 1, "\\\"");
            pos += 2;
        }
        json << "\"" << cmd << "\"";
    }
    json << "]";

    json << "}";
    return json.str();
}

// ============================================================================
// NAPI 函数 1: ngaInitFramework
// ============================================================================

napi_value NgaInitFramework(napi_env env, napi_callback_info info) {
    OH_LOG_INFO(LOG_APP, "[NGA NAPI] ngaInitFramework called");

    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // 解析参数: width (number), height (number)
    int32_t width = 0, height = 0;
    napi_get_value_int32(env, args[0], &width);
    napi_get_value_int32(env, args[1], &height);

    OH_LOG_INFO(LOG_APP, "[NGA NAPI] init with size: %{public}d x %{public}d", width, height);

    NgaEngine* engine = GetNgaEngine();
    bool success = engine->init(width, height);

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

// ============================================================================
// NAPI 函数 2: ngaSetReference
// ============================================================================

napi_value NgaSetReference(napi_env env, napi_callback_info info) {
    OH_LOG_INFO(LOG_APP, "[NGA NAPI] ngaSetReference called");

    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // 解析参数: ArrayBuffer, width (number), height (number)
    void* data = nullptr;
    size_t dataLen = 0;
    napi_get_arraybuffer_info(env, args[0], &data, &dataLen);

    int32_t width = 0, height = 0;
    napi_get_value_int32(env, args[1], &width);
    napi_get_value_int32(env, args[2], &height);

    OH_LOG_INFO(LOG_APP, "[NGA NAPI] setReference: dataLen=%{public}zu, size=%{public}d x %{public}d",
                dataLen, width, height);

    NgaEngine* engine = GetNgaEngine();
    bool success = engine->setReferenceFrame(static_cast<const uint8_t*>(data), width, height);

    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

// ============================================================================
// NAPI 函数 3: ngaProcessFrame
// ============================================================================

napi_value NgaProcessFrame(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // 解析参数: ArrayBuffer, width, height
    void* data = nullptr;
    size_t dataLen = 0;
    napi_get_arraybuffer_info(env, args[0], &data, &dataLen);

    int32_t width = 0, height = 0;
    napi_get_value_int32(env, args[1], &width);
    napi_get_value_int32(env, args[2], &height);

    NgaEngine* engine = GetNgaEngine();
    NgaResult ngaResult = engine->processFrame(static_cast<const uint8_t*>(data), width, height);

    // 序列化为 JSON 字符串返回
    std::string jsonStr = ResultToJson(ngaResult);

    napi_value result;
    napi_create_string_utf8(env, jsonStr.c_str(), jsonStr.length(), &result);
    return result;
}

// ============================================================================
// NAPI 函数 4: ngaDestroyFramework
// ============================================================================

napi_value NgaDestroyFramework(napi_env env, napi_callback_info info) {
    OH_LOG_INFO(LOG_APP, "[NGA NAPI] ngaDestroyFramework called");

    ReleaseNgaEngine();

    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

// ============================================================================
// NGA NAPI 函数导出声明 (注册在 module.cpp 中)
// ============================================================================
// 这些函数不是 static，由 module.cpp 的 Init 统一注册到 libentry.so 的 NAPI 导出表
