# Chronicon 地图全开 Mod

![vibe coded](https://img.shields.io/badge/vibe--coded-%F0%9F%8E%B2-ff69b4)

清除 **Chronicon** 小地图上的迷雾：地图常驻全开，效果等同于反复使用物品
**“芬利的魔法地图”（item_699：使用 = 完全开启地图）**。

> 🧪 **本项目是 Vibe coding 产物。** 逆向分析、挂钩实现与 PowerShell 脚本都是在与 AI 对话中
> 生成并逐步调通的，人工部分只做了"进游戏能不能用"这一层验证。功能可用且有文档，
> 但对版本相关的偏移量（以及 [docs/technical-notes.md](docs/technical-notes.md) 未覆盖的部分），
> 请保持适当的怀疑态度。

* 适用版本：本机实测的 `Chronicon.exe`（59,400,704 字节，SHA256 `C0036176…`），GameMaker Studio 2 **YYC x64** 构建。
* 由 [Aurie](https://github.com/AurieFramework/Aurie) 加载，挂钩用 [MinHook](https://github.com/TsudaKageyu/minhook)。
* 所有挂钩/改写前都会校验机器码签名；游戏更新后 Mod 会记录错误并自动失效，而不是让游戏崩溃。

---

## 原理

Chronicon 是 **YYC** 编译版本，GML 脚本都编译进 `Chronicon.exe`，因此可以像普通原生函数那样挂钩。
本 Mod 使用两个互补机制：

**机制 A —— 反复调用游戏自带的“完全开启地图”脚本**

`gml_Script_minimapExplore` 就是“芬利的魔法地图”的效果：

```gml
ds_grid_clear(mapexplore, 1);   // 把“已探索”网格全部置 1
minimapRefresh();               // 重绘小地图
```

触发点（多点冗余，保证持续生效）：

| 挂钩函数 | 触发时机 |
| --- | --- |
| `gml_Script_minimapSetArea` | 区域小地图初始化 |
| `gml_Script_minimapUpdate` | 每次小地图更新 |
| `gml_Script_minimapRefresh` | 每次小地图重绘 |
| `gml_Script_world_gen_step` | 每 30 帧兜底 |

**机制 B —— 跳过绘制时的“未探索就不画”判定**

小地图绘制循环会跳过未探索的格子；Mod 在运行时把该条件跳转改成 NOP（VA `0x141290B82`）。

> 实测：只开机制 A 时地图仍会以“未探索”样式渲染，A + B 同时开启才会彻底全开。
> 纯改 exe 字节的静态补丁无效，是因为那段分支只在**区域生成**时执行一次。

更多细节（函数地址、逆向过程）见 [docs/technical-notes.md](docs/technical-notes.md)。

---

## 安装

1. 完全退出游戏。
2. 从 Releases 下载 `ChroniconMapReveal.dll`（或自行编译，见下）。
3. 运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\install.ps1 -ModDll .\ChroniconMapReveal.dll
```

脚本会下载 [Aurie](https://github.com/AurieFramework/Aurie) v2.0.2、备份并给 `Chronicon.exe` 打补丁，
然后把 Mod 复制到 `<游戏目录>\mods\aurie\`。

**卸载**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\uninstall.ps1
```

---

## 从源码编译

需要 MinGW-w64（GCC 11+，支持 C++20；实测 15.2）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
```

产物在 `build\ChroniconMapReveal.dll`。MinHook 已内置在 `third_party\minhook`，Aurie 的 SDK 头文件在 `third_party\aurie`。

---

## 快捷键与状态面板

两个机制的当前状态会实时显示在**游戏窗口右上角**的小面板上（绿色 `开启` / 红色 `关闭`），
位置在游戏自带区域名牌的下方；游戏窗口不在前台时面板自动隐藏。

| 按键 | 功能 |
| --- | --- |
| `小键盘 1` | 开关机制 A（`minimapExplore` 调用） |
| `小键盘 2` | 开关机制 B（绘制判定补丁） |
| `小键盘 3`（或 `F6`） | 输出运行状态到日志 |
| `小键盘 0` | 显示 / 隐藏状态面板 |

小键盘按键用低层键盘钩子读取，NumLock 开或关都能用，并且只在游戏窗口处于前台时响应。

日志：`<游戏目录>\aurie.log`，同时显示在 “Aurie Framework Log” 控制台窗口里。

```
[MapReveal] overlay: status panel thread running
[MapReveal] numpad hotkeys: keyboard hook installed (works with NumLock on or off)
[MapReveal] loaded. numpad 1=mechanism A numpad 2=mechanism B numpad 3=status numpad 0=panel
```

---

## 说明

* 单机、客户端本地生效，不影响存档格式。
* 状态面板是一个置顶的 layered 窗口，所以需要窗口化 / 无边框全屏模式；真·独占全屏下系统不会
  合成其他窗口，面板看不见（快捷键仍然可用）。
* 面板尺寸随游戏窗口宽度等比缩放（参考宽度 1512 px，限幅 75%–175%），分辨率变化时观感一致；
  用独立的 layered 窗口 + GDI 绘制，不依赖游戏的 D3D11 渲染。
* 偏移量针对实测的那个 `Chronicon.exe` 版本；游戏更新后签名校验会失败并自动禁用，
  带上新的 exe SHA256 提 issue 即可更新地址。
* Steam 更新或“验证游戏文件完整性”会覆盖 exe，之后重跑 `tools\install.ps1` 即可。
* Aurie / YYToolkit 采用 AGPL-3.0，因此本项目同样以 **AGPL-3.0** 授权（见 [LICENSE](LICENSE)、[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)）。

## 致谢

* [Aurie](https://github.com/AurieFramework/Aurie) 与 [YYToolkit](https://github.com/AurieFramework/YYToolkit)：GameMaker 通用 Mod 框架。
* [MinHook](https://github.com/TsudaKageyu/minhook)：挂钩库（BSD-2）。
* [atty303/chronicon-dps](https://github.com/atty303/chronicon-dps)：证明 Aurie/YYToolkit 可用于 Chronicon。
