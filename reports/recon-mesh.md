# ARMesh 定向侦察 + 接口式放置 MVP 方案 (recon-mesh)

> 生成日期:2026-05-25 · 仅侦察,无代码改动、未装机、未 commit
> 触发:需求变更——从"手指点击放置 + 旋转/缩放/删除"改为**接口式放置** `placeObjectAtWorld(x,y,z,modelId)`

---

## 先说结论(TL;DR)

1. **你说的"A 字符"= `AR_logo_obj.obj` + `AR_logo.png`**(AR Engine 的 logo,形似 A)。ARMesh 和 ARWorld 用的**是同一个模型文件**,只是 ARMesh 关了平面检测、靠环境网格 hit test,所以"点哪都能放"。
2. ARMesh 的放置链路 = native 触摸 → **hit test 打场景网格** → 命中点建 anchor → 每帧按 anchor 位姿渲染 logo。**用的是 anchor,所以稳定不飘。**
3. 你要的"接口式放置"**根本不需要 hit test**:NDK 里有 `HMS_AREngine_ARSession_AcquireNewAnchor(session, pose, &anchor)`(API 12 起,API 20 可用),直接给一个世界位姿就能建 anchor。这是整个 MVP 的支点,已在 SDK 头文件中核实存在。
4. **现成的 logo 渲染代码可以几乎原样复用**,只需把"anchor 从哪来"从 hit test 换成 ARPose。
5. 推荐:**复制 ARWorld/ARMesh 模式新建一个独立 `ARObject` 场景**(而非改 ARMesh 本身),避免污染官方 sample、且 baseline 完全不受影响。

---

## a) ARMesh 场景的入口 ets 文件

`entry/src/main/ets/pages/ARMesh.ets`(`@Builder ARMeshBuilder` → `struct ARMesh`)

- XComponent 挂载 `ARMesh.ets:40`:`XComponent({ id, type: SURFACE, libraryname: 'entry' })`,`id = 时间戳 + 'ARMesh'`
- 帧驱动 `ARMesh.ets:49`:`setInterval(33ms)` → `arEngineDemo.update(idStr)`(**返回值被忽略**,ARMesh 没有平面数 UI)
- 生命周期:`init` / `start(id, Int32Array[1,rotation])` / `update` / `show` / `hide` / `stop` —— 与 ARWorld 完全同构
- **UI 极简**:RelativeContainer 里只有一个全屏 XComponent,无按钮、无状态条、无 ArkTS 手势

导航注册:`resources/base/profile/router_map.json` → `ARMesh → pages/ARMesh.ets → ARMeshBuilder`;入口按钮在 `pages/Selector.ets`。

---

## b) ARMesh 的 native 主循环

工厂分发:`napi_manager.cpp:495` `scene=="ARMesh"` → `new ARMesh::ARMeshApp(scene)`

- **会话配置**(`mesh_ar_application.cpp:34 OnStart`)——**与 ARWorld 的关键差异在这里**:
  ```cpp
  HMS_AREngine_ARConfig_SetPlaneFindingMode(..., ARENGINE_PLANE_FINDING_MODE_DISABLED); // 关平面检测
  HMS_AREngine_ARConfig_SetMeshMode(..., ARENGINE_MESH_MODE_ENABLED);                   // 开场景网格重建
  ```
- **每帧主循环**(`mesh_ar_application.cpp:99 OnUpdate`)→ 推任务队列:
  `SetCameraGLTexture` → `SetDisplayGeometry` → `ARSession_Update` → 排空 `mTouchPoints` 逐个 `DispatchTouchEvent(x,y)` → `MeshRenderManager::OnDrawFrame()`
- **渲染**(`mesh_render_manager.cpp:69 OnDrawFrame`):取相机 view/projection 矩阵 → 画相机背景 → `SceneMeshDisplayRenderer.onDrawFrame`(画环境网格线框)→ `RenderObject`(画 logo)→ SwapBuffers

---

## c) "屏幕点击 → 出现 A 字符" 完整链路

### ① 点击事件从哪进来 —— **native,不是 ArkTS**
- `NapiManager::DispatchTouchEventCB`(`napi_manager.cpp:83`)经 `OH_NativeXComponent_RegisterCallback` 注册
- → `ARMeshApp::DispatchTouchEvent(component, window)`(`mesh_ar_application.cpp:175`):只取 `OH_NATIVEXCOMPONENT_DOWN` 单指 `touchPoints[0].x/y`,**push 进 `mTouchPoints` 队列**(不立即处理)
- → 下一帧 `OnUpdate` 排空队列,调 `DispatchTouchEvent(float x, float y)`(`:209`)做真正的 hit test
- ArkTS 侧 `ARMesh.ets` **没有任何 `.onTouch` / `.gesture`**

### ② 屏幕坐标 → 世界坐标 —— **用 hit test(打场景网格,不是平面)**
`mesh_ar_application.cpp:209 DispatchTouchEvent(x,y)`:
```cpp
HMS_AREngine_ARFrame_HitTest(session, frame, x, y, hitResultList);  // 屏幕(x,y)射线求交
GetHitResult(...);            // 见 :256
HMS_AREngine_ARHitResult_AcquireNewAnchor(session, arHitResult, &anchor); // 命中点建 anchor
```
- `GetHitResult`(`:256`)与 ARWorld 不同:**不按 trackable 类型过滤**(ARWorld 只收 PLANE 且要在多边形内)。这里遍历命中列表、对每个取 `GetHitPose`,循环里反复覆盖 `arHitResult` → **实际取的是命中列表最后一个**(距离最远的那个网格命中)。
- 命中目标是 **`ARENGINE_MESH` 场景重建网格**(因为平面检测被关了),所以对着墙/物体/地面任意有重建网格处点都能命中。

### ③ A 字符怎么渲染的 —— **.obj 顶点网格 + .png 纹理**(不是字体/不是 glTF)
- 模型加载 `mesh_render_manager.cpp:39`:`mObjectRenderer.InitializeObjectGlContent("AR_logo_obj.obj", "AR_logo.png")`
- `mesh_object_renderer.cpp`:`LoadObjFile` 读顶点/法线/UV/索引;`LoadPngFromAssetManager` 读纹理;自带 Phong-ish vertex+fragment shader(`:23`/`:40`)。**不是文字渲染,是一个三角网格模型。**
- 资源位置:`entry/src/main/resources/rawfile/`(obj/png 作为 rawfile 打包,`init(resMgr)` 传入 resourceManager 供 native 读取)
- 着色器里有个细节(`:63`):若 `u_ObjColor.a >= 255` 则用顶点色 × 纹理灰度上色(ARMesh 给的是蓝色 `66,133,244,255`,见 `:285`)

### ④ 模型矩阵怎么算 —— **anchor 位姿 × 缩放**
`mesh_render_manager.cpp:103 RenderObject`(与 ARWorld `:125` 逻辑几乎一致):
```cpp
HMS_AREngine_ARAnchor_GetTrackingState(...);     // 仅 TRACKING 才画
HMS_AREngine_ARAnchor_GetPose(session, anchor, pose);
HMS_AREngine_ARPose_GetMatrix(session, pose, modelMat, 16);  // modelMat = anchor 世界位姿(列主序)
modelMat = glm::scale(modelMat, vec3(0.2f));     // 缩 0.2 倍
mObjectRenderer.Draw(projectionMat, viewMat, modelMat, 0.8f, color);
// Draw 内部:mvp = projection * view * model
```
即 **模型矩阵 = anchorPose,最终 MVP = projection × cameraView × anchorPose × scale**。

### ⑤ 用 anchor 还是每帧重算 —— **用 anchor**
- 命中点经 `AcquireNewAnchor` 转成 anchor,存进 `std::vector<ColoredAnchor> mColoredAnchors`(上限 10,超了删最早)
- 每帧从 anchor **重新取位姿**(`GetPose`)。AR Engine 会持续修正 anchor 位姿来抵消漂移/重定位 → **这就是"稳定不飘"的根因**。

---

## d) 现成 A 字符渲染代码能否直接复用做"接口式放置任意位置"

**能,且改动很小。** 渲染侧(`*_object_renderer` + `RenderObject` 的"遍历 anchor → 取位姿 → 画模型")与"anchor 是怎么来的"完全解耦:
- 现状:anchor 来自 `HMS_AREngine_ARHitResult_AcquireNewAnchor`(hit test)
- MVP:anchor 改为来自 `HMS_AREngine_ARSession_AcquireNewAnchor(session, pose, &anchor)`(给定世界位姿)
- 渲染、anchor 列表管理、tracking 校验、生命周期清理 **全部原样复用**。

唯一新增的是:把上层传入的 `(x,y,z)` 包成 `AREngine_ARPose` 再建 anchor。

---

## 3. MVP 实现方案(不写代码)

### 3.1 基于哪个场景改 —— 推荐**新建 `ARObject` 场景**(复制 World/Mesh 模式)

| 选项 | 评价 |
|---|---|
| 改 ARMesh 本体 | ❌ 破坏官方 sample 文件(红线);且 ARMesh 开了 MeshMode 较重,接口放置用不上 |
| 改 ARWorld 本体 | ❌ 同样破坏官方文件;且 baseline 已验证,不想动 |
| **新建 `ARObject` 场景** | ✅ 推荐。新增独立文件,官方 5 个场景一字不动,baseline 零风险;Selector 加一个入口按钮即可 |

新建场景需要的文件(均为**新增**,不改官方文件,符合"不重命名/删除官方文件"红线):
- `entry/src/main/ets/pages/ARObject.ets`(仿 ARMesh.ets,XComponent id=时间戳+'ARObject')
- `entry/src/main/cpp/src/object/object_ar_application.cpp/.h`(仿 world_ar_application,但**去掉 hit test**,加 `PlaceObjectAtWorld`)
- 复用 ARWorld 的 `world_object_renderer` / `world_background_renderer` / `world_render_manager`(或各拷一份到 object/,二选一,后续定)
- 注册点(需改的"现有文件",非官方场景逻辑):
  - `cpp/src/napi_manager.cpp` `CreateApp` 工厂加 `scene=="ARObject"` 分支 + 新 NAPI 方法 `NapiPlaceObjectAtWorld`
  - `cpp/src/module.cpp` `desc[]` 注册 `placeObjectAtWorld`
  - `cpp/src/app_napi.h` 加虚函数 `virtual int32_t PlaceObjectAtWorld(...)`
  - `cpp/types/libentry/index.d.ts` 加接口声明
  - `cpp/CMakeLists.txt` 把新增 .cpp 登记进 `add_library(entry SHARED ...)`
  - `resources/base/profile/router_map.json` + `pages/Selector.ets` 加入口

> 若你想"尽量少碰文件",备选:直接在 ARWorld 场景上**叠加**一个 `placeObjectAtWorld` NAPI(复用其 anchor 列表与渲染),不新建场景。代价是 ARWorld 仍带平面检测/点击放置(MVP 用不到但无害)。**这条更省事,但语义不如独立场景干净。** 最终请你选(见第 4 节末)。

### 3.2 新增 NAPI 接口签名草案

ArkTS 侧(`index.d.ts`):
```ts
// 返回 objectId(>=0 成功;<0 失败,如未在 tracking 状态)
export const placeObjectAtWorld:
  (id: string, x: number, y: number, z: number, modelId: string) => number;
```
- `id`:XComponent 实例 id(沿用现有约定,定位到具体 App 实例)
- `x,y,z`:世界坐标(米),见 3.3 零点定义
- `modelId`:模型标识。MVP 阶段只支持 `"AR_logo"`(映射到 `AR_logo_obj.obj`/`AR_logo.png`);保留扩展位
- 返回 `objectId`:自增整数,后续 `removeObject(objectId)` / `moveObject(...)` 用(MVP 不实现,仅预留)

native 侧落点:
```cpp
// object_ar_application.cpp
int32_t PlaceObjectAtWorld(float x, float y, float z, const std::string& modelId) {
    if (cameraTrackingState != TRACKING) return -1;   // 未跟踪不放
    float poseRaw[7] = {0,0,0,1,  x,y,z};              // 单位四元数 + 平移
    AREngine_ARPose* pose; HMS_AREngine_ARPose_Create(session, poseRaw, 7, &pose);
    AREngine_ARAnchor* anchor;
    HMS_AREngine_ARSession_AcquireNewAnchor(session, pose, &anchor);  // ★ 核心
    HMS_AREngine_ARPose_Destroy(pose);
    // 存入 anchor 列表(带 objectId),渲染逻辑复用现成 RenderObject
    return newObjectId;
}
```
> `poseRaw` 布局已由 SDK 头文件确认:`[0..3]=旋转四元数(qx,qy,qz,qw)`,`[4..6]=平移(x,y,z)`(`ar_engine_core.h:1411`)。`ARENGINE_POSE_RAW_SIZE` 即 7。

### 3.3 世界坐标系零点定义

AR Engine 的世界坐标系在 **session 开始跟踪的那一刻确立**:
- **原点 ≈ 启动跟踪时设备(相机)所在位置**
- **Y 轴沿重力向上**(gravity-aligned),单位**米**
- 朝向:OpenGL 相机约定(相机看向 -Z)

所以 `placeObjectAtWorld(0, 0, -1, "AR_logo")` ≈ 在"启动位置正前方 1 米"放一个 logo。
当前相机在世界系中的实时位姿可由 `HMS_AREngine_ARCamera_GetPose`(`ar_engine_core.h:1469` 一带)取得——若上层希望"放在当前相机前方 X 米"而非固定世界点,可提供一个 `placeObjectInFrontOfCamera(distance)` 辅助接口(基于相机当前位姿换算),MVP 可选。

### 3.4 "稳定不飘"靠什么保证 —— **靠 anchor**

- `HMS_AREngine_ARSession_AcquireNewAnchor` 建出的 anchor,AR Engine **每帧持续修正其位姿**以抵消跟踪漂移、重定位(loop closure)带来的世界坐标系校正。
- 渲染时每帧 `HMS_AREngine_ARAnchor_GetPose` 重新取最新位姿,物体就"焊死"在现实空间那个点。
- 仅当 `ARAnchor_GetTrackingState == TRACKING` 才渲染(`STOPPED` 的要删),沿用现成逻辑。

### 3.5 如果不用 anchor、直接每帧用固定世界坐标渲染 —— **会飘,你的判断正确**

原因正如你猜的:
1. AR Engine 的世界坐标系**不是绝对静止**的——随相机移动、重定位、闭环校正,整个世界系会被"修正"(平移/旋转微调)。一个硬编码的 `(x,y,z)` 是相对**旧的**世界系定义的,校正发生后它就和真实物理位置错位 → 视觉上漂移/跳变。
2. 没有 anchor,就没有任何机制告诉 AR Engine "请把这个点锚定到物理世界并持续维护它";累积的里程计漂移会直接体现在物体上。
3. anchor 的存在意义正是**吸收这种校正**:它代表"物理世界中的一个固定点",其在 AR 世界系里的坐标由引擎动态维护。

**因此 MVP 必须走 anchor 路线**,即"上层给世界坐标 → 立即建 anchor → 之后只信 anchor"。世界坐标只在**建立 anchor 的那一刻**使用一次,之后不再直接用。

---

## 4. 与"之前路线 B 手势验证"的差异 + 抛弃确认

### 4.1 与之前方向的差异
| 维度 | 之前(已废弃) | 现在(接口式放置 MVP) |
|---|---|---|
| 放置触发 | 用户手指点击屏幕 | **上层调 NAPI `placeObjectAtWorld(x,y,z)`** |
| 定位方式 | hit test 打平面/网格 | **直接给世界坐标建 anchor**,无 hit test |
| 平面检测 | 需要(ARWorld) | **不需要** |
| 旋转/缩放/删除 | 要做(双指/长按 + 路线 A/B 手势) | **MVP 不做**(objectId 预留,后续可加) |
| 状态条/清空按钮 | 要做 | MVP 不做(后续可加) |
| 手势共存验证(Stage 0.5) | 路线 B 要验证 ArkTS 手势与 native 点击是否打架 | **不再需要——MVP 无任何手指交互** |

### 4.2 抛弃 Stage 0.5 产出是否 OK —— **OK,且无需回滚任何代码**
- **我之前从未进入 Stage 1,也从未做过任何 probe 的 .ets/.cpp 改动**(当时在等你说 GO)。已用 `git status` 核实:
  - 工作区**没有任何 .ets/.cpp/.h 改动**(`git diff --stat -- '*.ets'` 为空)
  - 仅有的未跟踪/改动:`reports/`、`docs/`(我写的两份文档)、`cc-harmony-ar-playbook.md`(你给的手册)、以及你本就在的 `build-profile.json5`、`oh-package-lock.json5`
- 所以**没有"Stage 0.5 probe 的 .ets 改动"需要回滚或保留**——根本不存在。
- 已落盘的 `reports/recon.md`、`docs/playbook-adapted.md` 里关于 ARWorld、手势路线 A/B 的内容现在**部分过时**(旋转/缩放/删除/手势已不在 MVP 范围)。建议:**保留作为后续阶段的参考**(那些功能你说"后续可能加"),不删除;本报告 `recon-mesh.md` 为当前 MVP 的权威方向。如你希望我把它们标注为"已暂缓/历史",我可以加一行注记(等你指示)。

---

## 需要你拍板的点(看完报告后回我)

1. **场景落点**:新建独立 `ARObject` 场景(推荐,干净) vs. 直接在 `ARWorld` 上叠加 `placeObjectAtWorld`(省事,但带着用不到的平面检测)?
2. **坐标语义**:MVP 只做绝对世界坐标 `placeObjectAtWorld(x,y,z)` 即可,还是同时要 `placeObjectInFrontOfCamera(distance)`(相机前方相对放置,演示更直观)?
3. **modelId**:MVP 是否只需 `"AR_logo"` 一个模型?还是要我预留多模型加载(后续接 glTF/其他 obj)?
4. 旧文档(recon.md / playbook-adapted.md)是保留原样,还是要我加"已暂缓"注记?

确认后我再出实现计划(仍不写代码,先给 plan)。当前未写任何代码、未装机、未 commit。
