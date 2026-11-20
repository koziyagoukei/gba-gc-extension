# Held items (M4-B0, request V17)

Release 11 (`m4b0-dev`), branches `milestone4b0` and Frontier `frontier-gc-m4b0`.
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
items, plates, memories, drives, gems, Focus Sash / Band, Leftovers, Black
Sludge, Rocky Helmet, Weakness Policy, Air Balloon, Iron Ball, Utility
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
| B1a | non-consuming end-of-turn items (Leftovers, Black Sludge) | an end-of-turn item step after Shed Skin; Magic Guard and Heal Block stay item abilities / unsupported moves |
| B1b | consumption, HP and status berries, Focus Sash | the consumed state on the Battler, Pickup reclassified, Unnerve, re-evaluation within multi-hit moves |
| B1c | after-move items (Life Orb, Rocky Helmet, Weakness Policy, Expert Belt) | a move-end order table outside `strike()`, an effectiveness predicate |
| B1d | action limits (Choice items, Assault Vest) | legal-action re-evaluation for the GBA prompts and the AI |
| B2 | item changes (Knock Off, Trick, Thief, Magician, Pickpocket) | `item_from` changes; holder-independent definitions already hold |
| Result | persisting consumed items | only once a roster battle may apply its result (the GC refuses that today) and berries are consumed; the nonce rule stays |
