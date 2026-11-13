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
