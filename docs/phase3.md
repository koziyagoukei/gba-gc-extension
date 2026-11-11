# Phase 3: generic connector, frontier-hack content and a GC 1vs1 battle

**Primary target:** Dolphin + Integrated GBA. **Real hardware:** not a requirement.

Phase 3 separates three responsibilities and proves the round trip GBA party → GameCube battle → result → GBA on real game content.

```text
gba-gc-extension (this repo, private)     GameCube runtime: protocol, transport, cache, rendering, battle core
        ↑↓  GC Extension Protocol (frames v1, messages v3, app protocols)
pokeemerald-expansion  branch gc-extension (public)   generic connector / API: GcStartMode, export, MODE/RESULT
        ↑
frontier-hack  branch frontier-gc-battle (public fork) game content: GC Battle test entries, trainer, rules
```

The master-side connector contains no battle code and no use-specific API. The battle request/result codec (`protocol/battle_protocol.*`) is vendored only into content branches (`tools/sync-pokeemerald.ps1 -Battle`).

## Generic connector (pokeemerald-expansion `gc-extension`)

Content calls `GcStartMode(&handler)`. `include/gc_extension.h` defines the `GcModeHandler`:

| Callback | Purpose |
|---|---|
| `init` | Builds the request; may refuse with a reason shown on the GBA |
| `getRequest` | Request bytes the host fetches after MODE |
| `handleApp` | The mode's own APP opcodes (separate app id) |
| `draw` / `input` | GBA second screen while the session is active |
| `onResult` | RESULT payload. The connector delivers each result id at most once and acks repeats as `RESULT_DUPLICATE` |
| `finish` | Runs once, after the link is closed and after the unchanged-state check. This is the only place persistent state may change |
| `free` | Release the mode's memory |

What the connector keeps doing for every mode:

- JoyBus, raw handshake, HELLO/CAPS.
- Party/record/sprite/label export.
- Progress display, cancel, timeout, BYE and reconnect.
- The digest check and the return to the field.

`GcExtension_SetExtraMons()` exports non-party Pokémon (e.g. an opponent) at slots `0x80+`. The Phase 2 party viewer is now the built-in `GC_MODE_PARTY_VIEW` handler.

The message layer goes to version 3. Version-2 peers still work: a client without `CAP_MODE` is treated as the party viewer.

| Frame | Name | Request | ACK |
|---:|---|---|---|
| 13 | MODE | empty | u16 mode id, u16 mode version, u16 mode app id, u16 app version, u32 request length, u32 request CRC, u32 session nonce, u32 flags |
| 14 | RESULT | u16 mode id, u16 result version, u32 result id, payload ≤ 216 B | u16 status (accepted / duplicate / rejected / unsupported), u16 0 |

The request is served as asset `{ASSET_MODE_REQUEST = 0x0001, mode id, 0, nonce}` through the existing ASSET_INFO/ASSET_CHUNK path, so it is CRC-checked and resumable.

## Battle request (GcBattleRequestV1, app 0x4247 "GB")

**Header (24 bytes):**

| Offset | Field |
|---:|---|
| 0 | version |
| 1 | format (1 SINGLE, 2 DOUBLE, 3 TRIPLE, 4 ROTATION) |
| 2 | battle type (1 trainer) |
| 3, 4 | player count, opponent count |
| 5 | active per side |
| 6 | u16 rules |
| 8 | u16 trainer id |
| 10 | trainer class |
| 11 | reward mode |
| 12 | result policy (0 display only, 1 apply HP/PP) |
| 13 | type count N |
| 14 | u16 entry length (140) |
| 16 | u32 battle id |
| 20 | u32 reserved |

**Entries:** player entries, then opponent entries. Each is a `PokemonRecordV1` (100 B) plus a `BattleMonV1` (40 B):

- **Types:** count and two type ids.
- **Four `BattleMove`s (8 B each):** u16 power, type, category, accuracy (0 = never misses), s8 priority, target (single / all foes / other), and an unsupported-reason bit set.

**Type chart:** N×N bytes, multiplier in quarters (0, 2, 4, 8). Built from `gTypeEffectivenessTable`.

**Rules used by the frontier test:** ignore abilities, ignore items, ignore status, no gimmicks (Mega / Z / Dynamax / Tera), no critical hits, supported moves only. These are stated in the request and shown on the TV; they are explicit normalisations, not silent approximations.

## Battle data supply: option A (GBA resolves everything)

The GBA sends the resolved data for exactly the battlers in this battle: actual stats, types, per-move data and the type chart (≈745 bytes). The GameCube holds no generated species or move tables and no game assets. Reasons:

- It is the simplest thing that works: one request, no table generator, no versioning between two copies of the data.
- It follows hack-specific data automatically. frontier-hack changes moves, species and types; the GBA already has the truth.
- It needs no official data in this repository.

Costs: about 0.47 s per battle start, and the core sees only the data it was sent. If later battles need large static tables, a hybrid (C) can add a build-time generated GC table keyed by the build id.

## GC battle core (`gamecube/battle`)

`battle_core.c` is pure C with no SDK and is unit tested on the desktop (`battle_test.c`).

- **Model:** Battle → Side (battlers, active positions) → Battler. Actions use Position / Target.
- **RNG:** xorshift32, seeded per battle on the GameCube. The GC is the authority for RNG, move resolution, turn order and damage.
- **Validation:** a move is usable when it is supported and has PP left. A wrong or stale input (turn number mismatch) is rejected and the GBA asks again.
- **Turn order:** priority (always 0 today), then Speed, then a random tiebreak.
- **Damage:** `((2L/5+2)·P·A/D)/50 + 2`, random 85–100 %, ×1.5 STAB, type chart (product of both types), minimum 1, 0 on immunity. Physical/special split. Accuracy check (0 = never misses).
- **PP and faint:** PP is consumed per use; at 0 HP the battler faints. WIN when the opponent side is gone, LOSE when the player side is. FORFEIT gives CANCELLED. No usable move gives UNSUPPORTED (Struggle is not implemented).
- **Refused before the battle starts:** format ≠ SINGLE, more than one Pokémon per side, a lead without supported moves, or bad type data. The reason is shown on both screens.

**Supported moves:** effect `EFFECT_HIT`, damaging, no additional effects, priority 0, single target. Spread moves (both foes / foes and ally) are also accepted, but only in SINGLE, where they hit the one foe at full power. Everything else carries an explicit reason bit (effect, secondary, priority, target, status move, multi-hit, other). It is greyed out on the GBA as `(unsupported)`, never approximated.

The battle core does **not** implement:

- Mega Evolution, Z-Moves, Dynamax, Terastallization
- Abilities, held items, status effects
- Secondary effects, weather, terrain, critical hits
- Switching, Struggle
- EXP / EV / level up / rewards
- Double / Triple / Rotation

## Turn protocol

The battle uses two APP opcodes and RESULT:

- **BATTLE_STATE (host → GBA):** phase (WAIT / CHOOSE / ENDED), turn, outcome, player slot, both HPs and max HPs, and the player's PP.
- **BATTLE_ACTION (host polls):** turn, action (move / forfeit), move slot, and an exit flag.

Result (`GcBattleResultV1`, RESULT version 1):
- Header: outcome, turns, counts, battle id.
- Per player battler: party slot, flags (fainted), HP, status, PP[4].
- Per opponent: HP.

The GBA validates it before accepting:

- battle id
- slot
- HP ≤ max HP
- each PP ≤ its max PP

With policy *apply*, `finish` writes HP and PP of that party member once. It does so only if the result was accepted, the outcome is not cancelled or unsupported, and the personality still matches. EXP, EV, items, money, BP and status are never written.

## frontier-hack content (`frontier-gc-battle`)

- **Entry points:** debug menu → Utilities → `GC Battle (show only)`, `GC Battle (apply HP/PP)`, `GC Battle (vs Steven)`. Scripts pass `VAR_0x8004` (policy) and `VAR_0x8005` (trainer). Nothing is added to master.
- **Opponent:** the lead Pokémon of the trainer's party, built with `CreateNPCTrainerPartyFromTrainer` into a mode-owned buffer.
- **Player battler (test rule):** the first party member that can battle and knows a supported move.

## Results (Dolphin 2609b, frontier save copy)

| Test | Result |
|---|---|
| Start from frontier-hack | Debug entry, GBA connects, `PH3 MODE id=2` |
| Request | 745 B (header 24 + 2×140 + 21×21 chart) in 0.46–0.49 s |
| Assets | 12 keys, 7,744 B cold in 2.49 s; 0 B / 14 ms from cache |
| Refusal | Lead with no supported move: "This mode cannot start: Lead has no supported move." (before the first-able-member rule was added) |
| Player | オオニューラ (Sneasler) Lv.100, slot 2: Slash supported; Agility / Screech / Close Combat unsupported |
| WIN | vs Youngster カズキ's ポチエナ Lv.5: Slash 2,270–2,587 damage, faint, WIN in 1 turn |
| LOSE | vs Steven's エアームド (Skarmory) Lv.77. Slash ×0.5 for 40–42; Aerial Ace ×2 (never misses) for 104–114. LOSE on turn 3; result: player 0/301, opponent 88/210, Slash PP 19 → 16 |
| GBA input | たたかう → move list (PP, unsupported marked) → choice. Input-to-turn 6–16 s, mostly human; the TV follows within one poll |
| Display only | Party unchanged; return screen "Result shown only; party unchanged." |
| Apply HP/PP | Slash PP 20 → 19, then 19 → 18 in a later battle, verified with the party viewer. Applied once per battle id |
| Forfeit (GBA B) | CANCELLED, nothing applied |
| GC START mid-battle | BYE, no result; GBA "No battle result; party unchanged." |
| Timeout (GC Z, 3 s) | GBA session resets, host NACKs then reconnects; the battle restarts from its request and the later win is applied once |
| Field return | Every run; the player walks normally afterwards |
| Digests | Start = end in every session (party, decrypted bag, flags, vars, SaveBlock1) |
| Save | Test save SHA-256 unchanged after all runs |

**Integration bug found and fixed:** in Dolphin, the first SI write after a pause of a few frames with no JoyBus traffic failed (`write seq=… attempt=0 word=0`). The battle host had stopped polling while it played turn events. It now polls every frame during playback, as the party viewer already did. While investigating, `session.c` gained two changes:

- A per-session reply timeout (Phase 1 keeps 120 ms; the companion uses 500 ms).
- A recorded reason for every failure.

Stale NACKs for an older sequence are now skipped like stale ACKs.

## Ready for Double? Before Triple/Rotation

The format field, active positions, Position/Target and the ALL_FOES target exist. Double needs:

- two active slots per side
- target selection on the GBA
- spread-damage reduction and ally hits
- faint replacement / switching

Before Triple or Rotation, the core also needs:

- **Engine features:** switching and replacement, Struggle, status conditions, end-of-turn handling, abilities/items (at least the common ones), secondary effects and stat stages, and critical hits.
- **Format rules:** position adjacency rules (Triple) and the rotate action (Rotation).
- **Data path:** a richer data feed, either more `BattleMove` fields or the hybrid table, plus a GBA UI for targets and party choice.
