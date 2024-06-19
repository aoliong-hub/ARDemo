/**
 * Copyright 2022. Huawei Technologies Co., Ltd. All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "world_ar_application.h"

#include <array>

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/quaternion.hpp>

#include <ace/xcomponent/native_interface_xcomponent.h>

#include "world_render_manager.h"
#include "app_util.h"

namespace ArWorld {
    namespace {
        constexpr size_t K_MAX_NUMBER_OF_OBJECT_RENDERED = 10;
    }

    ArWorldApp::ArWorldApp(std::string& id) 
    : AppNapi(id)
    {
        LOGD("ArWorldApp::Constructor()");
    }

    ArWorldApp::~ArWorldApp()
    {
        LOGD("ArWorldApp::Destructor()");
        mTaskQueue.Stop();
    }

    void ArWorldApp::OnStart() {
        LOGD("ArWorldApp::OnStart() (%{public}lu, %{public}lu)", mWidth, mHeight);
        mTaskQueue.Start();
        mTaskQueue.Push([this] {
            CHECK(HMS_AREngine_ARSession_Create(nullptr, nullptr, &mArSession));
            AREngine_ARConfig *arConfig = nullptr;
            CHECK(HMS_AREngine_ARConfig_Create(mArSession, &arConfig));
            CHECK(HMS_AREngine_ARConfig_SetPreviewSize(mArSession, arConfig, 1440, 1080));
            CHECK(HMS_AREngine_ARConfig_SetUpdateMode(mArSession, arConfig, ARENGINE_UPDATE_MODE_LATEST));
            CHECK(HMS_AREngine_ARSession_Configure(mArSession, arConfig));
            HMS_AREngine_ARConfig_Destroy(arConfig);

            CHECK(HMS_AREngine_ARFrame_Create(mArSession, &mArFrame));

            CHECK(HMS_AREngine_ARSession_SetDisplayGeometry(mArSession, mDisplayRotation, mWidth, mHeight));
        });
    }

    void ArWorldApp::OnStop() {
        isPaused = true;
        mTaskQueue.Push([this] {
            LOGD("ArWorldApp::OnStop()");
            for (auto &anchor : mColoredAnchors) {
                CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, anchor.anchor));
                HMS_AREngine_ARAnchor_Release(anchor.anchor);
            }
            mColoredAnchors.clear();

            if (mArFrame != nullptr) {
                HMS_AREngine_ARFrame_Destroy(mArFrame);
                mArFrame = nullptr;
            }

            if (mArSession != nullptr) {
                HMS_AREngine_ARSession_Destroy(mArSession);
                mArSession = nullptr;
            }
        });
    }

    void ArWorldApp::OnPause() {
        isPaused = true;
        mTaskQueue.Push([this] {
            LOGD("ArWorldApp::OnPause()");
            CHECK(HMS_AREngine_ARSession_Pause(mArSession));
        });
    }

    void ArWorldApp::OnResume() {
        isPaused = false;
        mTaskQueue.Push([this] {
            LOGD("ArWorldApp::OnResume()");
            HMS_AREngine_ARSession_Resume(mArSession);
        });
    }

    void ArWorldApp::OnUpdate()
    {
        if (isPaused) {
            LOGD("ArWorldApp::OnUpdate is paused");
            return;
        }
        mTaskQueue.Push([this] {
            if (isPaused) {
                LOGI("ArWorldApp OnDrawFrame isPaused!");
                return;
            }
            LOGD("ArWorldApp::OnDrawFrame()");
            mWorldRenderManager.OnDrawFrame(mArSession, mArFrame, mColoredAnchors);
        });
    }

    void ArWorldApp::OnSurfaceCreated(OH_NativeXComponent *component, void *window) {
        LOGD("ArWorldApp::OnSurfaceCreated()");

        int32_t ret = OH_NativeXComponent_GetXComponentSize(component, window, &mWidth, &mHeight);
        LOGD("ArWorldApp::OnSurfaceCreated size (%{public}lu, %{public}lu)", mWidth, mHeight);
        if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            ret = OH_NativeXComponent_GetXComponentOffset(component, window, &mX, &mY);
            if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
                LOGE("ArWorldApp::OnSurfaceCreated Offset : x = %{public}lf, y = %{public}lf ", mX, mY);
            }
        }
        mTaskQueue.Push([this, window] {
            LOGD("WorldRenderManager init");
            mWorldRenderManager.Initialize(window);
            CHECK(HMS_AREngine_ARSession_SetDisplayGeometry(mArSession, mDisplayRotation, mWidth, mHeight));
        });
    }

    void ArWorldApp::OnSurfaceChanged(OH_NativeXComponent *component, void *window) {
        int32_t ret = OH_NativeXComponent_GetXComponentSize(component, window, &mWidth, &mHeight);
        LOGD("ArWorldApp::OnSurfaceChanged(%{public}lu, %{public}lu)", mWidth, mHeight);
        if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            LOGD("after width = %lu, height = %lu", mWidth, mHeight);
            ret = OH_NativeXComponent_GetXComponentOffset(component, window, &mX, &mY);
            if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
                LOGD("Offset : x = %{public}lf, y = %{public}lf ", mX, mY);
            } else {
                LOGE("Offset get failed");
            }
        }

        mTaskQueue.Push([this] {
            if (isPaused) {
                LOGI("ArWorldApp OnSurfaceChanged isPaused!");
                return;
            }
            glViewport(0, 0, mWidth, mHeight);
            CHECK(HMS_AREngine_ARSession_SetDisplayGeometry(mArSession, mDisplayRotation, mWidth, mHeight));
        });
    }

    void ArWorldApp::DispatchTouchEvent(void *window, float eventX, float eventY)
    {
        AREngine_ARHitResultList *hitResultList = nullptr;
        CHECK(HMS_AREngine_ARHitResultList_Create(mArSession, &hitResultList));
        CHECK(HMS_AREngine_ARFrame_HitTest(mArSession, mArFrame, eventX, eventY, hitResultList));
    
        int32_t hitResultListSize = 0;
        CHECK(HMS_AREngine_ARHitResultList_GetSize(mArSession, hitResultList, &hitResultListSize));
        LOGD("HMS_AREngine_ARHitResultList_GetSize %{public}d", hitResultListSize);
        // The hitTest method sorts the result list by the distance to the camera in ascending order.
        // When responding to user input, the first hit result is usually most relevant.
        AREngine_ARHitResult *arHitResult = nullptr;
        AREngine_ARTrackableType trackableType = ARENGINE_TRACKABLE_INVALID;
        bool hasHitFlag = false;

        if (!GetHitResult(arHitResult, hasHitFlag, hitResultListSize, trackableType, hitResultList)) {
            return;
        }
        if (hasHitFlag != true) {
            return;
        }
        if (arHitResult) {
            // Note that the app should release the anchor pointer after using it.
            // Call ArAnchor_release(anchor) to release the anchor.
            AREngine_ARAnchor *anchor = nullptr;
            CHECK(HMS_AREngine_ARHitResult_AcquireNewAnchor(mArSession, arHitResult, &anchor));
        
            AREngine_ARTrackingState trackingState = ARENGINE_TRACKING_STATE_STOPPED;
            CHECK(HMS_AREngine_ARAnchor_GetTrackingState(mArSession, anchor, &trackingState));
            if (trackingState != ARENGINE_TRACKING_STATE_TRACKING) {
                HMS_AREngine_ARAnchor_Release(anchor);
                return;
            }
            if (mColoredAnchors.size() >= K_MAX_NUMBER_OF_OBJECT_RENDERED) {
                CHECK(HMS_AREngine_ARAnchor_Detach(mArSession, mColoredAnchors[0].anchor));
                HMS_AREngine_ARAnchor_Release(mColoredAnchors[0].anchor);
                mColoredAnchors.erase(mColoredAnchors.begin());
            }
            SetAnchorColour(anchor, trackableType);
            HMS_AREngine_ARHitResult_Destroy(arHitResult);
            arHitResult = nullptr;

            HMS_AREngine_ARHitResultList_Destroy(hitResultList);
            hitResultList = nullptr;
        } else {
            LOGE("ArWorldApp::OnTouched arHitResult empty");
        }
    }
    void ArWorldApp::DispatchTouchEvent(OH_NativeXComponent *component, void *window) //OnTouched(float eventX, float eventY)
    {
        LOGD("ArWorldApp::OnTouched()");
        float eventX = 0.0f;
        float eventY = 0.0f;
        int32_t ret = OH_NativeXComponent_GetTouchEvent(component, window, &mTouchEvent);
        if (ret == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            if (mTouchEvent.type == OH_NATIVEXCOMPONENT_DOWN) {
                eventX = mTouchEvent.touchPoints[0].x;
                eventY = mTouchEvent.touchPoints[0].y;
                LOGD("Pos: %{public}f %{public}f", eventX, eventY);
            } else {
                return;
            }
        } else {
            LOGE("Touch fail");
            return;
        }
    
        mTaskQueue.Push([this, window, eventX, eventY] {
            if (isPaused) {
                LOGI("ArWorldApp DispatchTouchEvent isPaused!");
                return;
            }
            DispatchTouchEvent(window, eventX, eventY);
        });
    }

    void ArWorldApp::OnSurfaceDestroyed(OH_NativeXComponent *component, void *window) 
    {
        LOGD("ArWorldApp::OnSurfaceDestroyed");
        mTaskQueue.Push([this] {
            LOGD("WorldRenderManager release");
            mWorldRenderManager.Release();
        });
    }


    bool ArWorldApp::GetHitResult(AREngine_ARHitResult *&arHitResult, bool &hasHitFlag, int32_t hitResultListSize,
                                  AREngine_ARTrackableType &trackableType,
                                  AREngine_ARHitResultList *hitResultList) const {
        for (int32_t i = 0; i < hitResultListSize; ++i) {
            AREngine_ARHitResult *arHit = nullptr;
            CHECK(HMS_AREngine_ARHitResult_Create(mArSession, &arHit));
            CHECK(HMS_AREngine_ARHitResultList_GetItem(mArSession, hitResultList, i, arHit));

            if (arHit == nullptr) {
                return false;
            }

            AREngine_ARTrackable *arTrackable = nullptr;
            CHECK(HMS_AREngine_ARHitResult_AcquireTrackable(mArSession, arHit, &arTrackable));
            AREngine_ARTrackableType ar_trackable_type = ARENGINE_TRACKABLE_INVALID;
            CHECK(HMS_AREngine_ARTrackable_GetType(mArSession, arTrackable, &ar_trackable_type));

            // If a plane or directional point is encountered, an anchor point is created.
            if (ARENGINE_TRACKABLE_PLANE == ar_trackable_type) {
                AREngine_ARPose *arPose = nullptr;
                CHECK(HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &arPose));
                CHECK(HMS_AREngine_ARHitResult_GetHitPose(mArSession, arHit, arPose));
                int32_t inPolygon = 0;
                AREngine_ARPlane *arPlane = reinterpret_cast<AREngine_ARPlane *>(arTrackable);
                CHECK(HMS_AREngine_ARPlane_IsPoseInPolygon(mArSession, arPlane, arPose, &inPolygon));

                // Use the hit pose and camera pose to check whether the hit position comes
                // from the back of the plane.
                // If yes, no anchor needs to be created.
                AREngine_ARPose *cameraPose = nullptr;
                CHECK(HMS_AREngine_ARPose_Create(mArSession, nullptr, 0, &cameraPose));
                AREngine_ARCamera *arCamera;
                CHECK(HMS_AREngine_ARFrame_AcquireCamera(mArSession, mArFrame, &arCamera));
                CHECK(HMS_AREngine_ARCamera_GetPose(mArSession, arCamera, cameraPose));
                HMS_AREngine_ARCamera_Release(arCamera);
                float normal_distance_to_plane = CalculateDistanceToPlane(mArSession, arPose, cameraPose);

                HMS_AREngine_ARPose_Destroy(arPose);
                HMS_AREngine_ARPose_Destroy(cameraPose);
                if (!inPolygon || normal_distance_to_plane < 0) {
                    continue;
                }

                arHitResult = arHit;
                trackableType = ar_trackable_type;
                hasHitFlag = true;
                break;
            } else if (ARENGINE_TRACKABLE_POINT == ar_trackable_type) {
                AREngine_ARPoint *ar_point = reinterpret_cast<AREngine_ARPoint *>(arTrackable);
                AREngine_ARPointOrientationMode mode;
                CHECK(HMS_AREngine_ARPoint_GetOrientationMode(mArSession, ar_point, &mode));
                if (ARENGINE_POINT_ORIENTATION_ESTIMATED_SURFACE_NORMAL == mode) {
                    arHitResult = arHit;
                    trackableType = ar_trackable_type;
                    hasHitFlag = true;
                }
            }
        }
        return true;
    }

    void ArWorldApp::SetAnchorColour(AREngine_ARAnchor *anchor, AREngine_ARTrackableType trackableType) {
        ColoredAnchor coloredAnchor{};
        coloredAnchor.anchor = anchor;
        switch (trackableType) {
        case ARENGINE_TRACKABLE_POINT:
            // Set the anchor color when the anchor is generated due to click on the point cloud.
            SetColor(66.0f, 133.0f, 244.0f, 255.0f, coloredAnchor);
            break;
        case ARENGINE_TRACKABLE_PLANE:
            // Set the anchor color when the anchor is generated due to click on the point cloud.
            SetColor(139.0f, 195.0f, 74.0f, 255.0f, coloredAnchor);
            break;
        default:
            // The virtual object is not displayed if it is not generated by click on the point cloud or plane.
            SetColor(0.0f, 0.0f, 0.0f, 0.0f, coloredAnchor);
            break;
        }
        mColoredAnchors.push_back(coloredAnchor);
    }

    void ArWorldApp::SetColor(float colorR, float colorG, float colorB, float colorA, ColoredAnchor &coloredAnchor)
    {
        // Set the color.
        *(coloredAnchor.color) = colorR;
        *(coloredAnchor.color + 1) = colorG;
        *(coloredAnchor.color + 2) = colorB;
        *(coloredAnchor.color + 3) = colorA;
    }
}