# GBA–GameCube Extension

Experimental framework for local GBA ↔ GameCube communication over standard JoyBus. It runs independently of pokeemerald-expansion.

The PoC exchanges 32-bit values, tests framed request/response reliability over 10,000 requests, streams test data up to 64 KiB, and renders an original sprite received from the GBA. The reference test environment is Dolphin 2609b with integrated mGBA on port 2. Real GBA/GC hardware is a future validation target.

Requires PowerShell 7 and devkitPro (devkitARM + libgba, devkitPPC + libogc, gbafix and elf2dol).

```powershell
./build.ps1
```

Use `build/client.gba` for the GBA and `build/host.dol` for the GameCube. [Build instructions](docs/build.md) · [Test procedure](docs/testing.md) · [Benchmarks](docs/benchmarks.md) · [Known issues](docs/known-issues.md).

- `gba/` — standalone GBA client and original test sprite.
- `gamecube/` — SI transport, test runner and GX display.
- `protocol/` — platform-independent serialization, CRC and fixture definitions.
- `third_party/` — pinned MIT LinkCube dependency.
- `docs/` — architecture, protocol, testing and results.

Only original test content and licensed dependencies belong in this repository. Supply any required BIOS privately. ROMs, BIOS, official Nintendo/Pokémon assets, emulator packages and generated executables are excluded. No save integration, battle engine, online transport or matchmaking is implemented.
