# GBA–GameCube Extension

Experimental framework for local GBA ↔ GameCube communication over standard JoyBus. Independent of Pokémon and pokeemerald-expansion.

Requires PowerShell 7 and devkitPro (devkitARM + libgba, devkitPPC + libogc, gbafix and elf2dol). Run `./build.ps1` from this directory. Outputs are under `build/`. See [build instructions](docs/build.md).

- `gba/` — standalone GBA client.
- `gamecube/` — GameCube SI host and display.
- `protocol/` — machine-independent wire definitions.
- `third_party/` — pinned MIT LinkCube dependency.
- `docs/` — build, protocol, testing and results.

Only original test content and licensed dependencies belong in this repository. Supply any required BIOS privately. ROMs, BIOS, official assets and generated executables are not tracked.

This is an experimental communication PoC. It has no battle, save integration, online transport or matchmaking.
