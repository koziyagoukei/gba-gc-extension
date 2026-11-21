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
