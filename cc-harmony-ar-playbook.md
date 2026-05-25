# Claude Code 一次性开发鸿蒙 AR Demo:主控指令手册

> 目标:把 HarmonyOS NEXT + AR Engine 的"平面检测 + 点击放置 3D 立方体 + 旋转/缩放/删除"Demo 交给 Claude Code 端到端开发,要求每步带单元测试 + 真机效果验证。

---

## 0. 你要先理解的现实(诚实预期)

| 阶段 | 谁来做 | 单次耗时 |
|---|---|---|
| 项目克隆、签名、连接首台真机 | **你**(DevEco Studio GUI 一次性操作) | ~15 分钟 |
| 写代码 / 改代码 / 单元测试 | Claude Code | ≈ 80% 工作量 |
| 编译 / 装机 / 抓日志 / 抓截图 / 判定通过 | Claude Code(全自动) | — |
| 相机权限弹窗确认 | **你**(真手指点) | 5 秒 × 每次重装 |
| Stage 5/6/7 多指手势的最终验证 | **你**(手持手机做双指旋转/捏合) | 各 1 分钟 |
| 视角跟踪稳定性判定(走两步看物体是否飘) | **你**(肉眼) | 各 1 分钟 |

**总而言之**:一次性 ≠ 撒手不管。预期是"Claude Code 跑大部分循环,你做 4–5 次 1 分钟的真机交互 + 偶尔回答它的问题"。

---

## 1. 人类前置步骤(只做一次,~15 分钟)

执行下面这些之后,后续才能交给 Claude Code。

```bash
# 1.1 注册华为开发者账号并完成实名认证
#     https://developer.huawei.com  (需身份证+银行卡四要素)

# 1.2 安装 DevEco Studio 5.0.3.900+  (Windows / macOS;Linux 无 GUI 版)
#     https://developer.huawei.com/consumer/cn/deveco-studio/
#     安装时勾选 OpenHarmony SDK API 12+、Node、ohpm、hdc

# 1.3 把 hdc、ohpm、node 加进 PATH(macOS .zshrc 示例)
echo 'export HARMONYOS_HOME=$HOME/Library/Huawei/Sdk' >> ~/.zshrc
echo 'export OHPM_HOME=$HOME/Library/Huawei/ohpm'     >> ~/.zshrc
echo 'export PATH=$HARMONYOS_HOME/hmscore/X.X.X/toolchains:$OHPM_HOME/bin:$PATH' >> ~/.zshrc
echo 'export HDC_SERVER_PORT=7035' >> ~/.zshrc
source ~/.zshrc
hdc version && ohpm -v && node -v

# 1.4 克隆官方 AR Sample 作为起点
cd ~/Projects
git clone https://gitee.com/harmonyos_codelabs/arengine-codelab-ar-sample.git ARDemo
cd ARDemo
git checkout -b dev    # 不要直接动 master

# 1.5 用 DevEco Studio 打开 ARDemo,做一次自动签名
#     File → Project Structure → Project → SigningConfigs → 勾 Automatically generate signature
#     (登录已实名的开发者账号即可,debug profile 自动生成,可同时签 100 台设备)

# 1.6 连接真机(Mate 60+/Pura 70+/Nova 14+/MatePad Pro 等支持 AR Engine 的机型)
#     手机端:设置 → 关于手机 → 版本号(连点 7 次)→ 系统 → 开发者选项 → 开启 USB 调试
hdc list targets                       # 应看到设备 SN
hdc shell bm get --udid                # 记下 UDID,加进 AppGallery Connect 的调试设备列表

# 1.7 在手机上预装"华为AR Engine"服务端
#     打开 AppGallery → 搜索"华为AR Engine" → 安装。Demo 启动会调用它,没装会闪退。

# 1.8 准备 Claude Code(假设已装)
cd ~/Projects/ARDemo
claude   # 启动 Claude Code

# 1.9 安装 ArkTS 文档 MCP(让 Claude 能查鸿蒙 API)
claude mcp add arkts-docs -- npx -y @llt22/harmonyos-dev-helper-mcp
```

完成后做一次"baseline 跑通"——不要直接进 Claude Code,先确认手工能跑:

```bash
hvigorw clean
hvigorw assembleHap --mode debug
hdc install -r entry/build/default/outputs/default/entry-default-signed.hap
hdc shell aa start -a EntryAbility -b com.huawei.arengine.demo   # 包名以工程为准
```

如果手机上出现相机画面 + 能识别平面 + 能点击放置示例物体——baseline OK,前置完成。 **接下来一切交给 Claude Code**。

---

## 2. 主控指令:复制粘贴到 Claude Code

把下面整段直接粘贴给 Claude Code(它会进入 plan mode,自己读 playbook 之后开始干活):

```
我要在当前 ARDemo 项目(基于鸿蒙官方 arengine-codelab-ar-sample)上做一次端到端
开发,目标是一个内部演示用 AR Demo:平面检测 + 点击放置立方体 + 双指旋转/缩放 +
长按删除 + 视角跟踪。请严格按以下规范工作:

【1】先读这三份文档,读完前不要写任何代码:
   - CLAUDE.md                 (硬性约束)
   - docs/playbook.md          (8 个 Stage 的详细任务卡)
   - docs/arengine-api-cheatsheet.md  (AR Engine NDK 函数签名,已从官方头文件抓取)

【2】进入 plan mode (Shift+Tab),把每个 Stage 拆成 3-7 个 todo,写到 tasks/todo.md。
   写完停下让我 review,我说 GO 再开始 Stage 1。

【3】每个 Stage 完成的判定标准(必须全部满足):
   a. 该 Stage 列出的单元测试已写、`hvigorw test --mode debug` 全部通过
   b. `hvigorw assembleHap --mode debug` 编译无 error
   c. `./scripts/deploy_and_verify.sh <N>` 自动跑完,生成 screenshots/stage-N.png 和
      logs/stage-N.log
   d. 你自己读截图(用 view 工具)和日志,对照 playbook.md 中 Stage N 的"自动 Gate"
      逐条判定,写报告到 reports/stage-N-report.md
   e. 如果 Stage 有"人工 Gate"(playbook 里标了【需人工】),停下提示我,等我回复
      "passed"/"failed: <observation>" 之后再继续

【4】单元测试硬性要求:
   - C++ 改动 → entry/src/cpp/test/ 加 gtest 用例,覆盖纯逻辑(矩阵、Anchor 列表、
     状态机)。不要测 AR Engine 本身,改用 mock。
   - ArkTS 改动 → entry/src/ohosTest/ets/test/ 加 hypium 用例,覆盖权限状态机、
     手势事件分发、NAPI 调用参数。
   - 测试不通过禁止进入设备验证。

【5】失败处理协议:
   - 同一编译错误连续 3 次未解决 → 停止,写 issues/blocker-N.md 描述上下文与你的
     假设,然后 @我
   - hilog 出现 "AR Engine service not found / version mismatch" → 停止,提示我去
     AppGallery 装/升 华为AR Engine 服务,等我回复 "installed"
   - 截屏发现全黑/全白屏 → 这是 GL 上下文问题,不要靠瞎改,先抓
     `hdc shell hilog | grep -iE 'GL|EGL|render'` 定位
   - hvigor 报 ohpm 包下载失败 → 切镜像:
     `ohpm config set registry https://repo.huaweicloud.com/harmonyos/ohpm/`

【6】不要做的事:
   - 不要升级官方 sample 锁定的 SDK 版本(API 12)
   - 不要尝试绕过签名
   - 不要重写官方 sample 的 background_renderer.cpp / plane_renderer.cpp,只在它们
     基础上加功能
   - 不要在 C++ 里调用 ArkGraphics 3D(EGL 冲突)
   - 不要 commit screenshots/ 和 logs/(.gitignore 已配)

【7】每个 Stage 报告完成,等我说 "next" 才进入下一个 Stage。
   绝对不要一次连跑两个 Stage,中间必须等我 review 报告。

现在开始第 1 步:读三份文档,然后写 tasks/todo.md。读完停下。
```

---

## 3. CLAUDE.md 全文(保存到项目根目录)

```markdown
# ARDemo — Claude Code 工作约束

## TIER 1 硬规则

### 平台事实
- 目标:HarmonyOS NEXT,API 12+,arm64-v8a
- 基础工程:gitee 华为官方 arengine-codelab-ar-sample
- 包管理:ohpm(华为云镜像);构建:hvigor;真机:hdc;日志:hilog
- 不能引入 npm 包(@types/* 除外)

### ArkTS 严格子集 — 严禁:
- `any` / 隐式类型
- 动态属性访问(`obj[key]`,key 非字面量)
- 结构化类型(必须 `class X implements Y`)
- `Object.defineProperty` / `Reflect.*`
- 模块顶层副作用(import 后立即执行)
- 运行时 require

### C++ 约束
- C++17,无异常(`-fno-exceptions`),无 RTTI
- AR Engine NDK 函数签名以 ar/ar_engine_core.h 为准,不允许猜函数名
- 任何 GL 调用后必须 `glGetError()` 检查并 hilog
- 所有从 NAPI 进 native 的数据必须做 nullptr 检查

### 分层契约
- entry/src/main/ets/**            仅 UI、权限、手势 → ArkTS
- entry/src/main/cpp/**            AR + GL ES + NAPI → C++
- 二者通信仅通过 cpp/types/libentry/index.d.ts 声明的接口

## TIER 2 工作流

### Plan 模式触发(强制 plan 后再写代码)
1. 新增任何 C++ 文件
2. 修改 module.json5 / oh-package.json5 / CMakeLists.txt
3. 改任何会影响 EGL/GL 上下文初始化的代码
4. 修改 NAPI 接口签名

### 每次代码变更后立即跑
hvigorw test --mode debug           # 单元测试
hvigorw assembleHap --mode debug    # 构建
有 error 必须先修。

### Stage 验证循环
完成一个 Stage 必须:
1. ./scripts/deploy_and_verify.sh <N>
2. 用 view 工具读 screenshots/stage-N.png,描述看到什么
3. 读 logs/stage-N.log,提取关键 hilog 行
4. 对照 docs/playbook.md Stage N 的 Gate 逐条判定
5. 写 reports/stage-N-report.md
6. 等我 next

## TIER 3 项目知识

### 已知坑(改 lessons.md 累积)
- OES 纹理需要 `#extension GL_OES_EGL_image_external_essl3 : require`
- 屏幕旋转必须重调 `HMS_AREngine_ARSession_SetDisplayGeometry`
- Anchor TrackingState != TRACKING 时不要渲染对应物体
- 个人开发者签名:每个新设备 UDID 要先加进 AppGallery Connect

## 三大原则
1. 简单优先:能复用 sample 既有代码就不重写
2. 找根因:渲染不出来先看 hilog 不要瞎调矩阵
3. 范围最小:不顺手改不在本 Stage 的代码
```

---

## 4. 八个 Stage 的任务卡(`docs/playbook.md`)

每张卡片格式:**目标 → 改动文件 → 单元测试 → 自动 Gate → 人工 Gate**

### Stage 0:环境校验 + 基线

**目标**:确认 baseline 工程可编译可装机,Claude Code 工具链就绪。

**改动**:无代码改动,只新增 `scripts/deploy_and_verify.sh`、`scripts/run_unit_tests.sh`、`.gitignore`(过滤 screenshots/ logs/ reports/)。

**单元测试**:无(只是环境)。

**自动 Gate**:
- `hdc list targets` 返回至少一台设备
- `hvigorw assembleHap --mode debug` 退出码 0
- `./scripts/deploy_and_verify.sh 0` 执行完成,screenshots/stage-0.png 存在,且 Claude 读图判定"非全黑、非全白、能看到 UI 元素"
- `logs/stage-0.log` 出现 "EntryAbility onCreate"

**人工 Gate**:无。

---

### Stage 1:相机权限 + 相机背景渲染

**目标**:启动 App,弹相机权限,授权后 XComponent 上显示实时相机画面。

**改动**:
- `entry/src/main/ets/pages/Index.ets` — 权限请求 + XComponent 挂载
- `entry/src/main/cpp/plugin_manager.cpp` — XComponent 生命周期回调里调 `HMS_AREngine_ARSession_Create` + `SetCameraTextureName`

**单元测试**:
- 【ArkTS / hypium】权限状态机测试:模拟 GRANTED / DENIED / NOT_DETERMINED 三种返回,验证 UI state 正确转移
- 【C++ / gtest】`PluginManager` 在 OnSurfaceCreated 被调用前任何 GL 操作必须直接 return

**自动 Gate**:
- 单元测试全过
- hilog 出现 "ARSession created, status=0" 和 "CameraTextureName set"
- 截屏:Claude 用 view 工具读 screenshots/stage-1.png,判定"画面非黑、能看出是相机实时拍摄(有纹理/颜色变化)"

**人工 Gate**:【需人工】首次运行弹权限框,用户必须点击"允许"。Claude 提示你:"现在去手机上点允许相机,然后回复 ok"。

---

### Stage 2:平面检测可视化

**目标**:把手机对准有纹理的水平面(地板/桌面),3–5 秒内屏幕上叠加显示半透明白色网格表示检测到的平面。

**改动**:
- `entry/src/main/cpp/ar/ar_application.cpp` — 在 `onDrawFrame` 中遍历 `ARTrackable PLANE` 调用 plane_renderer
- 复用 sample 自带的 `plane_renderer.{h,cpp}`,只需要确认 vertex shader 生成正确

**单元测试**:
- 【C++ / gtest】`PlaneVertexGenerator` 测试:给定一个 4 顶点凸多边形,生成的 triangle fan 顶点数 = 6 个三角形;边界点不超出原始顶点

**自动 Gate**:
- hilog 在前 10 秒内至少出现一次 "PlanesDetected count >= 1"
- screenshots/stage-2.png 中 Claude 能描述出"有白色网格状叠加层"

**人工 Gate**:【需人工】Claude 提示:"请把手机对准有纹理的桌面或地板,慢慢移动 5 秒,然后回复 ok"。然后 Claude 再触发截图。

---

### Stage 3:点击放置立方体

**目标**:在已检测到的平面上点击,该位置出现一个 10cm × 10cm × 10cm 的立方体,持续渲染。

**改动**:
- `Index.ets` — XComponent `.onTouch` 把 (x, y) 通过 NAPI 推到 native
- `napi_init.cpp` — 注册 `onTap(x, y)` 接口
- `ar_application.cpp` — 实现 `onTap` → `HitTest` → `AcquireNewAnchor` → push 到 `anchors_`
- `object_renderer.{h,cpp}` — 立方体顶点 + 简单 Phong shader,每帧用 anchor pose 渲染

**单元测试**:
- 【C++ / gtest】
  - `AnchorList::Add` / `Remove` / `Clear` 行为
  - `PoseToModelMatrix`:给定 `[qx,qy,qz,qw,tx,ty,tz]` 输出 4x4 矩阵,与 GLM 参考实现对比误差 < 1e-5
- 【ArkTS / hypium】XComponent onTouch 事件正确序列化为 NAPI 调用参数

**自动 Gate**:
- 单元测试过
- Claude 用 `hdc shell uinput -T -c <x> <y>` 在 screenshots/stage-2.png 检出的平面中心位置注入一次点击
- 下一帧 hilog 出现 "AnchorAdded total=1"
- screenshots/stage-3-after-tap.png 中 Claude 判定"屏幕上多了一个有阴影/有颜色的方块,且位于平面附近"

**人工 Gate**:无(自动化点击通常能工作;若 uinput 失败,降级为请用户点一下)。

---

### Stage 4:视角跟踪验证

**目标**:放好立方体后,移动手机视角,立方体在现实空间里位置不变("不飘")。

**改动**:无新代码——这一步是验证 Stage 3 的 anchor 跟踪是否真的稳定。

**单元测试**:无新测试。

**自动 Gate**:
- 抓 5 秒的 hilog,期间每帧的 Anchor TrackingState 应保持 `ARENGINE_TRACKING_STATE_TRACKING`,中断比例 < 5%

**人工 Gate**:【需人工】Claude 提示:"放好立方体后,请保持立方体在画面里,缓慢向左走 1 步、向右走 1 步、向前蹲下 1 次,然后回复:'stable' / 'drift_small' / 'drift_severe' / 'lost'。"

如果 drift_severe 或 lost:Claude 进入诊断模式,检查光照、特征点密度、设备热降频。

---

### Stage 5:双指旋转

**目标**:在已放置的立方体上双指做旋转手势,立方体绕 Y 轴(世界向上)旋转。

**改动**:
- `Index.ets` — 加 `RotationGesture` 组合到 `GestureGroup`,把 `angle: number` 推到 native
- `ar_application.cpp` — 每个 Anchor 维护一个 `userYawRad`,渲染时模型矩阵 `= anchorMat * RotY(userYawRad)`
- 实现选中态:点击立方体周边视为"选中",后续手势作用于选中物;同时只允许一个选中

**单元测试**:
- 【C++ / gtest】`UserTransform::ApplyYawDelta`:累积旋转;角度归一化到 [-π, π]
- 【C++ / gtest】`SelectionPicker::PickNearest`:屏幕坐标 → 最近 Anchor;无 Anchor 时返回 nullptr

**自动 Gate**:
- 单元测试过
- 编译过、装机过

**人工 Gate**:【需人工】Claude 提示:"请双指放在立方体上做旋转手势,然后回复观察结果:
- 'ok_rotates_around_y'(绕竖直轴转,正确)
- 'rotates_wrong_axis'(绕了别的轴,Claude 会修正旋转轴)
- 'no_response'(没反应,Claude 抓 hilog 看 GestureEvent 有没有进来)
- 'jumps_to_origin'(转了之后跳到原点,矩阵乘法顺序错了)"

---

### Stage 6:双指捏合缩放

**目标**:双指捏合缩小、张开放大立方体,缩放范围 [0.2x, 3.0x]。

**改动**:
- `Index.ets` — 加 `PinchGesture`,把 `scale: number` 推到 native
- `ar_application.cpp` — `userScale` 字段,clamp 到 [0.2, 3.0]

**单元测试**:
- 【C++ / gtest】`UserTransform::ApplyScaleDelta`:`finalScale = clamp(prevScale * factor, 0.2, 3.0)`,边界两端值

**自动 Gate**:同上。

**人工 Gate**:【需人工】Claude 提示:"请双指捏合后张开,回复 'ok' / 'reversed'(方向反了)/ 'no_clamp'(可以缩到 0 或变巨大)/ 'no_response'。"

---

### Stage 7:长按删除

**目标**:对已放置物体长按 1 秒,该物体消失;空地长按无反应。

**改动**:
- `Index.ets` — 加 `LongPressGesture`(duration 1000)
- `ar_application.cpp` — long press → `SelectionPicker::PickNearest` → `HMS_AREngine_ARAnchor_Detach` → 从列表移除

**单元测试**:
- 【C++ / gtest】`AnchorList::Remove` 调用 `Detach` 一次且仅一次;无 anchor 时不调用

**自动 Gate**:
- 单元测试过
- Claude 用 `hdc shell uinput -T -m <x> <y> <x> <y> 1100`(原地按住 1.1 秒)
- hilog 出现 "AnchorRemoved" 一次
- screenshots/stage-7-after-longpress.png 比 stage-7-before-longpress.png 少一个立方体

**人工 Gate**:无(自动化通常 OK)。

---

### Stage 8:UI 状态提示 + 完工验收

**目标**:屏幕顶部加一个状态条:"正在初始化" / "请扫描平面" / "已检测到 N 个平面,点击放置" / "AR 跟踪丢失,请慢慢移动手机"。

**改动**:
- `Index.ets` — 一个 `@State` 字段 `arStatus: string`
- `ar_application.cpp` — 通过 NAPI 回调把状态变化推上去

**单元测试**:
- 【ArkTS / hypium】`ARStatusReducer`:输入(planeCount, trackingState)→ 输出状态字符串;覆盖所有分支

**自动 Gate**:整套从启动到放置 3 个物体的完整流程在 Claude 自动驱动下走通(uinput 模拟点击 3 次);所有 hilog state 转换正确。

**人工 Gate**:【需人工】最终验收——你拿手机走一圈,放 5 个物体,转一圈,删掉 2 个,绕场地走 10 步,回复"ACCEPTED"或具体问题。

---

## 5. 测试框架模板代码

### 5.1 ArkTS / hypium 配置

`entry/src/ohosTest/module.json5` 已存在,无需改动。新增测试文件:

```typescript
// entry/src/ohosTest/ets/test/PermissionStateMachine.test.ets
import { describe, beforeEach, it, expect } from '@ohos/hypium';
import { PermissionState, reducePermissionEvent } from '../../../../main/ets/state/PermissionState';

export default function permissionStateMachineTest() {
  describe('PermissionStateMachine', () => {
    it('NotDetermined_to_Granted', 0, () => {
      const next = reducePermissionEvent(PermissionState.NotDetermined,
        { type: 'UserResponse', granted: true });
      expect(next).assertEqual(PermissionState.Granted);
    });

    it('NotDetermined_to_Denied', 0, () => {
      const next = reducePermissionEvent(PermissionState.NotDetermined,
        { type: 'UserResponse', granted: false });
      expect(next).assertEqual(PermissionState.Denied);
    });
  });
}
```

测试入口:`entry/src/ohosTest/ets/test/List.test.ets` 把所有 test 文件 import 进来。

运行:`hvigorw test --mode debug`(会在连接的真机上跑)。

### 5.2 C++ / Google Test 配置

`entry/src/main/cpp/CMakeLists.txt` 末尾追加(只在 test build 时启用):

```cmake
# ---- Tests ----
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
```

新建 `entry/src/main/cpp/test/CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://gitee.com/mirrors/googletest.git
  GIT_TAG release-1.14.0)
FetchContent_MakeAvailable(googletest)

add_executable(ar_unit_tests
  pose_to_matrix_test.cpp
  anchor_list_test.cpp
  user_transform_test.cpp
)
target_include_directories(ar_unit_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ar_unit_tests PRIVATE gtest_main)
add_test(NAME ar_unit_tests COMMAND ar_unit_tests)
```

示例测试:

```cpp
// entry/src/main/cpp/test/pose_to_matrix_test.cpp
#include <gtest/gtest.h>
#include <cmath>
#include "../ar/math_utils.h"

TEST(PoseToMatrix, IdentityQuaternionGivesTranslationOnly) {
  float pose[7] = {0, 0, 0, 1,   1.0f, 2.0f, 3.0f};   // qx,qy,qz,qw,tx,ty,tz
  float m[16];
  PoseToModelMatrix(pose, m);
  EXPECT_NEAR(m[0], 1.0f, 1e-6);
  EXPECT_NEAR(m[5], 1.0f, 1e-6);
  EXPECT_NEAR(m[10], 1.0f, 1e-6);
  EXPECT_NEAR(m[12], 1.0f, 1e-6);
  EXPECT_NEAR(m[13], 2.0f, 1e-6);
  EXPECT_NEAR(m[14], 3.0f, 1e-6);
}

TEST(PoseToMatrix, Rotate90AroundYTransformsXToMinusZ) {
  float pose[7] = {0, std::sin(M_PI/4), 0, std::cos(M_PI/4),  0, 0, 0};
  float m[16];
  PoseToModelMatrix(pose, m);
  // 应有 m[0] ≈ 0, m[2] ≈ -1
  EXPECT_NEAR(m[0], 0.0f, 1e-5);
  EXPECT_NEAR(m[2], -1.0f, 1e-5);
}
```

跑测试:

```bash
mkdir -p build_test && cd build_test
cmake -DBUILD_TESTS=ON ../entry/src/main/cpp
make ar_unit_tests
./ar_unit_tests
```

(主机直接跑,不用上设备——只测纯逻辑。)

---

## 6. 验证脚本(保存到 `scripts/`)

### `scripts/run_unit_tests.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
echo "========== ArkTS hypium =========="
hvigorw test --mode debug 2>&1 | tee logs/unit-arkts.log

echo "========== C++ gtest =========="
mkdir -p build_test && cd build_test
cmake -DBUILD_TESTS=ON ../entry/src/main/cpp 2>&1 | tee ../logs/unit-cpp-cmake.log
make ar_unit_tests 2>&1 | tee ../logs/unit-cpp-build.log
./ar_unit_tests 2>&1 | tee ../logs/unit-cpp-run.log
```

### `scripts/deploy_and_verify.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
STAGE="${1:-X}"
APP_BUNDLE="${APP_BUNDLE:-com.huawei.arengine.demo}"
APP_ABILITY="${APP_ABILITY:-EntryAbility}"

mkdir -p screenshots logs reports

echo "[1/6] Build"
hvigorw assembleHap --mode debug 2>&1 | tee "logs/build-stage-${STAGE}.log"

echo "[2/6] Install"
HAP=$(find entry/build -name "entry-default-signed.hap" | head -1)
hdc install -r "$HAP" 2>&1 | tee "logs/install-stage-${STAGE}.log"

echo "[3/6] Clear old hilog buffer"
hdc shell hilog -r

echo "[4/6] Launch"
hdc shell aa start -a "$APP_ABILITY" -b "$APP_BUNDLE"
sleep 5

echo "[5/6] Screenshot"
hdc shell snapshot_display -f "/data/screen-${STAGE}.png" > /dev/null
hdc file recv "/data/screen-${STAGE}.png" "screenshots/stage-${STAGE}.png"

echo "[6/6] Capture hilog (last 8s)"
hdc shell "hilog | head -2000" > "logs/stage-${STAGE}.log" &
PID=$!
sleep 8
kill $PID 2>/dev/null || true

echo "Artifacts:"
echo "  screenshots/stage-${STAGE}.png"
echo "  logs/stage-${STAGE}.log"
```

赋权:`chmod +x scripts/*.sh`

---

## 7. 失败回退协议

| 现象 | Claude 的下一步 | 何时找你 |
|---|---|---|
| 编译错误同一类连续 3 次没修好 | 写 `issues/blocker-<N>.md` 描述假设链,停止 | 立刻 |
| 单元测试反复失败 | 检查测试是不是写错了(不是逻辑错),用 mock 替代外部依赖 | 试 2 轮没果再找你 |
| hilog "AR Engine service not found" | 暂停,提示去 AppGallery 装"华为AR Engine" | 立刻 |
| 截屏全黑 | grep `'GL\|EGL\|render'` hilog,看是 surface 没创建还是 OES 纹理没绑 | 自查 30 分钟,无果找你 |
| 截屏全白/纯色 | 检查 background_renderer shader 是不是用错纹理类型 | 自查 30 分钟,无果找你 |
| 人工 Gate 你回复 "drift_severe" | 进诊断:光照?设备温度?平面纹理是否足够? | 给出 3 个假设和验证步骤 |
| 单点 hit test 总返回 0 | 检查屏幕坐标系是否需要转换(竖屏/横屏)、平面 TrackingState | 自查 1 小时,无果找你 |
| `hvigorw test` 一直卡死 | 真机可能锁屏,先 `hdc shell power-shell wakeup` | — |
| 任何不可逆操作前 | 必须先问你 | 总是 |

---

## 8. 完工 Checklist

Claude 完成 Stage 8 后,跑这份清单,逐项打勾:

- [ ] 8 个 reports/stage-N-report.md 全部存在且 Status: PASSED
- [ ] `hvigorw test --mode debug` 全过(覆盖率 ≥ 60% 纯逻辑函数)
- [ ] `./scripts/run_unit_tests.sh` 退出码 0
- [ ] hap 包大小 < 30 MB
- [ ] 冷启动到相机画面出现 < 3 秒
- [ ] 平面检测平均触发时间 < 5 秒(textured 表面)
- [ ] 连续放置 10 个物体后帧率仍 ≥ 25 FPS(`hdc shell hidumper -s WindowManagerService -a -a` 抓帧率)
- [ ] 走 10 步后所有物体仍在原位(主观判定,你说 OK)
- [ ] tasks/lessons.md 至少累积 5 条经验
- [ ] `git status` 干净,没有未 commit 的临时文件
- [ ] 一份 README.md 描述"怎么跑这个 demo"(给同事用)

---

## 附录:如果想要"更一次性"的体验

如果你的目标是"演示给老板/客户看效果,不限平台"——同样这套方法论用在 **Unity 6 + AR Foundation + iOS/Android** 上,Claude Code 的全自动比例可以到 **95%**。原因:

- Unity 有官方 **MCP for Unity**(CoplayDev),Claude 能直接读 Console、操作场景层级、挂组件、改材质,无需"截屏 + hilog"间接通信
- AR Foundation 自带 **XR Simulation**,大量验证在 Editor 即可完成,不用每次都装机
- C# + Unity 的训练数据极其丰富,代码一次过的比例远高于 ArkTS+NDK

何时坚持鸿蒙路径:**演示对象明确要求"必须跑在华为机器上"**(政府、央企、面向华为生态的合作伙伴),且无法绕过。其他场景下,先用 Unity 路径出 Demo,鸿蒙等正式立项再做。
