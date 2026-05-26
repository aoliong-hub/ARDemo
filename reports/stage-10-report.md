# Stage 10 报告 — ARRingHunt 屏幕外目标引导 UI（+ 水滴/呼吸光环/粒子流 UX 升级）

**Status: PASSED ✅**

> 日期:2026-05-26 · 设备:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094`
> 构建:`BUILD SUCCESSFUL`（bundled hvigor 6.21.1，`assembleHap --mode module`）· 单测:**153/0 ALL PASS**(135 沿用 + 18 新增)
> 应用范围:仅 ARRingHunt 场景。ARObject / ARArrowAlign 未涉及。
> 签名:构建用 `sign-real.ps1`,提交前 `sign-placeholder.ps1`(占位回填)。

---

## 目标:屏幕外目标引导

解决"圆环目标在玩家身后/视野外找不到"的痛点。引导显示条件:**圆环已放置 && 屏幕投影 ∉ 视野 && 未 FINISHED**。

---

## 任务 1:native 端屏幕投影 + 屏幕外方位(新增 NAPI 字段)

### 1.1 纯数学 `ComputeOffscreenGuidance`(`object_math.h`,header-only,主机可测)
- 输入:相机 `view[16]` / `proj[16]`(列主序,GL/glm 约定)+ 圆环世界坐标 `ringPos[3]`。
- 计算:`clip = proj · view · vec4(ringPos,1)`;`w = clip.w`;透视除法得 `ndc.xy`。
- 输出 `OffscreenGuidance`:
  - `isInView = (w>0) && |ndc.x|<0.9 && |ndc.y|<0.9`(10% 边距缓冲)。
  - `isBehind`:用**眼空间方位角** `yaw = atan2(eye.x, -eye.z)`,`|yaw| > 135°` 即正后方。
  - `screenEdgeX/Y`:把方向投到 `[-1,1]` 屏幕框边缘(`scale=max(|x|,|y|)`),转屏幕比例 `(0~1)`;Y 翻转(ndc 上 = 屏幕上)。背后时镜像 `ndc` 取回正确侧。
  - `indicatorAngleDeg = atan2(edgeX, edgeY)`(12 点钟为 0°,顺时针),给小箭头/水滴旋转。
- **设计抉择**:`isBehind` 与方位用"到圆环**位置**的方向"(经 view 矩阵进眼空间),而非 Stage 8 的 `ComputeYawPitchDiff`(那是"看穿圆环"的 ringNormal 对齐方向)。引导要把人导向圆环**所在**,位置方向才正确,二者在玩家移动后会发散。

### 1.2 矩阵贯通
- `RingCameraInfo` 扩 `viewMat[16]`/`projMat[16]`;`ring_hunt_render_manager` 在 `OnDrawFrame` 取到 view/proj 后(早返回前)`memcpy` 给 `outCam`——**复用渲染同一对矩阵**,不额外 AcquireCamera。
- `RingHuntApp::OnUpdate`:拿到 `ringPos3` 后调 `ComputeOffscreenGuidance`,结果存 5 个 atomic。无圆环/重置时 `isTargetInView=true`(引导隐藏)。

### 1.3 NAPI `getRingState` 扩字段(沿用原 out-param 风格)
新增 `isTargetInView:boolean / screenEdgeX:number / screenEdgeY:number / isBehind:boolean / indicatorAngleDeg:number`。改动:`app_napi.h` 默认桩、`ring_hunt_ar_application.{h,cpp}`、`napi_manager.cpp`、`types/libentry/index.d.ts` 的 `RingState` 接口。`screenEdgeX/Y` 为屏幕比例(0~1),ArkTS 端乘屏幕尺寸,不依赖分辨率。

### 1.4 单元测试(主机交叉编译 aarch64 → 真机 hdc shell)
新增 `TestComputeOffscreenGuidance` 6 例(语义断言,不硬比浮点):
1. 正前方 1m → `isInView=true`、非背后;
2. 右前方出右缘 → `!isInView`、`screenEdgeX>0.9`、Y 居中、箭头指右(45~135°);
3. 左上出左上 → `!isInView`、左侧、上半、箭头指左上(-90~0°);
4. 正后方 → `isBehind=true`、`!isInView`;
5. 屏内贴右缘(ndc.x=0.85<0.9)→ 仍 `isInView`;
6. 刚出右缘(ndc.x=0.95>0.9)→ `!isInView`、`screenEdgeX>0.9`。
合计 **153 断言全过**(135 沿用 + 18 新增)。

---

## 任务 2:ArkTS 引导 UI(`ARRingHunt.ets`)

- 16fps 轮询 `getRingState` 同步新字段到 `@State`。
- 显示门控:`ringPlaced && !isTargetInView && finishState!==2 && distance>=0.30`,`guidanceOpacity` 经 `animateTo` 0.3s 淡入淡出(`guidanceMounted` 在淡出期间保持挂载,完成后卸载)。**FOUND 立即隐藏**。
- **任务 4 保护**:`distance < 0.30m` 强制隐藏引导——既符合"贴脸时目标必在视野内 + 圆盘/双圈接管",也规避近裁剪面投影抖动。

### Gate 2(人工,5/26 全绿)
| 项 | 结果 |
|---|---|
| (a) 视野内不显示 / 视野外屏幕边缘出现缩略图 + 箭头指向 | `guidance_appears_correctly` ✅ |
| (b) 无目标时不显示 | `no_guidance_without_target` ✅ |
| (c) 转身出视野→缩略图贴目标方向边缘 | `edge_position_correct` ✅ |
| (d) 正后方→贴底部中央 + "向后转" | `behind_works` ✅ |
| (e) 重新进入视野→0.3s 淡出 | `fades_out_smoothly` ✅ |
| (f) <15cm→引导消失,只剩圆盘 + 双圈 | `close_distance_correct` ✅ |

---

## UX 升级(合并进本 Stage,纯 ArkTS,不动 native)

把"圆形缩略图 + 三角箭头"升级为"**水滴大箭头 + 呼吸光环 + 粒子轨迹流**"。复用已暴露的 `screenEdgeX/Y / isBehind / indicatorAngleDeg / distance / ringColor`。

### A 方向 — 水滴主体 + 呼吸光环
- **水滴**:ArkUI `Path` 包在 `Shape` 内,`viewPort{0,0,120,120}` + `width/height 120vp`。
  - ⚠️ **关键坑(已查证 OpenHarmony 文档)**:`Path.commands` 单位是 **px,1:1 绝对坐标,不随 width/height 缩放**。早期裸 `Path().width(70)` 在高密度屏上水滴只有 ~20vp("红点"),光晕却是真 vp。修复 = 用 `Shape` 的 `viewPort`(SVG 式按组件 vp 尺寸缩放子 path),做到分辨率无关。
  - commands 几何中心 (60,60),尖端朝上;`rotate(centerX/Y:'50%')` 绕中心**原地旋转**,角度 = `indicatorAngleDeg`(背后强制 180°,尖端朝下)。
- **呼吸光环**:`Circle` 108vp,`scale` 无限交替脉动 **0.95↔1.10**(≈103↔119vp,始终大于水滴,尖端不外露),周期随距离 800/1500/2000ms。
- **距离感光晕**:`Circle` 124vp(细描边氛围,`strokeWidth`/`opacity` 随距离 4/2/1、0.9/0.6/0.3)。
- 三者 + 水滴用 `Stack({alignContent: Center})` **共享中心**,严格同心。

### D 方向 — 粒子轨迹流(Canvas)
- 全屏 `Canvas`(`hitTestBehavior: None`,在水滴**下层**),60fps `clearRect`+重绘,粒子为普通数组(非 @State,避免每帧重建)。
- 从屏幕中心生成,1s 匀速飞向水滴**实时中心**,`alpha=1-t²`、`size=4-2t` 淡出收缩;出生时锁定圆环色。
- 生成间隔随距离 **50 / 150 / 300ms**(近密远疏);`<0.30m`/FOUND 立即清空,重新进入视野时让在途粒子自然飞完再自停。

### 正后方布局
水滴贴底部中央(Column 顶 `y=h-320`,尖端朝下),其下 `↻`(48fp)+ "向后转"(20fp);整组 160vp 宽居中。

### UX 真机验收(5/26 全绿)
| 项 | 结果 |
|---|---|
| 水滴为饱满主体、光晕为边缘氛围、比例 OK | `size_now_perfect`(经 70→120 放大 + 光环 95→124/108 多轮调比例后) ✅ |
| 水滴尖端跟目标方向**原地旋转**不漂移 | `rotates_in_place` ✅ |
| 中心→水滴持续粒子流 | `particles_flow` ✅ |
| 近(<1m)粒子/呼吸变快、远(>3m)变慢 | `distance_speed_correct` ✅ |
| 正后方:水滴贴底中央朝下 + ↻ + "向后转" | `behind_layout_correct` ✅ |
| <30cm:水滴/粒子/光环全消,只剩圆盘 + 双圈 | `clean_transition` ✅ |
| 重新进入视野平滑淡出 | `fades_smoothly` ✅ |

### 迭代记录(UX 升级中修复的 3 个回归/视觉 bug)
1. **正后方 ↻ + 文字消失(功能回归)**:旧布局 Column 定位过低被屏幕底裁掉 → 整组改单一 Column,顶部上移到 `h-320`,留足空间。
2. **水滴渲染为红点(太小)**:根因 `Path.commands` 是 px 非 vp(见上),Shape+viewPort 修复 + 整组放大到水滴 120vp。
3. **水滴跑圈外**:呼吸脉动 ±15% 让光环最小值(89vp)< 水滴(92vp)尖端外露 → 收窄脉动到 ±~8%(0.95↔1.10),光环始终包住水滴。

---

## Gate 1(自动)
- ✅ 编译(C++ ninja + ArkTS)+ 装机通过;HAP 4.92 MB。
- ✅ 单测 153/0 全过。
- ✅ ArkTS 仅余既有风格 WARN(`display.getDefaultDisplaySync` may-throw,与未改动的 `ARImageByAdd.ets` 同类),无 ERROR。
- ✅ 粒子瞬时数 < 20,Canvas 60fps 重绘 + 水滴 Path 旋转流畅,无掉帧。

## 红线遵守
- 仅改 ARRingHunt 场景(native 投影 + NAPI 扩字段 + ArkTS UI);ARObject / ArrowAlign **未动**。
- UX 升级**纯 ArkTS**,不动 native、不动 Fresnel/圆盘渲染管线、不动距离响应 / `<30cm` 清理 / 粒子方向 / `indicatorAngleDeg` 计算。
- 未改 Stage 7-8-9 阈值常量(`kDistOnTargetMeters=0.15` 等)与颜色逻辑。
- 未删既有代码(缩略图升级为水滴,但呼吸光环 / 距离光晕 / 方向指示均保留)。
- SDK 未升级(API 仍按工程锁定)。

---
**Stage 10 完成(含 UX 升级)。`sign-placeholder.ps1` 已回填占位签名,准备提交。**
