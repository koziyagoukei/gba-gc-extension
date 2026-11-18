# Data-driven modifiers (proof of concept, request V13)

Status: **proof of concept** on branches `battle-rule-poc` (this repository)
and `frontier-battle-rule-poc` (Frontier), release 7 `rule-poc`. Nothing
here is merged into a public branch. The question it answers: can the GBA
define a conditional effect as data that the GC runs with code it already
has, for moves and abilities alike, without changing what V1–V12 mean?

Not built (as decided in the 2026-11-18 review): a general VM, an
independent rule engine, separate definition sets, a per-ROM manifest, a
rule cache, memory card storage, and any weather, terrain or held-item rule.

## Impact on V12 (studied first)

- The V12 damage multipliers were five hook kinds read in three functions
  (`bc_ability_attack/defense/power`), each with its own condition code:
  ATTACK_MULT (category, optional type, holder status or state), PINCH
  (type and HP), LOW_POWER (base power), RESIST (one of two types, the
  defender's ability), DEFENSE_MULT (category, holder status). Moves had no
  damage modifier at all; Hex, Venoshock, Facade and Brine were unsupported.
- Each kind decided its conditions in code, so "HP at most 1/2 **and** a
  status" needed a new kind, a new function and a new DOL.
- The order inside a stage matters for rounding (each step floors). V12
  applies every ATTACK_MULT, then the first PINCH, then the defender's first
  RESIST; PINCH, LOW_POWER and RESIST use only the first hook of their kind.
  A shared executor must keep that order to keep V12 results.
- The effect and hook records are 8 bytes and their legacy fields have
  per-kind meanings (`flags` carries the V10 timing for every effect). A new
  record therefore gets its own body instead of reusing those fields.
- The Frontier request buffer had been sized by hand (the M4-A2 bug). V13
  does not change any block size, but the buffer is now sized by the
  protocol's own formula.

No compatibility problem or large rework was found, so the minimal design
below was built.

## The record (V13)

One record type for both places, `BattleModifier`, 7 bytes after the kind
byte, in an 8-byte effect (`BEFF_MODIFIER` = 16) or hook (`BABIL_MODIFIER` = 30):

| Byte | Field |
|---|---|
| 1 | stage (bits 0–2), move category 0 any / 1 physical / 2 special (bits 3–4), bit 7 = applies while the holder is the defender (abilities only) |
| 2 | percent 1–255 |
| 3 | flags: `BAHF_BREAKABLE` (abilities, carried), 0 for moves |
| 4–5 | predicate 0: kind (bits 0–5), bit 7 = subject is the defender; argument |
| 6–7 | predicate 1 (same) |

Both predicates must hold (AND); an empty one holds. Predicates:

| Kind | Holds when | Where |
|---|---|---|
| `HP_AT_MOST` (1) | subject HP ≤ max HP / arg (arg ≥ 2) | moves, abilities |
| `STATUS` (2) | subject's major status is in arg (mask of 1 << BST_*) | moves, abilities |
| `MOVE_TYPE` (3) | the move's type is arg; never for typeless Struggle | abilities (a move's own type is known to the GBA) |
| `POWER_AT_MOST` (4) | the base power after the base-power stage ≤ arg | abilities, not at the base-power stage |

The move category is part of the stage byte rather than a predicate: it is
what V12 ATTACK_MULT / DEFENSE_MULT already filtered on, and it leaves both
predicate slots for conditions (the custom ability below needs HP, status
and physical). Unknown kinds, reserved bits, unpacked predicates, a
category or holder role on a move record, and any old field set on a
modifier record make the request malformed. A modifier never draws a
random number, so a predicate that fails costs nothing.

### Stages: what the record cannot change

| Stage | Applies to | Rounding, minimum | Sources in order |
|---|---|---|---|
| BASE_POWER (1) | the move's power, first | floor, min 1 | move records, attacker's, defender's |
| POWER (2) | the power after BASE_POWER | floor, min 1 | V12 LOW_POWER (first), move, attacker, defender |
| ATTACK (3) | attacking stat after stages | floor, min 1 | V12 ATTACK_MULT (all), PINCH (first), RESIST (defender, first), move, attacker, defender |
| DEFENSE (4) | defending stat after stages | floor, min 1 | V12 DEFENSE_MULT (all), move, attacker, defender |
| FINAL (5) | damage after type and burn | floor, no minimum between steps; the damage minimum of 1 follows | move, attacker, defender |

`POWER_AT_MOST` reads the power after BASE_POWER, so a Technician-like
record and V12 Technician see Hex's doubled power, as the game does
(`CalcMoveBasePower` before the Technician check). Stage placement, order,
rounding and minimum live in one table (`battle_modifiers.c`), not in data.

### Version rules

- V1–V12 decode exactly as before: the V13 body is parsed only in a V13
  request; in an earlier request kind 16 / 30 is an unknown kind as before.
- The encoder refuses a V13 record in an earlier version (returns 0).
- A V12 battle never runs a move record even if one reaches the damage
  code (the core checks the request version); `bc_effects_supported`
  marks such a move unsupported.
- Block sizes do not change. `BATTLE_REQUEST_MAX_BYTES(entries, types)`
  gives the largest request; the Frontier buffer is defined with it, and the
  tests check that the largest V12 and V13 requests are exactly that size,
  fit a buffer of that size and are refused (nothing written) one byte shorter.

## One path for moves and abilities

`bc_calculate_damage` calls `bc_modify(&context, stage, value)` once per
stage. `bc_modify` walks the stage's sources; a move record (`run_move`)
and an ability record (`run_hooks`) both become the same `Mod` through
`from_record`, are checked by the same `holds` / `pred_holds` and applied
by the same `apply`. The V12 multiplier hooks are translated by `from_hook`
into the same `Mod`, so they use the same three functions too:

| V12 hook | As a modifier |
|---|---|
| ATTACK_MULT | ATTACK, category arg0, MOVE_TYPE arg2 if TYPE_FILTER |
| PINCH | ATTACK, MOVE_TYPE arg0, attacker HP_AT_MOST arg2 |
| RESIST | ATTACK, holder = defender, MOVE_TYPE arg0 or arg2 |
| DEFENSE_MULT | DEFENSE, holder = defender, category arg0 |
| LOW_POWER | POWER, POWER_AT_MOST arg0 |

The V12 holder flags (REQUIRES_STATUS / REQUIRES_STATE) still select the
hook in `bc_ability_next`. Not translated, because they are not multipliers:
STAB_MULT (replaces the 1.5), IGNORE_BURN (cancels a rule), TYPE_PASS (type
chart), Sturdy (HP cap), ACCURACY and SECONDARY_CHANCE (other formulas).

## GBA side (Frontier)

- Hex, Venoshock and any `EFFECT_DOUBLE_POWER_ON_ARG_STATUS` move without a
  status cure: `BASE_POWER x2 while the defender's status is in the move's
  status argument`, the mask taken from the move table. V13 only (V12 keeps
  them unsupported). Comatose counting as sleep is not modelled (Comatose
  is not simulated).
- The custom ability exists only in the lab table:
  `POWER (physical) x2, HP_AT_MOST 2 AND STATUS any` (`LAB_CUSTOM_LOW_HP_STATUS`)
  and its redefinition `POWER (physical) x1.5, HP_AT_MOST 2`
  (`LAB_CUSTOM_LOW_HP`). The holder keeps its game ability id (Rattata's
  Run Away) as the record's source.
- Lab fixtures (Utilities → GC Ability Lab..., fixed seed):

| Fixture | Format | Shows |
|---|---|---|
| L21 | Single, 2 each | Hex x2 on a poisoned Machop, x1 on the next one |
| L22 | Double | custom ability: poisoned Rattata at 60% (crosses 1/2 by poison), unpoisoned Rattata at 40% |
| L23 | Double | L22 with the redefined ability and L22's seed |
| L24 | Triple | Hex on a burned and an unstatused Machop, custom ability in the center |
| L25 | Rotation | Hex from the front on the poisoned front Machop |

## Evaluation

### What a new effect costs

| Change | V12 way (a kind per rule) | V13 records |
|---|---|---|
| Hex / Venoshock (power x2 on a status) | new effect kind or move flag; protocol enum, validation and version gate; a branch in `bc_calculate_damage`; tests; GBA mapping; new DOL and release | GBA mapping only (the status mask comes from the move table) |
| "HP at most 1/2 AND status: physical power x2" | new hook kind (V12 has no AND of HP and status); protocol enum, name, validation; a GC function and its call site; tests; GBA mapping; new DOL | GBA definition only (L22) |
| Redefine that ability (x1.5, HP only) | GC code change and new DOL | GBA definition only, same DOL (L23) |
| A rule outside the vocabulary (weather, "super effective", grounded, a new stage) | new kind | one new predicate or stage in the GC and the protocol (still a version), then data |

The PoC infrastructure itself was one change on both sides (protocol
+158 lines, `battle_modifiers.c` 151 lines, 25 lines of the old functions
removed, GBA exporter +114 lines). After it, every rule in the vocabulary
(5 stages × category × 4 predicates × 2 subjects, AND of two) is data.

### Reuse

- Moves and abilities share one record type, one codec
  (`battle_modifier_put/get`), one validity function and one execution path
  (`from_record` → `holds` / `pred_holds` → `apply`). The V12 multiplier
  hooks run through the same path after `from_hook`.
- Held items (M4-B) fit as a third source per stage (Choice Band: ATTACK
  150; Charcoal: POWER 120 with MOVE_TYPE; Life Orb: FINAL 130). They need
  an item block in the request and a holder source in the pass table, not a
  new executor. Expert Belt needs a "super effective" predicate.

### What stayed special, and what got more complex

- Not multipliers, still code: STAB replacement (Adaptability), the burn
  exception (Guts; also Facade from Gen 6, so Facade is still unsupported),
  type-chart changes, Sturdy, accuracy and secondary-chance scaling, the
  spread x0.75 and the semi-invulnerable x2.
- The V12 order had to be kept as fixed passes, including "first hook only"
  for PINCH, LOW_POWER and RESIST. A data-only design would have changed
  rounding for unusual V12 data; the pass table makes that order explicit
  but is a rule a reader must know.
- Two "defender" bits (the holder's role and a predicate's subject) and
  per-place rules (a move record may not filter on its own type or
  category) are easy to misread; the validity function enforces them.
- The decoded record lives next to the old fields of every effect and
  hook, so the structures grew (see sizes). A union or a separate per-move
  modifier table would remove most of that.
- Rounding model: the game multiplies most modifiers into one 4.12 fixed
  point factor and rounds once; the GC (V12 and V13) floors after each
  step. With more stacked sources (weather + item + ability) the two can
  differ by 1. This is not new in V13, but data makes stacking common, so
  the stage table should decide its rounding (step or chained) before M4-B.

### Testability

Each predicate and stage is tested once and then covers every move and
ability that uses it. The trace tool compares whole cores on 1,200 V12
battles; the replay tool checks a Dolphin run line by line against the
native core and gives the counterfactual (same bytes, seed and commands,
records removed). Both are new and reusable for M4-W/T/B.

### Weather and terrain

The damage side of weather and terrain fits: sun/rain Fire/Water
x1.5/x0.5 and terrain boosts are POWER or FINAL records with a new
`WEATHER` / `TERRAIN` predicate (and `GROUNDED`), from a field source in
the pass table; Swift Swim-like speed needs a SPEED stage in turn order.
The field state itself (one writer, durations, replacement, residual
damage, setters) is a state machine, not a modifier, and stays code as in
[weather-terrain](weather-terrain.md); the hook kinds proposed there for
multipliers become records.

## Results

Native (`-Wall -Wextra -Werror`, -O2 and ASan + UBSan): all suites pass,
V1–V12 tests unchanged. The new tests are listed in
[testing](testing.md). Coverage of `battle_modifiers.c` 96% of lines (the
two `default:` branches for kinds that never reach the executor are not
run); `battle_protocol.c` 94%. Mutation check: 43 mutants of the executor,
the codec and the call sites; all killed except two that change nothing
observable (the hook decoder's version gate, which the V12 hook check also
enforces, and listing MODIFIER as a secondary effect, which has no chance
and no handler).

V12 unchanged:

- Trace: 1,200 V12 battles (4 formats × 300 seeds, every multiplier kind
  and unusual hook combinations, 18,098 damage events) give the same wire
  bytes, events, final state and RNG on the M4-A2 core and the PoC core.
  The same data relabelled V13 plays identically.
- Dolphin, release 7, Ability Lab L01–L20 with the M4-A2 inputs: L02–L20
  are line for line the M4-A2 Dolphin output (commands, events, state lines,
  results; L01 was not kept in M4-A2). Every run's dumped request replays
  with 0 mismatches on the PoC core **and** on the M4-A2 core (built from
  `53e648b`), ending on the same RNG value. L20 is still refused (strict).
- Debug entries V12/V11/V10 endurance, V9, V8, V7 conditions, V6 pivot, V5
  drag, V4 Struggle (on its own: Struggle hits, the max-HP/4 recoil of 14
  knocks out the 1-HP user, won in 3 turns), V3 switch, V2 reserve, V1: all
  ran. Cube Tester V12: Single won (4 turns), Double won (18), Triple lost
  (10), Rotation won (14); 20 ability events; request sizes as in M4-A2
  (925 / 1,717 / 1,701 / 1,701 B); no link loss.

V13 in Dolphin (release 7, `frontier-battle-rule-poc`); every run replays
on the native core with 0 mismatches; "without" is the same request, seed
and commands with the V13 records removed:

| Fixture | Observed (with records) | Without records |
|---|---|---|
| L21 Single | Hex on the poisoned Machop 90, then 55 and 46 on the unstatused one | first hit 46 |
| L22 Double | poisoned Rattata (60% HP): 13, then 28 / 27 / 28 once poison takes it to 1/2; unpoisoned Rattata (40%): 15 / 15 / 15 / 13 | 13, 15, 15, 15 / 15, 15, 15, 13 |
| L23 Double (L22's seed, ability redefined on the GBA only) | poisoned: 13, then 21 / 19 / 21; unpoisoned: 21 / 21 / 21 / 19 | identical to L22's |
| L24 Triple | Hex on the burned Machop 115 (knocked out), on the unstatused one 46; custom ability Tackle 28 | 58, 46, 16 |
| L25 Rotation | Hex from the front on the poisoned front Machop 123 | 63 |

L22 and L23 ran on the same DOL with the same seed and end on the same RNG
value; their counterfactuals are identical, so the only difference is the
GBA definition.

Result contract on V13: link cut (3 s silence) on the L22 result screen →
reconnect → "result already final … not restarting the battle"; the GBA
returned to the field. Save: the Frontier save copy hashed AFF1EB7B…
before and after a V13 and a V12 battle; the profile was restored
(expansion ROM, phase2 save F838F993…).

Phase 2: Frontier party viewer 42 assets (5.6 s), forced 3 s silence,
reconnect 42/42 from the cache in 33 ms, digests unchanged; generic
connector (expansion ROM, development DOL of the same source) 23 assets
2.8 s, reconnect 23/23 in 17 ms. Phase 1: 160 µs 10,000 round trips 0
errors, 64 KiB at 5.767 KiB/s (crc 4e5e3311); with faults 1 CRC → 1 NACK
and 1 lost ACK → 1 timeout, recovered; 250 µs 0 errors, 5.008 KiB/s.

JoyBus over the 31 logged PoC sessions: the GBA received 62,029 requests,
CRC 0, queue overflows 0, 40 stale partial frames dropped; the GC sent
64,787 frames, 55 retries, all recovered, no timeout outside the forced
silences. The first-word loss is still unexplained (as before).

Frontier `make check`, same tree, toolchain and options, run back to back:
`frontier-gc-m4a2` (`f8c02476e1`, the PoC's parent) and
`frontier-battle-rule-poc` (`04bec70cc7`) give the same status for every
one of the 6,806 tests: PASS 3,890, FAIL 2,413, FLAKY 41, TO_DO 419,
KNOWN_FAILING 19, ASSUMPTION_FAIL 14, EXPECTED_FAIL 7, CRASH 2, ERROR 1
(summary: 2,455 failed). No new failure; the failures are the existing
ones of this Frontier tree.

Communication and load:

| | M4-A2 (release 6) | PoC (release 7) |
|---|---:|---:|
| Largest request (6 Pokemon, 21 types) | 2,789 B | 2,789 B (block sizes unchanged) |
| A V13 record | — | 8 B in an effect or hook list |
| Measured requests | V12 L05 1,181 B in 0.47 s | V13 L22 1,189 B in 0.52 s, L24 1,557 B in 0.54 s |
| Native core, 4,000 V12 battles | 139–150 ms | 139–148 ms |
| GC core per turn (Dolphin peak) | — | 29–137 µs (Cube Tester V12), 50 µs (L22) |
| GC DOL | 315,008 B | 318,432 B |
| GC ELF text / data / BSS | 253,510 / 61,232 / 2,988,928 B | 256,454 / 61,712 / 2,993,312 B |
| GC `BattleRequest` / `Battle` / `EventLog` | 5,488 / 3,784 / 45,556 B | 7,840 / 5,800 / 45,556 B |
| Frontier ROM used | 26,340,772 B | 26,343,488 B |
| Frontier EWRAM (static) | 231,984 B | 231,984 B |
| Frontier request buffer | 2,789 B | 2,789 B (`BATTLE_REQUEST_MAX_BYTES`) |
| Frontier heap while building a request | 5,488 B | 7,840 B (temporary) |

The structure growth is the decoded record kept in every effect and hook
(7 bytes each); it is the main cost of this PoC and can be removed (see
above).

## Known limits

- Hex with Comatose (treated as asleep in the game) is not modelled;
  Comatose is not simulated, and strict rules refuse it.
- Facade stays unsupported (its Gen 6 burn exception is not a multiplier).
- Venoshock goes through the same exporter path as Hex but was not run in
  Dolphin.
- The custom ability is a lab definition; no game ability was converted
  from V12 kinds to records on the GBA (V12 kinds keep working in V13).
- Predicates are AND only, two per record; OR is two records (as the RESIST
  translation shows), NOT does not exist.

## Recommendation

**GO, limited**: adopt the shared modifier record (stages, two AND
predicates, the pass table) as the way the GC applies damage and stat
multipliers of moves, abilities and, later, held items, weather and
terrain. It removed a class of per-rule GC code without changing a V12
result, and made those rules testable once and checkable against Dolphin.

Before it becomes the formal base (in the next milestone, not here):

1. Store the record compactly (union or a per-move table) to remove the
   +2.4 KB request / +2.0 KB battle growth on the GC and the larger
   temporary heap block on the GBA.
2. Decide the rounding per stage (floor per step as now, or the game's
   chained 4.12 factor) before items and weather stack modifiers.
3. Grow the vocabulary only with predicates and stages that M4-W/T/B need
   (weather, terrain, grounded, super effective, speed), each with its
   tests; keep non-multiplier rules (field state, durations, STAB
   replacement, burn exception, type chart, Sturdy) as code or typed
   operations. No VM or independent rule engine.

DEFER: converting the existing V12 multiplier kinds on the GBA to records
(they already run through the shared path), and any OR/NOT logic.
