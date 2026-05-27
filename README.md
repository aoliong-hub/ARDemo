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

### ARRingHunt — Wayfinder 信标 + 6DoF 对齐挑战
信标视觉:地面光圈 + 光柱(volumetric noise 雾气 + bloom 辉光)+ 面向相机的旋转奖章 + 屏幕外水滴引导;整体颜色按距离 warm red → soft mint 渐变。走近 30cm(300ms 防抖)进入 **ALIGNING**:光柱/奖章隐藏,出现 6DoF **流彩框**(粉紫蓝色相旋转)+ 框中心凸出的**炫彩 3D 旋转箭头** + 屏幕中央十字/圆点 HUD。转动手机让朝向对齐(yaw/pitch,5°/7° 滞后防抖):箭头 0.3s 平滑变绿并停转;持续对齐 1 秒 → **LOCKED**「对齐成功」+ 80ms 震动。

**6DoF 目标朝向外部传参**:除内部随机的 `placeRing` 外,新增 `placeRingWithOrientation`,供外部模块(物体识别 SDK / 远程任务推送 / AR 教学)指定信标对齐目标:

```ts
// 在相机前方 1m 地面放置信标;目标朝向 = 放置时刻相机视线方向 + 给定偏移
placeRingWithOrientation(id: string, yawDeg: number, pitchDeg: number, rollDeg: number): number
```
- **坐标系**:相对**放置时刻的相机视线**。`(0,0,0)` = 框正面朝向你视线的远方(你看到框背面,箭头朝远方延伸)。
- `yawDeg` 正 = 向右偏(clamp ±180),`pitchDeg` 正 = 向上仰(clamp ±90),`rollDeg` 正 = 顺时针(clamp ±180);超范围静默截断。
- 返回 `objectId`(≥0 成功 / -1 失败:未就绪或参数非法)。原 `placeRing`(随机朝向)接口保留,完全兼容。
- 查询当前目标:`getRingState(id)` 返回 `targetYawDeg / targetPitchDeg / targetRollDeg`(相对值,度)+ `huntPhase`(0=APPROACHING / 1=ALIGNING / 2=LOCKED)/ `isAligned` / `isLocked` / `distance`。

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

**签名隔离**:`build-profile.json5` 入库为**占位版**(无密码)。命令行构建需真实签名:
1. 一次性:把 DevEco 自动生成的真实 `build-profile.json5` 另存为 `build-profile.local.json5`(已 .gitignore,永不入库)
2. 构建前 `scripts\sign-real.ps1`(local→build-profile.json5),提交前 `scripts\sign-placeholder.ps1`(占位→build-profile.json5)

```
powershell -File scripts\sign-real.ps1                 # 构建前:还原真实签名
hvigorw.bat assembleHap --mode debug
hdc install -r entry\build\default\outputs\default\entry-default-signed.hap
hdc shell aa start -a EntryAbility -b com.huawei.ARSample
powershell -File scripts\sign-placeholder.ps1          # 提交前:还原占位
```

> DevEco Studio GUI 用户无需上述脚本:勾选自动签名会在本地 `build-profile.json5` 回填真实签名(该本地修改不要提交)。

## 项目结构
- `entry/src/main/ets/pages/` — ArkTS UI(官方 5 场景 + 新增 ARObject / ARArrowAlign / ARRingHunt)
- `entry/src/main/cpp/src/object/` — ARObject 与 ARRingHunt(Wayfinder)的 native + 共享数学(`object_math.h`)/几何(arrow、wayfinder)
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
