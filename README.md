# HarmonyOS NEXT AR Demo

基于华为 AR Engine 的 AR 交互演示。在官方 `arengine-codelab-ar-sample` 基础上**增量交付 3 个新场景**(零破坏官方 5 个原有场景),展示接口式放置、对齐挑战、3D 寻找三种交互模式。

## 设备要求
- 测试设备:HUAWEI Pura 90 Pro / HarmonyOS 6.1.0
- 最低要求:支持 AR Engine 的华为机型 + HarmonyOS 6.0+(API 20),需安装"华为 AR Engine"服务

## 3 个 demo 场景

### ARObject — 接口式世界坐标放置
上层通过 NAPI 在世界坐标放置 3D logo(`placeObjectAtWorld` / `placeObjectInFrontOfCamera` / `removeObject` / `clearAllObjects`)。物体用 AR anchor 跟踪,实测 40 秒漂移 ~1mm;放置物每帧 billboard 朝向相机。

### ARArrowAlign — 箭头朝向对齐挑战
随机生成目标朝向(yaw ±90° + pitch ±45°),玩家转手机让镜头朝向对齐目标。角度差 < 2° 触发绿色十字 + 80ms 震动;采用 2°/4° 滞后阈值防抖。

### ARRingHunt — 圆环 3D 精瞄寻找
随机放置发光圆环,双维度**独立**反馈:圆环颜色=距离、中央箭头颜色=角度。距离进入 15cm 自动启用屏幕中央 2D 双圈精瞄 HUD(对准圈按 yaw/pitch 偏移)。距离 < 15cm 且角度 < 5° 持续 0.5 秒触发 FOUND + 计时显示。

## 快速运行
1. 用 DevEco Studio 5.0.3+ 打开项目根目录
2. File → Project Structure → Signing Configs → 勾选 Automatically generate signature(自动签名,需登录已实名华为开发者账号)
3. 连接华为真机(设置 → 系统 → 开发者选项 → 开启 USB 调试)
4. 在 AppGallery 预装"华为 AR Engine"服务(若设备无内置)
5. 点 DevEco 工具栏绿色 Run 按钮即可装机运行

> 注:`build-profile.json5` 已脱敏为占位版,自动签名会回填真实签名信息。

## 命令行构建(可选,Windows)
需要 3 个环境配置:
- 在**项目根目录**执行 hvigorw(不在根目录会找不到脚本/配置)
- 设置 `DEVECO_SDK_HOME` 指向 DevEco SDK 路径(如 `...\DevEco Studio\sdk`)
- 把 DevEco 自带的 `jbr\bin` 加进 PATH(签名步骤需其内置 Java)

```
hvigorw.bat assembleHap --mode debug
hdc install -r entry\build\default\outputs\default\entry-default-signed.hap
hdc shell aa start -a EntryAbility -b com.huawei.ARSample
```

## 项目结构
- `entry/src/main/ets/pages/` — ArkTS UI(官方 5 场景 + 新增 ARObject / ARArrowAlign / ARRingHunt)
- `entry/src/main/cpp/src/object/` — ARObject 与 ARRingHunt 的 native + 共享数学(`object_math.h`)/几何(arrow、ring)
- `entry/src/main/cpp/src/arrowalign/` — ARArrowAlign 场景 native
- `entry/src/main/cpp/test/` — 纯数学单元测试(交叉编译 aarch64 推真机运行)
- `reports/` — 8 个 Stage 的设计与验收报告(完整开发流程档案)
- `docs/` — 架构与决策文档

## 已知限制
- 虚拟物体无遮挡(始终画在最上层)
- 3D 模型为程序化生成(圆柱/圆锥/圆环 + AR_logo obj),不支持 glTF 加载
- 每场景物体上限 50 个
- AR 跟踪失稳时 `placeXxx` 返回 -1,UI 端 Toast 提示重试

## 开发流程
通过 Claude Code 端到端协作开发,8 个 Stage 递进交付。每 Stage 含:单元测试(累计 135 项全过)+ 编译验证 + 真机 Gate 验收 + 开发报告。完整设计决策与迭代记录见 `reports/`。项目方法论与提示词模板见根目录 `cc-harmony-ar-playbook.md`。
