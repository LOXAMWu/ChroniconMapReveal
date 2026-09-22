# Chronicon Map Reveal

Removes the fog of war from **Chronicon**'s map — every zone stays fully revealed,
equivalent to the in-game item *Fin'ley's magical map* (`item_699`, "Use: Fully reveal the map").

> 中文说明见 [README.zh-CN.md](README.zh-CN.md)。

* Tested against Chronicon `Chronicon.exe` (59,400,704 bytes, SHA256 `C0036176…`), GameMaker Studio 2 **YYC x64** build.
* Loaded by [Aurie](https://github.com/AurieFramework/Aurie); the hooks themselves are done with [MinHook](https://github.com/TsudaKageyu/minhook).
* Every hook validates a machine-code signature first, so a game update makes the mod log an error and do nothing instead of crashing.

---

## How it works

Chronicon ships as a **YYC** build: the GML scripts are compiled into `Chronicon.exe`,
so they can be hooked as native functions. Two complementary mechanisms are used:

**A. Repeatedly call the game's own "reveal the whole map" script.**

`gml_Script_minimapExplore` *is* the effect of Fin'ley's magical map:

```gml
ds_grid_clear(mapexplore, 1);   // mark every cell as explored
minimapRefresh();               // rebuild the minimap
```

It is invoked from several triggers so that the map stays revealed no matter where
you are or when the zone was generated:

| Hooked function | When it fires |
| --- | --- |
| `gml_Script_minimapSetArea` | zone minimap initialisation |
| `gml_Script_minimapUpdate` | every minimap update tick |
| `gml_Script_minimapRefresh` | every minimap redraw |
| `gml_Script_world_gen_step` | every 30 frames as a safety net |

**B. Skip the "not explored → don't draw" check.**

The minimap drawing loop skips cells that are not explored; the mod rewrites that single
conditional jump into NOPs at runtime (`VA 0x141290B82`).

> Mechanism A alone leaves the map rendered in its "unexplored" style; A **and** B together
> produce a clean full map. A pure on-disk byte patch cannot work reliably here because the
> relevant branch only runs once, at zone *generation* time.

Details, addresses and the reasoning are in [docs/technical-notes.md](docs/technical-notes.md).

---

## Install

1. Close the game.
2. Download `ChroniconMapReveal.dll` from the [Releases](../../releases) page
   (or build it yourself, see below).
3. Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\install.ps1 -ModDll .\ChroniconMapReveal.dll
```

The script downloads [Aurie](https://github.com/AurieFramework/Aurie) (v2.0.2),
backs up and patches `Chronicon.exe`, and copies the mod into `<game>\mods\aurie\`.

**Uninstall**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\uninstall.ps1
```

---

## Build from source

Requirements: MinGW-w64 with C++20 (GCC 11+; tested with 15.2).

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
```

The DLL is written to `build\ChroniconMapReveal.dll`. MinHook is vendored in
`third_party\minhook`, the Aurie SDK header in `third_party\aurie`.

---

## In-game keys

| Key | Action |
| --- | --- |
| `F5` | toggle mechanism A (the `minimapExplore` calls) |
| `F6` | print status (hook call counts, reveal count, patch state) to the log |
| `F7` | toggle mechanism B (the drawing-gate patch) |

Log file: `<game>\aurie.log` (also shown in the "Aurie Framework Log" console window).

Expected log output:

```
[MapReveal] init (multi-trigger + draw-gate patch)
[MapReveal] minimapIni hooked at ...
[MapReveal] minimapRefresh hooked at ...
[MapReveal] minimapSetArea hooked at ...
[MapReveal] minimapUpdate hooked at ...
[MapReveal] world_gen_step hooked at ...
[MapReveal] draw-gate patch = ON
[MapReveal] loaded. F5=reveal F6=status F7=draw-gate
[MapReveal] tick: worldGen=... reveals=... update=... refresh=...
```

---

## Notes and limitations

* Single-player, local, client-side. Save format is untouched.
* The offsets are specific to the tested `Chronicon.exe` build. If the game updates, the
  signature checks fail and the mod disables itself — open an issue with the new
  `Chronicon.exe` SHA256 and the RVA list can be updated.
* Steam "verify integrity of game files" or a game update restores the original `exe`;
  re-run `tools\install.ps1` afterwards.
* Because Aurie/YYToolkit are AGPL-3.0, this project is licensed under **AGPL-3.0** as well
  (see [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)).

## Credits

* [Aurie](https://github.com/AurieFramework/Aurie) and [YYToolkit](https://github.com/AurieFramework/YYToolkit) — the modding framework for GameMaker games.
* [MinHook](https://github.com/TsudaKageyu/minhook) — hooking library (BSD-2).
* [atty303/chronicon-dps](https://github.com/atty303/chronicon-dps) — showed that Aurie/YYToolkit works with Chronicon.
