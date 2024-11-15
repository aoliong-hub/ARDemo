# AREngine

## Introduction

This example demonstrates the plane detection, motion tracking, environment tracking, and hit detection capabilities provided by AREngine.

## Effect Preview

|                **App home page**                 |                  **Identify plane**                  |         **Display model through hit detection**            |
|:-----------------------------------:|:--------------------------------------:|:---------------------------------------:|
| ![](entry/screenshots/homePage.jpg) | ![](entry/screenshots/detectPlane.jpg) | ![](entry/screenshots/showModel.jpg) |


1. On the home screen of your phone, touch ArSample to launch the app. The ArWorld button is displayed on the home screen.
2. Click ArWorld to open the ArEngine plane identification interface. Move slowly to the ground, desktop, and wall to identify the plane and draw it on the screen.
3. After a plane is identified, click a point on the plane. Through the hit detection capability provided by AREngine, a 3D model is placed at the clicked position on the screen.


## Implementation
### Integration Service
To use the AREngine service interface, you need to introduce dependencies in CMakeLists.
find_library(
arengine-lib
libarengine_ndk.z.so
)
target_link_libraries(entry PUBLIC
${arengine-lib}
)

Header files are introduced during use.
#include "ar/ar_engine_core.h"



### Code Structure
```cpp
├─entry/src/main
│  module.json5                                            // Module configuration file             
│ 
├─cpp                                                      // C++ code area
│  │  CMakeLists.txt                                       // CMake configuration file
│  │
│  ├─src
│  │  │  app_napi.h                                        // Virtual base class on the service side.
│  │  │  global.cpp                                        // NAPI initialization.
│  │  │  global.h                                          // Mapping configuration between C++ and ETS APIs
│  │  │  module.cpp                                        // C++ API registration
│  │  │  napi_manager.cpp                                  // C++ API implementation
│  │  │  napi_manager.h
│  │  │
│  │  ├─graphic                                            // Rendering-related utility class
│  │  │
│  │  ├─utils                                              // Utility class                                                              
│  │  │
│  │  └─world                                              // ArWorld module
│  │          world_ar_application.cpp                     // ArWorld module API implementation
│  │          world_ar_application.h
│  │          world_background_renderer.cpp                // Background rendering
│  │          world_background_renderer.h
│  │          world_object_renderer.cpp                    // 3D object rendering
│  │          world_object_renderer.h
│  │          world_plane_renderer.cpp                     // Plane rendering
│  │          world_plane_renderer.h
│  │          world_render_manager.cpp                     // Rendering of each frame
│  │          world_render_manager.h
│  │
│  ├─thirdparty                                            // Rendering-related third-party libraries
│  └─types                                                 // Folder for storing APIs
│      └─libentry
│              index.d.ts                                  // API file
│              oh-package.json5                            // API registration configuration file
│
├─ets                                                      // ETS code area
│  ├─entryability
│  │      EntryAbility.ets                                 // Entry point class
│  │
│  ├─pages
│  │      ArWorld.ets                                      // ArWorld screen
│  │      Selector.ets                                     // Home screen
│  │
│  └─utils
│          Logger.ets                                      // ETS log printing
│
└─resources                                                // Directory for storing resource files
```

### Create interfaces related to sessions and frame data.
```c
AREngine_ARStatus HMS_AREngine_ARConfig_Create(const AREngine_ARSession *session, AREngine_ARConfig **outConfig);
void HMS_AREngine_ARConfig_Destroy(AREngine_ARConfig *config);

AREngine_ARStatus HMS_AREngine_ARSession_Create(void *env, void *applicationContext, AREngine_ARSession **outSessionPointer);
AREngine_ARStatus HMS_AREngine_ARSession_Configure(AREngine_ARSession *session, const AREngine_ARConfig *config);
void HMS_AREngine_ARSession_Destroy(AREngine_ARSession *session);

AREngine_ARStatus HMS_AREngine_ARFrame_Create(const AREngine_ARSession *session, AREngine_ARFrame **outFrame);
void HMS_AREngine_ARFrame_Destroy(AREngine_ARFrame *frame);
```


### Interfaces related to plane identification:
```c
AREngine_ARStatus HMS_AREngine_ARTrackableList_Create(const AREngine_ARSession *session, AREngine_ARTrackableList **outTrackableList);
AREngine_ARStatus HMS_AREngine_ARSession_GetAllTrackables(const AREngine_ARSession *session, AREngine_ARTrackableType filterType, AREngine_ARTrackableList *outTrackableList);
AREngine_ARStatus HMS_AREngine_ARTrackableList_GetSize(const AREngine_ARSession *session, const AREngine_ARTrackableList *trackableList, int32_t *outSize);
AREngine_ARStatus HMS_AREngine_ARTrackableList_AcquireItem(const AREngine_ARSession *session, const AREngine_ARTrackableList *trackableList, int32_t index, AREngine_ARTrackable **outTrackable);
void HMS_AREngine_ARTrackableList_Destroy(AREngine_ARTrackableList *trackableList);

AREngine_ARStatus HMS_AREngine_ARTrackable_GetTrackingState(const AREngine_ARSession *session, const AREngine_ARTrackable *trackable, AREngine_ARTrackingState *outTrackingState);
void HMS_AREngine_ARTrackable_Release(AREngine_ARTrackable *trackable);

AREngine_ARStatus HMS_AREngine_ARPlane_AcquireSubsumedBy(const AREngine_ARSession *session, const AREngine_ARPlane *plane, AREngine_ARPlane **outSubsumedBy);
AREngine_ARStatus HMS_AREngine_ARPlane_AcquireSubsumedBy(const AREngine_ARSession *session, const AREngine_ARPlane *plane, AREngine_ARPlane **outSubsumedBy);
AREngine_ARStatus HMS_AREngine_ARPlane_GetCenterPose(const AREngine_ARSession *session, const AREngine_ARPlane *plane, AREngine_ARPose *outPose);
AREngine_ARStatus HMS_AREngine_ARPlane_GetPolygonSize(const AREngine_ARSession *session, const AREngine_ARPlane *plane, int32_t *outPolygonSize);
AREngine_ARStatus HMS_AREngine_ARPlane_GetPolygon(const AREngine_ARSession *session, const AREngine_ARPlane *plane, float *outPolygonXz, int32_t polygonSize);
AREngine_ARStatus HMS_AREngine_ARPlane_IsPoseInPolygon(const AREngine_ARSession *session, const AREngine_ARPlane *plane, const AREngine_ARPose *pose, int32_t *outPoseInPolygon);
```

### Interfaces related to hit detection:
```c
AREngine_ARStatus HMS_AREngine_ARHitResultList_Create(const AREngine_ARSession *session, AREngine_ARHitResultList **outHitResultList);
AREngine_ARStatus HMS_AREngine_ARHitResultList_GetSize(const AREngine_ARSession *session, const AREngine_ARHitResultList *hitResultList, int32_t *outSize);
AREngine_ARStatus HMS_AREngine_ARHitResultList_GetItem(const AREngine_ARSession *session, const AREngine_ARHitResultList *hitResultList, int32_t index, AREngine_ARHitResult *outHitResult);
void HMS_AREngine_ARHitResultList_Destroy(AREngine_ARHitResultList *hitResultList);

AREngine_ARStatus HMS_AREngine_ARHitResult_AcquireNewAnchor(AREngine_ARSession *session, AREngine_ARHitResult *hitResult, AREngine_ARAnchor **outAnchor);
AREngine_ARStatus HMS_AREngine_ARHitResult_GetHitPose(const AREngine_ARSession *session, const AREngine_ARHitResult *hitResult, AREngine_ARPose *outPose);
AREngine_ARStatus HMS_AREngine_ARHitResult_AcquireTrackable(const AREngine_ARSession *session, const AREngine_ARHitResult *hitResult, AREngine_ARTrackable **outTrackable);
void HMS_AREngine_ARHitResult_Destroy(AREngine_ARHitResult *hitResult);
```

## Related Permissions

Use the camera, accelerometer, and gyroscope sensor permissions. The camera permission is requested by the app.
1. Camera permission: ohos.permission.CAMERA.
2. Acceleration sensor permission: ohos.permission.ACCELEROMETER.
3. Gyroscope sensor permission: ohos.permission.GYROSCOPE.

## Dependency

Depends on the device's camera, acceleration sensor, and gyroscope sensor capabilities.

## Constraints

1. This instance can run only on the standard system and supports Huawei mobile phones (Mate 60,Mate 60 Pro,and Mate X5).
2. DevEco Studio version: DevEco Studio NEXT Developer Beta2 or later.
3. HarmonyOS SDK version: HarmonyOS NEXT Developer Beta2 and above.