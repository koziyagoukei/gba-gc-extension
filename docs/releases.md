# Releases and compatibility

Each public test build pairs one GameCube `companion.dol` with GBA builds of
the same **release number**. Both sides send it in CAPS (bytes 18–19, see
[protocol](protocol.md)) and refuse a peer with a different number:

- The GC shows `Version mismatch: GBA patch rN, companion.dol rM` and ends the session.
- The GBA shows `Version mismatch.` with both numbers and returns to the field with B.
- Release 0 is a development build. It accepts any peer, so development
  ROMs and DOLs keep working with each other. A release build refuses release 0.

The **link check** (Cube Tester → せつぞく かくにん, or the debug entry
*GC Link Check*) shows both sides' release, build ID and protocol version.

| Release | Name | GC `companion.dol` | GBA build | Protocol | Status |
|---|---|---|---|---|---|
| 0 | dev | `./build.ps1` | any development build | message layer v3 | development |
| 2 | m3-dev | `./build.ps1 -Release 2` (branch `milestone3`) | Frontier branch `frontier-gc-m3` (`GcExtension_GetRelease() = 2`) | message layer v3, battle request V8 (move effects) | development only, not distributed |
| 4 | m3.5-audit | `./build.ps1 -Release 4` (branch `m3.5-audit-fixes`) | Frontier branch `frontier-gc-m3.5-audit` (`GcExtension_GetRelease() = 4`) | message layer v3 with final results (result id = MODE nonce, `MODE_FLAG_RESULT_FINAL`), battle request V10 (effect application units) | development only, not distributed |
| 8 | m4p-dev | `./build.ps1 -Release 8` (branch `milestone4p`) | Frontier branch `frontier-gc-m4p` (`GcExtension_GetRelease() = 8`) | message layer v3 with final results, battle request V14 (V13 layout, Gen 5+ fixed-point damage, [modifiers](modifiers.md)) | development only, not distributed |
| 9 | m4w-dev | `./build.ps1 -Release 9` (branch `milestone4w`) | Frontier branch `frontier-gc-m4w` (`GcExtension_GetRelease() = 9`) | message layer v3 with final results, battle request V15 (V14 + field rules, weather, [weather](weather.md)) | development only, not distributed |
| 10 | m4t-dev | `./build.ps1 -Release 10` (branch `milestone4t`) | Frontier branch `frontier-gc-m4t` (`GcExtension_GetRelease() = 10`) | message layer v3 with final results, battle request V16 (V15 + terrain rules, [terrain](terrain.md)) | development only, not distributed |
| 11 | m4b0-dev | `./build.ps1 -Release 11` (branch `milestone4b0`) | Frontier branch `frontier-gc-m4b0` (`GcExtension_GetRelease() = 11`) | message layer v3 with final results, battle request V17 (V16 + item blocks, [items](items.md)) | development only, not distributed |
| 12 | m4b1a-dev | `./build.ps1 -Release 12` (branch `milestone4b1a`) | Frontier branch `frontier-gc-m4b1a` (`GcExtension_GetRelease() = 12`) | message layer v3 with final results, battle request V18 (V17 + end-of-turn item HP records, [items](items.md) section 10); M4-B1a+ (branches `milestone4b1aplus` / `frontier-gc-m4b1aplus`) keeps release 12, V18 and the same `companion.dol` byte for byte: audited Z-Crystals and Mega Stones held with no effect (items section 11) | development only, not distributed |
| 13 | m4b1b-dev | `./build.ps1 -Release 13` (branch `milestone4b1b`) | Frontier branch `frontier-gc-m4b1b` (`GcExtension_GetRelease() = 13`) | message layer v3 with final results, battle request V19 (V18 layout + consumable item records: HP and status berries, Focus Sash, [items](items.md) section 12) | development only, not distributed; the development branches `milestone4b1c0b` / `frontier-gc-m4b1c0b` add request/APP V20 (Life Orb, [items](items.md) section 14) on the same release; `milestone4b1c0c` / `frontier-gc-m4b1c0c` run V20 on the GBA → GC path (GC V20 Lab, [items](items.md) section 15); `milestone4b1c1b` / `frontier-gc-m4b1c1b` add request/APP V21 (Shell Bell, [Shell Bell](shell-bell.md) section 11) |
| 7 | rule-poc | `./build.ps1 -Release 7` (branch `battle-rule-poc`) | Frontier branch `frontier-battle-rule-poc` (`GcExtension_GetRelease() = 7`) | message layer v3 with final results, battle request V13 (shared modifier records, [PoC](rule-poc.md)) | proof of concept only, not distributed |
| 6 | m4a2-dev | `./build.ps1 -Release 6` (branch `milestone4a2`) | Frontier branch `frontier-gc-m4a2` (`GcExtension_GetRelease() = 6`) | message layer v3 with final results, battle request V12 (multi-hook ability blocks, strict and fixed-seed rules) | development only, not distributed |
| 5 | m4a-dev | `./build.ps1 -Release 5` (branch `milestone4a`) | Frontier branch `frontier-gc-m4a` (`GcExtension_GetRelease() = 5`) | message layer v3 with final results, battle request V11 (ability descriptors, contact flag) | development only, not distributed |
| 3 | m3.5-dev | `./build.ps1 -Release 3` (branch `milestone3.5`) | Frontier branch `frontier-gc-m3.5` (`GcExtension_GetRelease() = 3`) | message layer v3, battle request V9 (volatile status, multi-turn actions) | development only, not distributed |
| 1 | v0.1.0-alpha | `./build.ps1 -Release 1` | Frontier Hack with `GcExtension_GetRelease() = 1` (`src/frontier_cube_tester.c`) | message layer v3, battle request V7 | **not released** (see below) |

Build IDs identify exact binaries. The GBA build ID is a CRC of the ROM
header, a few data-table addresses and the build time; the GC build ID is
`0x48320001`. Both are shown by the link check and in bug reports.

## v0.1.0-alpha status

The release-1 pair was built and verified in Dolphin 2609b (see
[public test guide](public-test-guide.md#検証記録)), but no GitHub Release
was published. The GBA side would have to be distributed as a BPS patch
against the Japanese Emerald ROM, and that patch was audited before release:

| Item | Value |
|---|---|
| Base ROM | ポケットモンスター エメラルド (Japan), game code BPEJ, revision 0, 16,777,216 bytes |
| Base ROM SHA-256 | `33f5610b9186b4add09fef68895deb00f552b997b3d133b5a961e5123506343c` |
| Base ROM SHA-1 / CRC32 | `d7cf8f156ba9c455d164e1ea780a6bf1945465c2` / `4881f3f8` |
| Patched ROM | 33,554,432 bytes (26,322,912 bytes used) |
| BPS patch | 20,576,348 bytes: 19,824,410 bytes of new data, 13,517,079 bytes copied from the base ROM (much of it padding) |
| New data, by linker symbol | about 2.5 MB code, 8.3 MB graphics, 11.2 MB sound, 4.4 MB other data (whole ROM, not only the new part) |

The new data is mostly the rebuilt game code and the Pokémon graphics and
sounds of the pokeemerald-expansion base, much of which is derived from the
official games. Because the project requires that distributed patches do
not contain large amounts of official material, the patch was not published.
The base-ROM hashes above were measured on the build machine; they match
the values the Frontier build itself checks (`tools/check_baserom_jp.py`).
