# AutoTycoon（自动铺满全城）0.1.0

活动「铺满全城」（TYCOON'S DECK，内部系统 `BalatroLike`）的自动完成插件，插件 ID：`b1ank.tycoondeck`。

## 做什么

正常流程：点活动 → 开始游玩 → 选盲注 → 出牌 → 过盲注 → 商店 → … → 结算。
本插件把中间的重复操作交给游戏自己的请求接口，**能直接结束盲注就直接结束**（不走出牌），
不能时才退回“直接打出全部手牌”，用最短的合法序列把一局推到结算。

| 阶段 | 动作 |
| --- | --- |
| Idle | `RequestStartGame` |
| BlindSelection | 可跳则 `RequestSkipCurrentBlind`，否则 `RequestStartCurrentBlind` |
| Playing | 可结束则 `RequestFinishCurrentBlind`，否则 `RequestPlayAllHandCards`；次数用完则 `RequestManualSettlement` |
| BlindCleared | `RequestFinishCurrentBlind` |
| Shop / ShopCleared | `RequestSkipShop` |
| 待选卡包 | `RequestConfirmPackSelection`（按可选张数选前 N 个） |

## 边界

- 只读牌局状态、只调用组件自身的 `Request*` 包装函数；**不写内存、不手拼 `ServerBalatroLikeRequestRPC` 载荷**。
- 局内状态是服务端权威（`SessionId` + `Revision`），被拒绝时插件停下并显示原因，不会硬刷。
- 兼容数据（引擎布局与 `ProcessEvent.entry` 签名）在 `profile/compatibility.profile.json`；
  类名/函数名与参数形状在代码里逐项校验（`FunctionCount`/`FunctionSize`/属性名+偏移+大小+类型）。

## 使用

1. 把 `TycoonDeck` 文件夹放进 `Anomaly/plugins/`，在插件管理器里启用“自动铺满全城”。
2. 进活动页面点“开始游玩”，再勾选“启用自动完成铺满全城”。
3. 想观察牌局就取消“优先直接结束盲注”，插件会改为自动出牌推进。

## 构建

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-relwithdebinfo --target anomaly_builtin_tycoondeck --parallel
```

产物：`.build/windows-vs2022/bin/RelWithDebInfo/builtin-plugins/TycoonDeck/`。
纯逻辑（阶段判定表 + 配置解析）由 `tests.cpp` 在 `ANOMALY_BUILD_TEST_FIXTURES=ON` 时验证。
