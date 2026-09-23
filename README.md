# Chronicon Map Reveal

![vibe coded](https://img.shields.io/badge/vibe--coded-%F0%9F%8E%B2-ff69b4)

Removes the fog of war from **Chronicon**'s map — every zone stays fully revealed,
equivalent to the in-game item *Fin'ley's magical map* (`item_699`, "Use: Fully reveal the map").

> 中文说明见 [README.zh-CN.md](README.zh-CN.md)。

> 🧪 **This project is vibe coded.** The reverse engineering, the hook implementation and the
> PowerShell tooling were all produced conversationally with an AI agent; the human contribution
> was checking that the result actually works in game. It is functional and documented, but treat
> the version-specific offsets (and anything not covered by
> [docs/technical-notes.md](docs/technical-notes.md)) with the appropriate amount of suspicion.

* Tested against Chronicon `Chronicon.exe` (59,400,704 bytes, SHA256 `C0036176…`), GameMaker Studio 2 **YYC x64** build.
* Loaded by [Aurie](https://github.com/AurieFramework/Aurie); the hooks themselves are done with [MinHook](https://github.com/TsudaKageyu/minhook).
* Every hook validates a machine-code signature first, so a game update makes the mod log an error and do nothing instead of crashing.

---

## How it works

Chronicon ships as a **YYC** build: the GML scripts are compiled into `Chronicon.exe`,
so they can be hooked as native functions. Two complementary mechanisms are used:

**A. Call the game's own "reveal the whole map" script as a one-shot pulse.**

`gml_Script_minimapExplore` *is* the effect of Fin'ley's magical map:

```gml
ds_grid_clear(mapexplore, 1);   // mark every cell as explored
minimapRefresh();               // rebuild the minimap
```

`gml_Script_minimapSetArea` fires when a zone's minimap is (re)built, which the mod uses as a
"you entered a zone" signal: **1 second after the zone is entered mechanism A switches on and
0.5 second later it switches off again** (both numbers live in the ini). Clearing the grid once
is enough — it keeps those values until the zone is regenerated. `minimapUpdate`,
`minimapRefresh` and `world_gen_step` stay hooked too, so anything they call while the pulse is
on goes through as well.

**B. Skip the "not explored → don't draw" check — always on.**

The minimap drawing loop skips cells that are not explored; the mod rewrites that single
conditional jump into NOPs at runtime (`VA 0x141290B82`).

> Mechanism A alone leaves the map rendered in its "unexplored" style; A **and** B together
> produce a clean full map. B is therefore patched in at load and left on, which is what makes
> the short A pulse sufficient — the mod no longer has to call `minimapExplore` continuously.
> A pure on-disk byte patch cannot work reliably here because the relevant branch only runs once,
> at zone *generation* time.

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

## In-game keys and status panel

The current state of both mechanisms is drawn in a small always-on-top panel in the
**top-right corner of the game window** — green `开启` = on, red `关闭` = off. It sits just
below the game's own zone-name plate and hides itself whenever the game window is not in
the foreground. Each row also shows the key currently bound to it (`A 自动全开地图  小键盘1  开启`).

| Key | Action |
| --- | --- |
| `Numpad 1` | pulse mechanism A once (the same 1 s → 0.5 s pulse a zone entry does) |
| `Numpad 2` | toggle mechanism B (the drawing-gate patch) |
| `Numpad 3` | print status (hook call counts, reveal count, patch state, key bindings) to the log |
| `Numpad 0` | show / hide the status panel |
| `Numpad 9` | rebind mode (see [Changing the keys](#changing-the-keys)) |

The numpad keys are read through a low-level keyboard hook, so they work whether NumLock
is on or off, and only while the game window has focus.

## Changing the keys

Both ways act on the same file, `<game>\mods\aurie\ChroniconMapReveal.ini` (created on first
run, UTF-8):

* **In game** — press `Numpad 9`. The panel shows `【改键】<action> → 请按新键`; press the key you
  want (modifier combinations work: hold `Ctrl` and press `F5`), and it is bound and written to
  the ini. Press `Numpad 9` again for the next action, `Esc` to cancel. Changes apply immediately.
* **By editing the ini** — change a value and save; the game reloads it within ~2 seconds, no
  restart. Names look like `NUMPAD1`, `F5`, `A`, `SPACE`, `CTRL+F1` (case-insensitive), and each
  action can hold exactly one binding.

```ini
[keys]
pulse=NUMPAD1        ; pulse mechanism A once
draw_gate=NUMPAD2    ; toggle mechanism B
status=NUMPAD3       ; dump status to the log
panel=NUMPAD0        ; show / hide the status panel
rebind=NUMPAD9       ; rebind mode

[auto]
enabled=1            ; pulse mechanism A automatically when a zone is entered
delay_ms=1000        ; wait this long after entering the zone
hold_ms=500          ; keep mechanism A on for this long
```

`enabled=0` turns the automatic zone pulse off (mechanism B alone still draws every cell, in the
"unexplored" style). If a zone ever still looks fogged, raise `hold_ms`.

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
[MapReveal] overlay: status panel thread running
[MapReveal] keys: pulse=小键盘1 draw_gate=小键盘2 status=小键盘3 panel=小键盘0 rebind=小键盘9 | auto=1 delay=1000ms hold=500ms
[MapReveal] loaded: B always on, A pulses 1000 ms after a zone entry for 500 ms
[MapReveal] zone entered: mechanism A pulses in 1000 ms for 500 ms
[MapReveal] mechanism A = ON (zone pulse)
[MapReveal] mechanism A = OFF (idle)
[MapReveal] tick: worldGen=... reveals=... update=... refresh=...
```

---

## Notes and limitations

* Single-player, local, client-side. Save format is untouched.
* Mechanism A no longer runs continuously: it is a ~0.5 s pulse on zone entry (plus the manual
  `Numpad 1` pulse), while mechanism B stays patched in. If a zone ever still shows fog, raise
  `hold_ms` in the ini.
* Key bindings live in `<game>\mods\aurie\ChroniconMapReveal.ini`; deleting it restores the
  defaults (numpad 1 / 2 / 3 / 0 / 9) on the next load.
* The status panel is a layered topmost window, so it needs a windowed or borderless-fullscreen
  display mode; in exclusive fullscreen Windows does not composite other windows on top of the
  game, and the panel will not be visible (the hotkeys keep working).
* The panel scales with the width of the game window (reference layout: 1512 px client width,
  clamped to 75–175 %), so it keeps roughly the same proportions at any resolution. It is drawn
  by a separate layered window with GDI, independent of the game's D3D11 renderer.
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
