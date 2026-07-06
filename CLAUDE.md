# ARDemoCode_v2 项目说明

## 项目概述

HarmonyOS AR 相机构图辅助应用。用户通过云端 AI 生成目标构图参考图，AR 引擎引导用户走到目标机位，本地 NGA 算法完成像素级精调后自动拍照。

平台：HarmonyOS（DevEco Studio + Hvigor），原生语言 ArkTS + C++（NDK）。

---

## 完整用户流程

### 阶段一：生成目标图（云端）
1. 用户点击**炫彩球按钮**，触发 `onBeaconTap()`
2. 调用 `captureJpeg()` 抓取当前画面
3. 通过 `Da3Service.generate()` 上传至云端，生成 AI 风格化目标参考图
4. 目标图显示在底部 `ReferencePanel` 卡片中，缓存为 `targetJpeg`

### 阶段二：计算机位（云端）
1. 用户点击参考图，触发 `onTargetTap()`
2. 再次抓取当前帧作为"当前位置"图
3. **Da3 后端**：HTTP 调用 `da3GetGuide(targetJpeg, currentJpeg)` → 返回 `guide_string {x,y,z,yaw,pitch,roll,zoom}`
4. **vggt-omega 后端**：WebSocket 连续推帧，服务端返回 `guide_string` 或 `beacon_world`（世界坐标直接放置）
5. 调用 `arEngineDemo.placeRingAt()` 或 `placeRingAtWorld()` 在 AR 场景中放置 Wayfinder 信标

### 阶段三：走向信标（AR 导航）
1. native 侧 `RingHuntApp` 每帧计算相机到信标的距离与角度差
2. `stateTimer`（62ms 轮询）从 `getRingState()` 读取状态：
   - `huntPhase=0`：APPROACHING，显示离屏引导箭头
   - `huntPhase=1`：ALIGNING（进入 30cm），AR 对齐框出现，L 括显示，炫彩点随 yaw/pitch 移动
   - `huntPhase=2`：LOCKED（角度+填充率满足条件），LOCKED banner 淡入

### 阶段四：NGA 像素级精调（本地）
1. 检测到 `isLocked` 从 false→true 且 `targetJpeg` 存在，启动 **1.5 秒延迟定时器**
2. 延迟结束后调用 `startNgaPhase()`：
   - 将 `targetJpeg` 解码为 RGBA，调 `ngaSetReference()` 设定参考帧
   - AR 信标和对齐框隐藏（信标锁定后不再需要）
   - 屏幕中央淡入**环形进度 UI**（140vp 圆环）
3. 以约 15fps 持续运行 `ngaProcessOneFrame()`：
   - 抓取当前帧 → `ngaProcessFrame()` → 解析 JSON 结果
   - 计算各维度偏差（dX/dY/roll/scale）的归一化比值，取最大值得到综合进度（0~100%）
   - 更新圆环颜色（红<40% → 黄<75% → 绿）和环内指令文字
4. `is_aligned=true` 时 → 震动 + 快门音 + **自动拍照**（`captureCleanJpeg()`）→ 存图库 → 全场景重置

### 异常处理
- LOCKED 期间走出（`isLocked` 变 false）→ 取消 1.5s 定时器，不进入 NGA
- NGA 跟踪丢失（`tracking_lost=true`）→ 圆环归零，提示"跟踪丢失，请稳住"，继续循环
- 页面退出（`onWillDisappear` / `onHidden`）→ 调 `stopNgaPhase()`，释放 NGA 引擎

---

## 架构分层

```
ArkTS (UI 层)                       C++ Native 层
─────────────────────────────────   ──────────────────────────────
ARRingHuntCam.ets                   RingHuntApp (AppNapi)
  ├── Da3Service.ets (HTTP)    ←→     ring_hunt_ar_application.cpp
  ├── Da3WsService.ets (WS)           ring_hunt_render_manager.cpp
  ├── CloudConfig.ets                 wayfinder_renderer.cpp
  └── NgaTypes.ets             ←→   nga/nga_core.cpp (SIFT+光流)
                                      nga/nga_napi.cpp (NAPI桥)
```

NAPI 桥接：ArkTS `import arEngineDemo from 'libentry.so'`，所有跨语言调用通过 `module.cpp` 注册的函数表路由。

---

## 关键文件索引

| 文件 | 职责 |
|------|------|
| `entry/src/main/ets/pages/ARRingHuntCam.ets` | 主页面，完整流程控制（2700+ 行） |
| `entry/src/main/ets/services/Da3Service.ets` | 云端 HTTP 服务（generate + getGuide） |
| `entry/src/main/ets/services/Da3WsService.ets` | 云端 WebSocket 连续引导 |
| `entry/src/main/ets/services/NgaTypes.ets` | NGA 结果接口定义 |
| `entry/src/main/ets/config/CloudConfig.ets` | 后端切换（da3 / vggt-omega）及配置 |
| `entry/src/main/cpp/src/object/ring_hunt_ar_application.cpp` | AR 信标状态机（6DoF 对齐、帧捕获） |
| `entry/src/main/cpp/src/nga/nga_core.cpp` | NGA 算法（SIFT + RANSAC 单应矩阵 + 光流兜底） |
| `entry/src/main/cpp/src/nga/nga_napi.cpp` | NGA NAPI 桥接层（4 个导出函数） |
| `entry/src/main/cpp/src/module.cpp` | 所有 NAPI 函数注册入口 |
| `entry/src/main/cpp/CMakeLists.txt` | 构建配置（含 OpenCV 静态库链接） |
| `entry/src/main/cpp/types/libentry/index.d.ts` | NAPI 导出函数的 TypeScript 类型声明 |

---

## NGA 模块说明

来源：`/mnt/d/code/camera_agent/NGA_Module`，已集成至项目。

**算法**：SIFT 特征匹配 + 自定义 RANSAC DLT 单应矩阵求解（不依赖 `calib3d`），Farneback 光流作兜底。

**依赖**：OpenCV 4.13.0（`opencv-mobile` 精简版），静态链接，仅 arm64-v8a：
- `thirdparty/opencv_libs/`：`libopencv_core/imgproc/features2d/video.a`
- `thirdparty/opencv_libs/3rdparty/`：`libkleidicv/hal/thread.a`（Arm KleidiCV 加速）

**NAPI 接口**：
```typescript
ngaInitFramework(width, height): boolean   // 初始化引擎
ngaSetReference(rgba, w, h): boolean       // 设定参考帧（targetJpeg 解码后）
ngaProcessFrame(rgba, w, h): string        // 处理当前帧，返回 JSON（NgaResult）
ngaDestroyFramework(): void                // 释放资源
```

**对齐判定阈值**（C++ 默认值）：
- dX/dY ≤ 12px，roll ≤ 0.6°，scale 偏差 ≤ 0.03

---

## 后端切换

在页面右上角小标签点击切换，或长按设置图标进入 DevSelector：

| 后端 | 协议 | 机位计算方式 |
|------|------|-------------|
| `da3` | HTTP 轮询 | 单次 getGuide → guide_string |
| `vggt-omega` | WebSocket | 连续推帧，服务端滑动窗口推理 |

`getUseBeaconWorld()` 为 true 时，vggt-omega 后端直接使用 `beacon_world`（绝对世界坐标）放置信标，绕过 camera-relative 坐标转换。

---

## 构建说明

- 工具链：DevEco Studio 5.0+，HarmonyOS SDK 6.0.0(20)+
- ABI：arm64-v8a only
- C++ 标准：C++17
- 构建命令：DevEco Studio 内 Build → Build Hap(s)
