# Weather and terrain (design for M4-W and M4-T)

Status: **weather implemented in M4-W** (request V15, release 9; the
implementation, its order and rounding and what changed from this design
are in [weather](weather.md)). **Terrain is still design only** (M4-T).
The field state below is implemented as `Battle.field` with one
`FieldEffect` for the weather; the request block became the V15 field
rules, the proposed hook kinds became V15 move effects, hooks, predicates
and modifier stages. Corrections from checking this design against the
Frontier build: the weather counts down at the start of the end-of-turn
order (a weather that ends does nothing more that turn), the weather
damage factor is its own step of the formula, ability weather before
Gen 6 keeps the counter it finds, and harsh sunlight prevents freezing.
The reference is pokeemerald-expansion's battle code and its `B_*`
configs, read for behaviour only (the GC engine stays an independent
implementation).

## Goals

- One field state in the battle core, changed through one function used by
  moves, abilities and (later, M4-B) items: a weather from Rain Dance and a
  weather from Drizzle are the same state change with a different source.
- Everything game-generation dependent is resolved by the GBA into rules
  and descriptors, as for moves and abilities. The GC never reads `B_*`.
- Durations, sources and modifiers are data, so held items (Damp Rock,
  Utility Umbrella, Terrain Extender, seeds) can be added without changing
  the structure.

## Field state (GC core)

```
typedef struct {
    uint8_t kind;        // BWEATHER_* / BTERRAIN_*, 0 = none
    uint8_t turns_left;  // 0 with kind != 0: no end (primal weather, pre-Gen 6 ability weather)
    uint8_t source_kind; // FSRC_MOVE / FSRC_ABILITY / FSRC_ITEM (M4-B) / FSRC_RULE (battle start)
    Position source;     // who set it (for abilities that end with their holder)
    uint16_t source_id;  // move id / ability id, diagnostic and message only
} FieldEffect;

typedef struct { FieldEffect weather, terrain; } FieldState;   // in Battle
```

`bc_field_set(b, FIELD_WEATHER | FIELD_TERRAIN, kind, turns, source, log)`
is the only writer. It applies the replacement rules (below), emits
`EV_FIELD_START` / `EV_FIELD_FAIL`, and calls the entry hooks that react to
a field change. `bc_field_end_turn` counts down and emits `EV_FIELD_END`.

## Request (V13, proposed)

- A field rules block (like the V7 condition and V9 volatile rules): the
  default duration of each kind, residual damage divisors, the immune types
  per weather, the modifiers (sun/rain Fire/Water 150/50, terrain boost 130
  from Gen 8 or 150 before: `B_TERRAIN_TYPE_BOOST`), Snow's Ice Defense
  boost, which ice weather a hail move makes (`B_PREFERRED_ICE_WEATHER`),
  whether ability weather lasts 5 turns (`B_ABILITY_WEATHER`), Blizzard's
  hail accuracy (`B_BLIZZARD_HAIL`), whether a weather already present can
  be overwritten (`B_OVERWORLD_WEATHER_OVERRIDE` for a battle-start weather).
- Move effect kinds: `BEFF_WEATHER` (arg0 kind), `BEFF_TERRAIN`, and flags
  for the weather-dependent move rules the GBA resolves per move: accuracy
  in weather (Thunder, Hurricane, Blizzard), power in weather (Solar Beam
  halved), charge skipped in sun (Solar Beam/Blade), Weather Ball type and
  power, Earthquake halved on Grassy Terrain (already flagged in V9 as
  "never applies").
- Ability hook kinds: `ENTRY_FIELD` (Drizzle, Drought, Sand Stream, Snow
  Warning, the Surges), `WEATHER_SPEED` (Swift Swim, Chlorophyll, Sand Rush,
  Slush Rush), `WEATHER_IMMUNE` (Overcoat and the type-free cases),
  `WEATHER_RESIDUAL` (Rain Dish, Ice Body, Dry Skin, Solar Power),
  `WEATHER_EVASION` (Sand Veil, Snow Cloak), `WEATHER_SUPPRESS` (Cloud
  Nine, Air Lock: the weather stays but has no effect while the holder is
  on the field).
- An optional battle-start field (`FSRC_RULE`) for a map or rule weather.

## Weather rules to model

| Topic | Rule to resolve from the reference |
|---|---|
| Kinds | sun, rain, sandstorm, hail, snow; primal (harsh sun, heavy rain, strong winds) only with their abilities (otherwise UNSUPPORTED) |
| Duration | 5 turns from moves and (Gen 6+) abilities; abilities before Gen 6 never end; items extend to 8 (M4-B) |
| Same weather again | fails (the move fails, the ability does not restart the count) |
| Another weather | replaces it, except a primal weather, which only another primal ability or its holder leaving ends |
| Several setters entering together | in speed order: the slowest setter's weather remains |
| Residual damage | sandstorm 1/16 except Rock/Ground/Steel and immune abilities; hail 1/16 except Ice; snow none; at the start of the end-of-turn order, before status damage |
| Damage | sun: Fire x1.5, Water x0.5; rain: the reverse; snow: Ice types' Defense x1.5; sandstorm: Rock types' Sp. Def x1.5 (Gen 4+) |
| Moves | Thunder/Hurricane: never miss in rain, 50% in sun; Blizzard never misses in hail (and snow); Solar Beam: no charge in sun, half power in rain/sand/hail/snow; Weather Ball; Moonlight/Synthesis/Morning Sun heal amounts |
| Speed | Swift Swim, Chlorophyll, Sand Rush, Slush Rush x2 (affects turn order through the existing dynamic speed rule) |
| Suppression | Cloud Nine / Air Lock while on the field; Utility Umbrella (M4-B) per holder |

## Terrain rules to model

| Topic | Rule to resolve from the reference |
|---|---|
| Kinds | Electric, Grassy, Misty, Psychic |
| Duration | 5 turns (Terrain Extender 8: M4-B); a new terrain replaces the old one; the same terrain fails |
| Grounded | not Flying type, not Levitate (and later Air Balloon, Magnet Rise); one shared `bc_grounded(b, pos)` used by every terrain effect and by Ground moves |
| Electric | grounded Pokemon cannot fall asleep (also blocks Yawn later); Electric moves of grounded users x1.3 (Gen 8+) or x1.5 |
| Grassy | grounded Pokemon heal 1/16 at the end of turn; Grass moves x1.3/x1.5; Earthquake, Bulldoze, Magnitude halved against grounded targets |
| Misty | grounded Pokemon cannot get a major status or confusion; Dragon moves against grounded targets x0.5 |
| Psychic | priority moves fail against grounded targets (in the Protect/target-failure step); Psychic moves x1.3/x1.5 |
| Status prevention | inside the status handlers, at the effect's own unit, like the M4-A status immunity hooks |

## Order and units

- Field changes from a move happen in that move's effect list (a status
  move's effect, per use).
- Entry setters run in the entry-ability step, in speed order (the same
  order the leads use now).
- End of turn: the weather countdown first (as implemented in M4-W), then
  the weather residual, then the existing status residuals, then terrain
  healing, then end-of-turn abilities, then the terrain countdown, following
  the reference's end-of-turn blocks.
- Damage and accuracy modifiers are per hit / per target, like the
  existing ability modifiers.

## Interaction with items (M4-B)

The `FieldEffect` keeps `turns_left` and the source, so an item can extend
a duration at the moment of setting (the setter's held item), protect its
holder from a weather (Utility Umbrella: a per-holder check in the same
modifier functions), or react to a terrain (seeds: an entry/field-change
hook). None of these needs a new field-state shape.

## Not planned in M4-W / M4-T

Fog, the overworld-weather rules beyond a battle-start field, Thunderstorm
terrain, Forecast/Flower Gift form changes (form changes are not modelled),
Mimicry, and any interaction that needs abilities or items not yet
implemented; these stay UNSUPPORTED.
