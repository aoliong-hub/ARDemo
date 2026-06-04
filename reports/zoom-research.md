# 变焦调研 — AR Engine 5.0.0(12) 真变焦可行性

> 任务:Stage 12A 引入的变焦条(W / 1x / 2x / 4x / 10x)目前是纯视觉占位。本报告调研「能不能做真变焦」、「需要怎么做」、「工作量估计」。
> 调研范围:DevEco Studio SDK 自带 `ar/ar_engine_core.h`(API since 5.0.0(12),约 3200 行)+ 项目现有 native 代码。
> 结论先行:**AR Engine 不支持真光学变焦,也不支持镜头切换。可行的只有「GL 数字变焦」 — 裁剪预览纹理 + 同步缩放投影矩阵。3-4x 以上画质明显劣化,W(广角)不可达。** 实现工作量约 1-2 天。

---

## 1. AR Engine 公开的 zoom / 焦距 / FOV / 镜头切换 API

| 关键词 grep 结果(对 `ar_engine_core.h` 全文) | 是否存在? |
|---|---|
| `zoom` / `magnif` / `scale` | **0 处** |
| `fov` / `FieldOfView` | **0 处** |
| `lens` / `wide` / `tele` | **0 处** |
| `front` / `rear` / `facing` | 0 处(`PLANE_FACING_*` 是平面朝向枚举,与摄像头无关) |
| `cameraId` / `cameraType` / `cameraMode` | **0 处** |
| `focal` | 仅 1 处:`HMS_AREngine_ARCameraIntrinsics_GetFocalLength`(GET,只读) |
| `principal` | 仅 1 处:`HMS_AREngine_ARCameraIntrinsics_GetPrincipalPoint`(GET,只读) |
| `intrinsic` | 仅 4 处:全是 `GetXxx`,无 `SetXxx` |

**结论:AR Engine 不提供任何运行时变焦 / 镜头切换 / 焦距设置接口。**

相关数据结构:
- `AREngine_ARCameraIntrinsics` — 含 fx/fy/cx/cy + 畸变。**全部 Get,无 Set**(`ar_engine_core.h:1638-1699`)。
- `AREngine_ARCameraConfig` — 含图像/纹理尺寸。`HMS_AREngine_ARCameraConfig_Create` 创建空壳,`HMS_AREngine_ARSession_GetCameraConfig(session, &cfg)` 填充。**无任何 Setter**(`ar_engine_core.h:2567-2617`)。
- `AREngine_ARConfig`(session 配置)— 可 set 的字段:plane finding、update mode、power mode、focus mode、depth mode、mesh mode、pose mode、semantic dense、preview size、AR type、semantic mode、max map size、camera preview mode。**全部跟 zoom/FOV 无关**(`ar_engine_core.h:854-1204`)。
- 唯一接近的:`HMS_AREngine_ARConfig_SetPreviewSize(session, config, w, h)` — 但这只设**纹理缓冲尺寸**(GL 那张写入的预览贴图分辨率),不改 FOV / 焦距 / 视角。

---

## 2. 摄像头管线现状(本项目实现)

```
            ┌──────────────────────────────────────────┐
            │  AR Engine 内部                          │
            │  ┌──────────────┐    ┌──────────────┐    │
   摄像头帧→ │  │ pose tracker │ →  │ 渲染 GL 纹理 │    │  → renderManager.GetPreviewTextureId()
            │  └──────────────┘    └──────────────┘    │
            └──────────┬───────────────────┬───────────┘
                       │                   │
                       ↓                   ↓
              GetProjectionMatrix    SetCameraGLTexture(textureId)
              GetViewMatrix
                       │                   │
                       ↓                   ↓
            ┌──────────────────────────────────────────┐
            │  app native(world_background_renderer / │
            │  ring_hunt_render_manager 等)            │
            │  - 用 textureId 全屏画相机背景四边形       │
            │  - 用 view·proj 画 AR 叠加(信标/箭头/anchor) │
            └──────────────────────────────────────────┘
```

调用现场(`ring_hunt_ar_application.cpp:62` / `object_ar_application.cpp:62`):
```cpp
HMS_AREngine_ARSession_SetCameraGLTexture(mArSession, mRenderManager.GetPreviewTextureId());
HMS_AREngine_ARSession_SetDisplayGeometry(mArSession, mDisplayRotation, mWidth, mHeight);
```

预览纹理 UV 由 AR Engine 自己负责变换(处理屏幕旋转、横竖比):
```cpp
HMS_AREngine_ARFrame_TransformDisplayUvCoords(session, frame, elementSize, uvsIn, uvsOut);
```

投影矩阵由 AR Engine 从摄像头内参 + 显示几何算出:
```cpp
HMS_AREngine_ARCamera_GetProjectionMatrix(session, camera, clipPlaneDistance, outMat4x4, 16);
```

---

## 3. 数字变焦(GL 层裁剪 + 投影修正)— 唯一可行路径

### 3.1 原理
- **预览背景**:把"全屏 quad 采样整张相机纹理(UV 0..1)"改成"全屏 quad 采样纹理中心子矩形",中心子矩形大小 = 1/zoom。例如 zoom=2x → 采样 0.25..0.75 区间,得到 2 倍数字放大。
- **AR 叠加**:投影矩阵 fx/fy 同步乘以 zoom,使得世界中的 anchor 投到屏幕上的像素位置跟"被裁剪放大后的相机帧"对齐。
- **数学**:`P_zoomed = scaleNDC(zoom, zoom, 1) * P_arengine`。中心点 (cx, cy) 保持不变,焦距 fx → fx·zoom、fy → fy·zoom。

### 3.2 实现切入点(每个 zoom 需要改的地方)

| # | 文件 | 改动 | 工作量 |
|---|---|---|---|
| A | `ring_hunt_render_manager.cpp` 的 background pass(同理 `world_background_renderer.cpp` 等) | 给 background fragment shader 加一个 `u_zoom` uniform,采样时把 UV 经过 `(uv - 0.5) / zoom + 0.5` 重映射;`TransformDisplayUvCoords` 输出后再叠加该缩放 | 1h |
| B | `ring_hunt_render_manager.cpp` 的 anchor/wayfinder draw call | `GetProjectionMatrix` 得到的 `proj` 改用 `scale(zoom, zoom, 1) * proj`,各 mvp 重算 | 2h |
| C | `ring_hunt_ar_application.cpp` 的 off-screen NDC 计算(给 ArkTS 屏外引导用) | 用同样的 `scale * proj` 算 NDC,否则放大后水滴指引会偏 | 1h |
| D | `napi_manager.cpp` + `app_napi.h` | 新增 `setZoom(idStr, zoomLevel)` NAPI(纯数字,不动 AR session) | 30min |
| E | `ring_hunt_ar_application` 添加 `std::atomic<float> mZoom{1.0f}` 让 task 线程读 | — | 包含在 D 内 |
| F | ArkTS `ZoomBar` 选中时调 `arEngineDemo.setZoom(idStr, level)`(W=0.5? 不可行,见下) | — | 10min |
| G | hit-test 反映射 | 用户点屏幕坐标→还原到原始预览像素再传 `HMS_AREngine_ARFrame_HitTest`(本 demo 没用 HitTest,可暂不处理) | 跳过 |

合计 **~5-6 小时纯编码 + 4-6 小时联调/anchor 对齐 fix = 1-2 天**。

### 3.3 W(广角)— 不可达
W 意味着 FOV **比当前默认更大**,这需要**切换到广角镜头硬件**。AR Engine 不允许指定镜头,且其内部 SLAM 跟踪也依赖固定的镜头标定参数,即使能 bypass 切镜头,跟踪会立刻失稳。**结论:W 在 AR Engine 框架下不可实现。** 想做要么彻底放弃 AR Engine 用 Camera Kit 自己接(失去 6DoF),要么等华为 SDK 增加多镜头切换 API。

### 3.4 4x / 10x — 可达但画质差
裁剪到原图 25% / 10% 区间,有效像素从 1440×1080 跌到 360×270 / 144×108,**严重糊**。**实用上 2x 是上限**,3x 已经能看出软,4x 起明显马赛克,10x 完全不可用。

### 3.5 数字变焦的潜在风险
1. **anchor 对齐**:投影矩阵简单乘 scale 是近似 — 真实摄像头 cx/cy 不一定是图像中心,数字裁切的中心也不一定能完全匹配光学中心。需要联调微调。
2. **预览畸变补偿**:AR Engine 内部可能在内参里带畸变,直接裁切会让边缘畸变更明显。可接受。
3. **track 稳定度**:数字裁切只影响显示,跟踪用的是 AR Engine 内部的全分辨率帧,理论上不影响 pose 质量。
4. **`TransformDisplayUvCoords` 互相干扰**:AR Engine 自己已经在做 UV 变换处理屏幕旋转。我们的 zoom 裁切要叠加在它输出之后,不能替换它,否则旋转/翻转会错。

---

## 4. 推荐方案

| Stage | 内容 | 工作量 |
|---|---|---|
| **12A(当前)** | ZoomBar 纯视觉占位,5 档位只切高亮、不真变焦 | 已完成 |
| **12B(可选)** | 实现 1x / 2x **数字变焦**;3 + 档位仍占位或屏蔽 | ~1.5 天 |
| **12C(不推荐)** | 4x / 10x 数字变焦 | 0.5 天编码,但糊到几乎没用,**不建议交付** |
| **未来** | W(广角)等 AR Engine 加多镜头 API 才能做,目前**不可行** | — |

### 落地建议
- **演示阶段**:维持 12A 占位即可,变焦是相机 App 的"必备件",但 wayfinder demo 不依赖它。
- **正式版**:如果要做,只做 **1x → 2x** 这一段,效果足够说明"是真变焦"且画质能看;3 + 档隐藏。
- **走出 AR Engine**:如果产品真要 W/4x/10x 全套,得评估是否接 Camera Kit 自己驱动相机,然后把 AR Engine 的 6DoF 跟踪和预览解耦 — 这是一个 1-2 周的架构改造,**不在 Stage 12 范围**。

---

## 5. 已读 SDK API 清单(供后续参考)

config 相关(全部"Get/Set 是否存在"):
- SetPreviewSize / GetPlaneFindingMode / SetPlaneFindingMode / GetUpdateMode / SetUpdateMode
- GetPowerMode / SetPowerMode / GetFocusMode / SetFocusMode / GetDepthMode / SetDepthMode
- GetMeshMode / SetMeshMode / GetPoseMode / SetPoseMode
- SetSemanticDenseMode / GetSemanticDenseMode / GetARType / SetARType
- GetSemanticMode / SetSemanticMode / GetMaxMapSize / SetMaxMapSize
- SetCameraPreviewMode / GetCameraPreviewMode

camera 相关:
- ARSession_SetCameraGLTexture / ARSession_GetCameraConfig
- ARCamera_GetPose / GetDisplayOrientedPose / GetViewMatrix / GetTrackingState / GetTrackingStateReason
- ARCamera_GetProjectionMatrix / GetImageIntrinsics / Release
- ARCameraIntrinsics_Create/Destroy/GetFocalLength/GetPrincipalPoint/GetImageDimensions/GetDistortion
- ARCameraConfig_Create/Destroy/GetImageDimensions/GetTextureDimensions
- ARFrame_TransformDisplayUvCoords(关键 — 数字变焦要在它之后叠加)
- ARFrame_AcquireCamera / AcquireCameraImage(取 CPU 帧用)

**所有"摄像头能怎么样"的可调旋钮就这些 — 都不含 zoom / FOV / 镜头切换。**
