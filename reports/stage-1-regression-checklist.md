# Stage 1 — 回归 + 新场景核对清单

> 日期:2026-05-25 · 构建:BUILD SUCCESSFUL · 已装机并拉起到 Selector 主菜单
> 设备:HUAWEI Pura 90 Pro / MLN-AL00 / HarmonyOS 6.1.0 / SN `7LK0126123000094`
> **由你手动操作**(Claude 不 uinput)。每步看到预期画面记 ✅,异常记 ❌ 并简述现象。

已自动确认:
- ✅ 编译/签名/装机/启动通过(`BUILD SUCCESSFUL in 14s`,`install bundle successfully`,`start ability successfully`)
- ✅ 主菜单截图显示 6 个按钮,新增 `ARObject` 在最底部(`screenshots/stage1_menu.jpeg`)

---

## 7 步手动回归

| # | 操作 | 预期画面 | 结果 |
|---|---|---|---|
| 1 | 进入 Selector 主菜单 | 6 个按钮:ARWorld / ARMesh / ARDepth / ARImage / ARSemanticDense / ARObject | ⬜ |
| 2 | 点 ARWorld → 观察 → 返回 | 相机实时画面 + 检测到平面时叠加白/青色网格;点屏可放 logo(原功能) | ⬜ |
| 3 | 点 ARMesh → 观察 → 返回 | 相机画面 + 环境网格重建线框;点屏可放 logo | ⬜ |
| 4 | 点 ARDepth → 观察 → 返回 | 相机画面 + 深度可视化 | ⬜ |
| 5 | 点 ARImage → 观察 → 返回 | 相机画面(图像追踪场景) | ⬜ |
| 6 | 点 ARSemanticDense → 观察 → 返回 | 相机画面 + 语义分割叠加 | ⬜ |
| 7 | 点 **ARObject(新)** → 观察 → 返回 | **相机实时画面,画面里不出现任何物体**(本阶段还没接放置逻辑) | ⬜ |

> 返回方式:各场景已 `hideBackButton`,用系统返回手势(屏幕左/右缘内滑)回到 Selector。

## 判定标准
- 第 2–6 步:画面表现与你之前 baseline 一致(无新增黑屏/白屏/闪退/卡死)。
- 第 7 步:**只要能看到相机画面、不崩、能正常返回**即通过(不放物体是预期)。
- 全程不得新增 fatal/error 级 hilog(这部分**我来抓**:你做完 7 步后告诉我,我 `hdc shell hilog` 过滤 `ARSample`/`AREngine`/`fatal`/`error` 核验)。

## 你的回复格式(示例)
```
1 ✅  2 ✅  3 ✅  4 ✅  5 ✅  6 ✅  7 ✅
（或:7 ❌ 进 ARObject 后黑屏；其余 ✅）
```
我收到后:拉 hilog 核验错误级日志 → 写 `reports/stage-1-report.md` → 停下等 `next` 进 Stage 2。
