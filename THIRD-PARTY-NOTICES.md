# Third-party notices

This project is distributed under **AGPL-3.0** (see `LICENSE`), because it uses the
following components:

## Aurie Framework

* https://github.com/AurieFramework/Aurie — license: **AGPL-3.0** (`third_party/aurie/LICENSE.txt`)
* Used as the mod loader (`AurieCore.dll`, installed by `AuriePatcher.exe`, both downloaded at
  install time and *not* redistributed here).
* `third_party/aurie/Aurie/shared.hpp` is the Aurie SDK header (the copy maintained in the
  YYToolkit repository, which is the one that compiles with MinGW/GCC).

## YYToolkit

* https://github.com/AurieFramework/YYToolkit — license: **AGPL-3.0**
* Not required at runtime by this mod and not redistributed here; it was used as a reference
  (SDK layout, Aurie ABI details, and how existing GameMaker YYC mods are structured).

## MinHook

* https://github.com/TsudaKageyu/minhook — license: **BSD-2-Clause**
  (`third_party/minhook/LICENSE.txt`)
* Vendored source: `third_party/minhook/` and compiled into `ChroniconMapReveal.dll`.

## Chronicon

Chronicon is © Subworld AB. This repository contains no game assets or game code; it is an
independent, client-side modification for single-player use.
