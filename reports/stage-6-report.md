# Stage 6 报告 — 对齐游戏可玩性升级

**Status: PASSED ✅**

> 日期:2026-05-25 · 设备:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094`
> 构建:`BUILD SUCCESSFUL`(C++/ArkTS 一次过)· 单测:94/0 ALL PASS · 未 commit

---

## 1. 六项设计变更(全部完成)
1. **删除蓝色玩家箭头**:渲染分支、NAPI `enablePlayerArrow`、ArkTS 按钮、native `mPlayerVisible`、getAlignmentState 的 playerVisible 字段——彻底删除,无废代码。ArrowGeometry/ArrowRenderer 保留(红箭头仍用)。
2. **中央十字准星 UI**:48×48vp、线宽 3vp,代表镜头朝向。
3. **目标朝向 = 前方半球随机**:`placeTargetArrow` 内 native 随机(yaw∈[-90°,90°]、pitch∈[-45°,45°]),`std::mt19937 + uniform_real_distribution`,seed=`std::random_device`。
4. **阈值 2°/4°**:`kAlignEnterRad=2°`、`kAlignExitRad=4°`(滞后)。
5. **十字颜色反馈**:白 `#CCFFFFFF` → 对齐绿 `#FF00E676`,由 `@State isAligned` 驱动。
6. **震动反馈**:`vibrator.startVibration({type:'time',duration:80},{usage:'notification'})`,在 isAligned false→true 边沿触发(ArkTS `prevAligned`),try/catch + `Logger.warn` 容错。权限 `ohos.permission.VIBRATE` 已加入 module.json5。
7. **tracking 加固**:`placeTargetArrow` 当前帧未 TRACKING → 返回 -1 + `LOGW`;ArkTS 收 -1 弹 Toast「请稳住手机等待跟踪稳定」,不切按钮态。

## 2. 关键架构变化
- 对齐角度从「初始相机朝向 vs 当前相机」改为「**随机目标方向 vs 当前相机 forward(十字)**」:每帧 `ComputeAngleBetweenArrows(mTargetQuatXYZW, camQuat)`;`mTargetQuatXYZW` 由 `QuaternionAlignZNegTo(随机 targetDir)` 构造。
- 红箭头渲染朝向改用 `mTargetQuatXYZW`(原为 `mInitialQuatXYZW`)。
- `mInitialCameraQuat` 字段**保留但本场景不再使用**(只缓存作 ready 信号/参考,符合红线)。
- 新增 `mCameraTracking` 原子量(每帧更新),供放置加固判定。

## 3. 规格冲突的处理(透明记录)
任务 4 称「3° 仅用于震动判定、不参与状态机」,任务 6 称「震动在 isAligned false→true 边沿触发」。两者矛盾。**采用任务 6**:状态机 2°/4° 产出的 `isAligned` 同时驱动十字变色与震动边沿(单一阈值源,行为一致)。独立 3° 未单独实现。

## 4. 单元测试(主机交叉编译 aarch64 → 真机 hdc shell)
- 新增 `ComputeRandomTargetDirection`(零偏/纯 yaw/纯 pitch/复合-锥度验证)+ `QuaternionAlignZNegTo`(4 方向往返)+ `UpdateAlignState` 改用 2°/4° 重写全路径。
- 合计 **94 项断言全过**。

## 5. Gate 1(自动 + hilog)
- ✅ **随机生效**:6 次 `TargetArrow placed` 的 yaw/pitch/targetDir/quat **均不同**;yaw∈[-1.15,1.04]⊂[-π/2,π/2]、pitch∈[-0.59,0.28]⊂[-π/4,π/4],范围正确。
- ✅ **tracking 加固**:`[ArrowAlign] target placement rejected: not tracking`(动作 f)。
- ✅ 无 `CHECK FAILED` / 无 Cppcrash(残留 E 级为 media_service/netmanager 系统服务,与本应用无关)。
- ✅ 震动:无 `vibrate failed` 日志;VIBRATE 低敏感权限安装即授予。

## 6. Gate 2(人工,6/6 全绿)
| 动作 | 答案 | 判定 |
|---|---|---|
| (a) 白十字 + 两按钮无玩家按钮 | `ui_correct` | ✅ |
| (b) 两次目标朝向明显不同 | `random_works` | ✅ |
| (c) 对齐时十字白→绿、进度满 | `cross_turns_green` | ✅ |
| (d) 对齐瞬间震动 | `vibrates` | ✅ |
| (e) 偏开>4° 十字绿→白、不再震 | `cross_turns_white_no_revibe` | ✅ |
| (f) 失稳放置弹 Toast | `toast_shown` | ✅ |

## 7. 结论
六项变更 + tracking 加固全部达成,单测 94/0,Gate 1+2 全绿 → **PASSED**。

## 8. 遗留 / 红线
- `placeTargetArrow` 始终返回 id=1(单目标,重复放置替换旧的)——沿用设计。
- 调试日志(`[ArrowAlign]` I 级)保留,最终收尾可清理。
- 红线遵守:未动 ARObject 场景文件(`object_ar_application.*`/`object_render_manager.*` 未改);`object_math.h` 仅追加/改动 **gameplay 阈值**(`kAlignEnterRad/ExitRad`,非 Stage 2/3 标定常量),`kQuatFormat`/`kLogoFrontYawOffset` 未动;ArrowGeometry/ArrowRenderer 保留;`mInitialCameraQuat` 字段保留(未用);未 commit;未升级 SDK。

---
**等用户 review。说 `next` / 收尾 / 新方向。未 commit。**
