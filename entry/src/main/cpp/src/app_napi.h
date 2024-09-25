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

#ifndef APP_NAPI_H_
#define APP_NAPI_H_

#include <string>
#include <ace/xcomponent/native_interface_xcomponent.h>

class AppNapi {
public:
    explicit AppNapi(std::string& id) : id_(id) {};
    virtual ~AppNapi() = default;
    
    // XComponent Callback
    virtual void OnSurfaceCreated(OH_NativeXComponent* component, void* window) {};
    virtual void OnSurfaceChanged(OH_NativeXComponent *component, void *window) {};
    virtual void OnSurfaceDestroyed(OH_NativeXComponent *component, void *window) {};
    virtual void DispatchTouchEvent(OH_NativeXComponent *component, void *window) {};
    virtual void DispatchMouseEvent(OH_NativeXComponent *component, void *window) {};
    
    // Lifecycle method on the service side.
    virtual void OnStart() {};
    virtual void OnPause() {};
    virtual void OnResume() {};
    virtual void OnUpdate() {};
    virtual void OnStop() {};

public:
    std::string id_;
};

#endif // APP_NAPI_H_
