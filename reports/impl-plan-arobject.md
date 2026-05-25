# ARObject 接口式放置 — 实现计划 (implementation plan)

> 生成日期:2026-05-25 · **仅计划,无代码 / 未装机 / 未 commit** · 等 GO
> 上游依据:`recon-mesh.md`(权威方向)、四项决策(2026-05-25 用户确认)

---

## 0. 工程细节核实(读 SDK 头文件 + sample,非凭印象)

### A. 世界坐标系零点 —— **基本确认,一处需运行时校验**
来源:`ar/ar_engine_core.h:1469-1474`(`HMS_AREngine_ARCamera_GetPose` 文档):
> "The pose is that of the OpenGL camera, where the **positive x = right, positive y = upwards, negative z = look-at direction** of the camera ... orientation not affected by screen rotation."

- **Y 轴向上**:✅ 明确(且 AR Engine 世界系重力对齐,见 `:441` "consistent with world coordinate system" 及 `:635-649` ENU/gravity 对齐配置)
- **-Z = 相机朝向(look-at)**:✅ 明确
- **X = 右**:✅ 明确(右手系,多处 "right-handed",如 `:1879`)
- **原点 = session 启动那一刻相机位置**:SDK 头文件**未逐字明述**"原点在启动点",但:相机 pose 是"在世界系中"表达且重力对齐,这与 AR Engine/ARCore 通用约定一致(世界系在首次进入 TRACKING 时确立,原点≈起始相机位置)。**判定:你的描述正确**;为稳妥,Stage 2 会做一次运行时核验——启动后立刻 `GetPose`,确认平移≈0、旋转≈单位(若非,再修正原点假设)。

> 衍生:`placeObjectInFrontOfCamera` 用 `HMS_AREngine_ARCamera_GetDisplayOrientedPose`(`:1491`)更合适——它"考虑屏幕旋转",与渲染用的 view 矩阵同一参考系,"前方"=屏幕看进去的方向。两者平移相同,仅 x/y 轴随屏幕旋转不同;-Z 朝向一致。

### B. AR_logo 模型默认朝向 —— **几何已测,渲染无旋转,但"正面=+Z 还是 -Z"需 Stage 2 device 校准**
核实方式:① 读 `world_object_renderer.cpp`/`mesh_object_renderer.cpp` 的 `Draw` + `RenderObject`;② 算 `AR_logo_obj.obj` 顶点包围盒。
- **渲染端不施加任何旋转**:`RenderObject` 里 `modelMat = anchorPose;  modelMat = glm::scale(modelMat, 0.2)`,仅缩放。模型按 .obj 原始朝向 + anchor 位姿绘制。
- **模型几何**(实测 5007 顶点 / 10074 面,真 3D 非薄片):
  - X: -0.433 .. 0.434(extent 0.867)
  - **Y: 0.000 .. 0.971(extent 0.971,最高轴)**
  - Z: -0.398 .. 0.399(extent 0.797)
  - 中心 ≈ (0, 0.486, 0)
  - 结论:**局部 +Y = 上**;底座坐落于 Y=0 平面(放在锚点上、向上立起);X/Z 近似对称居中。
- **"正面"(可读 logo 面)朝 +Z 还是 -Z**:**无法仅凭静态读取可靠判定**(取决于纹理 UV 贴在哪些面,而非纯几何)。**不猜。** 方案:接口默认让模型 **yaw 朝向相机**(见 §3.4),并引入一个常量 `kLogoFrontYawOffset ∈ {0, π}`,Stage 2 在真机上一次性标定(看 logo 是正面还是背面对着相机,背面则置 π)。
- 因此 **identity 四元数不可靠**——既因正面朝向未知,也因下面的四元数分量序问题。统一走"yaw 朝相机"路径。

### 补充核实:poseRaw 布局(关系到 §3.4 四元数构造)
- `ARENGINE_POSE_RAW_SIZE = 7`(`:213`)✅
- 布局:`poseRaw[0..3] = 旋转四元数`,`poseRaw[4..6] = 平移 (x,y,z)`(`:1411-1413`)✅
- **四元数分量序(xyzw vs wxyz):SDK 头文件未逐字写明。** AR Engine NDK 与 ARCore C API 同构,ARCore 约定为 **(x, y, z, w)**。**判定:按 xyzw 实现**,但这是**唯一一处未被头文件 100% 钉死的假设**——Stage 2 用 `HMS_AREngine_ARPose_GetMatrix` 对一个已知四元数做断言核验(构造 yaw=90° about Y,验证返回矩阵第 0/2 列符合预期),确认后再信任非单位旋转。这点会写进 Stage 2 的 Gate。

---

## 1. 新建文件清单(完整路径)

| # | 路径 | 作用 |
|---|---|---|
| N1 | `entry/src/main/ets/pages/ARObject.ets` | ArkTS 页面,仿 `ARMesh.ets`:XComponent(libraryname='entry', id=时间戳+'ARObject')+ 生命周期 init/start/update/show/hide/stop |
| N2 | `entry/src/main/cpp/src/object/object_ar_application.h` | `ARObject::ARObjectApp : public AppNapi`,AR 生命周期 + `PlaceObjectAtWorld/InFrontOfCamera/RemoveObject/ClearAllObjects` |
| N3 | `entry/src/main/cpp/src/object/object_ar_application.cpp` | 上述实现;**无 hit test、无平面检测** |
| N4 | `entry/src/main/cpp/src/object/object_render_manager.h` | 管理"带 objectId 的 anchor 列表",调用复用的 renderer |
| N5 | `entry/src/main/cpp/src/object/object_render_manager.cpp` | 帧渲染:背景 + 遍历 anchor 画 logo(逻辑仿 world,但 anchor 来源不同) |
| N6 | `entry/src/main/cpp/src/object/object_math.h` | **纯数学、无 AR/GL 依赖**:相机前坐标、yaw-朝相机、poseRaw 组装。供 gtest 直接 include |
| N7(可选) | `entry/src/main/cpp/test/object_math_test.cpp` + `entry/src/main/cpp/test/CMakeLists.txt` | gtest 单测(见 §5) |

**复用(include,不拷贝、不修改)**:
- `world/world_background_renderer.{h,cpp}`(相机背景)
- `world/world_object_renderer.{h,cpp}`(logo 模型绘制,Draw 签名已知)
- 这两个类是独立 renderer,`object_render_manager` 直接 `#include "world/world_background_renderer.h"` 并实例化 `ARWorld::WorldBackgroundRenderer` / `ARWorld::WorldObjectRenderer` 即可。**不重写、不改它们一行。** 它们已在 `CMakeLists.txt` 的 `add_library(entry ...)` 里(world 段),无需重复登记。

---

## 2. 现有文件需要改的清单(**比你预估的多——这里如实列全**)

> ⚠️ 修正你的假设:路由**不在** `main.ets`,入口注册**不在** `module.json5`。实际是 `router_map.json` + `Selector.ets`;NAPI 注册是 C++ 侧的"契约链"。以下 8 个现有文件需改,**均不属于** 决策 1 划红线的 5 个场景文件(ARWorld/ARMesh/ARDepth/ARImage/ARSemanticDense),全部允许。

| # | 文件 | 改动(均为"追加",不动既有逻辑) |
|---|---|---|
| M1 | `entry/src/main/cpp/CMakeLists.txt` | `add_library(entry SHARED ...)` 列表追加 `object/object_ar_application.cpp` 和 `object/object_render_manager.cpp`(约 `:84` 前,仿 world 段) |
| M2 | `entry/src/main/cpp/src/napi_manager.cpp` | (a) `CreateApp` 工厂(约 `:489`)追加 `if (scene=="ARObject") return new ARObject::ARObjectApp(scene);`;(b) 新增 4 个静态方法 `NapiPlaceObjectAtWorld` / `NapiPlaceObjectInFrontOfCamera` / `NapiRemoveObject` / `NapiClearAllObjects`(仿 `NapiOnPageUpdate` 的取 id + 解参模式) |
| M3 | `entry/src/main/cpp/src/napi_manager.h` | 声明上述 4 个静态方法 + `#include "object/object_ar_application.h"`(仿现有 include 段) |
| M4 | `entry/src/main/cpp/src/module.cpp` | `desc[]`(`:31-45`)追加 4 个 `{"placeObjectAtWorld", nullptr, NapiManager::NapiPlaceObjectAtWorld, ...}` 等 |
| M5 | `entry/src/main/cpp/src/app_napi.h` | 基类加 4 个虚函数默认空实现(仿 `:48` `GetDistance` 的写法),签名见 §3.2 |
| M6 | `entry/src/main/cpp/types/libentry/index.d.ts` | 追加 4 行 `export const ...` 声明(见 §3.2) |
| M7 | `entry/src/main/resources/base/profile/router_map.json` | 追加 `{ "name":"ARObject", "pageSourceFile":"src/main/ets/pages/ARObject.ets", "buildFunction":"ARObjectBuilder" }` |
| M8 | `entry/src/main/ets/pages/Selector.ets` | `build()` 里追加一行 `this.sampleButton('ARObject');`(供真机手动进场景测试;纯追加) |

> `module.json5` **不需要改**(它已用 `$profile:main_pages` / `$profile:router_map` 间接引用,无需逐页登记)。

---

## 3. NAPI 设计与绑定细节

### 3.1 绑定方式(具体到文件/行附近)
注册一个 NAPI 函数 = 改 4 处(本工程既有模式,见 recon.md "契约四件套"):
1. **声明**:`index.d.ts`(M6)
2. **登记**:`module.cpp` `Init()` 的 `desc[]` 数组(M4,`:31` 起,与 `"update"` 同款一行)
3. **入口静态方法**:`napi_manager.cpp`(M2)解 JS 参数 → 取 `AppNapi* app = GetApp(id)` → 调 `app->PlaceObjectAtWorld(...)` → `napi_create_int32` 回传 objectId
4. **虚函数派发**:`app_napi.h`(M5)声明虚函数;`object_ar_application.cpp`(N3)override 实现

### 3.2 接口签名

ArkTS(`index.d.ts`):
```ts
export const placeObjectAtWorld:
  (id: string, x: number, y: number, z: number, modelId?: string) => number; // 返回 objectId(>=0 成功, -1 未跟踪/失败)
export const placeObjectInFrontOfCamera:
  (id: string, distance: number, modelId?: string) => number;
export const removeObject: (id: string, objectId: number) => boolean;
export const clearAllObjects: (id: string) => number;                          // 返回清掉的数量
```
C++ 虚函数(`app_napi.h`,默认空实现):
```cpp
virtual int32_t PlaceObjectAtWorld(float x, float y, float z, const std::string& modelId) { return -1; }
virtual int32_t PlaceObjectInFrontOfCamera(float distance, const std::string& modelId) { return -1; }
virtual bool    RemoveObject(int32_t objectId) { return false; }
virtual int32_t ClearAllObjects() { return 0; }
```
> `modelId` 默认 `"AR_logo"`(决策 3):实现里 `if (modelId != "AR_logo") { LOGW(...); /* 回退 AR_logo */ }`,不报错、不做注册表。

### 3.3 线程模型(关键:NAPI 同步返回 + AR 操作异步上 TaskQueue)
sample 的 AR 操作全在 `mTaskQueue` 线程(JS 线程不能直接碰 session/frame)。但 NAPI 要**同步**返回 objectId。方案:
1. NAPI 调用线程:`int32_t id = mNextObjectId++;`(`std::atomic`)**立即生成 id 并返回**
2. 同时 `mTaskQueue.Push([=]{ ...建 anchor 并以 id 为 key 存入列表... })`
3. 若 task 内 `AcquireNewAnchor` 失败(未 TRACKING / 资源不足),task 内把该 id 标记为失败(不加入渲染列表)。后续 `RemoveObject` 对不存在的 id 返回 false。
- `RemoveObject` / `ClearAllObjects` 同样 Push task 做 `Detach`+`Release`+从列表删(复用 `OnStop` 已验证的释放写法);返回值用快照(当前列表 size)即时给出。

### 3.4 "相机前糖" + 朝向计算(都在 TaskQueue 内,因需 frame)
```
task:
  AcquireCamera(session, frame, &cam)
  GetDisplayOrientedPose(session, cam, pose) → poseRaw[7]
  camPos = poseRaw[4..6];  camQuat = poseRaw[0..3]
  forward = rotate(camQuat, (0,0,-1))           // 世界系朝向
  worldPoint = camPos + forward * distance      // 前方 distance 米(含俯仰)
  → 转调 内部 PlaceAtWorldImpl(worldPoint, modelId)
```
朝向(让 logo 面对相机,保持竖直):
```
PlaceAtWorldImpl(P, modelId):
  // 水平面内,从物体指向相机的方位角
  yaw = atan2(camPos.x - P.x, camPos.z - P.z) + kLogoFrontYawOffset   // 偏移 Stage2 标定
  q = angleAxis(yaw, (0,1,0))            // 仅绕 Y,模型保持直立
  poseRaw = { q.x, q.y, q.z, q.w,  P.x, P.y, P.z }   // xyzw(待 Stage2 核验)
  ARPose_Create(session, poseRaw, 7, &pose)
  ARSession_AcquireNewAnchor(session, pose, &anchor)   // ★ 无 hit test
  ARPose_Destroy(pose); 存 {objectId, anchor} 入列表
```
> `placeObjectAtWorld(x,y,z)` 绝对坐标版:同样用"yaw 朝当前相机"做默认朝向(决策 2 要求模型默认可见/面朝用户)。若 P 与 camPos 水平重合(正上/正下方),退化为 yaw=0。

---

## 4. 渲染侧(object_render_manager)
仿 `world_render_manager::OnDrawFrame`,但:
- **不调 RenderPlanes**(无平面)
- 背景:`ARWorld::WorldBackgroundRenderer`(复用)
- 物体:遍历 `{objectId, anchor, modelId}` 列表 → `GetPose`→`GetMatrix`→`scale(0.2)`→`ARWorld::WorldObjectRenderer::Draw`(复用)
- 仅渲染 `TrackingState==TRACKING` 的 anchor(沿用)
- session 配置(`OnStart`):平面检测可显式 `SetPlaneFindingMode(DISABLED)`(不需要平面);**不**开 MeshMode(接口放置不依赖环境网格)

---

## 5. 单元测试可测点(纯数学,主机跑,不上设备)
放进 `object_math.h`(无 AR/GL 依赖),用 gtest(`test/CMakeLists.txt` 走 FetchContent;若拉取受限则降级为本地 gtest,Stage 标注):
1. `ForwardPointFromCamera(camPos, camQuat, distance)`:
   - 相机在原点、朝 -Z(单位四元数),distance=1 → 期望 (0,0,-1)
   - 相机 yaw 90°(朝 -X)→ 期望 (-1,0,0);与 glm 参考实现误差 < 1e-5
2. `YawToFaceCamera(objPos, camPos, frontOffset=0)`:
   - obj 原点、cam 在 +Z → yaw=0(front=+Z 面向相机)
   - cam 在 +X → yaw=90°;cam 在 -Z → yaw=180°
   - frontOffset=π 时整体 +180°
3. `MakePoseRaw(pos, yaw)`:验证 `[4..6]==pos`,`[0..3]` 为绕 Y 的单位四元数(模=1,x=z=0)
4. `ClampNop`:objectId 单调递增、Remove 不存在 id 返回安全值(可在不依赖 AR 的小逻辑类上测)
> ArkTS/hypium 侧 MVP 可不写(无状态机/无手势);如需,仅测 `placeObjectInFrontOfCamera` 参数序列化。

---

## 6. Stage 划分(每个 Stage 完成 → 装机+截图+hilog → 写 reports/stage-N → 等 "next")

### Stage 1 — ARObject 场景骨架 + 相机画面
- 改/建:N1、N2、N3(仅生命周期,空渲染)、N4、N5(仅背景)、M1–M8
- AR session 起、相机背景上屏,**不放任何物体**
- **Gate**:进入 ARObject 页面 → 截图非黑、是相机实时画面;hilog 有 `ARObjectApp::OnStart`、无 CHECK 失败/崩溃;baseline 5 场景仍能进(回归)

### Stage 2 — placeObjectAtWorld 接通 + 写死 (0,0,-1) + 稳定性 + 两项校准
- 实现 `PlaceObjectAtWorld` + `object_math.h` + 渲染遍历
- 临时触发:onLoad 后延时调一次 `placeObjectAtWorld(id, 0,0,-1)`(或调试按钮),不依赖手指
- **本 Stage 完成 §0 的两项 device 校准**:
  - 四元数分量序(xyzw)— 用 `ARPose_GetMatrix` 断言核验
  - `kLogoFrontYawOffset`(0 / π)— 看 logo 正面是否朝相机
- **Gate**:logo 出现在相机前方约 1 米、直立、正面朝相机;**移动/转动手机时 logo 钉在原处不飘**(人工:左右走、前后蹲);hilog `AnchorAdded id=0`;单测(测点 1–3)通过

### Stage 3 — 补完 InFrontOfCamera / Remove / ClearAll
- 实现 `PlaceObjectInFrontOfCamera`(§3.4 相机 pose 路径)、`RemoveObject`、`ClearAllObjects`
- **Gate**:`placeObjectInFrontOfCamera(0.8)` 连放 3 个均面朝相机且稳定;`removeObject(id)` 精确删 1 个、其余不动;`clearAllObjects()` 全清并返回数量;objectId 生命周期正确(删后再 place 不复用旧 id)

### Stage 4(可选)— 演示用调试 UI
- `ARObject.ets` 叠加 2 个按钮:"前方1米放一个"(调 `placeObjectInFrontOfCamera(1.0)`)、"清空"(调 `clearAllObjects`)
- 仅为演示便利;**纯 UI,不改 native**
- **Gate**:点按钮即放/清,演示顺畅

---

## 7. 红线复核(对齐四项决策)
- ✅ 新建独立 ARObject 场景;5 个官方场景文件一字不改
- ✅ 复用 background/logo renderer(include,不重写)
- ✅ 4 个 NAPI 接口 + objectId 单调递增;InFrontOfCamera 是 AtWorld 的薄包装
- ✅ modelId 仅 "AR_logo",非法值 warn+回退,无注册表
- ✅ 旧文档已加 SUPERSEDED 注记(已完成,未删内容)
- ✅ 全程不升级 SDK、不碰签名、不改 build-profile.json5、不 commit
- ⚠️ 需你知晓:现有文件改动是 **8 个**(非你预估的 2 个),清单见 §2,均非红线场景文件
- ⚠️ 两处 device 待校准(四元数序、logo 正面偏移)集中在 Stage 2 解决,不阻塞 Stage 1

---

## Plan Addenda v1(2026-05-25,用户补强,GO 前并入)

### 补强 1 — Stage 2 四元数序自检函数 `ProbeQuaternionLayout()`
- 启动后(进入 TRACKING 后)调一次。
- 构造两个 candidate(绕 Y 轴 +90°,trans=(0,0,0)):
  - candidate A(假设 **xyzw**):`quat = (0, sin(π/4), 0, cos(π/4))`
  - candidate B(假设 **wxyz**):`quat = (cos(π/4), 0, sin(π/4), 0)`
- 各 `HMS_AREngine_ARPose_Create(session, poseRaw7, 7, &pose)` → `HMS_AREngine_ARPose_GetMatrix(session, pose, m, 16)`。
- **列主序判定**:SDK 文档 `ar_engine_core.h:1457` 明确 "stored in **column-major** order";`:1452-1453` "Coordinates in the local coordinate system can be converted into ones in the world coordinate system by multiplying this matrix"(即 `world = M · local`,M 为列主序、列向量约定)。
  - 绕 Y +90° 的旋转把局部 +X 映到世界 -Z?核对:Ry(+90°) 作用于 (1,0,0) → (0,0,-1)。列主序数组 `m[]` 中第 0 列 = `(m[0],m[1],m[2],m[3])` 即局部 +X 的世界像 → 期望 `m[2] ≈ -1`、`m[0] ≈ 0`。
  - 等价地局部 +Z (0,0,1) → (1,0,0),第 2 列 `m[8]≈1`。
  - **断言**:命中的 candidate 应满足 `m[2] ≈ -1`(容差 1e-3)。哪个命中,hilog `kQuatFormat = XYZW` 或 `WXYZ`。
  - (上面"补强 1"原文给的 `m[0,2]≈1` 是行/列与符号的口径差异;以此处用 SDK 实测矩阵元素 `m[2]≈-1` 为准——Stage 2 实跑时若发现 AR Engine 的 Y 旋转手性相反,以实测列向量为权威,二选一断言即可。)
- 结果写**编译期 const** 锁定:`enum class QuatFormat { XYZW, WXYZ }; constexpr QuatFormat kQuatFormat = ...;`(按 device 实测填,Stage 2 内定稿)。
- **若两个都不命中 / 都命中**:`LOGE` FATAL 级报告 → **Stage 2 判定不通过,不 fallback**,停下汇报。

### 补强 2 — logo 正面偏移 = 显式人工 Gate
- Stage 2 在 `(0,0,-1)` 用 **identity 四元数**(按补强1确认的序写 identity)放一个 logo。
- 提示用户二选一:
  - (a) 看到清晰 AR 字样/正面纹理 → 回 `FRONT` → `kLogoFrontYawOffset = 0`
  - (b) 看到背面/纯色/翻转 → 回 `BACK` → `kLogoFrontYawOffset = π`
- 该常量写进 `object_math.h`,之后所有"朝相机"接口使用它。

### 补强 3 — Stage 1 旧场景回归 Gate(量化,人工操作,禁 uinput)
- 7 步:① Selector 主菜单 → ② ARWorld(相机+平面网格)→ 返回 → ③ ARMesh(相机+网格重建线)→ 返回 → ④ ARDepth(相机+深度可视化)→ 返回 → ⑤ ARImage(相机)→ 返回 → ⑥ ARSemanticDense(相机+语义分割)→ 返回 → ⑦ ARObject(相机,不放物体)→ 返回。
- 每步过完**不得新增 fatal/error 级 hilog**。
- **由用户手动操作**,Claude **不得 uinput 模拟**(Selector 屏幕方向/手势穿透不稳)。
- 交付核对清单 `reports/stage-1-regression-checklist.md`。

### 补强 4 — 单元测试每项 ≥ 3 边界用例(Stage 2 实现 `object_math.h` 时落地)
- `ComputeForwardPoint`:(a) cam(0,0,0) yaw0 pitch0 d=1 → (0,0,-1);(b) cam(0,0,0) yaw90° d=1 → (-1,0,0);(c) cam(1,2,3) pitch -30° d=2 → 验 Y 分量为负。
- `ComputeYawToFaceCamera`:(a) obj(0,0,-1) cam 原点朝-Z → yaw=0;(b) obj(1,0,0) → yaw=π/2;(c) obj(0,0,1)(相机正后方)→ yaw=π。
- `PackPoseRaw(quat,trans)`:(a) identity+zero → `[0,0,0,1,0,0,0]`;(b) 绕Y180°+trans(1,2,3) → 验分量序+平移;(c) 任意 quat → 验归一化(若实现做归一化)。

### 补强 5 — 接口错误处理与上限(Stage 2/3 实现;Stage 1 仅 stub 占位)
- `constexpr int kMaxObjects = 50;`(置于 `object_math.h` 同级头文件)。
- `placeObjectAtWorld` / `placeObjectInFrontOfCamera`:
  - session 未 init/未 resume → `-1` + `LOGW "session not ready"`
  - 已有对象数 ≥ 50 → `-1` + `LOGW "object limit reached (50)"`
  - 成功 → `objectId ≥ 0`
- `removeObject`:`objectId<0` 或不在表 → `false` + `LOGW`(不报错);成功 → `true` + `Detach`+`Release`。
- `clearAllObjects`:返回清空数量(空表 0);对每个 anchor 都 `Detach`+`Release` 防泄漏。
> Stage 1 这些行为**仅以 stub 返回值占位**(基类默认虚函数 `-1/false/0`),真正实现在 Stage 2/3。
