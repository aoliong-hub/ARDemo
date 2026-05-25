# ARDemo 侦察报告 (recon)

> **STATUS: SUPERSEDED by `recon-mesh.md` as of 2026-05-25。** 理由:需求由用户交互式放置/旋转/缩放变更为接口式世界坐标放置。本文档对 ARWorld 场景的拆解、ArkUI 手势共存路线 B 的论证保留,以备未来如下场景需要时复用:(a) 给 MVP 增加手指交互;(b) 在 ARObject 场景上层加上 UI 选中/拖拽。当前权威方向见 `recon-mesh.md` 与 `impl-plan-arobject.md`。

> 生成日期:2026-05-25 · 分支:master · 仅侦察,无任何代码改动
> 真机:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 · SN `7LK0126123000094`(已连接)

---

## a) 工程目录结构(精简版)

这是华为官方 **多场景** AR Sample,不是单一 Demo。一个 entry 模块里塞了 5 个 AR 场景:
`ARWorld`(平面检测+点击放置)、`ARMesh`、`ARDepth`、`ARImage`、`ARSemanticDense`。

**我们的目标功能(旋转/缩放/删除/状态条/清空)全部落在 `ARWorld` 场景。其余 4 个场景不碰。**

```
D:\workspace\ARDemo
├─ AppScope/                         应用级配置(图标/应用名)
├─ build-profile.json5               ⚠️ 含签名敏感字段,禁止 git add / 修改
├─ hvigorfile.ts                     hvigor 入口(appTasks,勿改)
├─ hvigor/hvigor-config.json5        modelVersion 6.0.0
├─ oh-package.json5 / *-lock.json5   依赖锁
├─ entry/
│  └─ src/main/
│     ├─ module.json5                模块清单(权限:CAMERA/ACCELEROMETER/GYROSCOPE)
│     ├─ ets/
│     │  ├─ entryability/EntryAbility.ets    UIAbility,加载 pages/Selector
│     │  ├─ pages/
│     │  │  ├─ Selector.ets          ★ 首页,5 个场景按钮 + 相机权限请求
│     │  │  ├─ ARWorld.ets           ★★★ 我们的战场:XComponent 挂载 + 帧循环
│     │  │  ├─ ARMesh / ARDepth / ARImage* / ARSemanticDense*  (不碰)
│     │  │  └─ ...
│     │  └─ utils/ {Logger.ets, Utils.ets}
│     ├─ cpp/
│     │  ├─ CMakeLists.txt           ★ 编译目标 add_library(entry SHARED ...)
│     │  ├─ types/libentry/index.d.ts  ★★ ArkTS↔C++ 唯一契约(NAPI 接口声明)
│     │  ├─ src/
│     │  │  ├─ module.cpp            ★ NAPI 导出注册(napi_module nm_modname="entry")
│     │  │  ├─ napi_manager.cpp/.h   ★ XComponent 回调 + NAPI 分发 + App 工厂
│     │  │  ├─ app_napi.h            ★ AppNapi 抽象基类(虚函数表)
│     │  │  ├─ world/               ★★★ ARWorld 场景的全部 C++
│     │  │  │  ├─ world_ar_application.cpp/.h    AR 生命周期 + 点击放置
│     │  │  │  ├─ world_render_manager.cpp/.h    帧渲染 + 物体/平面/背景调度
│     │  │  │  ├─ world_object_renderer.cpp/.h   放置物模型绘制(AR_logo)
│     │  │  │  ├─ world_plane_renderer.cpp/.h    平面网格绘制
│     │  │  │  ├─ world_background_renderer.cpp/.h 相机背景
│     │  │  │  └─ world_file_manager.cpp/.h
│     │  │  ├─ depth/ image/ mesh/ semanticdense/   (其余场景,不碰)
│     │  │  ├─ graphic/ {RenderContext,RenderSurface,GLUtils,...}  GL/EGL 封装
│     │  │  └─ utils/ {app_util, app_file, renderer_ref, task_queue.h, log.h}
│     │  └─ thirdparty/glm, thirdparty/stb
│     └─ resources/base/
│        ├─ element/string.json      字符串资源(状态条文案要加在这里)
│        └─ profile/ {main_pages.json, router_map.json}  导航路由表
└─ cc-harmony-ar-playbook.md         你的原手册(假设 API 12 / 扁平结构,已过时)
```

---

## b) ArkTS UI 入口与 XComponent 挂载位置

- **App 入口**:`EntryAbility.ets` → `windowStage.loadContent('pages/Selector')`
- **首页**:`pages/Selector.ets`(`@Entry`)→ 点 "ARWorld" 按钮 → `pageInfos.pushDestinationByName('ARWorld')`
  - 首次进入会请求 `ohos.permission.CAMERA`(`Selector.ets:70 requestPermissionOnFirstStartup` + `Utils.requestPermissionOnSetting`)
- **导航注册**:`resources/base/profile/router_map.json` 里 `ARWorld → pages/ARWorld.ets → ARWorldBuilder`
- **XComponent 挂载点**:`entry/src/main/ets/pages/ARWorld.ets:44`
  ```ts
  XComponent({ id: this.idStr, type: XComponentType.SURFACE, libraryname: 'entry' })
  ```
  - **libraryname = `'entry'`** → 对应 `libentry.so`(CMake `add_library(entry SHARED)`,`module.cpp` 里 `nm_modname="entry"`)
  - **`id` = 时间戳 + `'ARWorld'`**(`ARWorld.ets:39`)。C++ 侧靠 `substr(len)` 截掉时间戳前缀拿到场景名 `"ARWorld"`,再由 `NapiManager::CreateApp` 工厂分发到 `ARWorldApp`(`napi_manager.cpp:476`)
  - XComponent 当前占满全屏(`width/height 100%`,在 `RelativeContainer` 内),**目前其上没有任何按钮/状态条/手势**——状态条和"清空全部"按钮要作为它的兄弟节点叠加进这个 RelativeContainer
- **帧驱动**:`ARWorld.ets:51 .onLoad` 里 `setInterval(33ms ≈ 30fps)` 调用 `arEngineDemo.update(idStr)`,返回值即平面数量,写入 `@State numberOfPlans`
- **生命周期 NAPI 调用**(`ARWorld.ets`):`init(resMgr)` / `start(id, Int32Array[1, rotation])` / `update(id)` / `show(id)` / `hide(id)` / `stop(id)`

### ArkTS↔C++ 契约(`cpp/types/libentry/index.d.ts`)
```ts
start(id, params:Int32Array) / show(id) / hide(id) / update(id):number / stop(id)
init(resmgr) / getDistance(id):string / initImage(...) / setPath / saveImageDataBaseToLocal / getImageCount / getVolume
```
**注意:这里没有任何 onTap / setRotation / setScale / delete 接口——新功能若走 ArkTS 路线,需要在此新增声明 + module.cpp 注册 + napi_manager 实现 + AppNapi 虚函数。**

---

## c) C++ AR 主循环位置 + 点击放置函数位置

### AR 主循环(每帧)
- `ARWorldApp::OnUpdate()` — `world_ar_application.cpp:101`
  → 推任务到 `mTaskQueue` → `HMS_AREngine_ARSession_Update` → `WorldRenderManager::OnDrawFrame()`
- `WorldRenderManager::OnDrawFrame()` — `world_render_manager.cpp:66`
  → `InitializeDraw`(取相机 view/projection 矩阵 + 背景绘制 + **相机 TrackingState 判定**)
  → `RenderObject`(画放置物)→ `RenderPlanes`(画平面)→ `SwapBuffers`

### 点击放置(全程在 C++,不经 ArkTS)
触摸事件由 XComponent **native 回调**直达 C++,ArkTS 侧 **没有** `.onTouch` / `.gesture`:

1. `NapiManager::DispatchTouchEventCB` (`napi_manager.cpp:83`) — 注册于 `OH_NativeXComponent_RegisterCallback`
2. → `ARWorldApp::DispatchTouchEvent(OH_NativeXComponent* component, void* window)` — **`world_ar_application.cpp:217`**
   - 只取 `OH_NATIVEXCOMPONENT_DOWN`、单指 `touchPoints[0].x/y`,推任务队列
3. → `ARWorldApp::DispatchTouchEvent(float pixeLX, float pixeLY)` — **`world_ar_application.cpp:166`**(放置核心)
   - `HMS_AREngine_ARFrame_HitTest` → `GetHitResult`(`:254`,筛 PLANE + 落在多边形内)
   - → `HMS_AREngine_ARHitResult_AcquireNewAnchor` → 校验 `TRACKING` → 超过 `K_MAX_NUMBER_OF_OBJECT_RENDERED=10` 则删最早 → `SetAnchorColour` → `push_back` 进 `mColoredAnchors`

### 放置物的模型矩阵(旋转/缩放要注入这里)
- `WorldRenderManager::RenderObject()` — **`world_render_manager.cpp:125`**
  ```cpp
  HMS_AREngine_ARPose_GetMatrix(...);          // modelMat = anchor 世界位姿
  modelMat = glm::scale(modelMat, vec3(0.2f)); // 固定缩 0.2 倍
  mObjectRenderer.Draw(projectionMat, viewMat, modelMat, lightIntensity, color);
  ```
  **这是旋转/缩放唯一的注入点。** 目标矩阵应改为
  `modelMat = poseMat * RotY(userYaw) * Scale(0.2 * userScale)`。

### 关键数据结构
- `struct ColoredAnchor { AREngine_ARAnchor* anchor; float color[4]; }` — `world_render_manager.h:28`
- `std::vector<ColoredAnchor> mColoredAnchors` — `world_ar_application.h:55`(锚点列表,放置/删除/清空都操作它)
- 平面数:`static int32_t WorldRenderManager::mPlaneCount` — 每帧在 `RenderPlanes`(`:168`)更新,经 `NapiOnPageUpdate`(`napi_manager.cpp:291`,仅当 id=="ARWorld")作为 `update()` 返回值回传 ArkTS
- 删除示例:`OnStop()`(`world_ar_application.cpp:65`)已示范 `HMS_AREngine_ARAnchor_Detach` + `ARAnchor_Release` + 清空 vector

---

## d) 当前已有的手势/触摸处理代码

| 能力 | 现状 |
|---|---|
| 单指点击放置 | ✅ 已有,但走 **native XComponent 回调**(`DispatchTouchEvent`),非 ArkTS |
| 多指(旋转/捏合) | ❌ 完全没有。native `OH_NativeXComponent_TouchEvent` 里仅读 `touchPoints[0]` |
| 长按 | ❌ 没有,也没有计时逻辑 |
| ArkTS 手势 (`.gesture` / GestureGroup) | ❌ XComponent 上一个都没挂 |
| `DispatchMouseEvent` | 已注册回调但 `ARWorldApp` 未 override(基类空实现) |

**结论:旋转/缩放/长按是从零新增。** 触摸架构是 native 路线,与原手册假设的 "ArkTS GestureGroup + NAPI" **不一致**(见 g 节)。

---

## e) 实现 5 个功能需要修改的文件清单(仅列文件,不写代码)

> 下方分两种输入路线;最终选哪条要你拍板(见 g 节末尾"待决策")。**红线:不新建/不重命名/不删除官方文件;尽量只改 world/ 场景 + 契约层。**

**1) 双指旋转(绕世界 Y)**
- `entry/src/main/cpp/src/world/world_render_manager.h` — `ColoredAnchor` 加 `userYawRad`(或新增并行的 user-transform 结构)
- `entry/src/main/cpp/src/world/world_render_manager.cpp` — `RenderObject` 模型矩阵注入 `RotY(userYawRad)`
- `entry/src/main/cpp/src/world/world_ar_application.cpp/.h` — 维护"选中锚点"+ 应用旋转增量的方法
- 输入来源(二选一):
  - 路线A(native):`world_ar_application.cpp` 的 `DispatchTouchEvent(component,window)` 扩展为多指识别
  - 路线B(ArkTS):`pages/ARWorld.ets` 加 `RotationGesture` + 新 NAPI;`cpp/types/libentry/index.d.ts`、`src/module.cpp`、`src/napi_manager.cpp/.h`、`app_napi.h` 各加一个接口

**2) 双指捏合缩放(0.2x–3.0x)**
- 同上 `world_render_manager.{h,cpp}`(`userScale` 字段 + `Scale()` 注入,clamp 0.2~3.0)
- `world_ar_application.cpp/.h`(应用缩放增量)
- 输入来源:同路线 A/B(PinchGesture 或 native 双指距离)

**3) 长按 1 秒删除**
- `world_ar_application.cpp/.h` — 选中拾取 + `HMS_AREngine_ARAnchor_Detach`/`Release` + 从 `mColoredAnchors` 移除(逻辑可复用 `OnStop` 已有写法)
- 输入来源:路线A 在 native 加计时;路线B 在 `ARWorld.ets` 加 `LongPressGesture(duration 1000)` + 新 NAPI

**4) 顶部状态条**
- `pages/ARWorld.ets` — RelativeContainer 内叠加 `Text`,绑定 `@State arStatus`;由现有 `update()` 返回的平面数 + 新增的 tracking 状态推导
- `entry/src/main/resources/base/element/string.json` — 加状态文案(扫描中/已检测 N 个/跟踪丢失)
- (若状态条要显示"跟踪丢失")`world_render_manager.cpp` 的 `InitializeDraw` 已拿到 `cameraTrackingState`,需把它上报:要么扩展 `update()` 返回值编码,要么 `cpp/types/libentry/index.d.ts` + `module.cpp` + `napi_manager` 新增一个状态 getter

**5) 清空全部按钮**
- `pages/ARWorld.ets` — RelativeContainer 内叠加 `Button`
- `world_ar_application.cpp/.h` — `ClearAll()`(遍历 `mColoredAnchors` 全部 Detach/Release/clear)
- `cpp/types/libentry/index.d.ts` + `src/module.cpp` + `src/napi_manager.cpp/.h` + `app_napi.h` — 新增 `clearAll(id)` NAPI 接口

**几乎必然要动的"契约四件套"(只要走 NAPI 新接口就都要改)**:
`cpp/types/libentry/index.d.ts` → `src/module.cpp`(desc[] 注册)→ `src/napi_manager.cpp/.h`(Napi* 静态方法)→ `src/app_napi.h`(虚函数)→ `world_ar_application.cpp/.h`(override 实现)。

---

## f) 工具链状态

| 工具 | 状态 | 路径 / 备注 |
|---|---|---|
| **hdc** | ✅ 可用 | `C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe` |
| 设备连接 | ✅ 1 台 | `hdc list targets` → `7LK0126123000094` |
| 拉起 App | ✅ 成功 | `hdc shell aa start -a EntryAbility -b com.huawei.ARSample` → `start ability successfully`;任务栈出现 `com.huawei.ARSample` |
| **hvigorw** | ⚠️ 项目根**没有** | 项目里只有 `hvigor/`(配置)和 `hvigorfile.ts`,**无 `hvigorw` / `hvigorw.bat`**。改用 DevEco 自带:`C:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat`,版本 **6.21.1**(已验证 `--version`) |
| node | ✅ 可用 | `C:\Program Files\nodejs\node.exe`;DevEco 也自带 `tools\node`(跑 hvigorw 时需把它加进 PATH) |
| ohpm | 🔶 存在未验证 | `C:\Program Files\Huawei\DevEco Studio\tools\ohpm`(本次未跑) |
| 编译 | ⏸️ 未执行 | 侦察阶段不做完整 `assembleHap`(耗时且会写 build 产物)。命令模板见 playbook-adapted.md |

**调用 hvigorw 的标准姿势(PowerShell):**
```powershell
$env:Path = "C:\Program Files\Huawei\DevEco Studio\tools\node;" + $env:Path
& "C:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat" --version
```

---

## g) 与原手册 `cc-harmony-ar-playbook.md` 的不一致点(以工程现状为准)

| # | 手册假设 | 工程现状 | 影响 |
|---|---|---|---|
| 1 | API 12 | **API 20 / 6.0.0**,runtimeOS=HarmonyOS(纯血) | 文档/CLAUDE.md 的 "API 12" 全部需改 20 |
| 2 | 包名 `com.huawei.arengine.demo` | **`com.huawei.ARSample`** | 所有 `aa start` / install 命令需改 |
| 3 | 扁平结构 `Index.ets` / `plugin_manager.cpp` / `napi_init.cpp` / `ar_application.cpp` / `object_renderer.cpp` | **多场景**,我们的代码在 `pages/ARWorld.ets` + `cpp/src/world/world_*.cpp`;NAPI 在 `module.cpp`+`napi_manager.cpp` | Stage 改动文件清单全部重映射 |
| 4 | 点击放置走 ArkTS `XComponent.onTouch` → NAPI `onTap(x,y)` | **点击全程在 native** `DispatchTouchEvent` 回调,无 onTap NAPI | Stage 3 已"内置完成";新功能输入路线要重选 |
| 5 | 手势走 ArkTS `RotationGesture/PinchGesture/LongPressGesture` + NAPI | XComponent 上**无任何 ArkTS 手势**,触摸是 native 路线 | **关键架构分叉**(见下"待决策") |
| 6 | `hvigorw` 在项目根 | **缺失**,用 DevEco 自带 6.21.1 | 构建命令需改 |
| 7 | 放置 10cm 立方体,需写 cube 顶点 + Phong shader | 已有 `AR_logo` obj 模型 + shader,固定缩 0.2 | "放置物"无需新建,直接复用 |
| 8 | 有 `entry/src/cpp/test/`(gtest) + `entry/src/ohosTest/`(hypium) 脚手架 | **均不存在**;gtest 走 FetchContent 拉 gitee 需联网 | 若要单测需自建脚手架(建议按需,而非阻塞) |
| 9 | `scripts/*.sh` + `.gitignore` 过滤 screenshots/logs/reports | 无 scripts;`.gitignore` **未**过滤 `screenshots/`(已存在该目录)/`reports/`/`docs/` | 注意别误 commit;脚本是 `.sh`,本机是 PowerShell,需改写或用 .ps1 |
| 10 | "AR Engine 服务端需 AppGallery 安装" | baseline 已跑通(相机/平面/放置 OK),服务端已就位 | 无需再装 |

**⚠️ 待决策(Stage 1 开工前需你拍板):双指/长按手势走哪条路线?**
- **路线 A — native 多指**:在 `DispatchTouchEvent(component,window)` 里读 `OH_NativeXComponent_TouchEvent` 的多点 `touchPoints[]`,自己算双指夹角/间距/长按计时。
  - 优点:与 sample 触摸架构一致,不引入 ArkTS↔native 双重事件源;不动契约层。
  - 缺点:多指状态机/手势识别全要手写,代码量大、易抖。
- **路线 B — ArkTS 手势 + 新 NAPI**:在 `ARWorld.ets` 的 XComponent 上挂 `RotationGesture`/`PinchGesture`/`LongPressGesture`,把 angle/scale/delete 通过新增 NAPI 推给 native。
  - 优点:手势识别用系统组件,稳、省事;旋转/缩放语义清晰。
  - 缺点:要扩"契约四件套";native 触摸回调与 ArkTS 手势同时在同一 XComponent 上,需验证事件不打架(放置用 native DOWN、手势用 ArkTS,理论可共存,需实测)。
- **我的倾向:路线 B**(手势识别交给系统更稳),但"选中物体/拾取"逻辑仍放 C++。最终请你确认。
