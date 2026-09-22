# Technical notes

How the mod was derived, and what to update when Chronicon is patched.

## 1. Target binary

| Item | Value |
| --- | --- |
| Game | Chronicon (Steam app `375480`) |
| Executable | `Chronicon.exe`, 59,400,704 bytes |
| SHA256 | `C003617663338A67A1617BFBF279F8060EC454391695B247D6522A5ED8392D32` |
| Engine | GameMaker Studio 2, **YYC** native x64 build |
| Image base / `.text` | `0x140000000`, `.text` starts at base + `0x1000` |

Because it is a YYC build, `data.win` has no bytecode chunk — all GML is compiled into the
executable, one native function per script. Script names are still present in the
executable's identifier tables, which is what makes name → address recovery possible.

## 2. Recovering script addresses

Two structures in `.data`/`.rdata` were used:

1. a **name → index** table (16-byte entries `{ char* name; uint32 index; uint32 pad; }`), and
2. a **script table** (24-byte entries `{ char* name; void* function; void* name_entry; }`)
   sorted by that index.

Walking the second table yields a full `gml_Script_*` → native function map
(9,714 entries for this build). Variable IDs used by GML code can be resolved the same way:
code loads a variable id from `name_entry + 8`, so `[rip+X]` with `X-8` inside the name table
identifies the variable (`mapexplore`, `minimap`, `DUNGEON`, `area_tileset`, …).

## 3. Functions involved

| Script | RVA | Prologue (first 16 bytes) | Purpose |
| --- | --- | --- | --- |
| `gml_Script_minimapIni` | `0x128AD00` | `48 8B C4 48 89 58 20 4C 89 40 18 48 89 50 10 48` | minimap init (called from `SYSTEM_Create_0`) |
| `gml_Script_minimapExplore` | `0x128AA30` | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55` | **"fully reveal the map"** (item 699 effect) |
| `gml_Script_minimapPlaceTile` | `0x128B3C0` | `48 8B C4 4C 89 40 18 48 89 50 10 48 89 48 08 55` | draws one minimap cell |
| `gml_Script_minimapRefresh` | `0x128FDE0` | `48 8B C4 48 89 58 20 4C 89 40 18 48 89 50 10 48` | rebuilds the minimap surface |
| `gml_Script_minimapSetArea` | `0x1297570` | `48 89 5C 24 10 44 89 4C 24 20 4C 89 44 24 18 55` | per-zone minimap setup |
| `gml_Script_minimapUpdate` | `0x1299860` | `48 8B C4 4C 89 40 18 48 89 50 10 48 89 48 08 55` | periodic minimap update |
| `gml_Script_world_gen_step` | `0x1B78D10` | `48 8B C4 48 89 58 20 4C 89 40 18 48 89 50 10 48` | per-frame world generation step (used as a tick) |

YYC GML script calling convention (x64):

```c
RValue& fn(CInstance* Self, CInstance* Other, RValue& Result, int argc, RValue** args);
```

`RValue` is a 16-byte struct: 8-byte value/payload, then `uint32 flags`, then `uint32 kind`
(`0` = real, `1` = string, `2` = array/refcounted, `5` = undefined).

## 4. Why the map stays fogged

`minimapExplore` decompiles to just:

```gml
ds_grid_clear(mapexplore, 1);
minimapRefresh();
```

so it only marks the "explored" grid. The minimap **drawing** loop in `minimapRefresh`
iterates the level grid and contains, right after a cell lookup:

```asm
0x141290B5D   call  sub_14261E810      ; grid handle from mapexplore
0x141290B73   call  sub_142699200      ; read cell [x,y]
0x141290B7B   call  sub_14261D200      ; to bool
0x141290B80   test  al, al
0x141290B82   0F 84 D6 01 00 00       ; je 0x141290D5E  -> skip drawing this cell
0x141290B88   ...                      ; compute tile to draw
0x141290D54   call  gml_Script_minimapPlaceTile
```

Two observations from in-game testing:

* clearing `mapexplore` (mechanism A) alone does **not** change what is rendered — the map is
  still drawn in the "unexplored" style;
* turning `0x141290B82` into six `NOP`s (mechanism B) makes every cell take the drawing path.

Running A continuously *and* applying B produces a fully revealed map.

The same branch also explains why a static patch is unreliable: `minimapSetArea` only reaches
the "reveal" branch during zone *generation*, so a byte patch has no effect in a zone that was
generated before the patch was installed.

Side effect of mechanism B: the loop reads one cell past the end of a 50×2 helper grid,
so the game's own debug console prints `Grid N, index out of bounds reading [...]` lines.
It is harmless (the game does the same read on the drawing path) and can be silenced with `F7`.

## 5. Loader

Aurie is used purely as a loader (it patches `Chronicon.exe` with a `.aurie` section that loads
`AurieCore.dll`, which loads every DLL in `mods\aurie`). Its exported `MmCreateHook` cannot be
used from a MinGW-built module — the header's inline wrapper generates Itanium-ABI calls, while
`AurieCore.dll` is MSVC-built — so the hooks are installed with MinHook instead, which is
compiled into the mod from source.

## 6. Updating for a new game build

1. Get the new `Chronicon.exe` SHA256 and open an issue.
2. Re-locate the scripts (name → address mapping, section 2) and refresh the `RVA_*` constants
   and `PROLOGUE_*` byte patterns in `src/ChroniconMapReveal.cpp`.
3. Re-check the drawing-gate instruction in `minimapRefresh` (`DRAW_GATE_ORIGINAL`) — it is a
   6-byte `je rel32`; only its existence and target semantics need to hold.
4. Rebuild and re-test with `F6` status output and `aurie.log`.
