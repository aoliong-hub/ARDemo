# Stage 8 报告 — ARRingHunt 精瞄 UI 升级

**Status: PASSED ✅**

> 日期:2026-05-25 · 设备:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094`
> 构建:`BUILD SUCCESSFUL` · 单测:135/0 ALL PASS · 未 commit

---

## 1. 五项升级 + 反馈修订 + 阈值调整
1. **废除 4 色 level**,改为 2 维独立反馈:**3D 圆环颜色只看距离**、**3D 箭头颜色只看角度**(GREEN `#00E676` / RED `#E63946`)。`ComputeAlignmentLevel` 按红线保留、不调用。
2. **2D 双圈精瞄 HUD**:距离 < 15cm 进入精瞄模式,屏幕中央叠加固定中圈(80vp)+ 对准圈(50vp),对准圈按 yaw/pitch 偏移移动。
3. **yaw/pitch 拆分**:`ComputeYawPitchDiff(camQuat, ringNormal)`,目标在右→yaw+,在上→pitch+;NAPI `getRingState` 新增 `yawDiffRad/pitchDiffRad/ringColor/arrowColor`,删除 `level`。
4. **删金色自旋**:`mSpinAngle` 及累加逻辑全删;FinishState 状态机保留(只判完成,不影响颜色)。
5. **完成动效**:仅震动 + "FOUND in N.Ns";UI 在 FINISHED 后锁定(定时器 early-return)。

**真机反馈修订(本阶段内迭代)**:
- **轴交叉(竖屏 90°)**:渲染端相机位姿由 `GetCameraPose` 改 `GetDisplayOrientedPose` → yaw/pitch 拆分对齐屏幕水平/垂直(`-Z` 朝向不变,角度/距离/FOUND 逻辑不受影响)。
- **"角度绿"加近距门控**(用户要求):3D 箭头 `arrowColor = (distOnTarget && angOnTarget)`;UI 十字/双圈 `isAligned = isProximityMode && angle<5°`。远处即使角度碰巧对准也保持红/白,只有"走近(<15cm)+ 对准"才变绿。
- **on-target 距离阈值 0.30 → 0.15m**(`kDistOnTargetMeters`),用于 IsDistanceOnTarget / 完成判定 / UI 精瞄触发;受影响的 `ComputeAlignmentLevel`、`UpdateFinishState` 单测距离值同步更新。

## 2. 新增数学函数与单测
- `IsDistanceOnTarget` / `IsAngleOnTarget` / `ComputeYawPitchDiff`。
- `ComputeYawPitchDiff` 单测 7 例:正对 / 右 / 左 / 上 / 下 / 右上 45° / 非单位相机(Ry90)对齐→0。
- 合计 **135 项断言全过**(主机交叉编译 aarch64 → 真机 hdc shell)。

## 3. Gate 1(自动 + hilog)
- ✅ `Ring placed` 多次 pos/quat **均不同**(随机生效)。
- ✅ `FOUND in 5.70s` / `6.14s`(完成计时正确);`reset` 正常;tracking 加固(placeRing 未跟踪返回 -1 + Toast,沿用 Stage 6/7)。
- ✅ 无 `CHECK FAILED` / 无 Cppcrash(残留 E 级为 media_service 系统服务,无关)。

## 4. Gate 2(人工,全绿)
首轮:`ui_correct; both_red; proximity_correct; (d)(e)轴交叉; dual_green(但箭头远处变绿); found_correct; reset_ok`。
修订后复测:
| 项 | 结果 |
|---|---|
| (c′) 15cm 精瞄、圆环绿、双圈出现、角度进度条隐藏 | `proximity_correct` ✅ |
| (d′) yaw → 对准圈水平移动 | `yaw_horizontal_ok` ✅ |
| (e′) pitch → 对准圈垂直移动 | `pitch_vertical_ok` ✅ |
| (f″) 远处对准箭头保持红;走近+对准才变绿 | `gated_ok` ✅ |
| (g′) 双对齐 0.5s → 震动+FOUND+不自旋+数值停更 | `found_ok` ✅ |
> a/b/h(布局/初始双红/重置)首轮已绿。

## 5. 结论
2 维独立反馈、2D 双圈精瞄、yaw/pitch 拆分(单测全过)、删金色自旋、近距门控、15cm 阈值、轴交叉修复 —— 全部达成,Gate 1+2 全绿 → **PASSED**。

## 6. 遗留 / 红线
- 方向标定:用户首轮报轴交叉(已修);最终 yaw 水平/pitch 垂直方向用户确认 OK(未要求再翻转符号)。
- `[RingHunt]` / `[ArrowAlign]` 等调试日志保留;最终收尾可统一清理。
- 红线遵守:ARObject、ARArrowAlign 场景文件一行未动;`ComputeAlignmentLevel` 保留(不调用);Stage 2/3/5/6 标定常量(`kQuatFormat`/`kLogoFrontYawOffset`)未动;ArrowGeometry/ArrowRenderer/LogoRenderer 未删;`GenerateArrowMesh` 原输出不变(参数化内部函数);未 commit;未升级 SDK。

---
**5 个 AR 子功能/游戏全部完成:ARObject(接口放置)、ARArrowAlign(箭头对齐)、ARRingHunt(圆环精瞄寻找)。等用户指示收尾(清理调试日志 / 写 README / git)。**
