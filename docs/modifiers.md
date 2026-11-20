# Shared damage modifiers (M4-P, request V14)

M4-P turns the V13 proof of concept ([rule-poc](rule-poc.md)) into the base
for later milestones. It changes three things and nothing else:

1. **Memory:** an effect or ability hook is 8 bytes in memory, as on the
   wire. The PoC kept a 7-byte modifier next to the legacy fields of every
   slot.
2. **Arithmetic:** request **V14** computes damage as the Gen 5+ formula of
   pokeemerald-expansion does (4.12 fixed-point modifier chains, rounded
   once, spread and semi-invulnerable factors in their places). V1–V13 keep
   their arithmetic; V14 has the V13 layout.
3. **Export:** the Frontier exporter takes modifier records from game data
   through one table-driven section, separate from the Ability Lab's own
   definitions.

Weather, terrain, held items, per-ROM manifests and memory card storage are
not part of M4-P.

## 1. Memory

### Why V13 grew

| Struct | V12 | V13 PoC | Cause |
|---|---:|---:|---|
| `BattleEffect` | 7 B | 14 B | `BattleModifier mod` (7 B) added next to `who, chance, arg0..2, flags` |
| `BattleAbilityHook` | 8 B | 15 B | the same 7 B added next to the V12 fields |
| `BattleMove` | 56 B | 98 B | 6 effects |
| `BattleRequest` (GC, GBA) | 5,488 B | 7,840 B | 12 Pokemon × (4 moves × 6 effects + 4 hooks) × 7 B = 2,352 B |
| `Battle` (GC) | 3,784 B | 5,800 B | each `Battler` copies its 4 moves: 12 × 4 × 6 × 7 B = 2,016 B |

Only one of the two forms is ever used per slot (the kind decides), so the
PoC carried 7 unused bytes in every slot.

### Options compared

| Option | Request / Battle (GC) | Change to code | Risk |
|---|---|---|---|
| PoC: modifier beside the fields | 7,840 / 5,800 B | none | memory only |
| **Union, wire-shaped (chosen)** | **5,776 / 4,072 B** | kind-gated reads; `flags` shared | a read of the wrong form; covered by tests (below) |
| Separate table (records in a per-request pool, the slot holds an index) | about 5,500 + pool (e.g. 32 × 7 = 224 B) | index allocation in encoder, decoder, exporter, replay tool; pool-full error | new limit and failure mode; two places to keep consistent |
| Bit-packed 7-byte union (predicate kind in 4 bits) | 5,488 / 3,784 B | packing in every reader | the record vocabulary is capped; harder to read in logs |

The union adds 1 byte per effect over V12 (the record needs 6 bytes, the
legacy effect 5) and nothing per hook: +288 B on each struct against V12,
-2,064 B / -1,728 B against the PoC. A pool would save those 288 bytes but
adds a capacity limit and index bookkeeping to every producer and consumer;
bit packing saves them too but limits future predicates. Neither is worth
288 bytes.

### Layout

```c
typedef struct {                        // 6 bytes: record bytes 1, 2, 4..7
    uint8_t stage, percent;
    BattlePredicate pred[2];
} BattleModifier;

typedef struct {                        // 8 bytes
    uint8_t kind;
    union {
        struct { uint8_t who, chance, arg0; int8_t arg1; uint8_t arg2; };
        BattleModifier mod;             // BEFF_MODIFIER
    };
    uint8_t flags;                      // BEFL_* (0 for BEFF_MODIFIER)
} BattleEffect;

typedef struct {                        // 8 bytes
    uint8_t kind;
    union {
        struct { uint8_t arg0; int8_t arg1; uint8_t arg2, chance, chance_den, percent; };
        BattleModifier mod;             // BABIL_MODIFIER
    };
    uint8_t flags;                      // BAHF_* (BAHF_BREAKABLE only for BABIL_MODIFIER)
} BattleAbilityHook;
```

- The wire format is unchanged (8-byte records; the modifier's flags byte is
  record byte 3, mapped to the slot's `flags`).
- The legacy field order is unchanged, so positional initializers keep their
  meaning (they now need the inner braces, `-Wmissing-braces`).
- `kind` and `flags` are valid for every kind; the union is read only after
  the kind is checked. `flags` is shared, so generic code that tests flags
  (`bc_ability_next`, effect timing, the exporter's breakable flag) is
  correct for records too.
- The decoder clears the whole slot before filling it, so no stale byte of
  the other form remains.
- Before V13 the modifier kind keeps its V12 meaning: on a supported move it
  is refused (its legacy bytes never formed a valid effect), on an
  unsupported move the bytes are kept. A test feeds V12 bytes that would
  form a valid record if misread.

### Measured

| Item | M4-A2 (V12) | PoC (V13) | M4-P |
|---|---:|---:|---:|
| `BattleEffect` / `BattleAbilityHook` | 7 / 8 B | 14 / 15 B | 8 / 8 B |
| `BattleRequest` (GC; GBA heap while building the request) | 5,488 B | 7,840 B | 5,776 B |
| `Battle` (GC) | 3,784 B | 5,800 B | 4,072 B |
| GC `companion.elf` text / data / BSS | 253,510 / 61,232 / 2,988,928 B | 256,454 / 61,712 / 2,993,312 B | 257,310 / 61,920 / 2,989,504 B |
| GC `companion.dol` | 315,008 B | 318,432 B | 319,488 B |
| Frontier ROM used | 26,340,772 B | 26,343,488 B | 26,345,828 B |
| Frontier EWRAM (static) | 231,984 B | 231,984 B | 231,984 B |
| Frontier request buffer (`REQUEST_CAPACITY`) | 2,789 B | 2,789 B | 2,789 B |

The request buffer and the wire did not change: the largest request is
still `BATTLE_REQUEST_MAX_BYTES(6, 21)` = 2,789 bytes.

## 2. Damage calculation in pokeemerald-expansion

Read from the Frontier build's `src/battle_util.c` and `include/fpmath.h`
(the expansion version vendored there), not from documentation.

### Fixed point

- Modifiers are `uq4_12_t`: value × 4096. `UQ_4_12(n)` rounds to the
  nearest (1.5 → 6144, 1.3 → 5325, 1.2 → 4915, 0.33 → 1352). Item boosts
  given as a percent use `PercentToUQ4_12_Floored` (Muscle Band 10% →
  4096 + 409), gems `PercentToUQ4_12AddOne` (rounded).
- Chaining: `uq4_12_multiply` = (a × b + 2048) >> 12 (half up);
  `uq4_12_multiply_half_down` = (a × b + 2047) >> 12.
- Applying: `uq4_12_multiply_by_int_half_down(m, v)` = (m × v + 2047) >> 12,
  the nearest integer, an exact half down. (×1.5 and ×0.5 therefore equal a
  floor; ×0.25 and ×0.75 do not.)

### Order (`DoMoveDamageCalcVars`)

| Step | Function | Arithmetic |
|---|---|---|
| Base power | `CalcMoveBasePower` | integer (Hex: `basePower *= 2`) |
| Power modifiers | `CalcMoveBasePowerAfterModifiers` | chain **half up**, applied once; order: move effect (Facade, Brine, Retaliate ...) → various (Helping Hand, gems, terrain, sports, Charge) → attacker's ability (Technician, Toxic Boost ...) → field abilities (auras) → attacker's partner → target's ability (Heatproof, Dry Skin) → attacker's item (Muscle Band, plates) |
| Attack | `CalcAttackStat` | stage ratio (integer), then chain **half down**: attacker's ability (Huge Power, pinch, Guts, Defeatist) → target's ability (Thick Fat) → ally (Flower Gift) → ruin → attacker's item (Choice Band) |
| Defense | `CalcDefenseStat` | stage ratio, then chain **half down**: target's ability (Marvel Scale, Fur Coat; Paradox abilities half up) → ally → ruin → target's item (Eviolite, Assault Vest) → sandstorm Sp. Def for Rock types |
| Base damage | `CalculateBaseDamage` | power × atk × (2L/5+2) / def / 50 + 2 |
| Spread | `GetTargetDamageModifier` | ×0.75 (2+ targets in a double), applied |
| Parental Bond, **weather**, critical hit, Glaive Rush | | each applied separately |
| Random | | × (100 − 0..15) / 100 |
| Same-type bonus | `GetSameTypeAttackBonusModifier` | ×1.5 / ×2.0 (Adaptability), applied |
| Type | `CalcTypeEffectivenessMultiplier` | one 4.12 factor (×0.25 … ×4), applied |
| Burn / frostbite | `GetBurnOrFrostBiteModifier` | ×0.5 (not with Guts, not for Facade from Gen 6), applied |
| Final modifiers | `GetOtherModifiers` | chain **half down**, applied once; order: Minimize, underground (Earthquake on Dig), Dive, airborne → screens → Collision Course; then **by unmodified Speed** (attacker ≥ defender): attacker's ability (Sniper, Tinted Lens) → target's ability (Multiscale, Filter) → target's partner → attacker's item (Life Orb, Expert Belt) → target's item (resist berries); a slower attacker puts the target's group first |
| Minimum | | damage 0 → 1 |

Overlaps: within a stage, abilities, items and field effects share one
chain in the order above (the result depends on the order: fixed-point
multiplication is not associative). Weather is not a chain member: it is
its own step between the spread and the random factor; terrain is a power
modifier; the sandstorm Sp. Def boost is the last defense modifier.

Several targets and several hits: each target and each hit runs the whole
calculation; the spread factor depends on the number of targets, not on the
hit; Parental Bond's second hit is ×0.25 at the spread step.

### What the GC did before V14

| Point | V1–V13 | Expansion |
|---|---|---|
| Several modifiers in one stage | each floored | one chain, rounded once |
| Spread ×0.75 | after the whole calculation and the minimum, floored | after the base damage, rounded |
| Semi-invulnerable ×2 | after the spread, on the rounded damage | first member of the final chain |
| Type ×0.25 / ×0.75 | floored | rounded (×0.25 of 7 = 2, not 1) |
| Same-type ×1.5 / ×2, burn ×0.5, ×2, ×4 | floored | rounded; equal, since the fraction is 0 or .5 |
| Source order in a stage | V12 kinds first, then the move's, attacker's, defender's records | move, attacker, defender (defense: defender first); final by speed |

## 3. V14

`BATTLE_REQUEST_VERSION_FIXED_POINT` = 14, `BATTLE_APP_VERSION_FIXED_POINT`
= 14. Same request and action layout as V13; only the arithmetic differs.

- Each stage collects its factors into one 4.12 chain (percent → nearest
  4.12 value), the power stage rounding half up, attack, defense and final
  half down, and applies it once (nearest, half down). Base power stays
  integer (×2 for Hex is exact either way).
- The spread factor (×0.75) follows the base damage; same-type bonus, type
  and burn are applied as 4.12 factors; the semi-invulnerable ×2 heads the
  final chain. The caller no longer applies them afterwards.
- Source order: power — move, attacker (LOW_POWER first-only, then
  records), defender; attack — move, attacker (ATTACK_MULT, PINCH
  first-only, records), defender (RESIST first-only, records); defense —
  move, defender (DEFENSE_MULT, records), attacker; final — move, then the
  faster Pokemon's records (the attacker's on a tie).
- What the V12 hook kinds mean (conditions, first-only) is unchanged. A
  stat or the power stays at least 1, as in V13 (Expansion has no minimum
  there; only an unrealistic factor below 1/4096 could reach 0).

### Checked against Expansion itself

`test/battle/gc_damage_reference.c` (Frontier branch) runs eleven hits in
the Frontier build's own battle engine through its test runner (fixed
stats, the damage roll rigged to 100 / 93 / 85 %, no critical hit) and
expects the GC's V14 values; `test_v14_expansion_reference` in
`battle_test.c` computes the same table on the GC core.

| Case | Expansion = V14 (100/93/85 %) | V13 |
|---|---|---|
| Ice Fang ×4 STAB (the Gen 5 example of the existing test) | 196 / 180 / 168 | same |
| Guts ×1.5 and Thick Fat ×0.5 (attack ×0.75) | 33 / 30 / 28 | 32 / 29 / 27 |
| Blaze at 1/3 HP and Thick Fat | 52 / 48 / 43 | same |
| Defeatist and Thick Fat (attack ×0.25) | 17 / 15 / 14 | 16 / 14 / 13 |
| Double resistance ×0.25 | 11 / 10 / 9 | 10 / 9 / 9 |
| Rock Slide spread in a double | 60 / 55 / 51 | 59 / 54 / 50 |
| Technician on each hit of Double Kick | 66 / 60 / 56 | same |
| Hex against a poisoned target | 111 / 102 / 93 | same |
| Brine at half HP, then Flare Boost | 107 / 99 / 90 | same |
| Toxic Boost | 73 / 67 / 62 | same |
| Earthquake on a Pokemon underground | 170 / 158 / 144 | same |

All 33 values match. 11 of them differ from V13. With the V13 values put
into the test, the Expansion run fails exactly there (`EXPECT_EQ(33, 32)`,
`EXPECT_EQ(60, 59)`), so the test can detect a difference.

## 4. Shared modifiers on the GBA

Two sources, kept apart in `src/frontier_gc_battle.c`:

| Source | Where | Used by | Records |
|---|---|---|---|
| Game data | "Game data -> modifier records": `ExportMoveModifier`, `sAbilityModifiers` | every battle | Hex / Venoshock (V13+); Brine, Toxic Boost, Flare Boost, Defeatist (V14+) |
| Ability Lab | "Ability Lab only": `sLabAbilities` | `BuildLabEntries` only (a `LabMon` with `custom`) | the PoC's custom abilities |

- Each game-data entry names the Expansion function whose modifier it
  reproduces. Requests below V14 get the V13 export (Hex only), so V13
  requests are byte-identical to the PoC.
- The V12 hook kinds stay as they are (Huge Power, pinch abilities,
  Technician, Thick Fat, Guts, Marvel Scale ...): they already run through
  the shared executor, and converting them would change nothing but the
  bytes. Abilities and moves with rules that are not multipliers stay code
  (Facade's burn exception, Adaptability's STAB replacement, Sturdy,
  absorb, immunity, type pass).
- Lab definitions keep the holder's game ability id (the decoder checks it
  against the record); they exist only in the debug fixtures.

## 5. Verification

Native (`battle_test.c`, -O2 and ASan + UBSan): all suites pass, including
- `test_v14_arithmetic`: 4.12 conversion and rounding, the power chain
  rounding half up and the stat chains half down (7988 / 7987 for
  1.5 × 1.3), rounding once (101 × 1.5 × 0.5 = 76, V13 75), minimums,
  integer base power, source order in attack, defense and final, the speed
  order with a tie, the semi-invulnerable factor, the largest chains;
- `test_v14_order`: orders that matter only with three or more factors
  (Technician after the move's records, PINCH before records, RESIST
  between the attacker's and defender's records, the move's final records
  before the faster ability) and a same-type bonus of 133 %;
- `test_v14_battle`: a V14 double battle's spread hit and a V14 Earthquake
  on Dig apply their factor once;
- `test_v14_union_decode`: the decoder writes the whole slot; the V12
  meaning of the modifier kind;
- `test_v14_expansion_reference` (above).

Mutation testing (33 mutants of the new code): 30 killed. The 3 survivors
are equivalent: burn ×0.5 rounded equals floored; the chain cap cannot be
reached by a valid request (the largest chain is 2 × 2.55¹³ in 4.12); the
encoder writes the flags of a valid effect record, which are always 0.

Trace: the 1,200-battle V12 corpus is identical on M4-A2, the PoC and M4-P
(V12 labels), and identical on the PoC and M4-P with V13 labels; with V14
labels 369 of 1,200 battles change.

Dolphin (release 8 DOL and ROM, Frontier save):

| Check | Result |
|---|---|
| Lab L26-L29 (V14: Single, Double, Triple, Rotation) | all ran; each replays natively with 0 mismatches; requests 853 / 1,205 / 1,557 / 1,565 B in 0.44-0.54 s |
| L26-L29 as V13 (`-version 13`, same bytes) | L27's spread Rock Slide 39 -> 38 on one foe; the other hits are equal at these levels |
| Lab L01-L25 | request bytes, events, states and results line for line equal to the PoC runs (L24 differs only by the player's target, an input; both logs replay with 0 mismatches); L20 still refused under strict rules |
| PoC and M4-P logs replayed on the final M4-P core | 54 logs: 52 with 0 mismatches, the two L20 logs refused at init as in Dolphin |
| Debug menu V1-V12 | all reached a result; no link loss |
| Cube Tester V12 | Single, Double, Triple, Rotation won (4 / 13 / 9 / 14 turns), requests 925 / 1,717 / 1,701 / 1,701 B as in M4-A2 |
| Result contract | V14 battle, 3 s silence on the result screen: the reconnect reports "result already final", no restart |
| Save | Frontier save AFF1EB7B... before and after |
| JoyBus (46 sessions) | GBA: 102,586 requests, CRC 0, overflows 0, 66 stale drops, 67 NACKs; GC: 78 retries, 8 single-frame timeouts (each recovered by one retry) |
| GC core per turn | 26-89 us peak (PoC 26-90 us) |
| Native, 1,200 V12 battles | 47 ms (M4-A2 46, PoC 49); V14 labels 48 ms |

Frontier `make check` (same tree and toolchain as the PoC baseline
`frontier-battle-rule-poc`): 6,817 tests. Each of the 6,806 baseline
entries has the same name and status (PASS 3,890 -> 3,901 with the 11 new
reference tests, FAILED 2,455 both); the failures are the existing ones
(mostly MESSAGE checks against the Japanese text).

## Known limits

- Not modelled by the GC at all: critical hits, weather, terrain, items,
  screens, Parental Bond, Tera (rules normalise them away); V14 places
  them in the formula only as far as this document describes.
- Expansion converts some item percents with a floor and some Paradox
  defense modifiers chain half up; a record has one conversion and its
  stage one chain rounding. Items will need a flag for the floored
  conversion (a free bit in the record's flags byte).
- Weather needs its own step (between spread and random), not a chain
  member; M4-W has to add it as a stage of the core, not as a predicate.
- The confusion self-hit keeps its own formula in every version.
- Hex does not treat Comatose as sleep; Facade stays unsupported.

## V17: truncated factors (M4-B0, [items](items.md))

Stage bit 6 (`BMOD_FLOORED`) is defined from request V17 for move, ability
and item records on the fixed-point stages (POWER, ATTACK, DEFENSE, FINAL,
WEATHER): the percent becomes its 4.12 factor by truncation
(`PercentToUQ4_12_Floored`, `UQ_4_12_FLOORED`: 110 -> 4505) instead of the
nearest value (4506). Before V17 the bit stays reserved and is refused; the
field records keep their V15/V16 checks. V17 adds the attacker's item
records (`BMSRC_ITEM`) at the end of the POWER chain, after both abilities.
