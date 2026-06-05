# 技术方案：VGGT-Omega + ARKit Pose 绝对尺度恢复

> **文档状态**：设计稿  
> **更新日期**：2026-06-05  
> **关联文档**：`docs/end-cloud-api-spec.md`（端云接口规格说明书）

---

## 1. 问题背景

### 1.1 现状

VGGT-Omega 模型每次推理产生一个**任意坐标系**，坐标系内部自洽（方向正确、角度正确），但**缺少绝对尺度**。这意味着：

- `guide_string` 中的**方向词正确**（"右"、"前"、"俯拍" 等）
- `guide_string` 中的**角度正确**（yaw/pitch/roll 不受尺度影响）
- `guide_string` 中的**距离数值没有物理意义**（"右 0.15 米" 中的 0.15 不是真实的 0.15 米）

### 1.2 目标

让 `guide_string` 中 `x`、`y`、`z` 的距离值变为真实物理世界的**米制距离**。不改变 guide_string 的格式和字段结构，对端侧接口的改动最小化。

### 1.3 方案选型

| 方案 | 原理 | 优势 | 劣势 |
|------|------|------|------|
| ~~双摄基线~~ | 用手机多摄的物理基线作为尺度参照 | 不需要额外传感器 | VGGT-Omega 不是双目模型，15mm 基线数值不稳定 |
| **ARKit/ARCore Pose** | 端侧每帧附带 metric pose，云侧做尺度对齐 | 模型在训练分布内，ARKit 提供可靠 metric scale | 需要端侧上传 pose 数据 |
| IMU 积分 | 用加速度计积分估计移动距离 | 不依赖 AR 框架 | 漂移严重，不适合精确距离 |

**选定方案：ARKit/ARCore Pose（方案 A）。**

---

## 2. 整体架构

### 2.1 改动前后数据流对比

**改动前：**

```
端侧                               云侧
────                               ────
JPEG帧 ─── [0x01 binary] ──→  解析 → 滑动窗口 → VGGT推理
                                  pose_enc [F,9]（任意尺度）
                                  guide.py 计算距离（单位无意义）
       ←── [JSON text] ────  guide_string（方向对，距离不准）
```

**改动后：**

```
端侧                               云侧
────                               ────
JPEG帧 + ARKit Pose               解析 → 存储 ARKit pose
  ─── [0x04 binary] ──→           滑动窗口 → VGGT推理
                                  pose_enc [F,9]（任意尺度）
                                  ScaleCalibrator:
                                    ARKit帧间距离(米) / VGGT帧间距离 = s
                                  guide.py 计算距离 × s（米制）
       ←── [JSON text] ────  guide_string（方向对，距离也准了）
```

### 2.2 核心原理

VGGT 每次推理的坐标系虽然任意，但**内部自洽**——如果帧 A 到帧 B 在 VGGT 空间距离为 `d_vggt`，帧 A 到帧 C 的距离也会按相同比例。ARKit 给出的是米制坐标，两帧间距离 `d_arkit` 是物理真实值。

全局尺度因子：

```
s = d_arkit / d_vggt
```

所有 VGGT 空间距离乘以 `s` 即得米制距离。角度和方向不受影响。

### 2.3 兼容性原则

- 旧客户端（发 `0x01` 消息）：**完全不受影响**，`s = 1.0`，行为和改动前一样
- 新客户端（发 `0x04` 消息）：距离数值变为米制
- 返回的 JSON 格式：**零改动**
- guide_string 的字段和格式：**零改动**

---

## 3. 尺度标定算法

### 3.1 单帧对标定

最简单的情况：取窗口中相距最远的两帧计算尺度。

```python
# VGGT 相机世界位置（从 world-to-camera 的 t 和 q 中恢复）
P_vggt_i = -R_i.T @ t_i    # R = quat_to_mat(q), t = pose_enc[:3]
P_vggt_j = -R_j.T @ t_j
d_vggt = ||P_vggt_j - P_vggt_i||

# ARKit 相机世界位置（端侧直接给的就是世界坐标，单位米）
d_arkit = ||arkit_pos_j - arkit_pos_i||

s = d_arkit / d_vggt
```

### 3.2 多帧鲁棒标定

用窗口内所有有 ARKit pose 的帧对，做最小二乘拟合：

```python
vggt_dists = [||P_vggt_i - P_vggt_j|| for i,j in pairs]
arkit_dists = [||arkit_pos_i - arkit_pos_j|| for i,j in pairs]

# 过零最小二乘：s = Σ(arkit * vggt) / Σ(vggt²)
s = dot(arkit_dists, vggt_dists) / dot(vggt_dists, vggt_dists)
```

### 3.3 时序平滑

连续推理间用 EMA 避免尺度闪烁：

```python
s_smoothed = α * s_current + (1 - α) * s_last    # α = 0.15
```

### 3.4 异常过滤

| 异常场景 | 检测条件 | 处理 |
|---------|---------|------|
| 用户几乎不动 | `d_vggt < ε` 或 `d_arkit < ε` | 跳过本次标定，沿用上一次 `s` |
| 尺度突变 | `s_current / s_last > 3` 或 `< 0.33` | 丢弃，沿用上一次 `s` |
| ARKit tracking 丢失 | 端侧发 `0x01`（无 pose） | 沿用上一次 `s`，降级为无标定模式 |

---

## 4. 云侧实现变更

### 4.1 protocol.py — 新增消息类型

新增 `MSG_FRAME_WITH_POSE = 0x04`，旧的 `MSG_FRAME = 0x01` 不变。

```python
MSG_FRAME_WITH_POSE = 0x04
POSE_PAYLOAD_SIZE = 28  # 7 × float32 (tx, ty, tz, qx, qy, qz, qw)

@dataclass
class IncomingFrame:
    sequence: int
    timestamp_ms: int
    jpeg_bytes: bytes
    arkit_pose: np.ndarray | None = None  # shape (7,) or None
```

解析逻辑：

```python
if msg_type == MSG_FRAME_WITH_POSE:
    pose_data = struct.unpack("!7f", raw[9:37])
    arkit_pose = np.array(pose_data, dtype=np.float64)
    jpeg_bytes = raw[37:]
    return IncomingFrame(sequence=seq, timestamp_ms=timestamp_ms,
                         jpeg_bytes=jpeg_bytes, arkit_pose=arkit_pose)
```

### 4.2 新增 scale_calibrator.py

独立模块，约 80 行代码。

```python
class ScaleCalibrator:
    """从 ARKit metric pose + VGGT pose 估计全局尺度因子。"""

    def __init__(self, ema_alpha: float = 0.15, min_distance: float = 0.02):
        self._scale: float = 1.0
        self._initialized: bool = False
        self._alpha = ema_alpha
        self._min_distance = min_distance

    def update(self,
               vggt_positions: list[np.ndarray],    # VGGT 世界坐标
               arkit_positions: list[np.ndarray],    # ARKit 世界坐标（米）
               ) -> float:
        """用多帧位置对更新尺度因子，返回当前 scale。"""
        ...

    @property
    def scale(self) -> float:
        return self._scale
```

### 4.3 window.py — 存储 ARKit pose

`SlidingWindow` 新增 `_arkit_poses: dict[int, np.ndarray]`（`frame_seq → arkit_pose`）。

`add_frame()` 时：如果 `frame.arkit_pose is not None`，存入字典。帧被驱逐时同步清理。

新增方法：

```python
def get_arkit_positions(self, frame_seqs: list[int]) -> dict[int, np.ndarray]:
    """返回指定帧的 ARKit 世界坐标。只返回有 ARKit pose 的帧。"""
```

### 4.4 app.py ws_infer — 插入标定步骤

在现有 `result = window.on_inference_complete(pose_enc)` 之后、`_compute_guide_string` 调用之前，新增约 15 行：

```python
# ---- 尺度标定（新增）----
scale = 1.0
arkit_positions = window.get_arkit_positions(window.get_frame_sequences())
if arkit_positions:
    vggt_positions = [...]  # 从 pose_enc 提取各帧的 P = -R^T @ t
    scale = calibrator.update(vggt_positions, arkit_positions)

# ---- 现有代码 ----
guide_string = _compute_guide_string(vggt_target, vggt_current, scale=scale)
```

### 4.5 guide.py — 距离乘以 scale

```python
def _compute_guide_string(
    target_pose: np.ndarray,
    current_pose: np.ndarray,
    scale: float = 1.0,           # 新增，默认不缩放
) -> dict[str, str]:
    ...
    spec_x = float(d_camera[0]) * scale
    spec_y = float(-d_camera[1]) * scale
    spec_z = float(d_camera[2]) * scale
    # yaw/pitch/roll/zoom 不受 scale 影响，不变
```

### 4.6 engine.py — 保留 depth 输出（可选，为后续铺路）

当前 `_infer_sync` 中 `del batch, predictions` 丢弃了 depth。方案 A 的核心逻辑不需要 depth（尺度来自 ARKit），但保留 depth 为后续「返回绝对深度图」铺路，边际改动极小（改动 3 行）。

不作为本次必须改动项，可后续追加。

### 4.7 变更影响汇总

| 模块 | 改动类型 | 改动量 | 对现有行为的影响 |
|------|---------|--------|----------------|
| protocol.py | 新增消息类型 + IncomingFrame 新增字段 | ~20 行 | 无（0x01 路径不变） |
| scale_calibrator.py | **新文件** | ~80 行 | 无 |
| window.py | 新增 arkit_poses 存储和查询 | ~30 行 | 无（现有字段不变） |
| app.py (ws_infer) | 插入标定步骤 | ~15 行 | 无（无 ARKit 时 scale=1.0） |
| guide.py | 函数签名新增 scale 参数 | ~5 行 | 无（默认 scale=1.0） |
| **合计** | | **~150 行** | **零回归风险** |

不需要改动的模块：preprocess.py、engine.py、vision_client.py、prompts.py、task_manager.py、alignment.py、config.py。

不需要改动的接口：`/ws/batch_infer`、HTTP `/camera-agent-api/api/generate`、HTTP `/mrightserver/uat/camera_agent_guide/infer`。

---

## 5. 端侧接口变更（端侧工程师必读）

> **核心结论：下行协议（云→端）零改动。仅上行协议新增一个消息类型。**

### 5.1 变更范围总览

| 项目 | 是否需要改动 | 说明 |
|------|------------|------|
| **上行 WebSocket 二进制消息** | **需要** | 新增 `0x04` 消息类型，附带 ARKit pose |
| 下行 WebSocket JSON 响应 | 不需要 | guide_string 格式和字段完全不变 |
| HTTP 服务 A (generate) | 不需要 | 图片生成与 pose 无关 |
| HTTP 服务 B (getGuide) | 不需要 | 双图引导接口独立 |
| guide_string 解析逻辑 | 不需要 | 返回的中文方向词 + 数值 + 单位格式不变 |

### 5.2 新增消息类型：`MSG_FRAME_WITH_POSE (0x04)`

#### 二进制格式

```
偏移   长度   类型        字段           说明
────   ────   ────        ────           ────
0      1B     uint8       type           固定 0x04
1      4B     uint32 BE   seq            帧序号（递增，同现有 0x01）
5      4B     uint32 BE   timestamp_ms   相对毫秒时间戳（同现有 0x01）
9      4B     float32 BE  arkit_tx       ARKit 世界坐标系 X（米）
13     4B     float32 BE  arkit_ty       ARKit 世界坐标系 Y（米）
17     4B     float32 BE  arkit_tz       ARKit 世界坐标系 Z（米）
21     4B     float32 BE  arkit_qx       ARKit 四元数 X
25     4B     float32 BE  arkit_qy       ARKit 四元数 Y
29     4B     float32 BE  arkit_qz       ARKit 四元数 Z
33     4B     float32 BE  arkit_qw       ARKit 四元数 W（scalar-last）
37     变长   bytes       jpeg_payload   JPEG 图像数据（同现有 0x01）
```

**头部总长 37 字节**（比现有 0x01 的 9 字节多 28 字节，即 7 个 float32）。

#### 与现有 0x01 的对比

```
0x01:  [type:1B] [seq:4B] [ts:4B]                    [JPEG...]    头部 9B
0x04:  [type:1B] [seq:4B] [ts:4B] [arkit_pose:28B]   [JPEG...]    头部 37B
```

两者的 `seq`、`timestamp_ms`、`jpeg_payload` 语义完全相同。0x04 只是在头部中间插入了 28 字节的 ARKit pose。

#### 打包示例（TypeScript / ArkTS 伪代码）

```typescript
function packFrameWithPose(
    seq: number,
    timestampMs: number,
    jpegBytes: ArrayBuffer,
    arkitPose: { tx: number, ty: number, tz: number,
                 qx: number, qy: number, qz: number, qw: number }
): ArrayBuffer {
    const headerSize = 37;
    const buf = new ArrayBuffer(headerSize + jpegBytes.byteLength);
    const view = new DataView(buf);

    // 与 0x01 相同的前 9 字节
    view.setUint8(0, 0x04);                         // type
    view.setUint32(1, seq, false);                   // seq, big-endian
    view.setUint32(5, timestampMs, false);           // timestamp_ms, big-endian

    // 新增的 28 字节 ARKit pose
    view.setFloat32(9,  arkitPose.tx, false);        // big-endian
    view.setFloat32(13, arkitPose.ty, false);
    view.setFloat32(17, arkitPose.tz, false);
    view.setFloat32(21, arkitPose.qx, false);
    view.setFloat32(25, arkitPose.qy, false);
    view.setFloat32(29, arkitPose.qz, false);
    view.setFloat32(33, arkitPose.qw, false);

    // JPEG payload（同 0x01）
    new Uint8Array(buf, headerSize).set(new Uint8Array(jpegBytes));
    return buf;
}
```

### 5.3 ARKit Pose 数据来源

端侧需要在每帧抓取时同步获取 AR Engine 的相机世界变换矩阵。

#### HarmonyOS AR Engine

```typescript
// 在 AR 帧回调中
const cameraPose = frame.getCamera().getPose();
// cameraPose 提供 translation [tx, ty, tz] 和 rotation quaternion [qx, qy, qz, qw]
// 坐标系：右手系，Y 朝上，Z 朝用户（OpenGL 系）
// 单位：米
```

#### iOS ARKit

```swift
let transform = frame.camera.transform
// transform 是 4x4 齐次矩阵（simd_float4x4）
// 提取 translation: transform.columns.3.xyz
// 提取 quaternion: simd_quaternion(transform) → (ix, iy, iz, r) = (qx, qy, qz, qw)
```

#### Android ARCore

```java
Pose cameraPose = frame.getCamera().getPose();
float[] t = cameraPose.getTranslation();    // [tx, ty, tz]
float[] q = cameraPose.getRotationQuaternion(); // [qx, qy, qz, qw]
```

### 5.4 四元数格式约定

| 属性 | 约定 |
|------|------|
| 排列顺序 | **XYZW**（scalar-last）：`[qx, qy, qz, qw]` |
| 归一化 | 必须归一化：`qx² + qy² + qz² + qw² ≈ 1.0` |
| 坐标系 | AR 引擎原生坐标系（通常是 OpenGL 右手系：Y朝上，Z朝后） |

> **注意**：云侧不需要端侧做坐标系转换。云侧拿到 ARKit pose 后只计算帧间**距离**（`||pos_i - pos_j||`），这个距离值在任何坐标系下都一样。

### 5.5 降级策略

如果端侧暂时无法获取 AR pose（AR tracking 丢失、低端设备不支持等）：

1. **降级为 0x01 消息**：不附带 pose，按原有格式发送
2. 云侧收到 0x01 后 arkit_pose = None，尺度因子保持上一次有效值（或默认 1.0）
3. **对客户端无任何负面影响**——guide_string 照常返回，距离不准但方向正确（和改动前一样）

建议端侧实现：

```typescript
if (arTrackingState === TrackingState.TRACKING) {
    // 有可靠 pose，发 0x04
    ws.send(packFrameWithPose(seq, ts, jpeg, cameraPose));
} else {
    // tracking 丢失或不支持，降级发 0x01
    ws.send(packFrame(seq, ts, jpeg));
}
```

### 5.6 端侧不需要改动的地方（确认清单）

以下逻辑**完全不需要改动**：

- [x] WebSocket 连接建立方式
- [x] 下行 JSON 消息解析（所有字段和嵌套层级不变）
- [x] guide_string 的解析正则和方向词映射
- [x] placeRingAt / AR 引导渲染逻辑
- [x] pitch 符号取反约定
- [x] 图片旋转扶正逻辑
- [x] HTTP 服务 A (generate) 全流程
- [x] HTTP 服务 B (getGuide) 全流程
- [x] 轮询逻辑和超时策略
- [x] task_id 会话管理

### 5.7 端侧改动工作量估算

| 改动点 | 预估工作量 |
|--------|-----------|
| 新增 `packFrameWithPose()` 打包函数 | ~20 行 |
| 帧回调中获取 AR camera pose | ~5 行 |
| 发送时判断 tracking 状态选择 0x01/0x04 | ~5 行 |
| **合计** | **~30 行** |

---

## 6. 下行响应格式（确认无变更）

以下是改动后云侧返回的 JSON 示例。与改动前**完全一致**——唯一的区别是 `x/y/z` 中的数值变成了物理真实的米制距离。

```json
{
  "type": "pose",
  "frame_seq": 42,
  "timestamp_ms": 12345,
  "translation": [0.12, -0.03, 0.85],
  "quaternion": [0.01, 0.02, 0.0, 1.0],
  "current_translation": [0.0, 0.0, 0.0],
  "current_quaternion": [0.0, 0.0, 0.0, 1.0],
  "fov": [1.05, 1.02],
  "guide_string": {
    "x": "右 0.12 米",
    "y": "上 0.03 米",
    "z": "前 0.85 米",
    "yaw": "右转 5.2°",
    "pitch": "俯拍 3.1°",
    "roll": "0.0°",
    "zoom": "1.05x"
  },
  "guidance": "向右移动一小步，镜头稍微抬高",
  "nav_status": "navigating"
}
```

---

## 7. 验证方案

### 7.1 云侧单元测试

```python
# test_scale_calibrator.py
def test_known_scale():
    """已知 ARKit 距离 = 2.0m，VGGT 距离 = 0.5（任意单位），s 应为 4.0"""
    cal = ScaleCalibrator()
    s = cal.update(
        vggt_positions=[np.array([0,0,0]), np.array([0,0,0.5])],
        arkit_positions=[np.array([0,0,0]), np.array([0,0,2.0])],
    )
    assert abs(s - 4.0) < 0.01
```

### 7.2 兼容性回归测试

```python
def test_no_arkit_fallback():
    """无 ARKit pose 时 scale=1.0，行为与改动前一致"""
    # 发送 0x01 消息（无 arkit_pose）
    # 验证 guide_string 输出与改动前完全相同
```

### 7.3 端到端验证

1. 在有 AR Engine 的设备上录制视频 + ARKit pose
2. 在距离 1m、2m、3m 处放置标定物
3. 对比 guide_string 中 z 值与实际距离
4. 验收标准：室内正常光照下误差 ≤ ±10%

---

## 8. 后续扩展路径

完成本方案后，可渐进式增加以下能力（均为增量改动，不影响现有接口）：

| 扩展 | 描述 | 前提 |
|------|------|------|
| 绝对深度图返回 | engine.py 保留 depth 输出，乘以 scale 后返回 | 本方案 + engine.py 改 3 行 |
| Metric 3D 点云 | depth + pose → 反投影得米制点云，返回给 AR 渲染 | 上一步 + 新增接口 |
| 全局坐标系对齐 | 启用 alignment.py 的 Procrustes 对齐，消除跨推理坐标系跳变 | 独立于本方案 |
| getGuide 接口也支持 metric | HTTP 双图推理也接受可选的 ARKit pose 参数 | 小改动 |
