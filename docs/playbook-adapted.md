# ARDemo 主控手册(适配版)

> **STATUS: SUPERSEDED by `recon-mesh.md` as of 2026-05-25。** 理由:需求由用户交互式放置/旋转/缩放变更为接口式世界坐标放置。本文档对 ARWorld 场景的拆解、ArkUI 手势共存路线 B 的论证保留,以备未来如下场景需要时复用:(a) 给 MVP 增加手指交互;(b) 在 ARObject 场景上层加上 UI 选中/拖拽。当前权威方向见 `recon-mesh.md` 与 `impl-plan-arobject.md`。

> 基于 `cc-harmony-ar-playbook.md` 修订,**对齐本工程真实情况**。
> 凡与原手册冲突,**以本文件为准**。修订依据见 `reports/recon.md` 第 g 节。
> 生成日期:2026-05-25。

---

## 0. 平台事实(已用工程核实,替换原手册第 3 节"平台事实")

| 项 | 原手册 | **本工程真实值** |
|---|---|---|
| SDK 等级 | API 12+ | **API 20 / compatibleSdkVersion=targetSdkVersion=6.0.0(20)** |
| 系统 | HarmonyOS NEXT | **HarmonyOS(纯血)**,runtimeOS=HarmonyOS |
| bundleName | com.huawei.arengine.demo | **com.huawei.ARSample** |
| Ability | EntryAbility | EntryAbility(一致) |
| 架构 | — | arm64-v8a |
| hvigor modelVersion | — | 6.0.0 |
| 真机 | 任意支持 AR 机型 | HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094` |
| 自动签名 | 手动一次 | **已配置完成,禁止改动签名相关文件**(`build-profile.json5` 含敏感字段,禁止 git add) |

ArkTS 严格子集约束、C++(C++17/无异常/无 RTTI/GL 后查错/NAPI nullptr 检查)约束**沿用原手册**。
分层契约修订为:`pages/ARWorld.ets`(UI/权限/手势)↔ `cpp/src/world/*`(AR+GL+NAPI),二者**仅经 `cpp/types/libentry/index.d.ts` 通信**。

---

## 1. 工具链命令(替换原手册 macOS/bash 假设)

本机是 **Windows + PowerShell**;项目根**无 `hvigorw`**,统一用 DevEco 自带。

```powershell
# hdc(已在 PATH;或用绝对路径)
$HDC = "C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe"

# hvigorw(DevEco 自带 6.21.1,需先把 node 加进 PATH)
$env:Path = "C:\Program Files\Huawei\DevEco Studio\tools\node;" + $env:Path
$HVIGORW = "C:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat"

# 设备
& $HDC list targets                                   # → 7LK0126123000094

# 构建(debug)
& $HVIGORW --mode module -p product=default assembleHap
# 产物:entry\build\default\outputs\default\entry-default-signed.hap

# 安装 + 启动
& $HDC install -r "entry\build\default\outputs\default\entry-default-signed.hap"
& $HDC shell aa start -a EntryAbility -b com.huawei.ARSample   # ✅ 已验证可拉起

# 截图 + 取回
& $HDC shell snapshot_display -f /data/local/tmp/cap.png
& $HDC file recv /data/local/tmp/cap.png screenshots\cap.png

# 日志
& $HDC shell hilog | Select-String -Pattern 'ARWorld|AREngine|GL|EGL'
```

> 原手册的 `scripts/deploy_and_verify.sh` 是 bash;本机需改写成 `.ps1` 或逐条手跑。**部署脚本属于代码改动,留到正式 Stage 再决定是否新增,不在侦察阶段创建。**

---

## 2. 目标功能(本项目实际目标,替换原手册 8-Stage)

只在 **ARWorld** 场景增量添加,不破坏 baseline(相机/平面/点击放置/视角跟踪已验证 ✅):

| Stage | 功能 | 验收 |
|---|---|---|
| **S1** | 双指旋转已放置物体(绕世界 Y) | 双指转动 → 选中物绕竖直轴转,不跳原点 |
| **S2** | 双指捏合缩放,范围 0.2x–3.0x | 捏合缩小/张开放大,到边界 clamp |
| **S3** | 长按 1 秒删除指定物体 | 长按物体 1s 消失;空地长按无反应 |
| **S4** | 顶部状态条 | "扫描平面中..." / "已检测到 N 个平面,点击放置" / "跟踪丢失" |
| **S5** | "清空全部"按钮 | 一键移除所有放置物 |

每个 Stage 完成后:`assembleHap` 无 error → 装机 → 截图+hilog → 写 `reports/stage-N-report.md` → **等"next"**。
多指/视角类验收需**人工**手持真机确认(原手册第 0 节"诚实预期"沿用)。

---

## 3. 真实代码锚点(替换原手册各 Stage 的"改动文件",均已核实行号)

### ArkTS 侧
- XComponent 挂载:`pages/ARWorld.ets:44`(`libraryname:'entry'`,id=时间戳+'ARWorld')
- 帧循环:`ARWorld.ets:51` `.onLoad` 内 `setInterval(33ms)` → `arEngineDemo.update(id)` 返回平面数
- UI 叠加位:`ARWorld.ets` 的 `RelativeContainer`(目前仅含全屏 XComponent)→ 状态条/清空按钮加这里
- 文案资源:`resources/base/element/string.json`

### C++ 侧(ARWorld 场景)
- AR 生命周期 + 点击放置:`cpp/src/world/world_ar_application.cpp/.h`
  - 帧入口 `OnUpdate()` `:101` → `WorldRenderManager::OnDrawFrame()`
  - 点击入口(native)`DispatchTouchEvent(component,window)` `:217` → `DispatchTouchEvent(float,float)` `:166`
  - 锚点列表 `std::vector<ColoredAnchor> mColoredAnchors`(`.h:55`)
  - 删除范例 `OnStop()` `:65`(Detach+Release+clear)
- 渲染:`cpp/src/world/world_render_manager.cpp/.h`
  - **模型矩阵注入点 `RenderObject()` `:125`**(旋转/缩放改这里:`poseMat * RotY(yaw) * Scale(0.2*scale)`)
  - 平面计数 `static mPlaneCount`(`:168` 更新)
  - 相机 TrackingState 在 `InitializeDraw()` `:116`(状态条"跟踪丢失"的来源)
  - `struct ColoredAnchor { AREngine_ARAnchor* anchor; float color[4]; }`(`.h:28`)— user-transform 字段加这里
- NAPI 契约层(新增任何 ArkTS→native 接口要四处同步改):
  1. `cpp/types/libentry/index.d.ts`(声明)
  2. `cpp/src/module.cpp`(`desc[]` 注册,`Init`)
  3. `cpp/src/napi_manager.cpp/.h`(`Napi*` 静态方法,参数解包)
  4. `cpp/src/app_napi.h`(虚函数)+ `world_ar_application.cpp/.h`(override)
- 编译目标:`cpp/CMakeLists.txt`(`add_library(entry SHARED ...)`;新增 .cpp 要登记到这里——但目标是尽量只改现有 world 文件,不新建)

### AR Engine NDK 关键函数(已在 sample 中实际使用,可直接参考调用法)
- HitTest:`HMS_AREngine_ARFrame_HitTest` / `ARHitResultList_*` / `ARHitResult_AcquireNewAnchor` / `ARPlane_IsPoseInPolygon`
- 锚点:`ARAnchor_GetTrackingState` / `ARAnchor_GetPose` / `ARPose_GetMatrix` / `ARAnchor_Detach` / `ARAnchor_Release`
- 相机:`ARCamera_GetViewMatrix` / `GetProjectionMatrix` / `GetTrackingState`
- 平面:`ARSession_GetAllTrackables` / `ARTrackableList_*` / `ARPlane_AcquireSubsumedBy`
> 函数签名以 sample 现有调用与 `ar/ar_engine_core.h` 为准,**不猜函数名**(原手册硬规则保留)。

---

## 4. 手势输入路线(原手册假设 ArkTS GestureGroup,本工程触摸是 native——需先定路线)

见 `reports/recon.md` 第 g 节"待决策"。两条路线:
- **A native 多指**:扩 `DispatchTouchEvent` 读 `touchPoints[]`,自算角度/间距/长按。不动契约层,但手势状态机要手写。
- **B ArkTS 手势 + 新 NAPI**(倾向):`ARWorld.ets` 挂 `RotationGesture`/`PinchGesture`/`LongPressGesture`,经新 NAPI 推 angle/scale/delete;选中/拾取仍在 C++。需扩契约四件套,并实测 ArkTS 手势与 native DOWN(点击放置)在同一 SURFACE XComponent 上不打架。

**无论 A/B,C++ 数据模型改动一致**:`ColoredAnchor` 加 `userYawRad`/`userScale`、引入"选中锚点"概念、`RenderObject` 注入变换。

---

## 5. 硬约束(沿用原手册"绝对不要做的事" + 本次补充)

- ❌ 不升级 SDK / 不换 API 等级(锁定 6.0.0/API 20)
- ❌ 不碰签名相关任何文件;❌ 不 `git add` / 不修改 `build-profile.json5`
- ❌ 不重命名 / 不删除任何官方 sample 文件
- ❌ 不 git commit(用户手动管 git)
- ❌ 不重写 `world_background_renderer.cpp` / `world_plane_renderer.cpp`,只在其上加功能
- ❌ C++ 里不调 ArkGraphics 3D(EGL 冲突)
- ✅ 每次 GL 调用后查 `glGetError`;NAPI 入参先 nullptr 检查
- ✅ 范围最小:只改本 Stage 文件,优先复用 sample 既有代码
- ⚠️ `.gitignore` 当前**未**过滤 `reports/` `docs/` `screenshots/`——留意别误提交临时产物

---

## 6. 失败回退(沿用原手册第 7 节,命令换成 PowerShell/绝对路径)
- 编译同类错误连续 3 次未解 → 写 `issues/blocker-N.md` + 停止找人
- hilog `AR Engine service not found / version mismatch` → 停,提示装/升 AR Engine 服务(baseline 已通,一般不会)
- 截屏全黑/全白 → 先 `hdc shell hilog | Select-String 'GL|EGL|render'` 定位,别瞎改矩阵
- HitTest 总返回 0 → 查屏幕坐标系(竖/横屏)与平面 TrackingState
- 人工 Gate 回 `drift_severe`/`lost` → 进诊断:光照 / 特征点 / 设备热降频
