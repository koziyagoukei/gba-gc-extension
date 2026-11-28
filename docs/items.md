# Held items (M4-B0 request V17, M4-B1a and M4-B1a+ request V18)

Release 11 (`m4b0-dev`), branches `milestone4b0` and Frontier `frontier-gc-m4b0`;
M4-B1a (end-of-turn Leftovers and Black Sludge, request V18) is release 12
(`m4b1a-dev`), branches `milestone4b1a` and `frontier-gc-m4b1a`: section 10.
M4-B1a+ (audited Z-Crystals and Mega Stones held with no effect in battles
without gimmicks) keeps release 12 and V18, branches `milestone4b1aplus` and
`frontier-gc-m4b1aplus`: section 11.
Built on the V14 shared modifiers ([modifiers](modifiers.md)), the V15
weather ([weather](weather.md)) and the V16 terrain ([terrain](terrain.md));
V1–V16 requests keep their meaning byte for byte. M4-B0 brings the item
state and the first items: passive power modifiers and the weather /
terrain durations. Consumption, hit triggers, action limits, item changes
and persisting items in the result are later stages (section 9).

## 1. Check of the design against Expansion

Read in this Frontier build (pokeemerald-expansion, Gen 9 configs):
`GetBattlerHoldEffect` / `GetBattlerHoldEffectInternal` (Klutz, Embargo,
Magic Room), `CalcMoveBasePowerAfterModifiers` (the attacker's hold effect
after both abilities; `holdEffectParam` capped at 100;
`PercentToUQ4_12_Floored` for Muscle Band and Wise Glasses, `PercentToUQ4_12`
for the type items), `CalcAttackStat` / `CalcDefenseStat` / `GetOtherModifiers`
(item steps the B0 items do not use), `TryChangeWeather` (the setter's rock
from `gBattleWeatherInfo`: 8 turns, only for a weather set for a limited time),
`TryChangeBattleTerrain` (Terrain Extender: 8), the item data
(`gItemsInfo`, `gHoldEffectsInfo`) and the battle form changes keyed to items.

| Item | Review | This build | M4-B0 |
|---|---|---|---|
| Type items, incenses | ×(1 + p%) after the abilities | `uq4_12_add(1.0, PercentToUQ4_12(p))`: the nearest factor, equal to the 4.12 value of 100 + p | a power record (`BPRED_MOVE_TYPE`), percent 100 + p |
| Muscle Band, Wise Glasses | truncated factor | `PercentToUQ4_12_Floored(10)` = 409: 4505, not 4506 | a power record with `BMOD_FLOORED` (stage bit 6) |
| Rocks, Terrain Extender | duration at the set | `weatherDuration = 8` / `terrainTimer = 8` when the setter holds it; an ability weather before Gen 6 keeps no count; the same weather fails first | a duration record read by the shared set operation for a count of 1–15 only |
| Klutz | turns items off | `GetBattlerHoldEffectInternal` returns none | not run: with any held item, every item is normalised |
| Items with `HOLD_EFFECT_NONE` | none | Rusted Sword has no hold effect but changes Zacian at the battle start | an item that changes its holder's form in battle is UNSUPPORTED |

## 2. Three layers

1. **GC state** (`Battler.item_from`): the entry whose item the Pokémon holds
   now; `entry->record.held_item` / `entry->mon.item` is the item it started
   with. The definition belongs to the item and goes with it, so moving an
   item later (M4-B2) is a pointer change. B0 never changes it.
2. **GC shared operations** (`battle_items.c`): `bc_item` is the only check of
   whether an item has an effect now (V17, items not normalised, an item held,
   a definition with records); `bc_item_next` walks its records; the power
   records run in the shared modifier chain (`battle_modifiers.c`, `SRC_ITEM`
   after both abilities); `bc_item_field_turns` gives the duration of a
   weather or terrain the holder sets, inside `bc_weather_set` /
   `bc_terrain_set`; `bc_items_check` and `bc_items_begin_battle` do the
   battle-start checks and the normalisation notice.
3. **GBA rules** (Frontier `ResolveItem`): percent, category, move type,
   truncation and durations from the item data and the battle code. No item
   id, species or holder ability is interpreted by the GC.

## 3. Request V17

Header and rules: `RULE_STRICT_ITEMS` (bit 9, V17 only); without
`RULE_IGNORE_ITEMS` the item blocks say what each item does.

One item block per entry, after the ability blocks (the blocks are of
variable length, so they sit with the other per-entry blocks; the fixed
volatile, field and terrain rules still close the request):

| Bytes | Field |
|---|---|
| 0 | class: `BICLASS_NONE` (no item, or no battle effect), `BICLASS_UNSUPPORTED`, `BICLASS_RECORDS` |
| 1 | record count (0, or 1–4 for RECORDS) |
| 2–3 | source item id (= the record's `held_item`) |
| 4+8n | `BITEM_MODIFIER` + the 7-byte modifier body (flags 0, source `BMSRC_ITEM`), or `BITEM_WEATHER_TURNS` / `BITEM_TERRAIN_TURNS`: mask, 0, turns 1–15, four zero bytes |

Modifier stage bit 6 (`BMOD_FLOORED`, V17, moves, abilities and items, the
fixed-point stages POWER / ATTACK / DEFENSE / FINAL / WEATHER): the percent's
4.12 factor is truncated. Before V17 the bit stays reserved and a record with
it is refused. V17 item records use the POWER stage only (the attacker's
item); `BPRED_POWER_AT_MOST` is allowed for items.

The decoder refuses: unknown class or kind, a source other than the
record's item, NONE or UNSUPPORTED with records, UNSUPPORTED or RECORDS
without an item, more than 4 records, a record with flags, a duration
outside 1–15 or with an empty / out-of-range mask or non-zero unused bytes,
two durations for the same weather or terrain, any V17 value in an earlier
version. The encoder writes no block that does not describe its record's
item. Largest request: `BATTLE_REQUEST_MAX_BYTES(6, 21)` = 3,165 B (+36 per
Pokémon).

## 4. What is supported, and the battle rules

| Item | Record |
|---|---|
| Muscle Band, Wise Glasses | physical / special power ×(100 + param)%, truncated |
| Silk Scarf, Charcoal, Mystic Water, Magnet, Miracle Seed, Never-Melt Ice, Black Belt, Poison Barb, Soft Sand, Sharp Beak, Twisted Spoon, Silver Powder, Hard Stone, Spell Tag, Dragon Fang, Black Glasses, Metal Coat, Fairy Feather | power ×(100 + param)% for moves of the item's type |
| Sea, Wave, Odd, Rock and Rose Incense | the same (type items in this build) |
| Damp, Heat, Smooth, Icy Rock | 8 turns for the weathers whose rock it is (`gBattleWeatherInfo`) |
| Terrain Extender | 8 turns for every terrain |
| no battle effect (NONE) | `HOLD_EFFECT_NONE`, prize money, Exp. Share, Lucky Egg, Soothe Bell, Everstone, Cleanse Tag / Pure Incense, Smoke Ball, unless the item changes the holder's form in battle |

Everything else is UNSUPPORTED (the default), among them berries, Choice
items, Life Orb, Expert Belt, Metronome, Eviolite, Assault Vest, species
items, plates, memories, drives, gems, Focus Sash / Band, Leftovers and Black
Sludge (in V17; V18 runs them, section 10), Rocky Helmet, Weakness Policy, Air Balloon, Iron Ball, Utility
Umbrella, Safety Goggles, terrain seeds, herbs, Red Card, Eject Button / Pack,
Quick Claw, Lagging Tail, lenses, King's Rock, Mega Stones, Z-Crystals, orbs,
the e-Reader Enigma Berry, and the speed items (Macho Brace, power items:
left for later in B0 by decision). Moves that use items (Knock Off, Thief,
Covet, Trick, Switcheroo, Bestow, Fling, Bug Bite, Pluck, Incinerate,
Acrobatics, Natural Gift, Recycle, Embargo, Magic Room, Poltergeist,
Corrosive Gas, Belch, Stuff Cheeks, Teatime, Judgment, Techno Blast,
Multi-Attack) are unsupported moves and are never run.

**All or nothing.** A battle's items take part only if every item is NONE or
RECORDS and no Pokémon has an item ability (Klutz, Pickpocket, Magician,
Sticky Hold, Frisk, Unburden, Symbiosis, Harvest, Gluttony, Cheek Pouch,
Ripen, Unnerve, As One, Multitype, RKS System, Pickup, Ball Fetch). Otherwise:

- normal rules: the GBA sends `RULE_IGNORE_ITEMS`; no item has any effect
  (the GC guarantees it: `bc_item` returns nothing, and a normalised battle
  is event for event the battle without items, `test_v17_formats`). The GBA
  says so before the battle ("ALL HELD ITEM EFFECTS ARE OFF" and the item or
  ability that caused it), the GC at the battle start (`EV_ITEMS_NORMALIZED`:
  "All held item effects are OFF." with the counts) and for the whole battle
  ("ITEMS OFF" in the command bar);
- strict rules (`RULE_STRICT_ITEMS`; Lab fixtures with `strictItems`, and
  `FrontierGcBattle_SetStrictItems(TRUE)` for formal use): the GBA refuses to
  start ("Strict items: not simulated: …"). The GC refuses an UNSUPPORTED item
  without `RULE_IGNORE_ITEMS` (`BI_UNSUPPORTED_ITEM`) and, under the strict
  rule, a normalisation that drops any item that is not NONE
  (`BI_ITEMS_NORMALIZED`).

A V16 request without `RULE_IGNORE_ITEMS` is still refused (`BI_ITEMS`).
V1–V15 requests are unchanged; they never carried item blocks (the GC
ignores `held_item` there, as before; the GBA always normalises).

## 5. Order and rounding (V17)

| Stage | Chain |
|---|---|
| POWER | the move's records, the field records (terrain), Technician, the attacker's ability records, the defender's, **the attacker's item** |

Each factor is the nearest 4.12 value of its percent, or truncated with
`BMOD_FLOORED`; the chain rounds half up and the power is applied once
(half down), as before. Examples: Muscle Band on power 45 gives 49 (the
nearest factor would give 50); a ×0.5 move record, Grassy Terrain ×1.3, an
ability ×1.2 and Muscle Band on power 74 give 63 (the band first, or a
nearest factor, give 64).

## 6. Not supported (refused or normalised, never silently inert)

See section 4. A Pokémon whose species changes form in battle because of its
item (Zacian with Rusted Sword, primal orbs, Mega Stones) has that item
UNSUPPORTED. The holder-dependent refusal is the GBA's; the definition sent
stays the item's.

## 7. Verification

| Check | Result |
|---|---|
| Native (`battle_test.c`, -O2 and ASan + UBSan; presentation test) | all suites pass; new `test_v17_wire`, `test_v17_init`, `test_v17_power`, `test_v17_field_turns`, `test_v17_formats` (240 random battles with items in four formats run twice: identical events, state and RNG; the 80 normalised ones are event for event the battle without items, after the notice) and `test_v17_expansion_reference`; the presentation test shows the normalisation notice |
| Mutations (65 changes to the item code: gate, iterator, checks, notice, durations, item pass position and stage, truncation, every block check, rule masks, sizes, decoder) | 60 caught in the first run; 5 survived: 2 were dead guards in the duration code (removed), 3 lacked a test (added: records without an item, the current item follows `item_from`, a record only at its own stage), all 3 caught, plus one more (the rules reaching a holder) caught: 64 of 64. The M4-T terrain and M4-W weather sets: 58 of 58 applicable caught (the one not applied is the V16 item-rule line, mutated in the new set) |
| Expansion reference (`make check TESTS="GC V17 reference"`) | 13 tests: 8 damage cases × 3 rolls = 24 of 24 values equal to the GC (Charcoal, Sea Incense, Muscle Band and Wise Glasses truncated, Miracle Seed after Grassy Terrain, Technician then Charcoal, the target's Charcoal, Muscle Band on a special move); Damp Rock / Heat / Smooth / Icy Rock (and a rock for another weather), Drizzle with Damp Rock, Terrain Extender on a move and on a surge: the build's counters show 8 (7 after one turn) or 5 |
| Truncation | Muscle Band Body Slam 85: 63/58/52 in both; the nearest factor would give 64/58/54; Wise Glasses Thunderbolt 95: 70/64/58 (nearest: 72/66/60) |
| V12–V16 traces (1,200 battles each) | identical to M4-T (V15 and V16 total `efa20edd`) |
| Frontier `make check` | 6,854 tests; the 6,841 (name, status) pairs of the M4-T run unchanged, 13 new PASS; FAILED 2,455 in both (pre-existing, mostly Japanese-text MESSAGE checks) |
| Dolphin Lab L43–L50 (V17) | L43 Single (Muscle Band record `stage=4a`, Charcoal, Drizzle with Damp Rock 8 turns), L44 Double (Electric Surge 5, then Grassy Surge with Terrain Extender 8), L45 Triple (Drought with Heat Rock 8), L46 Rotation (Sand Stream with Smooth Rock 8, Snow Warning with Icy Rock 8), L47 = L43 with Leftovers: the notice first, then the same battle with every item off (Flamethrower 61→52, Body Slam 47/46/43→42/41/39, rain 8→5 turns), L48 refused by the GBA ("Strict items: not simulated: たべのこし"), L49 = L43 with the Gen 3 type item records on the same DOL and seed (only Charcoal's Flamethrower differs: 61→57), L50 Klutz: every item off. All replay natively with 0 mismatches (45 of 45 replayable Lab logs of M4-T and M4-B0) |
| Display | GBA: "ALL ITEMS OFF: たべのこし" / "… ぶきよう" on the command screen from the first prompt; GC: "All held item effects are OFF. Rules: 4 held, 1 not simulated. No item is used." at the start and "ITEMS OFF" in the command bar |
| Dolphin Lab L01–L42 | request bytes, events, actions and results line for line equal to the M4-T runs (42 of 42) |
| Debug menu V1–V12, Cube Tester (4 formats) | all reached a result; Cube Tester requests 925 / 1,717 / 1,701 / 1,701 B as in M4-T |
| Result contract (V17) | link cut 3 s on the result screen → "result already final (nonce=…); not restarting the battle" |
| Save | Frontier save `DDE68022…` before and after |
| JoyBus (77 sessions) | GBA 153,311 requests, CRC 0, overflows 0, 128 stale drops, 138 NACKs; GC 149 retries, 7 timeouts (7 sessions), all recovered by a retry; the one link loss is the deliberate cut |

## 8. Memory, link and time

| Item | M4-T | M4-B0 |
|---|---:|---:|
| `BattleRequest` (x86-64 host build) | 5,912 B | 6,344 B (+36 per entry) |
| `Battle` (x86-64 host build; GC: +4 B per battler) | 4,184 B | 4,280 B |
| `EventLog` | 47,812 B | 47,812 B |
| Request buffer `BATTLE_REQUEST_MAX_BYTES(6, 21)` | 2,949 B | 3,165 B |
| ELF text / data / bss | 276,686 / 68,676 / 3,002,048 | 280,662 / 69,668 / 3,002,528 |
| DOL | 345,664 B | 350,624 B |
| Frontier ROM used | 26,358,580 B | 26,363,796 B |
| EWRAM (`.ewram.sbss`) | 231,984 B | 232,048 B (the strict refusal text) |
| A V17 request | — | V16 size + 4 B per Pokémon + 8 B per record (L43 1,429 B, L44 1,837, L45 1,845, L46 1,869) |
| GC core per turn (Dolphin, peak) | 162 µs (V16) / 215 µs overall | 309 µs (L45 Triple, 21 events in one turn); the peak counter covers the whole run |

## 9. Next stages and known limits

| Stage | Content | What it needs |
|---|---|---|
| B1a | non-consuming end-of-turn items (Leftovers, Black Sludge) | **done in M4-B1a** (request V18, section 10) |
| B1b | consumption, HP and status berries, Focus Sash | the consumed state on the Battler, Pickup reclassified, Unnerve, re-evaluation within multi-hit moves |
| B1c | after-move items (Life Orb, Rocky Helmet, Weakness Policy, Expert Belt) | a move-end order table outside `strike()`, an effectiveness predicate |
| B1d | action limits (Choice items, Assault Vest) | legal-action re-evaluation for the GBA prompts and the AI |
| B2 | item changes (Knock Off, Trick, Thief, Magician, Pickpocket) | `item_from` changes; holder-independent definitions already hold |
| Result | persisting consumed items | only once a roster battle may apply its result (the GC refuses that today) and berries are consumed; the nonce rule stays |

## 10. M4-B1a: end-of-turn items (request V18)

Release 12 (`m4b1a-dev`), branches `milestone4b1a` and Frontier
`frontier-gc-m4b1a`, built on M4-B0. V1–V17 keep their bytes, operations,
events and behaviour (10.6).

### 10.1 Check against Expansion

Read in this Frontier build: `battle_end_turn.c` (`sEndTurnEffectHandlers`,
`HandleEndTurnFirstEventBlock`, `DoEndTurnEffects`, `HandleEndTurnOrder` with
`SortBattlersBySpeed`), `battle_hold_effects.c` (`ItemBattleEffects`,
`TryLeftovers`, `TryBlackSludgeDamage`), `battle.h` (`SetHealAmount`,
`SetPassiveDamageAmount`), `GetBattlerHoldEffectInternal`, the battle scripts
`BattleScript_ItemHealHP_Ret` / `BattleScript_ItemHurtWithAnim`, and the
build's own tests `test/battle/hold_effect/leftovers.c`, `black_sludge.c` and
`test/battle/end_turn_effects.c`.

| Point | This build |
|---|---|
| When | `ENDTURN_FIRST_EVENT_BLOCK`, Pokémon by Pokémon in end-of-turn speed order: Grassy Terrain heal → Healer / Hydration / Shed Skin → `FIRST_EVENT_BLOCK_HEAL_ITEMS` (Leftovers, Black Sludge), then the next Pokémon. Before it: the weather countdown and weather damage (Rain Dish, Dry Skin ...). After it: Aqua Ring, Ingrain, Leech Seed, poison, burn ... the terrain countdown, Speed Boost |
| Order | `SortBattlersBySpeed` at the start of the end of turn: fastest first, a stable insertion sort, so equal speeds keep the battler id order (player left, foe left, player right, foe right) |
| Leftovers | `TryLeftovers`: only below max HP and not under Heal Block (Gen 5+); `GetNonDynamaxMaxHP / 16`, at least 1 (`SetHealAmount`); the heal stops at max HP |
| Black Sludge | `IS_BATTLER_OF_TYPE(POISON)` on the holder's types now: `TryLeftovers`; else `TryBlackSludgeDamage`: `/ 8`, at least 1, not with Magic Guard; damage at full HP too; `tryfaintmon` right after (the holder can faint) |
| A faint | the fainted Pokémon is skipped by every later step; the others go on; replacements come at the end of the turn |
| Turned off | `GetBattlerHoldEffectInternal`: Klutz, Embargo, Magic Room |

None of this contradicts the B1a plan. Heal Block, Embargo, Magic Room,
Soak, Forest's Curse, Trick-or-Treat, Reflect Type, Conversion, Roost,
Burn Up, Psychic Noise, Trick, Knock Off ... are unsupported moves (the
exporter's effect whitelist): they are never run, so their states never
exist in a GC battle. Magic Guard, Color Change, Protean, Libero, Imposter,
Trace, Power Construct ... are UNSUPPORTED abilities that the core would
ignore, so such a battle is normalised instead (10.4).

### 10.2 Request V18: `BITEM_HP`

V18 is V17's layout plus one item record kind (no new rule, no new block,
no size change: the largest request stays 3,165 B).

| Byte | `BITEM_HP` |
|---|---|
| 0 | kind 4 |
| 1 | timing (`BITIME_END_TURN` = 1; the only one in V18) |
| 2 | operation `BIHP_HEAL` (1) / `BIHP_DAMAGE` (2) |
| 3 | max HP divisor (≥ 1): the amount is max HP / divisor, at least 1 |
| 4–5 | a holder predicate: `BPRED_NONE` (argument 0) or `BPRED_HAS_TYPE` (a type below the type count) |
| 6 | 0 (reserved) |
| 7 | flags 0 |

At one timing the first HP record whose predicate holds is the item's
effect there; the later ones are its alternatives. A heal at full HP is
that effect doing nothing (no event). Leftovers is one record (heal 16);
Black Sludge two (heal 16 if Poison, else damage 8). The predicate reads the
holder's current types, so the definition stays right if the item changes
hands. The decoder and `battle_item_valid` refuse an HP record in V17, any
other timing, operation or predicate, a zero divisor, an out-of-range type,
an argument without a predicate, the reserved byte and flags;
`bc_items_check` applies the same check to a V18 request built in memory
(`BI_CONDITIONS`).

### 10.3 GC core

- `bc_item_hp(battle, position, timing, log)` (`battle_items.c`) runs the
  records of the item the Pokémon holds now (`Battler.item_from` through
  `bc_item`, so nothing runs under `RULE_IGNORE_ITEMS`). The heal stops at
  max HP and emits `EV_ITEM_HEAL`; the damage goes through the core's own
  `bc_apply_damage` (`EV_ITEM_DAMAGE`, then `EV_FAINT`). The event's `item`
  (sharing the storage of `ability`, so `BattleEvent` keeps its 24 bytes) is
  the item id, for the name only.
- A timing is one fixed point of the turn sequence: `resolve_end_of_turn`
  calls `bc_item_hp(..., BITIME_END_TURN)` for each Pokémon right after its
  terrain heal and its Shed Skin. Later stages add timings (after a hit, an
  HP threshold ...) as calls at their own points, never as one pass at the
  end of the turn.
- V18 end-of-turn order: equal speeds keep the reference's battler order
  (the sides alternate by slot number, also when a slot is empty). V17 and
  earlier keep the player's Pokémon first (unchanged).
- The heal and the damage are not modifiers: the modifier chain computes
  factors; the HP record changes HP.

Presentation: "Restored HP with its held item." / "Hurt by its held item!"
with the holder's name and the item's game-rendered label (`LABEL_ITEM`,
fetched for items with an HP record unless every item is off), then the HP
bar. The companion logs the records (`PH12 item id=… record0 hp timing=1
heal 1/16 pred=06:04`).

### 10.4 Exporter, normalisation, strict mode

`ResolveItem` (V18 rosters only): Leftovers → heal 16; Black Sludge → heal 16
if `TYPE_POISON`, else damage 8. The divisors are this build's code
(`TryLeftovers`, `TryBlackSludgeDamage`), checked by the Expansion reference.

The M4-B0 rules stay: all or nothing (`RULE_IGNORE_ITEMS`, shown on the GBA
command screen "ALL ITEMS OFF: …" and on the GC "ITEMS OFF" plus the
battle-start notice), strict refusal ("Strict items: not simulated: …",
`FrontierGcBattle_SetStrictItems`), the item ability list. New in V18
(`HpItemAbility`, read from the records): a holder of an HP record whose own
ability changes what the record does turns every item off (strict: refused).

| Ability | Why | For |
|---|---|---|
| Magic Guard | stops `TryBlackSludgeDamage` | a damage record |
| Color Change, Protean, Libero, Imposter | change the holder's types | a type predicate |
| Power Construct, Tera Shift | change the max HP | any HP record |
| Trace, Receiver, Power of Alchemy, Wandering Spirit | can give the holder one of the above | a damage record or a type predicate |

Klutz anywhere already turned every item off (M4-B0). A V17 roster still
sends Leftovers and Black Sludge as UNSUPPORTED (L47, L48 unchanged).

### 10.5 Order (V18)

| Step | Content |
|---|---|
| 1 | weather countdown, then each Pokémon's weather residual (damage, Rain Dish / Dry Skin / Ice Body) |
| 2 | Pokémon by Pokémon: Grassy Terrain heal → Shed Skin → **its item (Leftovers / Black Sludge)** |
| 3 | poison / bad poison, then burn, then frostbite (each group in speed order) |
| 4 | terrain countdown |
| 5 | Speed Boost |

### 10.6 Verification

| Check | Result |
|---|---|
| Native (-O2, ASan + UBSan, presentation test) | all suites pass. New: `test_v18_wire`, `test_v18_init`, `test_v18_end_turn_reference` (26 cases), `test_v18_state` (`item_from` pointing at another entry's Black Sludge; a record only at its own timing), `test_v18_positions` (switch, Shift, Rotation), `test_v18_versions` (V17 tie order kept; slot gaps), `test_v18_formats` (240 random battles in four formats run twice: identical events, state and RNG; the 80 normalised ones event for event the battle without items; 2,810 item heals, 848 item damages, 109 Black Sludge faints; at most 29 events in a turn) |
| Expansion reference (`make check TESTS="GC V18 reference"`, `test/battle/gc_end_turn_item_reference.c`) | 22 tests, all PASS on the first run with the same numbers and order as the native cases: /16 and /8 rounded down, at least 1, HP 1, full HP, cap at max HP, Poison / other types, faint, Grassy then item (heal and damage), Shed Skin then item, Leftovers before poison / bad poison / burn, sandstorm and hail before Leftovers, Rain Dish before Leftovers, Speed Boost after, speed order, equal speeds in battler order, Pokémon by Pokémon in a Double, a faint mid-sequence, a switch-in, Magic Guard stopping the damage (why it is normalised). V14–V17 references: 48 PASS, unchanged |
| Negative controls (temporary, not committed) | seven tests stating a wrong order or amount (Leftovers before Grassy, /8, rounded up, poison before Leftovers, Black Sludge hurting a Poison type, the V17 tie order, every item after every terrain heal): all 7 FAIL against the build |
| Mutations | 50 changes to the V18 code (HP step, timing, predicate, amounts, cap, event, order position, tie order, validation, wire, presentation): 46 caught in the first run; 4 survived: 2 were test gaps (the timing check, the event item through `item_from`), closed by `test_v18_state` and then caught; 2 are equivalent (`op != HEAL` = `op == DAMAGE` with two valid operations; the order's fainted check, kept from V17, is repeated by every later step). The M4-B0, M4-T and M4-W sets on the new tree: 122 caught, 0 survived; the 3 duration mutations whose text changed in M4-B0 were re-targeted and caught; 3 no longer apply (code removed in M4-B0) |
| Traces V12–V17 (1,200 battles each) | identical to M4-B0 (V15 / V16 `efa20edd`, V17 `203feab5`) |
| Frontier `make check` | 6,876 tests; the 6,854 (name, status) pairs of the M4-B0 run unchanged, the 22 new V18 tests PASS; FAILED 2,455 in both (pre-existing) |
| Dolphin L51–L57 (V18) | L51 Single: sandstorm damage, then Tyranitar's Black Sludge damage and Snorlax's Leftovers in speed order. L52 Double (Grassy Surge): an airborne Aerodactyl faints to Black Sludge; then Pokémon by Pokémon terrain heal + item; a capped Black Sludge heal (+3 to 120); Leftovers silent once the terrain fills the HP; burn last. L53 Triple: sandstorm, Black Sludge damage, two Leftovers, Leftovers before poison. L54 Rotation: only the front Pokémon; Rain Dish then Leftovers; Black Sludge damage and heal across rotations. L55 = L51 with Life Orb: the notice first, no item event (same seed). L56 refused on the GBA ("Strict items: not simulated: いのちのたま"). L57 Magic Guard with Black Sludge: every item off ("ALL ITEMS OFF: マジックガード" on the GBA, "ITEMS OFF" on the GC). All six battles replay natively with 0 mismatches |
| Display | GC, in event order: "Hurt by its held item! プテラ くろいヘドロ", "Fainted!", "Healed by the grassy terrain.", "Restored HP with its held item. ニドキング くろいヘドロ", …, "Restored HP with its held item. ドクロッグ たべのこし", "Status damage! BRN" |
| Dolphin L01–L50 | 48 of 48 battles line for line equal to M4-B0 (request bytes, events, actions, results); L42 and L48 refused on the GBA as before; 52 of 52 replayable Lab logs replay natively with 0 mismatches |
| Debug menu V1–V12, Cube Tester | every version reached a result; Cube Tester four formats 925 / 1,717 / 1,701 / 1,701 B as in M4-B0 |
| Result contract (V18) | GC Z right after the result: "result already final (nonce=cf5206a5); not restarting the battle"; Z before the result: the same battle restarts with the same nonce, nothing applied |
| Save | Frontier save SHA-256 `AFF1EB7B…` before and after; the profile restored |
| JoyBus (79 sessions) | GC 192,948 sent, CRC 0, 147 retries, 15 timeouts, all recovered; GBA 194,561 requests, CRC 0, overflows 0; the 2 link losses are the deliberate Z cuts |

### 10.7 Memory, link and time

| Item | M4-B0 | M4-B1a |
|---|---:|---:|
| `BattleRequest` / `Battle` / `EventLog` (x86-64 host) | 6,344 / 4,280 / 47,812 B | unchanged |
| Request buffer `BATTLE_REQUEST_MAX_BYTES(6, 21)` | 3,165 B | 3,165 B |
| ELF text / data / bss | 280,662 / 69,668 / 3,002,528 | 283,310 / 70,276 / 3,002,528 |
| DOL | 350,624 B | 353,888 B |
| Frontier ROM used | 26,363,796 B | 26,365,896 B |
| EWRAM (`.ewram.sbss`); GBA request heap | 232,048 B; `BattleRequest` | unchanged |
| A V18 request | — | V17 + 8 B per HP record (L51 1,485 B, L52 1,845, L53 1,821, L54 1,837) |
| Link | — | two item labels (Leftovers, Black Sludge) once per session: 455 wire bytes, about 0.2 s |
| GC core per turn (Dolphin peak) | 309 µs | 310 µs overall (L45, as in M4-B0); V18 peak 250 µs (L53 Triple, 24 events) |
| Events per turn | — | at most 2 more per active Pokémon (item, faint); 29 in the worst random turn; the log holds 1,992 |

### 10.8 Frontier coverage (auxiliary)

Every set of `gBattleFrontierMons` (2,040 sets, this build's
`src/data/battle_frontier/battle_frontier_mons.h`) classified with the
exporter's rules (`ResolveItem`, the item abilities, `HpItemAbility`, a
script that mirrors them); a set counts as usable if its own item takes part
and its ability does not turn items off. A battle needs all its Pokémon to
be usable; the player's party is not in this data, so no battle-level rate
is given. Facility battles are not exported to the GC yet.

| Class | Sets | Share |
|---|---:|---:|
| B0 records (type items, Muscle Band / Wise Glasses, rocks, Terrain Extender) | 23 | 1.1% |
| no item, or no item effect | 0 | 0% |
| B1a (Leftovers 258, Black Sludge 16) | 274 | 13.4% |
| unsupported item | 1,732 | 84.9% |
| item ability (Frisk 3, Power Construct 3, Sticky Hold 2, Magician, Unnerve, As One) | 11 | 0.5% |

Unsupported items, most common: Z-Crystals 568, Life Orb 142, Choice Scarf
119, form items (Mega Stones and the like) 114, Focus Sash 113, Assault
Vest 95, berries 81, Heavy-Duty Boots 79, Rocky Helmet 73, Choice Band 62,
Choice Specs 52, Eviolite 30.

### 10.9 Known limits

- Equal end-of-turn speeds follow the reference in V18 only; V17 and earlier
  keep their order (no earlier version changes).
- Shed Skin's chance is compared with the reference with the chance forced
  (`WITH_RNG`); the GC rolls its own RNG.
- The GBA screen shows no item activation (the GC does).
- The abilities of 10.4 are not simulated; such battles run with every item
  off.

## 11. M4-B1a+: Z-Crystals and Mega Stones held with no effect (request V18)

Exporter-only stage on Frontier branch `frontier-gc-m4b1aplus`; the GC side
(branch `milestone4b1aplus`) changes tests and documents only. Release 12 and
request / APP V18 stay: the `companion.dol` built from this branch with
`./build.ps1 -Release 12` is byte for byte the M4-B1a one (SHA-256
`891B9397…`, 353,888 B). No gimmick is implemented: no Mega Evolution, Z-Move,
Ultra Burst or Primal Reversion, no form change in the GC, no item moves.

### 11.1 The rule

A held Z-Crystal or Mega Stone has a battle effect only through its gimmick.
A GC request always has `RULE_NO_GIMMICKS` and the GC runs no gimmick, so an
audited one is sent as **held, with no effect**, in the V17 / V18 block the
GC already reads:

| Field | Value |
|---|---|
| `PokemonRecord.held_item` | the item's id (kept) |
| `BattleItem.source` | the same id |
| `BattleItem.cls` | `BICLASS_NONE` |
| `BattleItem.count` | 0 |

`BICLASS_NONE` with a non-zero id is a held item, not an empty hand
(`battle_held_item` returns the id). A later stage that adds Knock Off, Trick,
Thief, Poltergeist or Acrobatics must read the id and its removal rules,
never treat it as no item. No class, record kind or protocol bit is added;
the removal properties themselves come before M4-B2.

`AcceptGimmickItems` decides once the battle's rules are final (the strict
item rule included) and before `CheckItems`. Every candidate of the battle
becomes NONE only if all of these hold; otherwise every one stays
UNSUPPORTED, so `CheckItems` turns every item off (strict: refuses), exactly
as for any unsupported item:

| # | Condition | Check |
|---|---|---|
| 1 | request V18 | `r->version` (V17 and older: `OFF_VERSION`) |
| 2, 3 | gimmicks forbidden and never run | `RULE_NO_GIMMICKS` (`OFF_RULES`); the GC has no gimmick code; Z-Moves and Max Moves are unsupported moves |
| 4 | no automatic form change | no Pokémon of either side, bench included, has a form change this build makes by itself in battle: at the battle start, on entry or exit, by HP, weather, status, a move, a hit, the turn end, Battle Bond, or Primal Reversion with its orb held (`ChangesFormByItself`; `OFF_FORM`) |
| 5, 6 | audited item, safe holder | `GimmickItemCandidate`: on the audited list (11.2), hold effect Z-Crystal / Mega Stone with no parameter; not for a Mega, Primal, Ultra Burst, Gigantamax or Tera form; no Z-Crystal for Arceus; no form change of the holder's species names the item, except Mega Evolution with that Mega Stone |
| 7 | no move that reads or moves held items | no Pokémon of either side, bench included, knows such a move (Knock Off, Thief / Covet, Trick / Switcheroo, Bestow, Fling, Acrobatics, Poltergeist, Corrosive Gas, Embargo, Magic Room, Recycle, Natural Gift, Judgment / Techno Blast / Multi-Attack, Belch, Stuff Cheeks, Teatime, Bug Bite / Pluck, Incinerate, Z-Moves, Max Moves; `MoveUsesHeldItems`; `OFF_MOVE`). All are unsupported moves the GC never runs, but in this build they see the held item (11.4), so a battle with one is not accepted, supported or not |
| 8 | moves and abilities | the move-flag rule of 11.3 for every move; item abilities (Klutz, Frisk, Pickpocket …) turn every item off in `CheckItems` as before, accepted gimmick items with them; strict abilities and moves as before |
| 9 | id kept | `source = held_item`; NONE with an id is not an empty hand (11.1) |

An accepted Z-Crystal never lets another item in: Leftovers and Black Sludge
beside it act as in M4-B1a; Life Orb beside it turns every item off.

### 11.2 Audited items

All in this build (`src/data/items.h`, `src/data/pokemon/form_change_tables.h`
with `P_MEGA_EVOLUTIONS` and `P_GEN_9_MEGA_EVOLUTIONS` on), checked by
`test/battle/gc_gimmick_item_reference.c` against the build's own tables:

| Group | Items | Form changes naming them |
|---|---|---|
| Type Z-Crystals (18) | Normalium … Fairium Z | Arceus' Multitype forms (`FORM_CHANGE_ITEM_HOLD`) for the 17 non-Normal ones: refused for Arceus, accepted for any other holder |
| Signature Z-Crystals (16) | Pikanium, Eevium, Snorlium, Mewnium, Decidium, Incinium, Primarium, Lycanium, Mimikium, Kommonium, Tapunium, Solganium, Lunalium, Marshadium, Aloraichium, Pikashunium Z | none |
| Mega Stones (92) | every `HOLD_EFFECT_MEGA_STONE` item of this build, Venusaurite … Glimmoranite (Z-A and its DLC included) | only `FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM` (the gimmick), each to its own Mega form(s) |

Left out (UNSUPPORTED as before): Ultranecrozium Z (Ultra Burst), the Red
and Blue Orbs (Primal Reversion happens by itself on entry), every other
form item (Rusted Sword / Shield, plates, memories …), and any Z-Crystal or
Mega Stone added to the game later: the exporter refuses it, and the test
fails until it is audited.

### 11.3 Moves the exporter no longer sends as plain hits

The move check of condition 8 found moves whose behaviour this build keys to
move flags the request has no input for. They were sent as supported (run
with a wrong effect); they are now unsupported (`UnmodelledMoveFlags`,
`UNSUP_OTHER`, every version and the legacy V1 path; the target-ability flag
from V11, where abilities take part):

| Flag | Moves | What the GC would miss |
|---|---|---|
| `shadowMove` | Shadow Blitz … Shadow Blast and the status Shadow moves | 2× against any non-Shadow Pokémon, no STAB, no PP use (they were `TYPE_MYSTERY` hits) |
| `ignoresTargetAbility` | Sunsteel Strike, Moongeist Beam (Photon Geyser already unsupported) | they ignore Sturdy, Levitate, Thick Fat, Water Absorb … of the target |
| `ignoresTargetDefenseEvasionStages` | Sacred Sword, Chip Away, Darkest Lariat | they ignore the target's Defense and evasion stages |
| `ignoreTypeIfFlyingAndUngrounded` | Thousand Arrows (already unsupported by its effect) | it hits Flying types and Levitate |
| Z-Moves, Max Moves | every one | gimmicks (no Pokémon knows one) |

Not sent and not needed: crit stages (`RULE_NO_CRITICAL_HITS`); punch, bite,
pulse, sound, ballistic, wind, slicing and dance flags (only abilities and
items the GC does not run read them); Minimize, Dive, Magic Coat, Snatch,
Heal Block (unsupported moves). No Lab or debug fixture uses a move of the
table, so their requests are unchanged (11.7).

### 11.4 Expansion reference

`make check TESTS="GC V18 gimmick"` (part 1 of the test file), all PASS:
without its gimmick a Z-Crystal (Normalium Z, Snorlium Z) or a Mega Stone
(Venusaurite) changes nothing (same damage, same species); Mega Evolution and
Ultra Burst need the gimmick, while the Red Orb turns Groudon Primal by
itself; Acrobatics is not doubled with a held Z-Crystal or Mega Stone; Knock
Off removes neither a Z-Crystal nor its user's own Mega Stone and gets no
×1.5 against them (it does against a Potion); Trick fails with either item
on either side; Poltergeist hits a holder of either and fails against an
empty hand; Sunsteel Strike goes through Sturdy; Sacred Sword ignores
+2 Defense.

### 11.5 Exporter tests

`make check TESTS="GC V18 export"` (parts 2–4), through the real exporter
(`FrontierGcBattle_Test*`, compiled with `TESTING` only):

| Test | Checks |
|---|---|
| audited or refused | 34 Z-Crystals and 92 Mega Stones accepted for a plain holder; Ultranecrozium Z and every other item refused; a new unaudited item fails the test |
| form changes | every form change of every enabled species that names a Z-Crystal, Mega Stone or orb: only Mega Evolution with its own stone, for a non-gimmick form, gives a candidate |
| Arceus, Necrozma, orbs | Arceus (any form) with a Z-Crystal, Necrozma with Ultranecrozium Z, the orbs, Zacian's Rusted Sword, Mega Venusaur with its stone: refused; Necrozma with Solganium Z, Charizard with Charizardite X: accepted |
| move flags | the 22 moves of 11.3 unsupported (Single, Double), the legacy V1 path too; Sunsteel Strike unsupported in V11, supported in V10; Tackle, Iron Head, Close Combat supported |
| item moves | every move of condition 7 (and every Z-Move and Max Move) unsupported |
| a Z-Crystal beside Leftovers | the V18 request: NONE, id 377 kept in the record and the block, count 0; Leftovers RECORDS; `RULE_NO_GIMMICKS` set, `RULE_IGNORE_ITEMS` not; strict items: starts |
| rules and version | gimmicks allowed: UNSUPPORTED, every item off, the stone named (strict: refused); V17: UNSUPPORTED; V18 without gimmicks: NONE |
| no other item let in | beside Life Orb: every item off, Life Orb named (strict: refused); Frisk: every item off by the ability |
| form changes, either side and bench | Double with three each: accepted; a Castform, or a Xerneas (battle start only), on the opponent's bench: `OFF_FORM`, every item off; without the stone, the same battle is as in M4-B1a |
| item moves anywhere | an opponent with Knock Off: `OFF_MOVE`, every item off, the Z-Crystal named (strict: refused); without the Z-Crystal, the same battle simulates its Leftovers as in M4-B1a; a bench partner with Poltergeist in a Double: `OFF_MOVE` |
| holders refused | Arceus-Fire + Firium Z, Necrozma-Dawn Wings + Ultranecrozium Z, Kyogre + Blue Orb, Mega Venusaur + Venusaurite: UNSUPPORTED, every item off |
| Lab fixtures byte for byte | L01–L57: each request (battle id 0) equal to the M4-B1a export (`FrontierGcBattle_TestSetLegacyExport`); L58–L61 accepted with every ability and move simulated; L62 Life Orb; L63 not built; L64 `OFF_FORM` |
| debug fixtures byte for byte | V2–V12 rosters × 4 formats × endurance: 88 requests equal to the M4-B1a export |

### 11.6 GC side

`test_v18_gimmick_items` (native): a V18 Double with Snorlium Z (377) and
Venusaurite (292) as NONE beside Leftovers and Black Sludge. The wire keeps
class and id; a NONE block naming another id, or carrying a record, is
refused; `battle_held_item` gives the id (0 for the empty-hand twin), and
`bc_item` gives no records; the whole battle is event for event, RNG and
outcome the one with empty hands (13 Leftovers / Black Sludge events).
Normalised beside Life Orb, `EV_ITEMS_NORMALIZED` counts 4 held (2 with
empty hands) and 1 unsupported; strict: inert items alone start, with Life
Orb the battle is refused. The 240 random V18 battles give the M4-B1a
numbers (2,810 heals, 848 damages, 109 faints).

### 11.7 Verification

| Check | Result |
|---|---|
| Native (-O2, ASan + UBSan) | all suites pass, `test_v18_gimmick_items` added; no GC code changed |
| Failing first | before the exporter change, the 9 exporter tests of 11.5 then written failed, while the 9 Expansion references of 11.4 passed; after it, all pass. The item-move test (condition 7 for unsupported moves, added after the first review) failed on the first rule (`ACCEPTED` instead of `OFF_MOVE`) and passes after the change |
| Frontier `make check TESTS="GC V1"` | 93 tests (V14–V18 references and the new file), all PASS; `TESTS="GC V18"` 45 PASS on the final tree |
| Frontier `make check` (before the item-move test) | 6,898 tests: the 6,876 (name, status) pairs of the M4-B1a run unchanged, the 22 new tests PASS; FAILED 2,455 in both (pre-existing) |
| Mutations (exporter) | 30 changes (candidate checks, audit list, version / rule / form / item-move conditions, the NONE rewrite, its place before `CheckItems`, each move flag, the legacy path): 26 caught in the first run; 4 survived: 2 test gaps (a species whose only automatic change is at the battle start; the legacy V1 move path), closed with Xerneas and roster-0 checks, then caught; 2 equivalent (the hold-effect parameter check: every audited item has none; the Thousand Arrows flag: its effect is already unsupported) |
| Dolphin L58–L64 (V18) | (on the build before the item-move rule; the L01–L64 requests are byte for byte the same with it, and L58, L59, L62, L64 rerun on the final ROM are line for line equal) L58 Single: Snorlium Z 377 and Venusaurite 292 arrive as `none`; Tyranitar's Leftovers heals 8 a turn; no event for the gimmick items. L59 Double: Charizardite Y, Ampharosite, Gyaradosite, Steelium Z `none`; Snorlax's Leftovers (+11) and Venusaur's Black Sludge heal (+7) each turn. L60 Triple: three Z-Crystals and two Mega Stones `none`; Nidoking's Black Sludge heals. L61 Rotation: Firium Z, Waterium Z, Venusaurite, Blastoisinite `none`; Black Sludge heals only at the front. L62 = L58 with Life Orb, same seed: "NORMALISED", `EV_ITEMS_NORMALIZED` 4 held / 1 unsupported, no item event. L63 refused on the GBA: "Strict items: not simulated: ウルトラネクロZ". L64 Groudon with the Red Orb: Snorlium Z stays unsupported (`OFF_FORM`), every item off |
| Dolphin L01–L57 | 54 of 54 battles line for line equal to M4-B1a (request bytes and CRC, events, actions, results); L42, L48, L56 refused on the GBA as before |
| Legacy V1 (debug "GC Battle (show only)") | the same 745-byte request and move classes as M4-B1a; only the random battle id and damage roll differ |
| Result contract | GC Z right after the result: "result already final (nonce=d20d2291); not restarting the battle" |
| Save | Frontier save SHA-256 `AFF1EB7B…` before and after; the profile restored |
| JoyBus (63 sessions) | GC 123,668 sent, CRC 0, 94 retries, 10 timeouts, all recovered; GBA 123,504 requests, CRC 0, overflows 0; 1 link loss, the deliberate Z cut |
| Not run | Cube Tester in Dolphin (its V12 requests are among the 88 debug fixtures equal byte for byte) |

### 11.8 Memory, link and time

| Item | M4-B1a | M4-B1a+ |
|---|---:|---:|
| GC `companion.dol`, structures, request buffer, event log | 353,888 B DOL | the same binary |
| Frontier ROM used | 26,365,896 B | 26,368,776 B (+2,880: the audit list, the checks, L58–L64) |
| EWRAM (`.ewram.sbss`) | 232,048 B | unchanged |
| A request | — | an accepted item is a 4-byte NONE block, the size of the UNSUPPORTED block it replaces (L58 1,445 B; L62 with Life Orb 1,437 B) |
| Link | — | nothing new (a NONE item has no label and no event) |
| GC core per turn (Dolphin peak) | 310 µs | 310 µs overall; L58–L64 at most 269 µs, 20 events |
| Exporter | — | `AcceptGimmickItems` once per battle: at most 6 Pokémon × (126-item list, form table, 4 moves) |

### 11.9 Battle Frontier coverage (measured)

Every `gBattleFrontierMons` set (2,040) as the player's lead in a V18 Single
against a plain Snorlax (Immunity, Tackle, no item), through the exporter
(`GC V18 export tally`, 17 chunks summed). These count sets, not facility
battles (the player's party is not in this data, and facility battles are
not exported yet). "M4-B1a" is the same run on the M4-B1a exporter.

| Measure | M4-B1a | M4-B1a+ |
|---|---:|---:|
| Requests built (else: the lead has no supported move) | 1,996 | 1,996 (97.8%) |
| Items take part (not normalised) | 295 | 852 (41.8% of 2,040) |
| Items, the set's ability and all its moves simulated | 9 | 103 (5.0%) |
| Strict battle (items, abilities, moves) that starts | 9 | 103 (5.0%) |
| Z-Crystal holders accepted / holding | 0 / 570 | 484 / 570 |
| Mega Stone holders accepted / holding | 0 / 102 | 90 / 102 |

Why the other 1,144 built requests turn every item off: another unsupported
item 1,025 (Life Orb, Choice items, Focus Sash, Assault Vest, berries,
Heavy-Duty Boots, Rocky Helmet …, as in 10.8); an item ability 28 (Frisk and
the like, now also beside accepted items); a Z-Crystal or Mega Stone kept
unsupported 91: 62 `OFF_MOVE` (the set knows Knock Off, Poltergeist or another
move of condition 7), 27 `OFF_FORM` (exactly the 27 holders whose own species
changes form by itself: Mimikyu, Aegislash, Battle Bond Greninja, Power
Construct Zygarde, Darmanitan, Cramorant, Eiscue, Morpeko, Wishiwashi,
Palafin, Cherrim, Shaymin, Meloetta, Xerneas, Zacian, Zamazenta) and 2
Ultranecrozium Z. 44 sets build no request (the lead has no supported move).

Codex's static estimate (945 sets by item, 87 with moves and abilities)
counts candidates per set; the figures above are the exporter's own
judgement of a whole request: the 44 unbuilt sets, the item-move and
form-change rules and the opponent's side account for the difference.

### 11.10 Known limits

- A battle with any Pokémon that changes form by itself, or that knows a
  move reading held items, keeps gimmick items off (conservative: the whole
  battle, not only the holder).
- Accepted items show nothing on either screen (no effect, no event).
- The removal and transfer properties of these items are not sent; a stage
  that adds an item-moving move must add them first (before M4-B2).
- Sunsteel Strike, Moongeist Beam, Sacred Sword, Chip Away, Darkest Lariat
  and the Shadow moves are now unsupported in every version (they were run
  with a wrong effect before).

## 12. M4-B1b: consumable items (request V19)

Release 13 (`m4b1b-dev`), branches `milestone4b1b` and Frontier
`frontier-gc-m4b1b`, built on M4-B1a+. V1–V18 keep their bytes, operations,
events, order and Lab results (12.7).

| Supported (V19) | Hold effect (this build) | Record |
|---|---|---|
| Oran Berry | `RESTORE_HP` 10 | HP, at HP ≤ max HP / 2: heal 10 (fixed), used up |
| Sitrus Berry | `RESTORE_PCT_HP` 25 | HP, at HP ≤ max HP / 2: heal max HP / 4 (at least 1), used up |
| Cheri / Chesto / Pecha / Rawst / Aspear Berry | `CURE_PAR` / `CURE_SLP` / `CURE_PSN` / `CURE_BRN` / `CURE_FRZ` | cure paralysis / sleep / poison and bad poison / burn / freeze and frostbite, used up |
| Persim Berry | `CURE_CONFUSION` | cure confusion, used up |
| Lum Berry | `CURE_STATUS` | cure every major status, then confusion, used up |
| Focus Sash | `FOCUS_SASH` | endure: a lethal hit at full HP leaves 1 HP, used up |

The exporter maps hold effects, not item ids, so any berry of this build
with one of these hold effects (and a parameter the record can carry) is
the same record; there are exactly these nine berries now (12.5).

Deferred (still UNSUPPORTED: every item off, strict: refused): Berry Juice
(not a berry: the berry rules of Bug Bite, Cud Chew, Ripen and Harvest differ,
and no Frontier set holds it), Focus Band (a chance, not this), Leppa, the
confusion-heal berries (Figy …, they confuse by nature), the stat and pinch
berries, the type-resist berries, Enigma, Jaboca / Rowap, Kee / Maranga,
Micle, Custap, Starf, White Herb, Mental Herb, Power Herb, Air Balloon,
Weakness Policy, Red Card, Eject Button / Pack, Throat Spray, the seeds,
Booster Energy … Each acts at a point or with a rule (a stat stage change, a
priority change, a forced switch, a move PP) this stage does not model.

### 12.1 Check against Expansion

Read in this Frontier build: `battle_hold_effects.c` (`ItemBattleEffects`
with `IsOnSwitchInActivation`, `IsOnHpThresholdActivation`,
`IsOnStatusChangeActivation`, `ItemHealHp`, `TryCureParalysis` …
`TryCureAnyStatus`), `GetBattlerHoldEffectInternal`, `HasEnoughHpToEatBerry`,
the endure step of the damage (`IsBattlerAtMaxHp`, after Sturdy),
`Cmd_moveend` (`MOVEEND_ITEM_EFFECTS_TARGET`, `…_ATTACKER`,
`…_ATTACKER_2`, the HP check of the other Pokémon, `MOVEEND_ITEMS_EFFECTS_ALL`),
`Cmd_removeitem` (`cudChewTimer`), the battle scripts
`BattleScript_HangedOnMsg`, `BattleScript_DoTurnDmg`,
`BattleScript_ItemHealHP_RemoveItemRet`, and the build's own hold-effect
tests. 41 reference tests (`make check TESTS="GC V19 reference"`,
`test/battle/gc_consumable_item_reference.c`) fix what the GC follows:

| Point | This build |
|---|---|
| HP threshold | HP ≤ max HP / 2 (floor); Sitrus heals max HP × 25 / 100 = max HP / 4, at least 1; Oran 10; both capped at max HP; eaten once |
| A status from a move | cured at once, after Synchronize (the status Synchronize gives back is cured first, by its own berry) |
| A status from a hit, Static, a thaw | damage, then the status, then the berry; Aspear thaws a freeze from a hit |
| Confusion | cured at the end of the move (`ITEM_EFFECTS_TARGET`): Confuse Ray, Swagger |
| On entry | a status the Pokémon enters with, then HP it enters with; at the battle start each lead's entry ability, then its item, in speed order |
| A single hit | the attacker's check after its recoil and drain (`…_ATTACKER_2`), before Moxie; then the other Pokémon's HP in battler order; then every Pokémon's status and HP (`…_ALL`) |
| A multi-hit move | the target's status, the attacker's checks, then the target's HP, between the hits |
| A spread move | every target is hit first; then each target in turn: Sturdy / Focus Sash, its effects and contact abilities, its status berry, the attacker's checks |
| Residual damage | weather damage and status damage: the HP berry at once; Solar Power and Dry Skin sun damage: no check, the berry waits for the next move end (also after a turn lost to full paralysis) |
| Other HP losses | Substitute's cost, Rough Skin, Struggle's recoil, a confusion self-hit: the berry right after (Substitute: no animation in this build) |
| Switches | a pivot move: the target's berry before the user leaves; Dragon Tail drags the target out before its berry; a berry eaten before a switch is not back after it |
| Focus Sash | at full HP only, after Sturdy (Sturdy holds first and the sash is kept); also against a confusion self-hit; not against poison or weather; once in a multi-hit move; the HP bar, then the message |
| Turned off | `GetBattlerHoldEffectInternal`: Klutz, Embargo, Magic Room; Unnerve / As One stop berries |

### 12.2 The consumption state

| State | Where |
|---|---|
| Item at the battle start | `BattleEntry.record.held_item` with `BattleEntry.mon.item` (the request; never changed) |
| Item held now | `Battler.item_from` (an entry, or NULL: holds nothing). `battle_held_item()` and `bc_item()` read only this, so a used-up item has no id and no record |
| Used up | `item_from == NULL` and `Battler.item_used` set |
| Last item used up | `Battler.item_used` (the entry whose item it was) |
| Why and when | `Battler.item_used_timing` (`BITIME_HP_THRESHOLD`, `BITIME_STATUS`, `BITIME_LETHAL_HIT`) and `Battler.item_used_turn` (the turns completed then); `EV_ITEM_CONSUMED` carries both and the item id |
| End of the battle | nothing: the result carries no item, the GBA save keeps the held item (12.6) |

The state belongs to the roster Pokémon (`Battler`, one per roster entry,
not per position), so a switch, Shift or Rotation never brings an item back.
A record with `BIREC_CONSUMED` that acts uses the item up (`use_up`); a
record without it (Leftovers, Black Sludge) never does. The history is one
item deep: enough for Recycle, Harvest, Belch or Cud Chew later, which need
"the last item used up"; nothing else is kept.

### 12.3 Request V19

V19 is V18's layout (no block, no size change: the largest request stays
3,165 B) with two record kinds, one HP timing and one record flag:

| Byte | `BITEM_HP` at `BITIME_HP_THRESHOLD` | `BITEM_CURE` | `BITEM_ENDURE` |
|---|---|---|---|
| 0 | kind 4 | kind 5 | kind 6 |
| 1 | timing 2 | timing `BITIME_STATUS` 3 | timing `BITIME_LETHAL_HIT` 4 |
| 2 | `BIHP_HEAL` (1) or `BIHP_HEAL_FIXED` (3) | major status mask (1 << `BST_*`, bits 1–7) | condition `BIEND_FULL_HP` 1 |
| 3 | divisor, or the fixed HP (≥ 1) | volatile mask (`BIVOL_CONFUSION` 1) | 0 |
| 4–5 | predicate `BPRED_HP_AT_MOST`, argument ≥ 1 (HP ≤ max HP / argument) | 0 | 0 |
| 6 | 0 | 0 | 0 |
| 7 | flags: `BIREC_CONSUMED` (1) | flags | flags |

The decoder and `battle_item_valid` refuse these records in V18 and
earlier, `BIREC_CONSUMED` on any other record or in V18, any other timing,
operation, predicate or condition, an empty or unknown mask, a reserved or
padding byte, and a second CURE or ENDURE record in one item (they would
contradict each other). `bc_items_check` applies the same check to a request
built in memory. No item id goes into the core: the records are the
definition; the id is only for the name.

### 12.4 Check points and order

Each check is a call at its own point, not an event engine:

| Check | Called from | Does |
|---|---|---|
| `bc_item_status_check` | `bc_set_status` (a status given, after Synchronize); the move's end per target; entry; the actor and ALL passes | the first CURE record that matches cures, emits `EV_ITEM_CURE` (status, then confusion), uses the item up |
| `bc_item_hp_check` | weather damage, status damage, entry, between hits, the actor and ALL passes | the first threshold record whose predicate holds heals, emits `EV_ITEM_HEAL`, uses the item up |
| `bc_item_endure` / `bc_item_endured` | the damage of a hit and of a confusion self-hit, after Sturdy | the amount becomes HP − 1; then `EV_ITEM_ENDURE` and the item used up where the reference shows it |
| `bc_item_on_entry` | `send_out`, and the battle start after each lead's entry abilities | status, else HP |
| `bc_items_actor_check` | after the recoil and drain, before Moxie; Struggle after its recoil | the actor's status, else its HP |
| `bc_items_after_action` | the end of every move (`bc_execute_move`), Struggle, an action lost to sleep, freeze, paralysis or confusion | the actor (unless done), every other Pokémon's HP, then every Pokémon's status and HP, in battler order |

Battler order is the reference's: player left, opponent left, player right,
opponent right … over the Pokémon on the field. A single-hit spread move in
V19 hits every target first, then gives each its results (Sturdy, Focus
Sash, effects, contact abilities, its status berry); V18 and earlier keep the
target-by-target order (their events are unchanged, 12.7).

Presentation: "Restored HP with its held item." (as in V18), "Cured by its
held item!" with the status label or CONF, "Hung on using its held item!",
each with the holder's name and the item's game-rendered label;
`EV_ITEM_CONSUMED` shows nothing (the message of what it did says it). The
companion logs the records (`PH12 item id=523 record0 hp timing=2 heal 1/4
pred=01:02 flags=01`, `… cure timing=3 status=fe volatiles=01 flags=01`,
`… endure timing=4 condition=1 flags=01`).

### 12.5 Exporter, normalisation, strict mode

`ResolveItem` (V19 rosters only, `ConsumableItemsEnabled`): a berry goes to
`ResolveConsumable`, which reads its hold effect and parameter
(`RESTORE_HP` → fixed heal; `RESTORE_PCT_HP` with a percent dividing 100 →
heal 100 / percent; `CURE_*` → the masks), else it stays UNSUPPORTED;
`HOLD_EFFECT_FOCUS_SASH` → the endure record. A V18 roster sends every berry
and Focus Sash as UNSUPPORTED as before.

The M4-B0 rules stay: all or nothing (`RULE_IGNORE_ITEMS`), strict refusal,
the item ability list, which already holds every ability that eats, steals,
gives or blocks berries (Klutz, Gluttony, Ripen, Cheek Pouch, Harvest,
Pickup, Unnerve, As One, Unburden, Symbiosis, Pickpocket, Magician, Sticky
Hold, Frisk, Ball Fetch …): any of them on either side turns every item off.
New in V19 (`ConsumableItemAbility`): Cud Chew on a holder of a berry with a
used-up record (the GC does not eat it again). `HpItemAbility` (Power
Construct …) applies to the threshold records too. Z-Crystal and Mega Stone
acceptance (section 11), strict moves and abilities and `RULE_IGNORE_ITEMS`
are unchanged; item-moving moves (Bug Bite, Pluck, Incinerate, Knock Off,
Fling, Belch …) are unsupported moves the GC never runs.

### 12.6 Persistence (a later stage; design memo)

M4-B1b changes nothing in the save: the result carries HP, PP and status
only, so a used-up berry or sash is back after the battle, as in a facility
battle. A later stage that makes it permanent should:

- add one result field per party entry, "item used up" (or the held item
  after the battle), set from `item_from == NULL && item_used == entry`;
- apply it on the GBA under the same nonce rule as the result (a result
  already applied is never applied again, so the item cannot be removed
  twice), and only where the game would (not in facility battles, which
  restore items);
- keep the GC side free of item ids: the GBA maps "used up" to the entry's
  own `held_item`.

### 12.7 Verification

| Check | Result |
|---|---|
| Native (-O2, ASan + UBSan, presentation, asset check) | all suites pass. New: `test_v19_reference` (the reference cases as GC battles), `test_v19_more`, `test_v19_gaps` (added after the first mutation run), `test_v19_wire`, `test_v19_formats` (240 random battles in four formats run twice: identical events, state and RNG; the 80 normalised ones event for event the battle without items; 298 items used up; 545 heal (Leftovers included), 148 cure and 1 endure events; at most 28 events in a turn); the presentation test checks the new messages and that `EV_ITEM_CONSUMED` adds no frame |
| Expansion reference (`make check TESTS="GC V19 reference"`) | 41 tests, all PASS: the 36 written with the code and 5 added after the mutation run (a multi-hit move into Rough Skin, Moxie after the berry, a confusing contact hit, Focus Sash before the hit's paralysis, a lead's own entry ability before its own berry), each agreeing with the GC order it fixes |
| Exporter (`make check TESTS="GC V19 export"`) | 5 tests PASS: the nine berries and Focus Sash as records (V19) and UNSUPPORTED (V18); every other berry and Berry Juice UNSUPPORTED, every berry UNSUPPORTED before V19; Cud Chew, Gluttony, Unnerve, Ripen, Cheek Pouch, Harvest, Power Construct turn items off; strict refusal of Figy Berry; a Z-Crystal beside a berry accepted; the tally |
| Mutations (GC core) | 94 changes (records, thresholds, amounts, cap, use-up and history, each check point, battler order, spread two-pass, Focus Sash, validation, wire, presentation): 64 caught in the first run; of the 30 that survived, 25 were test gaps, closed by `test_v19_gaps` and stronger cases (entry before the next action, Struggle with a target berry, V18 refusal without the flag, a type predicate at the threshold, the consumed event's frames) and then caught; 5 remain: 4 equivalent (the sleep counter reset: read only while asleep and set by every new sleep; normalised items: `bc_item` already gives no records; V19 checks on a V18 request: it has no V19 records; Sash before Sturdy: never both) and the kind name of the companion log (diagnostic only) |
| Mutations (exporter) | 22 changes to `src/frontier_gc_battle.c` (the version gate, the threshold, Oran and Sitrus amounts, use-up flags, Focus Sash condition, each cure mask, Persim, unknown berries, Berry Juice, the berry pocket path, Cud Chew): 19 caught in the first run; 1 test gap (a berry with no hold effect, such as Razz Berry, reaching the hold-effect switch in a V18 roster, where HOLD_EFFECT_NONE means no item) closed by checking every berry before V19, then caught; 2 equivalent: the divisibility check of the percent (every percent berry is 25) and Cud Chew with a record not used up (every berry record is) |
| Traces V12–V18 (1,200 battles each) | identical to M4-B1a+ (V18 total `65a3b885`) |
| Frontier `make check` | 6,945 tests: 4,029 PASS, 2,455 FAILED (the same 2,455 as M4-B1a+ and `frontier-hack`); against the M4-B1a+ run (6,899) no test changed status and the only additions are the 46 V19 tests (41 reference, 5 exporter), all PASS |
| Frontier `GC V1` | the V14–V18 references and exporter tests PASS; the L01–L64 fixture requests byte for byte the M4-B1a+ ones (64 `FIXTURE` lines identical) |
| Dolphin L65–L71 (V19) | L65 Single: Lum cures Snorlax's paralysis at once ("Cured by its held item!" PAR), Jolteon's Sitrus +27 after a hit (47 → 74), Tyranitar's Focus Sash holds at 1 HP ("Hung on using its held item!") and is gone. L66 Double: a Sitrus +31, Lum cures a paralysis, Oran +10. L67 Triple: Sitrus +27, Persim cures confusion. L68 Rotation: only the front: Sitrus +46, then Lum cures sleep after rotations. L69 = L65 with Life Orb, same seed: "NORMALISED", no item event. L70 Cud Chew with Sitrus: every item off. L71 refused on the GBA: "Strict items: not simulated: フィラのみ" |
| Dolphin L01–L64 | 60 of 60 battles line for line equal to M4-B1a+ (request bytes and CRC, events, actions, results); L42, L48, L56, L63 refused on the GBA as before |
| Result contract (V19, L65) | GC Z right after the result: "result already final (nonce=ff57373a); not restarting the battle"; nothing applied twice |
| Save | Frontier save SHA-256 `AFF1EB7B…` before and after; the profile restored (`F838F993…`, expansion ROM) |
| JoyBus (70 sessions) | GC 141,193 sent, CRC 0, 124 retries, 7 timeouts, all recovered; GBA 140,987 requests, CRC 0, overflows 0; 1 link loss, the deliberate Z cut |
| Not run | Cube Tester and the debug-menu V1–V12 entries in Dolphin (their requests are among the byte-for-byte fixtures); a real GameCube and GBA |

### 12.8 Memory, link and time

| Item | M4-B1a+ | M4-B1b |
|---|---:|---:|
| `BattleRequest` / `EventLog` (x86-64 host) | 6,344 / 47,812 B | unchanged |
| `Battle` (x86-64 host) | 4,280 B | 4,472 B (+16 per `Battler`: the history) |
| Request buffer `BATTLE_REQUEST_MAX_BYTES(6, 21)` | 3,165 B | 3,165 B |
| ELF text / data / bss | 283,310 / 70,276 / 3,002,528 | 289,886 / 71,644 / 3,002,624 |
| DOL | 353,888 B | 361,824 B |
| Frontier ROM used | 26,368,776 B | 26,371,452 B |
| EWRAM (`.ewram.sbss`); GBA request heap | 232,048 B; `BattleRequest` | unchanged |
| A V19 request | — | the same 8 B per record (L65 1,421 B, L66–L68 1,805 B) |
| Link | — | one label per item name, once per session (about 200 wire bytes each; L66: 6 labels, 1,187 B) |
| GC core per turn (Dolphin peak) | 310 µs | 319 µs overall (a V18 Lab, as noise); V19 peak 163 µs (L67 Triple, 17 events) |
| Events per turn | — | at most 3 more per item use (heal / cure / endure, the use); 28 in the worst random turn; the log holds 1,992 |

### 12.9 Battle Frontier coverage (measured)

The run of 11.9 (every `gBattleFrontierMons` set as the player's lead in a
Single against a plain Snorlax), V18 and V19 side by side (`GC V19 export
tally`). Sets, not facility battles, and not a win rate.

| Measure | V18 | V19 |
|---|---:|---:|
| Requests built | 1,996 | 1,996 |
| Items take part (not normalised) | 852 | 992 (48.6% of 2,040) |
| Items, ability and all moves simulated | 103 | 108 |
| Strict battle that starts | 103 | 108 |

The 140 more sets whose items take part: Focus Sash 100, HP berries 22,
status berries 18; of the 5 more fully simulated, 4 hold Focus Sash and 1 a
status berry. Sets holding: Focus Sash 113, an HP berry 39, a status berry
19; the rest are kept off by another item of the battle, an item ability
(48 in V19, Cud Chew among them) or an unsupported move or ability.

### 12.10 Known limits

- (Repaired, 12.11) An action lost to recharge ran no move-end item pass.
- The ALL pass runs before a pivot's switch; a U-turn user with Solar Power
  at the berry threshold would differ (no fixture or Frontier set needs it).
- Item-moving moves stay unsupported; Recycle, Harvest and Cud Chew have the
  history but are not simulated.
- Every other berry and consumable item stays unsupported (12, Deferred).
- The GBA screen shows no item activation (the GC does).

### 12.11 ACT-002 repair: the recharge turn's move end

Branches `milestone4b1b-act002` and Frontier `frontier-gc-m4b1b-act002`,
still release 13 and request V19 (no new version). The battle-reference
audit (`docs/battle-reference`, ACT-002) found that this build's
`CancelerRecharge` sends a recharging Pokemon to
`BattleScript_MoveUsedMustRecharge`, which ends with `moveendall`: the turn
it must recharge runs the move end's item checks like any other lost turn,
while the GC's `continue_turn` stopped at `EV_RECHARGE`. A berry left due by
damage that checks no item (Solar Power's sun damage) was therefore eaten
after the next Pokemon's move instead of before it.

`continue_turn` now calls `bc_items_after_action(b, user, false, log)` after
`EV_RECHARGE`, the call the other lost turns make: the user's status, then
HP; every other Pokemon's HP in battler order; everyone's status, then HP.
It does nothing below V19, so V1-V18 and their fixed-seed traces are
unchanged. The recharge still spends no PP, and a Pokemon knocked out
before its recharge turn is skipped (no recharge, no move end of its own).

| Check | Result |
|---|---|
| Expansion reference (`gc_recharge_item_reference.c`) | 6 tests PASS: Solar Power leaves Sitrus due, the recharge message (matched in this build's Japanese text), the berry 70 -> 110, then the foe's move, PP spent once; no berry and Unnerve: nothing; Double: the recharging Pokemon's own berry, then an ally's and a foe's in battler order; knocked out first: skipped, berry kept; a sleeping Pokemon's lost turn in the same order; `recharge.c` without its English MESSAGE |
| GC before the repair | `test_v19_recharge` fails: `EV_RECHARGE`, the foe's move, then the berry |
| GC after the repair | `test_v19_recharge` passes: Single, Double, Triple, Rotation; strict items; no berry; `RULE_IGNORE_ITEMS`; a status berry (status set directly: every status of this build checks items when it comes); Double order; knocked out first. -O2, `-Werror`, ASan/UBSan, presentation pass |
| Mutations | 9: 6 caught (no call, the call before the message, only the user's check, no check of the other Pokemon's HP, no recharge turn, and the user's own check skipped once the Double case had the user's berry due); 3 equivalent (the pass twice: nothing is left to eat; resetting the Protect chain on the recharge turn: the recharge move already reset it; a pass on a fainted Pokemon's skipped action: the move that knocked it out already ran every check) |
| Traces V12-V19 (1,200 battles each) | identical to M4-B1b |
| Dolphin L72 (new) | "Must recharge!" (56/118), "Restored HP with its held item." Sitrus (85/118), then the foe's Sunny Day; PP 4/5; core peak 48 us, 8 events |
| Dolphin L01-L71 | 71 of 71 line for line equal to M4-B1b (66 battles, 5 GBA refusals) |
| Result contract, JoyBus, save | "result already final; not restarting the battle"; 72 sessions, CRC 0 both ways, no overflow, one deliberate link cut; Frontier save AFF1EB7B before and after |
| Sizes | Battle 4,472, BattleRequest 6,344, EventLog 47,812, max request 3,165 unchanged; companion text +32 B (289,918), DOL 361,824 unchanged; ROM used +192 B (L72) |
| Frontier `make check` | 6,951 tests: 4,035 PASS, 2,455 FAILED (the same 2,455 as M4-B1b; `recharge.c` keeps failing on its English text); against M4-B1b (6,945) no test changed status and the only additions are the 6 recharge reference tests, all PASS |

Still open: Expansion checks the chosen target's pending status
(`MOVEEND_ITEM_EFFECTS_TARGET`) before the user on every lost turn; the GC
checks the user first. No supported effect leaves a status pending, so no
battle shows it (battle-reference open questions).

## 13. M4-B1c-0a: Life Orb, specification and reference (no implementation)

M4-B1c-0 brings Life Orb to the GC core in request V20. This first step fixes
its behaviour from this build's code and runs, adds the reference tests and
designs V20; the core, the exporter and the protocol are unchanged (V19 still
sends Life Orb as UNSUPPORTED: every item off, strict refused). Branches
`milestone4b1c0a` and `frontier-gc-m4b1c0a`.

### 13.1 Expansion (frontier-gc-m4b1b-ability-fixes b091548d05, Gen 9 defaults)

Damage. `GetAttackerItemsModifier` returns `UQ_4_12_FLOORED(1.3)` = 5324
(truncated, not 5325). It is one factor of `GetOtherModifiers`, the final
chain: the move's factors (Minimize, underground, dive, airborne, screens,
Collision Course), then, when the attacker's unmodified speed is at least
the defender's, the attacker's ability, the defender's ability, the
defender's partner (Friend Guard), the attacker's item, the defender's item;
otherwise the defender's ability, its partner, the attacker's ability, the
defender's item, the attacker's item. Each factor joins the chain with
`uq4_12_multiply_half_down`; the chain is applied once to the damage with
`uq4_12_multiply_by_int_half_down` after STAB, type, burn and the
protection factor, then the damage minimum of 1
(`ApplyModifiersAfterDmgRoll`). The factor does not depend on type
effectiveness or a critical hit (both come earlier). Fixed-damage moves
(`DoFixedDamageMoveCalc`) never enter the chain. Drain and recoil are
computed from the HP the boosted hit took; each hit of a multi-hit move and
each target of a spread move (after the spread factor) is boosted.

HP loss. `TryLifeOrb` at `MOVEEND_LIFE_ORB_SHELL_BELL`, once per move:
the holder is alive (`ItemBattleEffects`), it used the move
(`!unableToUseMove`), it is still the battler that started the move
(`originalBattlerPartyId == PARTY_SIZE`; only a mid-move switch-out of the
attacker sets it), a target was damaged this turn including substitutes or
`savedDmg > 0`, and it has no active Magic Guard. The loss is
`GetNonDynamaxMaxHP / 10`, at least 1 (`SetPassiveDamageAmount`), shown
with the item pop-up; `tryfaintmon` faints the holder at once. The item is
not used up. The stage comes after the drain (`MOVEEND_ABSORB`), the contact
abilities, the target's faint (`MOVEEND_FAINT_BLOCK`), the multi-hit count,
the recoil, the attacker's own item check and Moxie
(`MOVEEND_ABILITY_EFFECT_FOES_FAINTED`), and before Emergency Exit, the
pivot's switch (`MOVEEND_HIT_ESCAPE`) and the all-battler item check.

Abilities. Magic Guard keeps the damage factor and stops the loss. Sheer
Force: when `MoveIsAffectedBySheerForce`, `MOVEEND_SHEER_FORCE` jumps to
`MOVEEND_ITEMS_EFFECTS_ALL`, so the loss (and the stages between) is
skipped while both boosts apply; a move without such an effect loses HP.
Klutz (and Embargo, Magic Room) make `GetBattlerHoldEffect` return none:
neither the factor nor the loss. A suppressed ability (Gastro Acid,
Neutralizing Gas) no longer stops anything. Mold Breaker does not touch the
holder's own Magic Guard (it ignores the target's breakable abilities).

### 13.2 Reference tests (`test/battle/gc_life_orb_reference.c`, 22 tests)

Built by `make check` from `frontier-gc-m4b1c0a` (b091548d05 plus this
file; the test ELF is generated from the tree). No MESSAGE checks except two
faint texts in Japanese; the order is carried by HP bars, damage captures,
`ITEM_POPUP` and animations. Damage rolls 100/93/85% (`WITH_RNG(RNG_DAMAGE_MODIFIER, 0/7/15)`),
no critical hit unless stated. Two runs gave the same values. Ten
deliberately wrong expectations (the loss before the recoil, rounded up,
with Magic Guard, with Sheer Force, Klutz boosting, a second loss on the
recharge turn, a loss between two hits, a loss per spread target, the
holder fainting first, the loss after the pivot's switch) all failed.

| Case | Expected | Measured |
|---|---|---|
| Physical: Strength, Snorlax L50 Atk 100 vs Def 100 | Life Orb = hd(d x 5324) | 55→71, 51→66, 46→60 |
| Special: Hyper Voice, SpA 100 vs SpD 100 | the same | 61→79, 57→74, 51→66 |
| Truncation: L100 Atk 400 vs Def 60 | 5324, not 5325 | 675→877 (5325: 878), 627→815, 573→745 |
| Water Gun x4, x2, critical | the same factor | 112→146, 56→73, 42→55 |
| Multiscale (x0.5), attacker faster and slower | one chain hd(5324, 2048) = 2662 | 108→70 both |
| Seismic Toss | no boost, the loss | 50 damage, loss 20 of 200 |
| Giga Drain | heal = half of the boosted damage, then the loss | 51→66, heal 33, loss 30 |
| Double-Edge | recoil 33% of the boosted damage, then the loss | recoil, then loss 30 |
| Substitute | boosted, the loss | 24→31, loss 30 |
| Hyper Voice, Double, both foes | each boosted, one loss | 69→90 on each, HP 300→270 |
| Double Kick | both hits boosted, one loss after the last | 42→55 per hit, HP 300→270 |
| Loss size | max HP / 10, at least 1 | 105→10, 109→10, 9→1, 1→1 (faints) |
| HP 1 | the holder faints after its hit, the foe still moves | yes |
| Knock-out, then own faint | target faints first, then the loss and the holder's faint | yes |
| Miss, Ghost immunity, Volt Absorb, Protect, Growl | no loss | HP 200 |
| Hyper Beam | loss on the attack turn only | 300→270, then 270 |
| Rough Skin knocks the holder out | no Life Orb | no pop-up |
| U-turn | loss before the switch | party HP 180 of 200 |
| Magic Guard | boost kept, no loss | 37→48, HP 300 |
| Gastro Acid on Magic Guard | the loss | 30 |
| Sheer Force | Rock Slide boosted, no loss; Strength boosted, loss | 45→58 no loss; 37→48, loss 30 |
| Klutz; Klutz suppressed | nothing; boost and loss | 55→55; 55→71, loss 30 |

### 13.3 The GC now (V19)

| Point | V19 | V20 needs |
|---|---|---|
| The damage arithmetic | already the reference's: a 130% `BMOD_FLOORED` record at `BMOD_FINAL` held by an ability gives every number above (`test_c0a_life_orb_arithmetic`: 55/51/46 → 71/66/60, 61/57/51 → 79/74/66, 675 → 877 and 878 with the nearest factor, 108 → 70) | the record from the item (`BMSRC_ITEM` is power-only in V17-V19; the codec refuses a final item record, the test pins it) |
| Its place in the chain | the final passes are move, attacker's ability, defender's ability (slower: the defender's first) | an item pass after the defender's ability (faster) or last (slower); the defender's item has no record yet |
| The loss | no record kind; BITEM_HP has end-of-turn, HP-threshold timings | a timing after the move (BITIME 5 refused by V19) |
| Fixed damage, Struggle | no fixed-damage move is exported; Struggle has its own path | Struggle: boosted and the loss in Expansion (no exception; not run) |
| Life Orb in a request | UNSUPPORTED (every item off, strict refused) | RECORDS |

### 13.4 Request V20 design (minimal, not implemented)

- **Wire:** the V19 layout; V20 only widens what an item block may hold.
  `BITEM_MODIFIER` may use `BMOD_FINAL` (with `BMOD_FLOORED`; predicates as
  for the power stage). `BITEM_HP` gains timing `BITIME_AFTER_MOVE` (5):
  operation `BIHP_DAMAGE`, divisor 2..255, predicate none, no
  `BIREC_CONSUMED`. Life Orb = these two records. A V19 request means what
  it meant; a V20 record in V19 is malformed (as now).
- **Damage:** `fx_final_passes` / `fx_final_slower_passes` get an item pass
  for V20 only: `MOVE, ATTACKER, DEFENDER, ITEM` and `MOVE, DEFENDER,
  ATTACKER, ITEM`. The chain is already one product applied once (V14); no
  other stage moves, so V14-V19 damage stays byte for byte.
- **One move's state:** `MoveContext` already has `landed` (a hit landed,
  a substitute included) and `lost`; V20 adds nothing on the wire. The
  after-move point is in `execute_move` after `bc_ability_after_move`
  (Moxie) and before it returns the pivot: the reference's order (recoil,
  the user's own item check, Moxie, Life Orb, then the pivot and the
  all-battler check in `bc_execute_move`). Conditions: the move reached its
  hits (lost turns, charge turns, the recharge turn, Protect and Substitute
  moves never get there), `c.landed`, the holder is not fainted. Struggle
  calls the same point. Each hit's factor needs nothing per hit; the loss
  runs once for multi-hit and spread moves.
- **Unsupported, kept safe:** the definition stays the item's. Magic Guard
  already turns every item off (strict: refuses) through `HpItemAbility`,
  since the after-move record is HP damage (as for Black Sludge); V20 adds
  Sheer Force for a holder of an after-move record, and Klutz is already an
  `ItemAbility`. Neutralizing Gas (it would lift Klutz and Magic Guard) is an
  ability the core does not run: strict refuses it, non-strict keeps the
  items as declared, as for every item now. No Gastro Acid move is exported.
  A Magic Guard or Sheer Force hook can replace these rules later.
- **Old versions:** V1-V19 requests, traces and Lab L01-L72 unchanged by
  construction (the new pass and timing are V20-gated; the exporter writes
  V20 only for a V20 roster).
- **Faint order (ABL-G04):** with Life Orb the target's faint comes before
  the loss on both sides (the reference faints the target at
  `MOVEEND_FAINT_BLOCK`, before the Life Orb stage), so V20 does not need
  the ABL-G04 change. Moving the GC's target `EV_FAINT` after the contact
  abilities would change every knock-out's event order (the GC battle
  view's text, `PH3 EVENT` lines of the Lab fixtures that knock a Pokemon
  out, the replay and trace hashes); keep it a separate stage with its own
  version gate.

### 13.5 V20 cases saved for later (`tools/v20-tests.sh`)

`battle_test.c` holds `test_v20_life_orb` under `#ifdef GC_TEST_V20`; the
default build leaves it out and the V19 suite passes. The script builds it
with `-Werror` and runs only those cases (the identifiers
`BATTLE_REQUEST_VERSION_MOVE_ITEMS` and `BITIME_AFTER_MOVE` are the
design's, defined locally until the protocol has them). Now: 14 of 14
checks fail at the start (the request is refused), exit 1, as expected.
The checks: the request starts; the reference's 71/66/60; the loss of
max HP / 10 at least 1 after the hit (105, 109, 9, 1 with the faint); no
loss on a miss, an immunity, Protect or a status move; the loss behind a
substitute; one loss after a two-hit move's count; recoil before the loss;
the target's faint before the holder's; no loss on the recharge turn.

### 13.6 Related known differences (unchanged)

- ABL-G04 (contact knock-out faint order): kept for V1-V19; independent of
  Life Orb (13.4).
- ABL-G05 (hook lists run in part): refused since the ability fixes; Life
  Orb adds no hook.
- ITM-TIME-003 (a pivot's switch and pending berries): the GC switches after
  the all-battler check; Expansion has Life Orb before the switch, which the
  V20 point keeps (the loss before the pivot), so ITM-TIME-003 neither blocks
  nor changes it.
- ACT-002 (recharge turn's move end): the recharge turn never reaches the
  after-move point; the reference has no loss there either.

### 13.7 Not verified

Struggle with Life Orb (no PP left; source only), Dynamax HP, the holder
switched out mid-move (Eject Button, Red Card, Emergency Exit are not
supported), the defender's item and screens in the same chain, Shell Bell
at the same stage, Neutralizing Gas, Triple and Rotation (the reference has
Single and Double; the GC's V20 cases are Single).

## 14. M4-B1c-0b: Life Orb in request V20

Branches `milestone4b1c0b` (GC) and `frontier-gc-m4b1c0b` (Frontier), still
release 13 (development). The design of 13.4, built on the existing item
records, modifiers and HP handling (Codex's plan B, `docs/item-dispatcher-design.md`;
no separate item dispatcher).

### 14.1 Request / APP V20

| Part | V20 |
|---|---|
| Layout | V19's; Action, Result, state, EventLog unchanged |
| `BITEM_MODIFIER` | may use `BMOD_FINAL` (with `BMOD_FLOORED`; no defender flag, as every item record); other stages but power stay refused |
| `BITEM_HP` | timing `BITIME_AFTER_MOVE` (5): operation `BIHP_DAMAGE`, divisor 2..255, predicate none (argument 0), reserved 0, no `BIREC_CONSUMED`, at most one per item |
| Life Orb | `MODIFIER(BMOD_FINAL \| BMOD_FLOORED, 130)` + `HP(AFTER_MOVE, DAMAGE, 10)`, in that order (either order is valid) |
| Older versions | V19 and older refuse both (the encoder writes nothing, the decoder and `battle_init` refuse): a V20 record never reaches an older peer |
| Negotiation | the GC companion accepts APP V20 (mode check, action layout as V6-V19); the request version must equal the APP version (as before); a V19 GC refuses a V20 GBA at the mode check, so the effect is never dropped silently |
| Size | a Life Orb block is 20 bytes against the 4-byte UNSUPPORTED block: +16 per holder, +192 at most (12 holders); `BATTLE_REQUEST_MAX_BYTES(6, 21)` = 3,165 unchanged |

`battle_item_valid` holds every rule, so the encoder, the decoder and the
in-memory start (`bc_items_check`) apply the same ones.

### 14.2 The final factor

`battle_modifiers.c`: for V20 only, the final stage uses `MOVE, ATTACKER,
DEFENDER, ITEM` when the attacker is at least as fast and `MOVE, DEFENDER,
ATTACKER, ITEM` when it is slower (`GetOtherModifiers`: the attacker's item
after both abilities and the partner; the defender's item, which has no
record yet, would follow). The item pass is the existing `run_items`; the
130% truncated record becomes 5324 and joins the one 4.12 product the chain
already applies once to the damage. V14-V19 keep their passes.

| Case (the reference's stats) | None → Life Orb (GC = Expansion) |
|---|---|
| Strength, Atk 100 vs Def 100 | 55 → 71, 51 → 66, 46 → 60 |
| Hyper Voice, SpA 100 vs SpD 100 | 61 → 79, 57 → 74, 51 → 66 |
| Strength, L100 Atk 400 vs Def 60 | 675 → 877 (5325 would give 878), 627 → 815, 573 → 745 |
| A substitute's hit | 24 → 31 |
| Each target of a spread move | 69 → 90 |
| Each hit of Double Kick | 42 → 55 |
| Struggle (typeless 50) | 24 → 31, 22 → 29, 20 → 26 |
| With a defender's x0.5, faster and slower | 108 → 70 |

The chain order is also pinned with factors no game ability has (50% and
95% on 64: 39, where the item before the second ability gives 40; a slower
attacker's two 55% factors and a defender's 75% on 100: 30, the faster order
29). No fixed-damage move is exported (the exporter refuses them), so the
reference's "no boost for fixed damage" has no GC path.

### 14.3 The loss after the move

`bc_item_after_move` (`battle_items.c`), called once per move:

| Where | Order around it |
|---|---|
| `execute_move` | per-target secondaries and drain, contact abilities, faints, the hit count, the after-move secondaries, recoil, the user's V19 item check, Moxie → **Life Orb** → (recharge flag) → `bc_items_after_action` and the pivot's switch (M4-B1c-0c: Life Orb moved into `bc_items_after_action`, after the other Pokemon's HP berries, 15.4) |
| `execute_struggle` | the hit, Struggle's recoil (max HP / 4), the user's item check, Moxie → **Life Orb** → `bc_items_after_action` (M4-B1c-0c: inside it, as above) |

It acts when the move landed on a target (`MoveContext.landed`: a damaging
hit, a substitute's included; a miss, an immunity, an absorbing ability and
Protect leave it false), the battler at the user's position is the one that
started the move, and its item has the record (`bc_item`: items simulated,
still held). The loss is `bc_item_hp` with the new timing: max HP / 10, at
least 1, `EV_ITEM_DAMAGE` with the item id, not used up; at 0 HP the holder
faints through `bc_apply_damage` and the normal replacement and outcome
follow. Lost turns, charge turns and the recharge turn never reach the
point; status moves have no landed hit. One synchronous call per move: no
per-move flag was needed (multi-hit and spread moves call it once).

### 14.4 Abilities and the exporter

`ResolveItem` writes Life Orb's two records from roster V20; before it Life
Orb stays UNSUPPORTED (every item off, strict refused), as before. The GC
runs no Magic Guard, Sheer Force or Klutz, so beside Life Orb every item is
turned off (strict: refused):

| Ability of the holder | Reference | V20 export |
|---|---|---|
| Magic Guard | factor kept, no loss | off: `HpItemAbility` (a damage record), as for Black Sludge |
| Sheer Force | no loss only for the moves it affects | off: `HpItemAbility`, new for an after-move record (Sheer Force with Leftovers stays simulated) |
| Klutz | neither | off: `ItemAbility` (any holder, as from M4-B0) |
| Trace, Receiver, Power of Alchemy, Wandering Spirit | could gain the above | off (a damage record) |
| Neutralizing Gas, Gastro Acid | lift them | an ability the core does not run (strict refuses); Gastro Acid is not exported |

The item ids stay in the records (`held_item`, block source); a Pokemon
without an item is class NONE with id 0, never mixed with an item that has
no effect. The save is not written.

Battle Frontier sets against a plain Snorlax (V20, Single): 1,996 built, 141
hold Life Orb, 126 of them simulated and 15 off by an ability; 1,118 sets
simulated in V20 against 992 in V19 (strict accepts the same 1,118); every
other set has the same item state as in V19.

### 14.5 Verification

| Check | Result |
|---|---|
| Expansion reference (`gc_life_orb_reference.c`) | 27/27 PASS (the 22 of 13.2 and Struggle boosted; Struggle's recoil, then the loss; none after that recoil knocked the holder out; the loss after knocking out the last foe; Moxie, then the loss) |
| Saved V20 cases (`test_v20_life_orb`) | 14 cases, 14 started, 26 effect checks reached, 0 failed (`tools/v20-tests.sh` exit 0) |
| Native V20 tests | damage table, codec edges (divisor 1/2/255, reserved, predicate, operation, used-up flag, two records, other stages, the defender flag, V19 relabelling, encoder and `battle_init`), items off and strict, V19 unsupported, a spread move, a pivot (the loss before the prompt; at 1 HP a replacement instead), Struggle, Moxie, the last foe (at 1 HP both faint: a draw), Single / Double / Triple / Rotation |
| Native suites | -O2 -Wall -Wextra -Werror, ASan + UBSan, presentation: pass |
| Traces V12-V19 (1,200 battles each) | identical to M4-B1c-0a; a V20 trace (no Life Orb in the generator) equals V19 (65a3b885) |
| Mutations (GC) | 20 of 22 killed; the 2 survivors are equivalent (the version gate: a V19 request cannot hold the records; the holder identity check: no supported effect switches the user out mid-move) |
| Mutations (exporter) | 3 of 3 killed (no Sheer Force gate, Life Orb from V19, the divisor) |
| Frontier `make check` | see testing.md (2,455 FAILED, the same as before) |
| Sizes | Battle 4,472, BattleRequest 6,344, EventLog 47,812, max request 3,165 unchanged; companion text 291,046 (+424), data 71,828 (+88), DOL 363,168 (+544); ROM used +264 B |
| Time (host, -O2) | one damage calculation 412 ns (V19), 422 ns (V20 without Life Orb), 444-453 ns with it; one Single turn 1.5-1.8 us in every case (noise) |

Not run: Dolphin (no new Lab fixture: the Lab list is full and the GBA's
normal battles use APP V1-V12; V20 is reached through the exporter tests),
long JoyBus runs.

### 14.6 Known limits

- Magic Guard, Sheer Force and Klutz with Life Orb turn every item off
  instead of running their effect.
- The defender's item has no final record yet (Expert Belt and the resist
  berries are later stages).
- The holder switched out mid-move (Eject Button, Red Card, Emergency Exit),
  Dynamax HP and False Swipe's 0-damage case have no GC path; the
  reference's rules for them are recorded in 13.1.
- Both last Pokemon fainting (the target to the hit, the holder to Life Orb)
  is a draw in the GC; the reference's outcome there was not run (its rule
  for both sides empty is LOST | WON = DREW).
- ABL-G04, ITM-TIME-003 and ACT-002 are unchanged (13.6).

## 15. M4-B1c-0c: V20 on the GBA → GC path (Dolphin, JoyBus)

Branches `milestone4b1c0c` (GC) and `frontier-gc-m4b1c0c` (Frontier), still
release 13 (development). No new item, ability or move: request V20 and Life
Orb of section 14 run on the real GBA → GC path, one order mismatch found
there is fixed (15.4), and the checks of this stage are recorded.
Status words: Dolphin = run in Dolphin on the release-13 DOL and the
Frontier ROM (`TEST_CONFIRMED` on the real path); Native = the host core;
Expansion = this build's battle engine (test runner); Source = read in code
(`SOURCE_CONFIRMED`).

### 15.1 The V20 entry: GC V20 Lab

The normal battles and the debug/Cube Tester entries keep their APP versions
(V1-V12), and the GC Ability Lab list holds L01-L72 (full at the menu
limit). V20 gets its own list instead of renumbering or replacing anything:
debug menu → Utilities → **GC V20 Lab...** (one entry above GC Extension
Mode; the GC entries below it keep their places from the bottom). Its
fixtures L73-L83 go through the same path as every Lab fixture
(`DebugAction_GcAbilityLab` → `FrontierGcBattle_StartLab`, roster =
`BATTLE_APP_VERSION_MOVE_ITEMS`, `RULE_FIXED_SEED`, battle id `0x4C4142nn`,
results shown only). L01-L72 keep their numbers, contents and inputs.

| Lab | Format | What it shows | Inputs (scratchpad runner `lab_v20.ps1`) |
|---|---|---|---|
| L73 | Single | Snorlax with Life Orb: a hit (knock-out), Double Hit, Swords Dance, Giga Drain | moves 1-4 on turns 1-4 |
| L74 | Single | Machamp against a Ghost that uses Protect and Substitute: protected hits, a miss (Zap Cannon), Bulk Up | Body Slam, Shadow Ball, Zap Cannon x2, Bulk Up, Zap Cannon |
| L75 | Double | Aerodactyl's Rock Slide (spread): a Sitrus Berry target, a foe that faints, Leftovers beside | Rock Slide x3 |
| L76 | Triple | Rock Slide from the left, Earthquake from the centre (hits an ally; Aerodactyl immune), a foe's Life Orb | turn 2: the right Snorlax has no foe in reach (only Shift): B forfeits |
| L77 | Rotation | two holders a side; only the one that acts loses HP; rotations by both sides | R (rotate) on turns 2-3 |
| L78 | Single | Salamence (Moxie): Double-Edge knocks out (recoil, Moxie, loss), Hyper Beam, the recharge turn, U-turn on the last foe | moves 1, 2, (recharge), 3 |
| L79 | Single | the holder at 11 HP faints to its own Life Orb, both sides replace | Body Slam x2 |
| L80 | Single | the last foe faints, then the holder to Life Orb: a draw | Body Slam |
| L81 | Single | Sheer Force and Magic Guard holders (non-strict): every item off | Body Slam x2 |
| L82 | Single | Klutz with Life Orb under strict item rules: refused on the GBA | (refusal screen, B) |
| L83 | Single | a Ghost behind Substitute only: Body Slam has no effect, Shadow Ball hits the substitute | Body Slam, Shadow Ball x2 |

`test/battle/gc_v20_lab_export.c` builds L73-L83 through `Init` (as a
Dolphin run does): V20, both Life Orb records on every holder, item ids
kept, every move run, strict item rules accepted (L81 normalised, L82
refused).

### 15.2 GBA → GC transfer (Dolphin)

Every V20 Lab battle started as `PH3 MODE ... app=4247 v20` (MODE app version
20 = request version 20) and the GC accepted it (`PH3 CORE init=0`, items
`simulated (strict)`; L81 `NORMALISED`). The bytes the GC received (its PH9
REQ dump) have the length and CRC-32 that the GBA announced in MODE, for
all 11 fixtures, in both runs. Against the exporter test's build of the same
fixture (`Init`, no Dolphin) the L73 bytes differ only in the header's
trainer class and in each record's OT id (the save's trainer, set by the
running game): species, stats, moves, abilities and every item block,
Life Orb's two records included, are the same bytes. The GC's PH12 lines
show each holder as `item id=479 records=2`, `record0 modifier stage=45
percent=130` (BMOD_FINAL | BMOD_FLOORED), `record1 hp timing=5 damage 1/10`.

| Lab | Format | Holders | Request bytes | Wire bytes (RLE) | Chunks (224 B) | Request time |
|---|---|---|---|---|---|---|
| L73 | Single | 1 | 1,429 | 1,022 | 5 | 547 ms |
| L74 | Single | 1 | 1,101 | 817 | 4 | 450 ms |
| L75 | Double | 1 | 1,781 | 1,244 | 6 | 581 ms |
| L76 | Triple | 3 | 1,805 | 1,269 | 6 | 616 ms |
| L77 | Rotation | 4 | 1,829 | 1,301 | 6 | 538 ms |
| L78 | Single | 1 | 1,437 | 1,028 | 5 | 514 ms |
| L79 | Single | 1 | 1,397 | 964 | 5 | 508 ms |
| L80 | Single | 1 | 1,029 | 714 | 4 | 454 ms |
| L81 | Single | 2 | 1,037 | 739 | 4 | 508 ms |
| L83 | Single | 1 | 1,061 | 751 | 4 | 492 ms |

A Life Orb block is 20 bytes against 4 for an unsupported item (14.1): +16
raw bytes per holder before the RLE; the largest V20 Lab request (L77,
four holders) is 1,829 bytes, under the 3,165-byte maximum.

### 15.3 Damage and the loss (Dolphin, Native, Expansion)

Each Dolphin battle was replayed on the native core from its PH9 dump, seed
and commands (`battle_replay`), with the core of the DOL that ran it: 0
differing lines in all 21 V20 runs (11 on the M4-B1c-0b core before the fix
of 15.4, 10 on the final one).
`battle_replay -without-items` (new) replays the same battle with every held
item's modifier record removed (Life Orb keeps its HP record); until the
two battles part (a knock-out only one of them has), each Life Orb hit
must be `(d * 5324 + 2047) >> 12` of the same hit without it:

| Lab | Hit (Dolphin) | Without the factor → with |
|---|---|---|
| L73 / L79 | Body Slam into Blissey | 273 → 355 |
| L75 | Rock Slide (Double, one target hit), turns 1 and 2 | 48 → 62, 48 → 62 |
| L76 | Rock Slide (Triple, left); Earthquake (centre) on an ally and two foes; the foe's Cross Chop | 220 → 286; 81 → 105, 76 → 99, 84 → 109; 116 → 151 |
| L77 | the foe's Rock Slide; Body Slam (turns 1, 2) | 84 → 109; 24 → 31, 81 → 105 |
| L78 | Double-Edge, Hyper Beam, U-turn | 79 → 103, 56 → 73, 269 → 350 |
| L80 | Body Slam | 79 → 103 |

17 Life Orb hits, 0 mismatches; every other hit compared was unchanged.
`test/battle/gc_v20_lab_reference.c` gives this build's damage for the
fixtures' own stats at all 16 rolls, with and without Life Orb (Expansion):
Body Slam 273 → 355 is its roll 5, Double-Edge 79 → 103 its roll 15, and the
hits compared only after the battles part (L73's Double Hit 44 and 43, Giga
Drain 31) are in its Life Orb column.

The loss (Dolphin): max HP / 10 once per move, after the hit or the last hit
(Double Hit: one loss for two hits), after the drain (L73), after the recoil
and Moxie (L78), on the Hyper Beam turn and not on the recharge turn (L78),
after the last foe faints (L78 U-turn: the loss, then the win), none for a
status move (L73 Swords Dance, L74 Bulk Up), a protected hit (L74), a miss
(L74, L75 turn 3), no effect (L83 Body Slam on a Ghost); a hit on a
substitute counts (L83). The holder faints to it and is replaced (L79; L77
Rotation); with the last foe already down it is a draw (L80, outcome 3).
Spread moves: one loss per move (L76 Earthquake hits three, one loss). The
item is never used up: 19 losses in the final runs, no `EV_ITEM_CONSUMED`
for Life Orb in any run.

Rotation (L77, identity): the foe's Rhydon loses 17 when it attacks (turn 1),
nothing when the foe rotates and its Snorlax attacks (turn 2), 17 again when
Rhydon is rotated back and attacks (turn 3); the loss names the battler that
moved, not the front position.

### 15.4 Fixed: the other Pokemon's HP berries before Life Orb

Dolphin L75 showed the target's Sitrus Berry after the attacker's Life Orb
loss. The reference runs every Pokemon but the attacker's HP-threshold item
at `MOVEEND_KEE_MARANGA_HP_THRESHOLD_ITEM_TARGET`, before
`MOVEEND_LIFE_ORB_SHELL_BELL`; a test of this build confirms the berry
first (`GC V20 Lab reference: L75 ...`, and the opposite order fails).
M4-B1c-0b called Life Orb before `bc_items_after_action`, whose second pass
is that stage. Now `bc_items_after_action` takes the holder (`life_orb`, the
Pokemon that started the move, when it landed) and runs it between the
other Pokemon's HP pass and the all-items pass (`MOVEEND_ITEMS_EFFECTS_ALL`),
still before the pivot's switch; `execute_struggle` passes it the same way.
Only V20 has the record, so V1-V19 are unchanged (traces V11-V20 identical
to M4-B1c-0b). Dolphin L75 now: hit, miss, Sitrus (+56, used up), the loss.
The native test `test_v20_paths` pins the order. The Struggle, Moxie, pivot,
recharge and last-foe orders of 14.3 are unchanged.

| Move end (reference) | GC V20 |
|---|---|
| `MOVEEND_ITEM_EFFECTS_TARGET` (status berries of the target) | at the hit (V19) |
| `MOVEEND_ITEM_EFFECTS_ATTACKER_2` (the user's berries after its recoil) | `bc_items_actor_check`, before Moxie |
| `MOVEEND_ABILITY_EFFECT_FOES_FAINTED` (Moxie) | `bc_ability_after_move` |
| `MOVEEND_KEE_MARANGA_HP_THRESHOLD_ITEM_TARGET` (others' HP berries) | `bc_items_after_action`, first pass |
| `MOVEEND_LIFE_ORB_SHELL_BELL` | `bc_items_after_action` → `bc_item_after_move` |
| `MOVEEND_HIT_ESCAPE` | the Core's pivot, after the move end (ITM-TIME-003, unchanged) |
| `MOVEEND_ITEMS_EFFECTS_ALL` | `bc_items_after_action`, last pass |

### 15.5 EventLog and the GC screen (Dolphin)

Each loss is one `EV_ITEM_DAMAGE` (`user` = `target` = the holder's position,
`amount` = max HP / 10, `hp_after`, item 479), after the move's own events
and before the replacement or the end (`EV_END`). The GC screen (frames
captured during L73) shows "Hurt by its held item!" with the holder's name
and the item's name sent by the GBA (いのちのたま), the HP bar going 227 → 205,
after the target's faint message and before the send-out; the header keeps
"ITEMS ON". L81 shows "All held item effects are OFF." and the two abilities
the GC does not run. Peaks over the V20 runs: 19 events in one turn
(L76), core 199 us per turn, presentation 290 us (the session peak); EventLog full: never.

### 15.6 JoyBus, disconnection and the result (Dolphin)

Dolphin 2609b, the test profile, release-13 DOL, GBA polled every 160 us;
the figures are per battle (one session each), Single to Rotation, one to
four holders, request V20:

| Figure | 10 V20 battles (final run) | 72 Lab battles of the ability fixes (V12-V19) |
|---|---|---|
| CRC errors (GC / GBA) | 0 / 0 | 0 / 0 |
| Timeouts | 1 (L76) | 13 |
| NACKs / retries | 14 / 15 (stale GBA frames: `stale_drops` 12) | 118 / 131 |
| GBA queue overflows | 0 | 0 |
| Link lost | 0 | 0 |
| Largest poll gap during play | 66.7 ms (L81) | 66.7 ms |
| SI transfer, longest | 413 us | 413 us |
| Request transfer | 450-616 ms, 4-6 chunks | 406-670 ms |
| Asset CRC mismatches | 0 | 0 |

The NACK/retry pattern is the known stale-frame recovery (one retry, then
the same exchange succeeds); V20 adds nothing to it.

Result contract (L80, a battle that ends by itself):

| Case | GC log | Outcome |
|---|---|---|
| 3 s host silence after `PH3 RESULT` (GC Z) | link lost, reconnect, the same MODE nonce: "result already final (nonce=…); not restarting the battle" | the GBA keeps its one result; nothing applied twice |
| 3 s host silence on the final `EV_END`, before the result | link lost, reconnect, the same nonce: the battle restarts (same seed), plays to one `PH3 RESULT` (draw, the same Life Orb loss) | one result |
| a new battle after the end | a new nonce (each Lab run) | — |

Lab and V20 battles are "show" results (`RESULT_POLICY_DISPLAY_ONLY`): the
GBA validates the result (`OnResult`: battle id, counts, HP and PP within
the maximums) and shows it; only the legacy Single battle with "apply"
writes HP and PP, to its lead, once. No path of a result writes a held item
(the only `MON_DATA_HELD_ITEM` write is the Lab's own Pokemon), nothing
saves: Life Orb stays held by construction. The save file of the test
profile (`pokeemerald-2.sav`, a copy of the Frontier save) was hashed before
and after all the Dolphin runs: the same bytes (SHA-256 AFF1EB7B295C7D76...); the profile's own save and Dolphin.ini were put back byte for byte afterwards.

### 15.7 Older versions

| Check | Result |
|---|---|
| GC Ability Lab L01-L72 (V12-V19) on the final DOL and ROM, Dolphin | the 67 fixtures that start a battle are line for line those of the ability fixes' run (requests, item and ability lines, events, states, actions and results; nonces and timings aside); the five GBA refusals (L42, L48, L56, L63, L71) refuse as before and send nothing to the GC |
| A V19 GC (the M4-B1c-0a DOL, 362,624 B) and a V20 GBA (L73) | refused at the MODE check ("MODE 2 not supported by this host", "Unsupported mode"); the GBA shows "The GameCube ended the session."; no request decoded, no battle |
| V18/V19 Life Orb (L62, L69) | unsupported, every item off, as before (in the regression) |
| Traces V11-V20 (Native) | identical to M4-B1c-0b (V12-V20 1,200 battles each; V11 cannot run the generator) |
| ACT-002, ABL-G05 | unchanged (L72 in the regression; the ability tests pass) |
| ABL-G04, ITM-TIME-003 | not changed (still the known differences) |
| CAPS, release, mode | release 13 on both sides; MODE v1 app 4247 |

### 15.8 Battle Frontier coverage (V19 and V20)

`test/battle/gc_v20_coverage.c` measures, for each of the 2,040
`gBattleFrontierMons` sets as the player's lead (level 50, IVs 31,
personality 0x12345678) in a Single against a plain Snorlax (Immunity,
Tackle, no item), with the exporter's default rules; "strict" builds with
strict items, abilities and moves. Not a facility success rate.

| Indicator | V19 | V20 | Change |
|---|---|---|---|
| Request built | 1,996 | 1,996 | 0 |
| Items run as held (not normalised) | 992 | 1,118 | +126 |
| Every item off: an item not simulated | 956 | 815 | −141 |
| Every item off: an item ability | 48 | 63 | +15 |
| The lead's ability run (not normalised) | 818 | 818 | 0 |
| Every move of the lead run | 447 | 447 | 0 |
| Strict (items, abilities, moves) | 109 | 130 | +21 |
| Holds Life Orb (sets / built) | 142 / 141 | 142 / 141 | — |
| Life Orb's factor and loss run | 0 | 126 | +126 |

No set holds nothing. The static bound of V20's gain is the 141 built Life
Orb holders; 126 of them run it, 15 turn every item off by an ability
(Magic Guard, Sheer Force, Klutz and the copiers). The strict figure is the
kind of figure the earlier audit counted (108 sets with moves, abilities and
items all strict); this measure gives 109 for V19 under its own foe and
rules, not reconciled set by set (INFERRED: the conditions differ). The
other figures are not comparable with it: "items" counts sets whose items
are not normalised, whatever their moves and abilities.

### 15.9 Verification

| Check | Result |
|---|---|
| Native | -O2 -Wall -Wextra -Werror, ASan + UBSan, presentation: pass; the 14 saved V20 cases (14 started, 26 effect checks, 0 failed); the V20 damage, wire, rules and paths tests (the Sitrus order added) |
| Traces (1,200 battles per version) | V11-V20 identical to M4-B1c-0b |
| Expansion | Life Orb reference 27/27 (14.5), V20 Lab reference 6/6, V20 export (Life Orb 4, Lab 1, coverage 1) pass |
| Frontier `make check` | 7,000 tests: 4,084 PASS, 2,455 FAILED, 19 KNOWN_FAILING, 419 TO_DO, 9 EXPECT_FAILING, 14 ASSUMPTION_FAIL (no SKIP; -j8 as for M4-B1c-0b: 6,992 / 4,076 PASS / 2,455 FAILED); the 8 new tests pass and no other test changed status. A -j4 run gave 6,980 / 4,084 / 2,435: two Frontier music tests and one contest animation test change with the run order (they pass alone), not with this stage |
| Mutations (GC) | 27: 22 killed, 5 equivalent (the version gate and the holder identity check, as in M4-B1c-0b; the loss after the all-items pass or after the whole move end: nothing that pass checks can become due from the holder's own loss; the loss only with consumable items: V20 implies them) |
| Dolphin | V20 Lab L73-L83 twice (11 + 10 battles, L82 refused on the GBA both times), result contract (2 cases), L01-L72 regression, V19 DOL against V20 |
| Sizes | Battle 4,472, BattleRequest 6,344, EventLog 47,812, max request 3,165 unchanged; DOL 363,328 B (+160: text +152, data +36, bss unchanged); the DOL built from the final sources is byte for byte the one Dolphin ran; ROM text + data 26,374,413 B (+2,238: the V20 Lab fixtures and list, debug only); the final ROM is byte for byte the one Dolphin ran |
| Time | host -O2, three runs each: one damage calculation 424-446 ns without Life Orb (V19 and V20), 444-470 ns with it; one Single turn 1.6-1.9 us; the M4-B1c-0b core and this one give the same figures |

### 15.10 Not done, still open

- Dolphin did not run Struggle with Life Orb or a pivot's switch after the
  loss (native and Expansion tests cover both, 14.5).
- The draw when the last foe and then the holder faint (L80) is the GC's
  rule; the reference's result for it was not run (INFERRED from its rule).
- V20 results are shown only, as every roster battle's; the "apply HP/PP"
  path is the legacy Single battle (APP V1) and was not re-run (unchanged
  code). No held item is written by any result (SOURCE_CONFIRMED).
- Long JoyBus runs (hours) were not done; the figures are per Lab battle.
- ABL-G04 and ITM-TIME-003 stay as recorded (13.6).

## 16. M4-B1c-1a: Shell Bell, specification and reference (no implementation)

Branches `milestone4b1c1a` (GC) and `frontier-gc-m4b1c1a` (Frontier), based
on M4-B1c-0c; no battle logic, exporter or request change. The full record
is [Shell Bell](shell-bell.md); in short:

- Expansion (`TEST_CONFIRMED`, 34 reference tests): the heal is the HP the
  move took (every strike and target, the ally included, cut at a fainting
  target's HP and a breaking substitute's HP) / 8, rounded down after the
  sum, at least 1, up to max HP; once, at `MOVEEND_LIFE_ORB_SHELL_BELL`,
  after the drain, the contact abilities, the recoil, Moxie and the other
  Pokemon's HP berries, before U-turn's switch. No heal at max HP, under
  Heal Block, Embargo or Klutz, for Sheer Force's boosted moves, Future
  Sight, after the holder fainted or a Red Card forced it out.
- GC today (`TEST_CONFIRMED`, `test_c1a_lost`): `MoveContext.lost` is the
  same sum per move (Struggle's path discards it).
- Design (`INFERRED`): request/APP V21 with `BIHP_HEAL_DEALT` at
  `BITIME_AFTER_MOVE` (divisor 8), the value passed with the M4-B1c-0c Life
  Orb pointer into `bc_items_after_action`; no new persistent state.
- Failing-first: `tools/shell-bell-tests.sh` (17 cases, all refused at the
  start today). Battle Frontier sets holding Shell Bell: 0.

## 17. M4-B1c-1b: Shell Bell in request V21

Branches `milestone4b1c1b` (GC) and `frontier-gc-m4b1c1b` (Frontier), based
on M4-B1c-1a; release 13 kept. The record is [Shell Bell](shell-bell.md)
section 11; in short (`TEST_CONFIRMED` natively, Dolphin is M4-B1c-1c):

- Request/APP V21 (`BATTLE_*_VERSION_AFTER_MOVE_HEAL`), the V20 layout, adds
  `BIHP_HEAL_DEALT = 4`: `BITEM_HP` at `BITIME_AFTER_MOVE`, divisor 2..255,
  no predicate, not used up, one after-move record per item. V20 and older
  refuse it in the encoder, the decoder and `battle_init`.
- The core passes `BcMoveEnd {origin, landed, lost}` (the M4-B1c-0c Life
  Orb point) to `bc_item_after_move`: Life Orb still on `landed`, Shell Bell
  on `lost` (`MoveContext.lost`, every hit and target, the ally included, at
  most the HP each had): `lost / 8`, at least 1, up to max HP, after the
  other Pokemon's HP berries and before the pivot's switch; Struggle returns
  its loss. No new state.
- Exporter: `HOLD_EFFECT_SHELL_BELL` → `HP(AFTER_MOVE, HEAL_DEALT,
  holdEffectParam)` from roster V21; Magic Guard keeps it; Sheer Force,
  Klutz, the item abilities and the ability copiers turn every item off.
- Battle Frontier: 0 of 2,040 sets hold Shell Bell, so V21 adds no set; the
  1,996 requests built, 1,118 with items simulated and 1,118 strict ones are
  V20's, item block for item block.

## 18. M4-B1c-1b-fix: the independent review of V21

Branches `milestone4b1c1b-fix` / `frontier-gc-m4b1c1b-fix`; V21 and release 13
kept ([Shell Bell](shell-bell.md) section 12):

- V21: a contact drain move drains each strike before the contact abilities
  (the reference's hit-script drain, then `MOVEEND_ABILITIES`): a Shell Bell
  holder at 1 HP now survives Rough Skin as in the reference (322 → 152 HP).
  V20 and older keep their order.
- Exporter: Imposter beside a V21 Shell Bell turns every item off (strict
  items: refused).
- `battle_init` checks item blocks from V17, as the encoder and the decoder.

## 19. M4-B1c-1b-fix2: Imposter beside Life Orb (exporter gate)

Branches `milestone4b1c1b-fix2` (documentation only) / `frontier-gc-m4b1c1b-fix2`.
This is a safe normalisation added to the V20 and V21 exporter. Request/APP
V20 and V21, their layouts, the GC battle code (no source change:
`gamecube/`, `protocol/` and `tools/` equal to M4-B1c-1b-fix) and release 13
are unchanged; a request the GC accepted before is read as before.

### 19.1 The finding (Codex's re-review of M4-B1c-1b-fix, MAJOR)

Imposter copies its target's ability on entry. In the reference a copied
Sheer Force skips Life Orb's loss after the moves it boosts
(`MOVEEND_SHEER_FORCE` jumps past `MOVEEND_LIFE_ORB_SHELL_BELL`) and a copied
Magic Guard stops it (`TryLifeOrb`); the copy follows the target and its
switches. Expansion, this build (`gc_imposter_life_orb_reference.c`, 3 tests):

| Holder: Ditto, Imposter, Life Orb, 100 / 400 HP | Life Orb loss | HP after |
|---|---|---|
| against Nidoking (Sheer Force), Flamethrower (boosted) | none | 100 |
| against Nidoking (Sheer Force), Tackle (not boosted) | 40 | 60 |
| against Clefable (Magic Guard), Tackle | none | 100 |
| Ditto with Limber, Flamethrower | 40 | 60 |

The GC runs neither copy. `HpItemAbility` gated Imposter only beside typed
records (V18) and heal-dealt records (V21 Shell Bell), so the exporter
accepted an Imposter Life Orb holder: V20 / V21, the player's / the
opponent's, strict items on / off, all 8 built with items simulated. The
exporter's own request bytes, decoded and run by GC Native (Flamethrower):
Life Orb 40, 60 HP in all four non-strict requests, where the reference
keeps 100.

### 19.2 The fix

`HpItemAbility` (Frontier `src/frontier_gc_battle.c`): Imposter turns every
item off beside an after-move damage record (`BITEM_HP` with
`BITIME_AFTER_MOVE` and `BIHP_DAMAGE`: Life Orb's loss, V20 and V21), next
to its typed (V18) and heal-dealt (V21) gates. The gate is the holder's, not
the foe's: the foe's starting ability is not checked (switches, and Magic
Guard as well as Sheer Force, change the outcome). Strict items refuse the
battle ("Strict items: item ability: かわりもの"); otherwise the existing
all-items-off path: the held item id and its records stay in the request,
`RULE_IGNORE_ITEMS` is set, and both screens say "ALL ITEMS OFF: かわりもの".

After the fix: the 4 non-strict requests are built with items off for
Imposter, the 4 strict ones refused; GC Native on the new requests: no
Life Orb event, 100 HP. That is the normalised battle (no item runs, so no
Life Orb boost either), not the reference's per-move result, and it is
announced as such.

### 19.3 What stays as it was

| Case | Verdict (V20 and V21) |
|---|---|
| Imposter with no item, Leftovers, Focus Sash, Sitrus Berry | simulated |
| Imposter with Choice Scarf | every item off for the item (unsupported), as before |
| Imposter with Shell Bell (V21) | every item off (the M4-B1c-1b-fix gate); V20: Shell Bell unsupported |
| Life Orb without Imposter (Ditto with Limber, Snorlax, Kecleon with Color Change), strict items too | simulated |
| Magic Guard with Shell Bell (V21) | simulated |
| Magic Guard, Sheer Force, Trace with Life Orb | every item off for that ability |
| V17-V19 with Life Orb | unsupported item (every item off for Life Orb), Imposter or not |

### 19.4 Verification

| Check | Result |
|---|---|
| Exporter (`gc_imposter_life_orb_export.c`, 8 tests) | the 8 conditions; a Double reserve (both sides) and Triple / Rotation slots 2 and 3 (both sides); any foe ability; the controls above; V17-V19; the Frontier tally |
| Expansion | the new Imposter Life Orb references 3/3 and exporter tests 8/8; Shell Bell 34/34, V21 references 2/2, Life Orb (`GC V20 reference`) 27/27, contact drain 6/6, every `GC V20` test 39/39 and `GC V21` test 13/13 |
| GC Native (rerun, code unchanged) | -O2 -Werror, ASan/UBSan, presentation, `tools/v20-tests.sh` (14), `tools/shell-bell-tests.sh` (17 cases, 20 effect checks, 12 refusals), `test_fix_contact_drain`, `test_fix_item_init` (V17-V21): pass; fixed-seed traces V11-V21 equal to M4-B1c-1b |
| Frontier `make check` | 7,064 tests: PASS 4,148 (+11: the new ones, and the renamed `GC V21 fix export` test), FAILED 2,455, KNOWN_FAILING 19, TO_DO 419, EXPECT_FAILING 9, ASSUMPTION_FAIL 14; against M4-B1c-1b-fix no test changed status (the 2,455 failures are the same tests) |
| Mutations (exporter) | 5 killed (no gate, i.e. the code before the fix; the loss read at the end of the turn; the loss read as a heal; Imposter off beside any HP record; the gate given to Color Change too) |
| Battle Frontier sets (2,040, against a plain Snorlax) | V20 and V21 alike: 1,996 requests built; 1,118 run their items as held (no normalisation); 1,118 built with strict items; 130 start with every strict rule (items, abilities, moves); 126 run Life Orb; 0 Shell Bell; 3 sets have Imposter (Choice Scarf, Focus Band, Focus Sash), none with Life Orb, so no figure changes (equal to section 15.8) |
| Sizes | ROM text + data 26,374,725 B (+88 over M4-B1c-1b-fix); Labs and debug menu unchanged; DOL not rebuilt (no GC code change: the M4-B1c-1b-fix DOL, 364,384 B) |

### 19.5 Not covered

- The GC still runs no ability copy; any Imposter holder of a record whose
  outcome a copied ability changes must be gated in the exporter like this.
- Life Orb's damage factor beside Imposter is not modelled either: the whole
  battle runs with items off.
