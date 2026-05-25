# Stage 1 报告 — ARObject 场景骨架 + 相机背景

**Status: PASSED ✅**

> 日期:2026-05-25 · 设备:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094`
> 构建:`BUILD SUCCESSFUL in 14s` · bundle:`com.huawei.ARSample` · 未 commit

---

## 1. 本阶段目标(回顾)
新建独立 `ARObject` 场景,能"启动场景 → 看到相机背景 → 返回主菜单",不破坏 baseline。placeObjectAtWorld 等放置接口仅接通契约链、留 stub,不实现放置逻辑(那是 Stage 2/3)。

## 2. 交付物

### 新建 6 个文件
- `entry/src/main/ets/pages/ARObject.ets` — 页面(仿 ARMesh,id=时间戳+'ARObject',帧循环调 `update()`)
- `entry/src/main/cpp/src/object/object_ar_application.{h,cpp}` — AR 生命周期;**关平面检测(`SetPlaneFindingMode(DISABLED)`)、无 hit test、无触摸 override**
- `entry/src/main/cpp/src/object/object_render_manager.{h,cpp}` — 背景+物体渲染;**复用** `ARWorld::WorldBackgroundRenderer` / `WorldObjectRenderer`(`#include`,未改一行)
- `entry/src/main/cpp/src/object/object_math.h` — 骨架:`kMaxObjects=50`、`kLogoFrontYawOffset`(Stage 2 校准占位)、`enum QuatFormat`、3 个纯函数声明(Stage 2 实现)

### 改 7 个现有文件(全部追加;均非红线的 5 个场景文件)
| 文件 | 改动 |
|---|---|
| `cpp/CMakeLists.txt` | `add_library(entry)` 追加 object 两个 .cpp |
| `cpp/src/app_napi.h` | 基类追加 4 个虚函数 stub(`-1/false/0`) |
| `cpp/src/napi_manager.h` | 声明 4 个 Napi 静态方法 |
| `cpp/src/napi_manager.cpp` | include + `CreateApp` 加 `ARObject` 分支 + 4 个 Napi 方法实现(解参→调虚函数→回传) |
| `cpp/src/module.cpp` | `desc[]` 注册 4 个接口 |
| `cpp/types/libentry/index.d.ts` | 声明 4 个 export |
| `resources/base/profile/router_map.json` | 加 `ARObject` 路由 |
| `ets/pages/Selector.ets` | 加 `ARObject` 入口按钮 |

> `module.json5` 未改(经 `$profile:router_map` 间接引用)。`build-profile.json5`、签名文件未碰。

## 3. 构建结果
- `BuildNativeWithCmake` ✅ → `BuildNativeWithNinja` ✅(5.8s,**新 C++ 编译链接通过**)
- `CompileArkTS` ✅(新 `ARObject.ets` 通过;仅有的 WARN 来自既有 `ARImageByAdd.ets:76/78/80/219`,与本次无关)
- `PackageHap` ✅ → `SignHap` ✅ → `assembleHap` ✅
- 安装:`install bundle successfully` ✅;启动:`start ability successfully` ✅
- 工具链备忘(3 个坑):需在项目根执行 + `DEVECO_SDK_HOME=...\sdk` + 将 `...\DevEco Studio\jbr\bin`(java)加进 PATH,否则分别报 配置目录错 / `DEVECO_SDK_HOME` 无效 / `spawn java ENOENT`(签名步骤)。

## 4. 自动 Gate
- ✅ 编译 0 error
- ✅ 装机 + 启动成功
- ✅ 主菜单截图 `screenshots/stage1_menu.jpeg`:6 个按钮,新增 **ARObject** 在最底部

## 5. 人工 Gate(7 步回归,用户手动操作,未 uinput)
用户回报 **全 ✅**:
| # | 场景 | 结果 |
|---|---|---|
| 1 | Selector 6 按钮(含 ARObject) | ✅ |
| 2 | ARWorld 相机+平面,返回正常 | ✅ |
| 3 | ARMesh 相机+网格重建,返回正常 | ✅ |
| 4 | ARDepth 相机+深度,返回正常 | ✅ |
| 5 | ARImage 相机,返回正常 | ✅ |
| 6 | ARSemanticDense 相机+语义分割,返回正常 | ✅ |
| 7 | **ARObject 相机画面、无物体**(符合预期),返回正常 | ✅ |

## 6. hilog 二次核验(error/fatal)
拉取 193,174 行 hilog 过滤分析:
- **我方代码(`[ARSample][C]` 标签)**:仅正常 **D 级**生命周期日志(`OnStart`/`OnUpdate`/`OnStop`/`OnSurfaceDestroyed`/`Destructor`/`ObjectRenderManager Release start/end`)。**无 `CHECK FAILED`、无崩溃、无 abort。**
- **`Destructor called without Release()!`(E 级,我方标签)**:经定位来自**共享基础代码** `graphic/RenderObject.h:27`(RenderContext/RenderSurface 基类析构警告),由 sample 既有的 `RenderRef` 引用计数 Release 逻辑决定。
  - **判定:非 ARObject 引入。** 证据:该日志时间戳 `15:24:59`,而 ARObject 已于 `15:24:07` 完整销毁(`ObjectRenderManager Release end` 已记录),即它发生在另一个(baseline)场景的退出过程;且 `ObjectRenderManager::Release` 与 `WorldRenderManager::Release` 行为完全一致(同样的 RenderRef 门控),ARObject 未改变此行为。属 sample 既有的退出期 benign 警告,记录备查、不计为本阶段回归。
- **其余 E 级日志**:`AREngine: some buffer hasn't been returned`(AR Engine 相机流退出内部日志)、`CAMERA: ProcessFlashStateChange`(相机子系统)、`Bufferqueue` / `WMS` / `UIAbility withResult failed:5`(场景切换与 app 退出时系统级日志)——均为各场景通用的 benign 系统日志,**无一条新增、无一条来自 ARObject 逻辑、无崩溃信号**。

## 7. 结论
Stage 1 所有 Gate 通过 → **PASSED**。ARObject 场景已可启动、显示相机、安全返回,baseline 5 场景无回归。契约链(4 个 NAPI)已就位并以 stub 占位。

## 8. 遗留 / 给后续阶段
- Stage 2 待办:`ProbeQuaternionLayout()` 四元数序自检 + logo 正面偏移人工标定 + `object_math.h` 三个纯函数实现与单测 + `PlaceObjectAtWorld` 真正放置逻辑(详见 `impl-plan-arobject.md` Plan Addenda v1)。
- 备查(非本阶段问题):`RenderObject` 退出期 `Destructor called without Release()!` 警告为 sample 既有,涉及 `RenderRef` 计数;若未来要清零退出告警,需在 sample 层面统一处理(超出本 MVP 范围,且会动到共享文件,暂不碰)。

---
**等用户 review 本报告并说 `next` 再进 Stage 2。未 commit。**
