# Terrain (M4-T, request V16)

Release 10 (`m4t-dev`), branches `milestone4t` and Frontier `frontier-gc-m4t`.
Built on the V15 field state ([weather](weather.md)) and the V14 shared
modifiers ([modifiers](modifiers.md)); V1–V15 requests keep their meaning
byte for byte. The design of record before this milestone was
[weather-terrain](weather-terrain.md).

## 1. Check of the design against M4-W and Expansion

Read in this Frontier build (pokeemerald-expansion, Gen 9 configs):
`TryChangeBattleTerrain`, `IsBattlerGrounded` /
`IsBattlerUngroundedByAbilityItemOrEffect`, `IsBattlerTerrainAffected`,
`CalcMoveBasePowerAfterModifiers`, `CanSetNonVolatileStatus`,
`CanBeConfused`, `CanPsychicTerrainProtectTarget` and the target-failure
canceler, `HandleEndTurnFirstEventBlock` and `ENDTURN_TERRAIN`
(`battle_end_turn.c`), `GetBattlerTotalSpeedStat`, `CalcDefenseStat`, the
surge abilities and the battle-start terrain.

| Item | Old design | This build | M4-T |
|---|---|---|---|
| Same terrain again | fails | a move fails; a surge ability does nothing (no notice, the count is not restarted) | as the build |
| Grassy heal | after the status residuals | first end-of-turn block, per Pokémon in speed order: the heal, then Shed Skin; before the status damage | as the build |
| Terrain countdown | after end-of-turn abilities | `ENDTURN_TERRAIN`, after the status damage and before Speed Boost: a terrain heals on its last turn, then ends | as the build |
| Earthquake / Bulldoze / Magnitude on Grassy Terrain | halved against grounded targets | ×0.5 when the target is not semi-invulnerable (no grounded check); Bulldoze uses `EFFECT_EARTHQUAKE` | a move record with `BPRED_EXPOSED` |
| Terrain power ×1.3 / Misty ×0.5 | modifiers | the "various effects" of the power chain: after the move's own factors, before the attacker's abilities; ×1.5 before Gen 8 (`B_TERRAIN_TYPE_BOOST`) | field records in the power chain between the move and the abilities |
| Psychic Terrain | priority moves fail | per target: priority > 0, the target affected and not the user's ally, the move not on all battlers / the opponents' field; checked after semi-invulnerability, before Protect | as the build (the two exempt target classes have no GC class and are refused) |
| Sleep / status / confusion | prevented | sleep: already asleep, Uproar, sleep clause, **Electric Terrain**, Sweet Veil, Insomnia; status: Comatose, **Misty Terrain**, Leaf Guard; confusion: already confused, Safeguard, **Misty Terrain**, Own Tempo; contact abilities go through the same check | the same order against the abilities the core runs |
| Surge Surfer / Grass Pelt | — | the terrain only (no grounded check) | `BPRED_TERRAIN` records |
| Battle start | — | overworld weather, then terrain | the start weather, then the start terrain |

No serious contradiction: the M4-W structure takes a second `FieldEffect`
and the corrections above are order and predicate choices, not a new
design. The weather order of M4-W is not reused for the terrain: each
terrain step sits where the build has it.

## 2. Three layers

1. **State** (GC): `Battle.field.terrain`, a second `FieldEffect` (kind,
   turns left, source kind and position) next to the weather. Weather and
   terrain have their own counts; neither change touches the other.
2. **Shared operations** (GC, `gamecube/battle/battle_field.c`):
   - `field_change`: the one writer of a field effect, used by
     `bc_weather_set` and `bc_terrain_set` (moves, surge abilities and the
     battle-start rule all go through `bc_terrain_set`, which only adds the
     same-terrain rule);
   - `battle_grounded`: the shared grounded rule (airborne type, then the
     `BABIL_UNGROUNDED` hook);
   - `bc_on_terrain`: terrain-affected = that terrain, grounded, not
     semi-invulnerable;
   - `bc_terrain_prevents` (status / sleep / confusion), `bc_terrain_heal`,
     `bc_terrain_end_turn`, `bc_terrain_blocks_move` (Psychic Terrain);
   - the predicates `BPRED_TERRAIN`, `BPRED_ON_TERRAIN`, `BPRED_EXPOSED` in
     the shared modifier executor, so a terrain boost is a record like any
     other.
3. **Game rules** (GBA, Frontier `ResolveTerrainRules` and the move /
   ability resolvers): which type is airborne, what each terrain prevents,
   the heal divisor, the power records and their percent, the terrain
   moves and abilities. The GC knows no move, ability or item id and no
   `B_*` config.

## 3. Request V16

Same layout as V15 plus a 72-byte terrain rules block after the V15 field
rules (`BATTLE_TERRAIN_RULE_BYTES`, in `BATTLE_REQUEST_MAX_BYTES`;
6 Pokémon a side and 21 types: 2,789 + 88 + 72 = 2,949 bytes):

| Bytes | Field |
|---|---|
| 0 | flags, 0 |
| 1–2 | battle-start terrain and turns (0 = no end; turns need a terrain) |
| 3 | airborne type (`BABIL_NO_TYPE` = none) |
| 4 | terrain record count (≤ 8) |
| 5–7 | reserved, 0 |
| 8–15 | per kind (electric, grassy, misty, psychic): heal divisor (0 or ≥ 2), flags `BTKF_NO_SLEEP`, `BTKF_NO_STATUS`, `BTKF_NO_CONFUSION`, `BTKF_BLOCK_PRIORITY` |
| 16–71 | 8 modifier record bodies (V13 format); unused ones are zero |

New in V16 (refused before V16 by the encoder, the decoder and the core):
effect kind `BEFF_TERRAIN` (21: a status move alone, target SELF, arg0 kind
1–4, arg2 turns 0–15, no KEEP), hook kinds `BABIL_ENTRY_TERRAIN` (35, arg0
kind 1–4, arg2 turns) and `BABIL_UNGROUNDED` (36, no arguments), predicates
`BPRED_TERRAIN` (7, a kind mask, no subject), `BPRED_ON_TERRAIN` (8, mask,
subject grounded and not semi-invulnerable) and `BPRED_EXPOSED` (9, subject
not semi-invulnerable, no argument), and the POWER stage for field records.
No new rule bit. A V16 request must carry `RULE_IGNORE_ITEMS` (V17 carries
the items instead, [items](items.md); the grounded
rule has no item input); without it `battle_init` refuses the battle
(`BI_ITEMS`). APP V16 keeps the V6 action layout.

The decoder takes the terrain block from the end of the request, checks
every byte (flags, kinds, turns, type, count, reserved bytes, divisors,
flag bits, each record and every record past the count being zero) and
refuses an unknown value; the encoder refuses V16 kinds in an earlier
version.

## 4. What is supported

| Terrain | Effect (grounded, not semi-invulnerable Pokémon unless noted) |
|---|---|
| Electric | Electric moves of the user ×1.3; no new sleep (shown) |
| Grassy | Grass moves of the user ×1.3; heals 1/16 of max HP at the end of the turn (not at full HP, at least 1, never past max); Earthquake and Bulldoze ×0.5 against a target that is not semi-invulnerable |
| Misty | Dragon moves ×0.5 against the target; no major status, no confusion (shown for a status move, silent for a chance effect, as an ability block) |
| Psychic | Psychic moves of the user ×1.3; a foe's move with priority > 0 fails against the target (per target, before Protect; PP is spent) |

Five turns from moves and surges; a new terrain replaces the old one.

| Moves | V16 export |
|---|---|
| Electric / Grassy / Misty / Psychic Terrain (`EFFECT_TERRAIN`) | `BEFF_TERRAIN`, 5 turns, the user stands for the field |
| Earthquake, Bulldoze (`EFFECT_EARTHQUAKE`) | a power record ×0.5 (`BPRED_TERRAIN` grassy, `BPRED_EXPOSED` defender) |

| Abilities | Hooks |
|---|---|
| Electric / Grassy / Misty / Psychic Surge | `ENTRY_TERRAIN`, 5 turns |
| Levitate | `UNGROUNDED` (with its Ground immunity) |
| Surge Surfer | speed ×2 record on Electric Terrain |
| Grass Pelt | Defense ×1.5 record on Grassy Terrain (physical) |

## 5. Not supported (refused, never silently inert)

Unsupported moves (not selectable; refused under `RULE_STRICT_MOVES`):
Rising Voltage, Expanding Force, Misty Explosion, Psyblade (terrain power),
Terrain Pulse, Grassy Glide, Steel Roller, Ice Spinner, Nature Power,
Camouflage, Secret Power, Floral Healing, Magnitude (random power, as
before), and the moves that change
grounding (Gravity, Magnet Rise, Telekinesis, Ingrain, Smack Down, Thousand
Arrows, Roost), which the exporter does not run. Unsupported abilities
(refused under `RULE_STRICT_ABILITIES`): Mimicry, Quark Drive, Hadron
Engine, Seed Sower and other terrain-setting or terrain-reading abilities
not listed in section 4. Items (Terrain Extender, the four seeds, Air
Balloon, Iron Ball) are not simulated: requests normalise items away
(`RULE_IGNORE_ITEMS`), and a V16 request without that rule is refused, so no
item can leave a Pokémon wrongly grounded. Form changes are not modelled.

## 6. Order and rounding (V16)

- Damage: as V15 (weather step after the spread factor, then random,
  STAB, type, burn, final chain).
- Power chain: move records (Solar Beam ×0.5 in rain; Earthquake ×0.5 on
  Grassy Terrain, a move record because the build has it in the move-effect
  switch) → **terrain records** → Technician-type first-only records
  → attacker records → defender records; each a 4.12 factor chained half
  up, as V14. Example (native test): ×0.5, ×1.3, ×1.2 gives 4096 → 2048 →
  2663 → 3195, so a 116-power move becomes 90 (the terrain after the
  ability would give 91).
- Defense chain: V14, then the V15 field records (Grass Pelt is an
  ability record).
- Speed: stages → speed records (Surge Surfer ×2) → paralysis.
- Move targets: semi-invulnerable → **Psychic Terrain** → Protect →
  accuracy → ability immunity.
- Status: existing status → type → freezing in sun → **Electric Terrain**
  (sleep) → ability immunity → **Misty Terrain**. Confusion: confused →
  **Misty Terrain** → Own Tempo.
- End of turn: weather countdown and weather residuals → per Pokémon in
  speed order: **Grassy heal**, then Shed Skin → status residuals →
  **terrain countdown** → Speed Boost.
- Battle start: abilities that run at the start → start weather → **start
  terrain** → entry abilities in speed order (the slowest surge's terrain
  stays). Without abilities (`RULE_IGNORE_ABILITIES`) the start weather and
  terrain still come.

## 7. Verification

| Check | Result |
|---|---|
| Native (`battle_test.c`, -O2 and ASan + UBSan) | all suites pass; new `test_v16_*`: wire and validation, set / same / replace / end / start, surges (speed order, same terrain silent, replacements, switching back in), the grounded rule, status / sleep / confusion and their order against abilities and contact abilities, Psychic Terrain per target (spread, ally, airborne, priority 0, status moves, before Protect), damage records, Surge Surfer, the orders of section 6, the item rule, 240 random terrain battles in four formats run twice (identical events, state, RNG), and `test_v16_expansion_reference` |
| Mutations (42 changes to the terrain code: grounded rule, semi-invulnerable, same terrain, countdown, heal guards and cap, priority block conditions and position, status / confusion order, chance notice, contact status, surge notice, heal / Shed Skin order, countdown position, start order, `BPRED_EXPOSED`, `BPRED_ON_TERRAIN`, field pass position, version gates, every terrain-rule check, item rule) | all 42 caught (the M4-W weather set: 17 of 17) |
| Frontier `make check` | 6,841 tests; the 6,828 (name, status) pairs of the M4-W run unchanged, 13 new PASS; FAILED 2,455 in both (pre-existing, mostly Japanese-text MESSAGE checks) |
| Expansion reference (`make check TESTS="GC V16 reference"`) | 13 tests: 9 damage cases × 3 rolls = 27 of 27 values equal to the GC (Electric / Grassy / Psychic boosts, no boost for a Flying user, Earthquake halved, Misty Dragon halving grounded / Flying, rain + Grassy Solar Beam, Grass Pelt); Psychic Terrain per target in a Double, Electric Terrain stops Spore, Misty Terrain stops Toxic, Grassy heal before the poison damage. Each check was confirmed to fail with the no-terrain value |
| V12 / V13 / V14 / V15 traces (1,200 battles each) | identical to M4-W (V15 total `efa20edd`) |
| Dolphin Lab L37–L42 (V16) | L37 Single (Electric Surge, Hypnosis stopped, Psychic Surge replaces it, Quick Attack stopped), L38 Double (Grassy: Earthquake halved on Gogoat with Grass Pelt, Levitate and Flying immune, heals), L39 Triple (Misty: Flying Dragonite poisoned, Confuse Ray / Thunder Wave stopped on grounded, Dragon Claw halved only on grounded), L40 Rotation (Psychic: three priority moves stopped across rotations), L41 = L38 with the Gen 7 ×1.5 record and the same seed (only the three Energy Ball hits differ: 55→61, 93→108, 27→31), L42 refused by the GBA (Rising Voltage under strict rules); L37–L41 replay natively with 0 mismatches |
| Dolphin Lab L01–L36 | request bytes, events, actions and results line for line equal to the M4-W runs |
| Final DOL | the Dolphin rows above ran on the DOL built before the `BI_ITEMS` check was added (it only refuses V16 requests without `RULE_IGNORE_ITEMS`, which the GBA never sends); on the final DOL L01, L37 and L38 were run again and are line for line equal, and every lab log replays natively on the final core with 0 mismatches |
| Debug menu V1–V12, Cube Tester V12 (4 formats) | all reached a result; requests 925 / 1,717 / 1,701 / 1,701 B as in M4-W |
| Result contract (V16) | link cut 3 s on the result screen → "result already final … not restarting" |
| Save | Frontier save AFF1EB7B… before and after |
| JoyBus (65 sessions) | GBA 311,739 requests, CRC 0, overflows 0, 137 stale drops, 144 NACKs; GC 157 retries, 10 timeouts, all recovered by a retry; the one link loss is the deliberate cut |

## 8. Memory, link and time

| Item | M4-W | M4-T |
|---|---:|---:|
| `BattleRequest` (GC and GBA heap) | 5,852 B | 5,912 B |
| `Battle` (GC, x86-64 host build) | 4,184 B | 4,184 B |
| `EventLog` | 46,612 B | 47,812 B |
| Request buffer `BATTLE_REQUEST_MAX_BYTES(6, 21)` | 2,877 B | 2,949 B |
| ELF text / data / bss | 271,750 / 67,156 / 2,995,392 | 276,686 / 68,676 / 3,002,048 |
| DOL | 339,168 B | 345,664 B |
| Frontier ROM used | 26,354,912 B | 26,358,580 B |
| EWRAM | 231,984 B | 231,984 B |
| A V16 request | — | V15 size + 72 B (L37 1,421 B / 1,061 on the wire; L38 1,765 / 1,285) |
| GC core per turn (Dolphin, peak) | up to 210 µs | V16 up to 162 µs (Triple, Rotation); overall peak 215 µs (V15 regression) |
