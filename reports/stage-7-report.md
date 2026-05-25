# Stage 7 报告 — ARRingHunt 圆环寻找游戏(补记)

**Status: PASSED ✅(用户验收)**

> 日期:2026-05-25 · 设备:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094`
> 说明:用户在 Stage 7 真机验证后直接宣布 PASSED 并进入 Stage 8,故本报告为补记。Stage 7 的功能随后在 Stage 8 的 Gate 测试中再次得到验证(放置/随机/距离-角度/FOUND/重置均正常)。未 commit。

---

## 1. 目标
新建独立 `ARRingHunt` 场景(与 ARObject、ARArrowAlign 平级):随机位置放一个发光圆环,玩家走到圆环处(距离)并把镜头穿过它(角度),双条件保持 0.5s 即"找到"并计时。

## 2. 交付物(均新增,未动其它场景)
- 几何:`object/ring_geometry.{h,cpp}`(torus,R=0.10/r=0.005,32×8 段,+Z 法线);`object/arrow_geometry` 加 `GenerateSmallArrow`(参数化内部函数,原 `GenerateArrowMesh` 输出不变)。
- 渲染:`object/ring_renderer.{h,cpp}`(emissive 着色器,圆环+中央小箭头)。
- 渲染管理:`object/ring_hunt_render_manager.{h,cpp}`(背景复用 `ARWorld::WorldBackgroundRenderer`)。
- 场景:`object/ring_hunt_ar_application.{h,cpp}`(随机放圈、每帧 dist/angle/level/finish、计时、tracking 加固)。
- 数学(`object_math.h` 追加):`Compute3DDistance`、`ComputeRingTargetParams`(mt19937,前方半球随机位姿)、`ComputeAlignmentLevel`(4 色,Stage 8 已弃用但保留)、`UpdateFinishState`(0.5s 滞后锁定)。
- NAPI:`placeRing` / `resetRing` / `getRingState`;`CreateApp` 加 `ARRingHunt` 分支;router_map + Selector 入口。
- UI:`pages/ARRingHunt.ets`(十字 + 双进度条 + 数值 + 按钮 + FOUND 横幅 + 震动 + tracking Toast)。

## 3. 验证
- 单测(主机交叉编译 aarch64 → 真机):Stage 7 时 122 项全过(新增距离/随机参数/level/finish)。
- Gate 1(随后在 Stage 8 复核):`Ring placed` 随机 pos/quat 每次不同;`FOUND in N.Ns` 计时;无 CHECK FAILED / 无崩溃。
- Gate 2:用户真机验收 PASSED。

## 4. 备注
Stage 7 的 4 色状态(RED/YELLOW/ORANGE/GREEN)在 Stage 8 改为"距离/角度 2 维独立反馈 + 2D 双圈精瞄";`ComputeAlignmentLevel` 按红线保留(不再调用)。详见 `stage-8-report.md`。
