#ifndef GLOBAL_H_
#define GLOBAL_H_

#include <napi/native_api.h>
#include <rawfile/raw_file_manager.h>

class Global 
{
public:
    static napi_value Init(napi_env env, napi_callback_info info);
    
    static NativeResourceManager* mNativeResMgr;
};

#endif // GLOBAL_H_
