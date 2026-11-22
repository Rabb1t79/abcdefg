# AutoGacha（自动扭蛋）0.1.0

独立的 Anomaly 扭蛋插件，插件 ID：`b1ank.autogacha`。

## 功能

- 打开扭蛋墙（`HTUI_GashaponMain`）后列出墙上每台机器的卡池，并显示卡池显示名。
- 在插件窗口里自选一台机器，选择单抽或十连，点“抽取一次”。
- 插件用游戏自己的入口完成这一轮：先按点击打开选中的机器，再调用该机器的抽取按钮。
- 通过 `ClientGashaponRPC` 判定服务器结果（成功 / 失败），失败不自动重试。

## 原理

与 `plugins/AutoClaw` 同一套结构，但**不自己构造 RPC**：

| 环节 | 做法 |
| --- | --- |
| 世界与玩家 | 复用 Profile 里已验证的 `Objects` / `World` 签名 + 反射字段 |
| 扭蛋墙 | `HTHUD.ActivedWidgetMap` → `BP_GetUIByClass` 取 `HTUI_GashaponMain` |
| 机器列表 | 读墙控件自己的 `ActorArray`，每台 `HTGashaponActor` 的 `PoolID` |
| 名称 | `HTGameData.GashaponPoolDataTable` 行 → `PoolName` FText |
| 打开机器 | `HTUI_GashaponMain::OnActorClickedDelegate(AActor*)`，等同点击 |
| 抽取 | `HTUI_GashaponMachine::OnStartRaffle(bool bIsSingle)`，等同按按钮 |
| 结果 | 订阅 `ProcessEvent`，只看 `HTPlayerState.ClientGashaponRPC` 的 `MsgType`（0 成功、3 失败） |

回合状态机（`draw.hpp`）保证：一次点击只抽一次；玩家自己在游戏里先抽了，插件让出本回合；
面板出现的位置必须由墙的选择或本回合自己的点击证明，才会按下抽取按钮；超时即失败，不复活。

## 使用

1. 将完整 `AutoGacha` 文件夹放入 `Anomaly/plugins/`，启用“自动扭蛋”。
2. 进游戏后勾选“启用扭蛋机识别”。
3. 走到扭蛋墙前打开它，等插件列出机器。
4. 选一台机器，选择单抽/十连，点“抽取一次”。

抽取消耗游戏内货币，插件不做货币检查以外的任何事；结果与奖励以游戏结算界面为准。

## 兼容性说明

- 游戏类名、RPC 名与 `MsgType` 取自当前 NTE 构建的 Dumper-7 转储（`HTUI_GashaponMain`、
  `HTUI_GashaponMachine`、`HTGashaponActor`、`HTPlayerState.ClientGashaponRPC/ServerGashaponRPC`）。
- 偏移与签名不写在代码里，全部来自 `profile/compatibility.profile.json`；缺项、失配或反射类型不符
  时插件停用并提示 `status.layout`，不会回退到默认值。
- 前提假设：两个扭蛋界面都注册在 HUD 的 `ActivedWidgetMap` 中（与娃娃机界面同一套 UI 管理）。
  若某个版本改为挂在墙 Actor 自己的控件树下，`status.layout` 会提示，需要改为从墙 Actor 取控件。
