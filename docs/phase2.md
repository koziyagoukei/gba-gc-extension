# Phase 2: pokeemerald-expansion GC Extension Mode

Phase 2 connects a real pokeemerald-expansion ROM to the Phase 1 communication base. In the game you enter **GC Extension Mode** from a test entry. The GameCube then reads the current party and its front sprites over JoyBus. The TV shows the Pokémon selected on the GBA.

**Primary target:** Dolphin + Integrated GBA.
**Real hardware support:** Not currently a project requirement. Real GBA/GC/DOL-011 behaviour is not validated and is not a blocker.

No battle logic, write-back, TCP/UDP or matchmaking is implemented.

## Pipeline

```text
pokeemerald-expansion (GBA, authority)          GameCube companion.dol
  debug menu > GC Extension Mode                   PROBE: JoyBus RESET + raw probe
  gc_companion.c       state machine + UI           SESSION: HELLO, CAPS
  gc_companion_pokemon.c  records / sprites / labels  PARTY: PKMN_RECORD x N
  gc_companion_protocol.c generic messages           ASSETS: ASSET_INFO + ASSET_CHUNK -> RAM cache
  gc_companion_link.c  JoyBus IRQ queues + reliable responder   ACTIVE: PKMN_UI_STATE poll, GX party view
          \______________ protocol/ (wire, message, pkmn_protocol) ______________/
```

The GameCube is still the only JoyBus master. The GBA never pushes data. It answers requests once per game frame from its own main callback, and the host polls the GBA UI state. That poll is also the keepalive.

## Protocol layering

| Layer | Files | Knows about |
|---|---|---|
| Transport | `gamecube/joybus.c`, GBA `gc_companion_link.c` | 32-bit JoyBus words |
| Reliable frames | `protocol/wire.c`, `gamecube/session.c`, GBA responder | CRC, ACK/NACK, sequence, retransmission, duplicates |
| Generic messages | `protocol/message.c`, `gamecube/companion/host.c`, GBA `gc_companion_protocol.c` | CAPS, APP, ASSET, BYE |
| Pokémon application | `protocol/pkmn_protocol.c`, `party_view.c`, GBA `gc_companion_pokemon.c` | Party records, sprite keys and text labels |

The frame codec has no Pokémon types. A future TCP/UDP transport can carry the same logical frames. The generic layer reaches game data only through two callbacks: an asset resolver and an APP handler.

### New frame types (frame format unchanged, `WIRE_VERSION` 1)

| Type | Name | Request | ACK payload |
|---:|---|---|---|
| 8 | CAPS | Caps (24 B) | Caps (24 B) |
| 9 | APP | u16 app id, u16 opcode, args | u16 app id, u16 opcode, u16 status, u16 0, data |
| 10 | ASSET_INFO | AssetKey (16 B) | AssetInfo (36 B) |
| 11 | ASSET_CHUNK | AssetKey, u32 offset, u32 length ≤ 224 | the bytes |
| 12 | BYE | u16 reason (1 host, 2 client) | empty |

HELLO stays empty and still uses sequence 1. The Phase 1 PoC client NACKs the new types. The Phase 1 suite and `client.gba` are unchanged.

**Caps** (little-endian): u16 message protocol version (2), u16 peer type, u32 build id, u32 capability bits, u16 max payload, u16 app id, u16 app version, u16 0, u8[4] tag.

- Peer types: 1 = PoC client, 2 = pokeemerald-expansion, 0x100 = GC host.
- Capability bits: APP = 1, ASSETS = 2, BYE = 4.
- The client tag is the ROM game code (`BPEE`).
- The client build id is a CRC over the ROM header, data-table addresses, `NUM_SPECIES`/`MOVES_COUNT` and the compile time of `gc_companion_pokemon.c`. Any ROM layout change gives a new id.

**AssetKey:** u16 type, u16 0, u32 id, u32 variant, u32 extra.

**AssetInfo:** u16 status (0 OK, 1 not found, 2 too large), u16 0, AssetKey echo, u16 width, u16 height, u8 pixel format, u8 palette format, u8 frames, u8 0, u32 data length, u32 CRC-32 of the data.

### Pokémon application (app id 0x4B50 "PK", version 1)

| Opcode | Name | Args | Data |
|---:|---|---|---|
| 1 | PARTY_INFO | — | count, record version, u16 record length, cursor, confirmed, screen, flags |
| 2 | RECORD | u8 slot | PokemonRecordV1 |
| 3 | UI_STATE | — | cursor, confirmed (0xff none), screen (0 menu, 1 party), flags (bit0 exit requested), u16 input sequence, u16 0 |
| 4 | SET_PHASE | phase, done, total | — (shown on the GBA: 1 party, 2 assets, 3 ready) |
| 5 | STATE_DIGEST | — | CRC-32 of party, bag, flags, vars, SaveBlock1; match bits vs mode entry |

## PokemonRecordV1 (100 bytes, little-endian)

The record is built field by field from `GetMonData`. It never copies `struct Pokemon` or `BattlePokemon`.

| Off | Size | Field | Off | Size | Field |
|---:|---:|---|---:|---:|---|
| 0 | 1 | record version (1) | 24 | 2 | current HP |
| 1 | 1 | party slot | 26 | 2 | max HP |
| 2 | 2 | record length (100) | 28–37 | 2 each | Atk, Def, Spe, SpA, SpD |
| 4 | 4 | personality | 38 | 2 | ability id |
| 8 | 4 | OT id | 40 | 2 | held item id |
| 12 | 2 | species (form species id) | 42 | 2 | reserved |
| 14 | 2 | National Dex number | 44 | 4 | status (STATUS1 bits) |
| 16 | 1 | form index | 48 | 8 | move[4] |
| 17 | 1 | level | 56 | 4 | PP[4] |
| 18 | 1 | nature | 60 | 4 | max PP[4] (with PP Ups) |
| 19 | 1 | Tera type | 64 | 1 | language |
| 20 | 1 | gender (0 M, 1 F, 2 none) | 65 | 1 | name encoding (1 = pokeemerald charmap) |
| 21 | 1 | flags (bit0 shiny, bit1 egg) | 66 | 1 | nickname length |
| 22 | 1 | ability slot | 68 | 16 | nickname, raw game bytes, 0xff padded |
| 23 | 1 | friendship | 84 | 16 | front sprite AssetKey (resolved by the GBA) |

The decoder rejects other versions or lengths. A new layout gets a new record version.

## Assets

The GBA resolves every key to final data and never sends ROM pointers. Each asset is 8×8-tiled 4bpp (row-major tiles, low nibble = left pixel) followed by 16 BGR555 colours. Index 0 is transparent.

- **`ASSET_PKMN_FRONT` (0x0101):**
  - **Key:** `id` is the species. Unown becomes its letter species. `variant` bits are shiny 1, female graphics 2 and egg 4. `extra` is the personality for spotted species (Spinda), otherwise 0.
  - **Resolution:** the GBA finds the party member with that key and calls `LoadSpecialPokePicIsEgg` and `GetMonSpritePalFromSpeciesAndPersonalityIsEgg`. Gender, shiny, Unown, Spinda spots and eggs therefore follow the game's own rules.
  - **Data:** 64×64, first animation frame only, 2,080 bytes.
- **`ASSET_TEXT_LABEL` (0x0102):**
  - **Key:** `id = kind<<24 | value`, where the kind is species 1, move 2, ability 3, item 4 or nickname 5. For nicknames, `variant` is the CRC of the raw name and `extra` is the personality.
  - **Rendering:** the GBA draws the string with the game's own `FONT_NORMAL` into an off-screen window and sends the used tile columns (width ≤ 128, height 16).
  - **Why:** the host needs no Japanese charset, kana table or font.

Chunks are stateless (`offset`, `length`) and idempotent. The host verifies the full CRC before caching.

## GameCube cache

`gamecube/companion/cache.c` keys entries by (client build id, AssetKey). It stores ready RGB5A3 GX textures in a static 1 MiB arena with up to 128 entries. Nothing is persisted. A new session with the same build id fetches only missing keys, so a reconnect or a cancelled transfer resumes from cache.

## GBA side

| File | Role |
|---|---|
| `src/gc_companion.c` | Special `GcCompanion_Start`, fade, own CB2, state machine, GBA UI, return to field |
| `src/gc_companion_link.c` | C port of LinkCube's IRQ queue scheme, raw handshake, reliable responder |
| `src/gc_companion_protocol.c` | HELLO/PING/CAPS/APP/ASSET/BYE dispatch |
| `src/gc_companion_pokemon.c` | Records, sprite resolver, text labels, state digest |
| `src/gc_ext/*`, `include/gc_ext/*` | Vendored `protocol/` (`tools/sync-pokeemerald.ps1`, `-Check` verifies) |

Small edits elsewhere: one debug-menu entry ("GC Extension Mode" under Utilities) in `src/debug.c`, one script in `data/scripts/debug.inc` and one `def_special ... waitstate=1` line. Battle, save, link and RFU code are not touched.

**State machine:** `IDLE → CONNECTING → HANDSHAKE → SEND_PARTY → SEND_ASSETS → ACTIVE → DISCONNECTING → RETURN_FIELD`, plus `ERROR`.

- **Each frame:** the mode drains the JoyBus queue once, then handles link events, then input. Nothing blocks.
- **B while connecting or transferring:** cancels.
- **B in the menu:** asks the host to close (UI flag). The host sends BYE. Without a BYE the GBA leaves after 1.5 s.
- **GC silent for 2 s, or a JoyBus RESET:** the GBA goes back to `CONNECTING` and keeps the cursor.
- **BYE from the host:** shows "The GameCube ended the session" with A: reconnect, B: exit.
- **Return:** the GBA waits for the last reply to drain, closes JoyBus and restores RCNT, IE and the serial IRQ vector. It then checks the state digest and returns through `CB2_ReturnToFieldContinueScript`.

## Measured results (Dolphin 2609b, integrated mGBA, 160 µs interval)

| Case | Result |
|---|---|
| 3-member save (キモリ/アチャモ/ミズゴロウ Lv.20) | 3 records (300 B) in 0.165–0.204 s. 23 assets (3 sprites + 20 labels, 12,896 B) in 4.49–4.50 s |
| 6-member party | 600 B of records in 0.37 s. 46 assets (27,520 B) in 9.41 s |
| One sprite | 2,080 B in 0.52–0.58 s (INFO + 10 chunks + progress); about 3.7 KiB/s |
| Text label | 224–480 B in 63–260 ms |
| Reconnect (cache warm) | 6 records 0.31 s; 46/46 cache hits, 0 bytes, 17 ms |
| Resume after cancel at 11/23 | 14 hits + 9 fetched (4,768 B) |
| GC cache use | 48,640 B (23 textures) / 104,192 B (46 textures) of the 1 MiB arena |
| GBA ROM | +10,140 B .text, +841 B .rodata |
| GBA static RAM | +108 B EWRAM (.sbss), IWRAM unchanged (28,576 B) |
| GBA heap during the mode | about 23 KB (windows 15,360 + BG map 2,048 + link ≈1.7 K + asset buffer ≈4.2 K); freed on exit |

Throughput is below Phase 1 bulk (5.8 KiB/s): the game answers once per frame instead of busy-polling, and every asset also costs an INFO and a progress request.

Displayed and verified on the TV:

- Treecko, Torchic and Mudkip from the save.
- The debug-menu party: Pheromosa, Rayquaza, Ogerpon and Lapras, with Japanese nicknames that differ from their species names and with held items (e.g. カクトウZ).
- A shiny female Venusaur: golden shiny palette and female sprite.
- An egg: egg sprite, contents hidden on the TV.
- A poisoned Lapras (PSN).
- Spinda, whose spots come from its personality.
- An Unown, resolved to letter species 1029.
- Alolan Raichu (No.0026, form 1).

## Safety

The mode only reads game state.

**GBA check.** The GBA hashes the party and count, the bag, flags, vars and SaveBlock1 at entry, and again just before returning. The return screen showed "Party/bag/flags/vars unchanged" in every run.

**Host check.** The host logs the digest at session start and end; every session logged `match=1f`. Across two visits with a field return in between, party, bag, flags and vars were identical.

**Why the bag hash is decrypted.** The game re-keys encrypted fields and moves SaveBlock1 on every map load. So the bag digest covers decrypted (item, quantity) pairs, and the raw SaveBlock1 CRC is compared only within one visit.

**Save file.** The test save's timestamp and SHA-256 were unchanged after all sessions. The mode never calls save functions.

**Party edits.** The richer test parties were created in RAM with the existing debug menu and never saved.

## Design changes from the plan

- **LinkCube is ported to C instead of compiled.**
  - *Why:* pokeemerald-expansion has no C++ build rule and no static constructors.
  - *Effect:* the queue logic and ordering are the same as LinkCube. The IRQ acknowledges exactly the flags it handled, and the original IRQ vector and registers are restored on exit.
- **Text is sent as GBA-rendered label images, not strings.**
  - *Why:* this fork's names are Japanese in the game's own encoding, and the GC has no matching font.
  - *Effect:* the host stays charset-free. The record still carries the raw nickname bytes for later use.
- **One host executable per phase.** `host.dol` stays the Phase 1 test suite. `companion.dol` is the Phase 2 host. Both share the transport, session and raw handshake (`gamecube/raw_link.c`).
- **The bag digest is computed on decrypted pairs**, for the reason given under Safety.
- **The scripts run on Windows PowerShell 5.1 as well as 7.** PowerShell 7 is not installed on the reference machine.

## Readiness for a GC-side 1v1 engine

Most of what a 1v1 engine needs is in place:

- Versioned per-Pokémon records with stats, moves, PP, ability, item, status, nature and Tera type.
- Sprites and labels cached on the GC.
- A GBA input channel.
- A clean session start and end with authority kept on the GBA.

Two things are still missing:

- **Battle data.** Base stats, types and move data are not transferred yet. They will need either a GC-side data set or new records.
- **A result channel.** Today everything is read-only.
