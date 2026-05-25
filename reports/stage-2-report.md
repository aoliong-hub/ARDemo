# Stage 2 报告 — 接口式放置 + 四元数自检 + 朝向标定 + 跟踪稳定性

**Status: PASSED ✅**

> 日期:2026-05-25 · 设备:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094`
> 构建:`BUILD SUCCESSFUL`(清理后干净重建)· 单测:38/0 ALL PASS · 未 commit

---

## 1. 目标(回顾)
实现 `placeObjectAtWorld` 真实放置;启动自检四元数布局;自动放一个 logo;标定 logo 正面朝向;验证 anchor 跟踪稳定。

## 2. 三个关键动作结果

### 动作 A(自动)— ProbeQuaternionLayout
- 启动即在 OnStart 任务里运行(早于任何放置任务,同一任务线程串行保证顺序)。
- 构造 Ry(+90°) 的 XYZW / WXYZ 两个候选,`ARPose_Create`+`ARPose_GetMatrix`,按列主序断言 `m[2]≈-1 && m[5]≈1 && m[0]≈0`。
- hilog 实测:`XYZW-candidate m2=-1 m5=1 hit=1`,`WXYZ-candidate m2=+1 m5=-1 hit=0` → **`result=XYZW`**,无歧义、非 FATAL。
- 运行期锁定 `mQuatFormat=XYZW`,placement 在 probe 通过前不放置(`mProbeOk` 门控)。

### 动作 B(自动+人工)— 放 logo + 朝向标定
- 自动在相机前放一个 logo;**人工 gate 回答 `FRONT`** → logo identity 旋转下正面即朝相机。
- 锁定 **`kLogoFrontYawOffset = 0`**(写入 `object_math.h`,供 Stage 3 朝向使用)。

### 动作 C(人工)— 跟踪稳定性
- 用户左右走 + 绕半圈,回报 **`stable`** → 通过。
- 日志佐证:anchor 世界坐标 40 秒内仅漂移 ~1mm(`(1.0329,-0.1662,0.4667)` → `(1.0332,-0.1672,0.4668)`),`anchorTrackingState=0`(TRACKING)恒定。

## 3. 过程中的诊断与修正(重要)
**现象**:首次自动放置 `placeObjectAtWorld(0,0,-1)` 后 logo 不可见(用户报 INVISIBLE / 扫一圈 NOT_FOUND)。
**诊断(纯日志,未瞎改)**:probe=XYZW ✅、`AnchorAdded pos=(0,0,-1)` ✅、`DRAW anchorWorldPos=(0,0,-1)` ✅(跟踪帧里确实画了)、移动时相机进 `PAUSED`(state=1)→ 暂停期不渲染。
**根因**:`(0,0,-1)` 是相对**世界原点 = session 启动瞬间相机位姿**,不是相对当前视线;用户进场景时手机朝向随意 → logo 钉在视野外。这与用户本意"相机前 1 米"(相机相对)不符。
**修正(经用户同意)**:把 Stage 2 自动放置触发改为 **`placeObjectInFrontOfCamera(1.0)`**——取当前相机位姿(`GetDisplayOrientedPose`)→ 复用已测 `ComputeForwardPoint` 算正前方 1 米世界点 → identity 旋转放置。`placeObjectAtWorld(x,y,z)` 绝对接口保持不变且经诊断证明精确。修正后 logo 必现于正前方,标定顺利完成。

## 4. 本阶段实现
- `object_math.h`(header-only 纯数学):`ComputeForwardPoint`、`PackPoseRaw`、`YawToQuaternionXYZW`、`RotateVectorByQuatXYZW`、`UnpackQuatXYZW`、`kMaxObjects=50`、`kLogoFrontYawOffset=0`、`QuatFormat`。
- `object_ar_application.{h,cpp}`:
  - `ProbeQuaternionLayout()`(动作 A)
  - `PlaceObjectAtWorld`(绝对世界坐标,identity 旋转)
  - `PlaceObjectInFrontOfCamera`(相机前定位,identity 旋转;**朝向 yaw 仍留 Stage 3**)
  - `RemoveObject` / `ClearAllObjects`(完整实现,Stage 3 直接用)
  - 共享 `ReservePlacement`(错误处理+上限)、`PlaceAtWorldOnTaskThread`
- `ARObject.ets`:Stage 2 调试触发(5s 后自动放一次,成功即停)。
- 接口错误处理与上限(Addendum 5):session 未就绪/probe 未过/未跟踪 → -1;≥50 → -1 + WARN;objectId 从 1 单调递增;anchor Detach+Release 配对,移除/清空在任务线程释放,渲染用加锁快照避免竞态。

## 5. 单元测试(Addendum 4)
- `entry/src/main/cpp/test/object_math_test.cpp`,自包含断言框架(ohos 无 gtest)。
- **因主机无 C++ 编译器**,用 SDK 的 `clang++ -target aarch64-linux-ohos` 交叉编译,推真机 `hdc shell` 运行(同架构、同代码路径)。
- 覆盖 `ComputeForwardPoint`(3 例:正前、yaw90°、低头看下)、`PackPoseRaw`(3 例:identity/Ry180°+平移/非归一化),共 **38 项断言全过**。

## 6. 行为确认(日志)
- **只放一次**:`auto-place attempt 1 -> objectId=1`,单个 `AnchorAdded`。
- **放置后固定**:anchor 世界坐标 40 秒漂移 ~1mm(不跟随相机)。
- **遮挡**:用户确认 **不做**——MVP 保持虚拟物体始终画在最上层(无深度遮挡)。

## 7. 自动 / 人工 Gate
- ✅ 单测全过;✅ 编译/装机通过
- ✅ 动作 A probe=XYZW(自动)
- ✅ 动作 B FRONT(人工)
- ✅ 动作 C stable(人工)

## 8. 清理与遗留
- 已移除临时诊断日志(`[ARObject][Diag]`)及相关成员,恢复干净渲染代码,重建通过。
- 仍保留:`ARObject.ets` 的"5s 自动放一次"调试触发(Stage 3/4 测试用,后续可删)。
- **Stage 3 待办**:`ComputeYawToFaceCamera` 实现 + 朝向相机(用 `kLogoFrontYawOffset=0`);`placeObjectInFrontOfCamera` 加 facing;`removeObject`/`clearAllObjects` 接通 UI/验证生命周期;objectId 复用验证。
- 备查:`placeObjectInFrontOfCamera` 的定位部分已在本阶段提前实现(facing 仍归 Stage 3),属诊断驱动的合理提前。

---
**等用户 review 后说 `next` 进 Stage 3。未 commit。**
