#include "global.h"
#include "utils/native_common.h"
#include "utils/log.h"

NativeResourceManager* Global::mNativeResMgr = nullptr;

napi_value Global::Init(napi_env env, napi_callback_info info) 
{
    LOGE("Global::Init");
    napi_status status;
    napi_value exports;
    size_t argc = 1;
    napi_value args[1];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));

    mNativeResMgr = OH_ResourceManager_InitNativeResourceManager(env, args[0]);

    return 0;
}