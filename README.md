# GBA–GameCube Extension

Experimental framework for local GBA ↔ GameCube communication over standard JoyBus. The GBA owns game data and acts as controller and second screen. The GameCube runs special modes and renders.

**Primary target:** Dolphin + Integrated GBA (reference: Dolphin 2609b, GBA on GC port 2).
**Real hardware support:** Not currently a project requirement. GBA/GC hardware, DOL-011 or third-party cables, multiboot and real link timing are not validated.

- **Phase 1 (PoC):** exchanges 32-bit values, runs framed request/response reliability over 10,000 requests, and streams test data up to 64 KiB. It also renders an original sprite received from the GBA.
- **Phase 3:** generic connector API (`GcStartMode`, MODE/RESULT frames) on the pokeemerald-expansion side, game content in frontier-hack, and a SINGLE 1vs1 battle run on the GameCube with GBA move input and a versioned result returned to the GBA. See [Phase 3](docs/phase3.md).
- **Phase 2:** connects a real pokeemerald-expansion ROM. In-game *GC Extension Mode* sends the player's party (versioned records), real ROM front sprites and game-font name labels to the GameCube. The TV follows the party slot selected on the GBA. See [Phase 2](docs/phase2.md).

Requires Windows PowerShell 5.1 or PowerShell 7, and devkitPro (devkitARM + libgba, devkitPPC + libogc, gbafix and elf2dol).

```powershell
./build.ps1
```

Outputs:

- `build/client.gba` + `build/host.dol`: Phase 1 suite.
- `build/companion.dol`: Phase 2 host for a pokeemerald-expansion ROM in GC Extension Mode.

[Build instructions](docs/build.md) · [Test procedure](docs/testing.md) · [Protocol](docs/protocol.md) · [Phase 2](docs/phase2.md) · [Phase 3](docs/phase3.md) · [Benchmarks](docs/benchmarks.md) · [Known issues](docs/known-issues.md).

- `gba/` — standalone Phase 1 GBA client and original test sprite.
- `gamecube/` — SI transport, raw handshake, session, Phase 1 test runner and GX display.
- `gamecube/companion/` — host runtime: message-layer client, asset RAM cache, GX party view and battle view.
- `gamecube/battle/` — SDK-free battle core (SINGLE) with desktop tests.
- `protocol/` — platform-independent frames, generic messages (CAPS/APP/ASSET/BYE/MODE/RESULT), the Pokémon application protocol and the battle protocol.
- `tools/sync-pokeemerald.ps1` — vendors `protocol/` into a pokeemerald-expansion checkout (`-Check` verifies).
- `third_party/` — pinned MIT LinkCube dependency.
- `docs/` — architecture, protocol, testing and results.

Only original code, original test content and licensed dependencies belong in this repository. Supply any required BIOS privately. ROMs, saves, BIOS, official Nintendo/Pokémon assets, emulator packages and generated executables are excluded. Pokémon graphics reach the GameCube only at run time from the user's own ROM. Only a minimal SINGLE battle core exists; no Double/Triple/Rotation, EXP/EV/rewards, online transport or matchmaking is implemented.
