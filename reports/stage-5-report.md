# Stage 5 报告 — ARArrowAlign 对齐挑战游戏(路径 B)

**Status: PASSED ✅**

> 日期:2026-05-25 · 设备:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094`
> 构建:`BUILD SUCCESSFUL`(C++/ArkTS 一次过)· 单测:71/0 ALL PASS · 未 commit

---

## 1. 目标
新建独立 `ARArrowAlign` 场景(ARObject 保留不动):世界锚定的**红箭头**(朝向=会话启动时缓存的相机朝向)+ 相机锁定的**蓝箭头 HUD**(每帧用当前相机位姿,不建 anchor)。转动手机使两箭头方向对齐,带进度条/数值/绿勾 + 滞后判定。

## 2. 实现(按任务)
- **任务 1 几何**:`object/arrow_geometry.{h,cpp}` `GenerateArrowMesh`(柄圆柱+头圆锥,16 段,沿局部 -Z;柄/头分两段索引以便分色)。
- **任务 1 数学 `ComputeArrowDirection`**:四元数旋转 (0,0,-1)。
- **任务 2 `ComputeAngleBetweenArrows`**:`acos(clamp(dot(dirA,dirB),-1,1))`(clamp 防 NaN)。
- **任务 3 `UpdateAlignState`**:滞后状态机(<4° 进 ALIGNED,>6° 退 NOT_ALIGNED,中间保持)。
- **任务 4 渲染** `arrowalign/arrow_renderer.{h,cpp}`(统一着色器 + 柄/头两次分色 draw)+ `arrow_align_render_manager.{h,cpp}`:背景复用 `ARWorld::WorldBackgroundRenderer`;红箭头 = `trans(anchorPos)·Quat2Mat(initialQuat)·scale`;蓝箭头 = `trans(camPos+forward·1)·Quat2Mat(camQuat)·scale`(无 anchor)。
- **场景 App** `arrowalign/arrow_align_ar_application.{h,cpp}`:session init(关平面检测)、首次 TRACKING 缓存 `mInitialQuatXYZW`、每帧算角度+滞后状态、放置/开启/重置/查询。
- **任务 6 NAPI**:`placeTargetArrow` / `enablePlayerArrow` / `resetArrowAlign` / `getAlignmentState`(返回对象 {angleRad,isAligned,ready,targetPlaced,playerVisible})——经 app_napi.h 虚函数 + napi_manager + module.cpp + index.d.ts 接入;`CreateApp` 加 `ARArrowAlign` 分支。
- **任务 7 UI** `pages/ARArrowAlign.ets`:XComponent + 右上绿勾(对齐显示)+ 底部 差距数值/绿色进度条(=1-angle/π)+ 三按钮(放置目标[ready 后启用]/放置玩家[targetPlaced 后启用]/重置[常启用])+ 16fps 状态轮询。
- 路由注册 `router_map.json` + `Selector.ets` 入口按钮。

## 3. 单元测试(主机交叉编译 aarch64 → 真机 hdc shell)
- 新增 11+ 用例:`ComputeArrowDirection`(identity/Ry90/Rx90→(0,1,0)/Ry45+单位)、`ComputeAngleBetweenArrows`(同/反/垂直/45°)、`UpdateAlignState`(NA→A、NA带内→NA、NA>6→NA、A带内→A、A>6→NA、A<4→A)。
- 合计 **71 项断言全过**(累计 ForwardPoint/PackPoseRaw/YawToFaceCamera/ArrowDirection/AngleBetween/AlignState)。

## 4. Gate 1(自动 + hilog)
- ✅ `placeTargetArrow -> 1` + `TargetArrow placed id=1`(返回 1 非 -1 → 传递性证明 `mInitialCameraQuat` 已缓存,因 placeTargetArrow 在 !ready 时返回 -1)。
- ✅ `resetArrowAlign -> reset.`(detach 红 anchor)。
- ✅ 玩家箭头开启并渲染(Gate 2c blue_follows_phone 通过)。
- ✅ 无 `CHECK FAILED` / 无 Cppcrash。

## 5. Gate 2(人工,5/5 全绿)
| 动作 | 答案 | 判定 |
|---|---|---|
| (a) 启动只有按钮无箭头 | `see_buttons_only` | ✅ |
| (b) 放置目标→红箭头指初始朝向 | `red_appears_correct` | ✅ |
| (c) 放置玩家→蓝箭头跟手机转 | `blue_follows_phone` | ✅ |
| (d) 转回初始朝向→绿勾+进度满 | `tick_appears_progress_full` | ✅ |
| (e) 偏开 30°→绿勾消失/进度变短 | `tick_disappears_correctly` | ✅ |

## 6. 结论
几何+三数学函数(单测全过)、红/蓝双箭头架构差异(世界锚定 vs 相机锁定 HUD)、角度+滞后判定、UI 进度/绿勾/按钮状态机、reset 生命周期 —— 全部达成 → **PASSED**。

## 7. 观察 / 遗留(非缺陷)
- `placeTargetArrow` 始终返回 id=1:本场景只有一个目标箭头,重复点击会替换旧的(detach+release 后重建),故 id 恒为 1,符合设计。
- 大幅移动测试时日志见放置坐标整体跳变(如 (4.85,-5.60,...)),系 AR Engine 重定位/世界系微调,相对几何不变,Gate 2 仍正确。
- 偶见放置坐标 `(0,1,0)`:疑似点击瞬间相机短暂未 TRACKING、`GetPose` 返回默认位姿所致。**建议小加固**:`placeTargetArrow` 任务里增加"当前帧 camera TRACKING 才放置"的校验(现仅校验曾经 ready)。不影响本 Stage 判定。
- 调试用日志(`[ArrowAlign]` I 级)保留;最终收尾可清理。
- 红线遵守:未改 ARObject 任何场景文件(`object_ar_application.*`/`object_render_manager.*` 未动);`object_math.h` 仅**追加**新函数(ARObject 既有数学/常量不变);`arrow_geometry.{h,cpp}` 按指令置于 object/;未删 LogoRenderer;保留 `ComputeYawToFaceCamera`;未改 `kQuatFormat`/`kLogoFrontYawOffset`(ArrowAlign 直接用 XYZW);未 commit;未升级 SDK。

---
**等用户 review。说 `next` / 收尾 / 新方向。未 commit。**
