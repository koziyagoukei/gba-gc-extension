# Weather (M4-W, request V15)

Release 9 (`m4w-dev`), branches `milestone4w` and Frontier `frontier-gc-m4w`.
Built on the V14 shared modifiers ([modifiers](modifiers.md)); V1–V14
requests keep their meaning byte for byte. The design of record before this
milestone was [weather-terrain](weather-terrain.md); terrain came in M4-T
([terrain](terrain.md), request V16).

## 1. Check of the design against V14 and Expansion

Read in this Frontier build (pokeemerald-expansion, Gen 9 configs):
`TryChangeBattleWeather`, `EndOrContinueWeather`, `HandleEndTurnWeather*`
(`battle_end_turn.c`), `GetWeatherDamageModifier`, `CalcDefenseStat`,
`CalcMoveBasePowerAfterModifiers`, `GetBattlerTotalSpeedStat`,
`CanMoveSkipAccuracyCalc` / `GetTotalAccuracy`, `HasWeatherEffect`,
`CanTwoTurnMoveFireThisTurn`, `Cmd_recoverbasedonsunlight`,
`DoSwitchInEvents`, the switch-in abilities and the overworld weather.

| Item | Old design | This build | M4-W |
|---|---|---|---|
| End of turn | residual first, countdown last | countdown first; a weather that ends does nothing that turn; then each Pokémon's residual in speed order | as the build |
| Weather damage factor | a modifier | its own step: after the spread ×0.75, before the random factor, one factor (`GetWeatherDamageModifier` returns), rounded half down | own stage `BMOD_WEATHER`, first matching field record |
| Sandstorm Sp. Def / snow Defense | modifiers | at the end of the defending stat's chain (after items) | field records closing the V14 defense chain |
| Ability weather before Gen 6 | never ends | keeps the counter it finds (no counter: no end) | `BWEATHER_TURNS_KEEP` |
| Accuracy stages | (not in the design) | the ratio table 33/36/43/50/60/75/100/133/166/200/233/266/300 % | V15 uses the table (V7–V14 keep 3/(3+n), which differs at −4 and −5) |
| Freezing | (not in the design) | no freeze or frostbite in sun | field flag `BWKF_NO_FREEZE` |
| Several Pokémon entering together | speed order | speed order after all are in (also replacements after a turn) | V15 defers the entry abilities of one replacement round, then speed order |

No contradiction needed a change of structure; V14's arithmetic is kept and
the weather is added where the build has it.

## 2. Three layers

1. **State** (GC): `Battle.field.weather` = kind, turns left (0 = no end),
   source kind and position; `locked` for a locked start weather. Written
   only by `bc_weather_set` and the end-of-turn countdown
   (`gamecube/battle/battle_field.c`). A terrain will be a second
   `FieldEffect` of the same shape.
2. **Shared operations** (GC): set (same weather → nothing / the move
   fails; locked → blocked), suppression (any Pokémon on the field with
   `BABIL_WEATHER_SUPPRESS`: the weather stays and counts down without
   effect), countdown, residual, the weather predicate `BPRED_WEATHER`
   and the stages `BMOD_WEATHER`, `BMOD_SPEED`, `BMOD_ACCURACY`, turn
   order (dynamic speed), no-freeze, move type / accuracy / charge / heal
   in a weather, deferred entry abilities.
3. **Game rules** (GBA): the field rules, the move effects and the ability
   hooks below, resolved from the build's code and `B_*` configs. The GC
   knows no game ids and no configs.

Rain Dance and Drizzle run the same `bc_weather_set`; so do Sunny Day and
Drought (only the duration and the source kind differ, as data).

## 3. Request V15

Same layout as V14 plus an 88-byte field rules block after the volatile
rules (`BATTLE_FIELD_RULE_BYTES`, in `BATTLE_REQUEST_MAX_BYTES`):

| Bytes | Field |
|---|---|
| 0 | flags: `BFR_START_LOCKED` |
| 1–2 | battle-start weather and turns (0 = no end) |
| 3 | field record count (≤ 8) |
| 4–28 | per kind (sun, rain, sandstorm, hail, snow): residual divisor, flags (`BWKF_UNDERGROUND_SAFE`, `BWKF_NO_FREEZE`), three immune types |
| 29–31 | reserved, 0 |
| 32–87 | 8 modifier record bodies (V13 format, flags 0); unused ones are zero |

New in V15 (refused in earlier requests by the encoder, the decoder and the
core): rule `RULE_STRICT_MOVES` (refuse to start if a known move cannot be
run), effect kinds `BEFF_WEATHER` (17), `BEFF_WEATHER_ACCURACY` (18),
`BEFF_WEATHER_TYPE` (19), `BEFF_WEATHER_HEAL` (20), `BEFF_CHARGE` arg2
(weathers without a charging turn), hook kinds `BABIL_ENTRY_WEATHER` (31),
`BABIL_WEATHER_SUPPRESS` (32), `BABIL_WEATHER_IMMUNE` (33),
`BABIL_WEATHER_RESIDUAL` (34) with `BAHF_NOT_UNDERGROUND`, predicates
`BPRED_WEATHER` (5) and `BPRED_HAS_TYPE` (6), stages `BMOD_WEATHER` (6),
`BMOD_SPEED` (7), `BMOD_ACCURACY` (8, written with bit 5 of the stage byte
as stage bit 3). Field records may use only the WEATHER and DEFENSE
stages; abilities SPEED (no defender, category or move predicate) and
ACCURACY; moves the V14 stages. APP V15 keeps the V6 action layout.

## 4. What is supported

| Weather | Start/replace/end, duration, same-weather rule, residual, damage step, stat boost |
|---|---|
| Sun | ×1.5 Fire / ×0.5 Water; no freezing |
| Rain | ×1.5 Water / ×0.5 Fire |
| Sandstorm | 1/16 (Rock, Ground, Steel immune; underground safe); Rock Sp. Def ×1.5 |
| Hail | 1/16 (Ice immune; underground safe) |
| Snow | Ice Defense ×1.5 |

| Moves | V15 export |
|---|---|
| Rain Dance, Sunny Day, Sandstorm, Hail, Snowscape (`EFFECT_WEATHER`) | `BEFF_WEATHER`, 5 turns, the user stands for the field |
| Thunder, Hurricane, Blizzard and every move with the build's rain / sun / hail-snow accuracy flags | `BEFF_WEATHER_ACCURACY` |
| Solar Beam, Solar Blade (and two-turn moves with a weather) | charge skipped in that weather; Solar Beam power ×0.5 in rain, hail, snow, sandstorm (`B_SANDSTORM_SOLAR_BEAM`) |
| Weather Ball | type per weather, base power ×2 in any weather |
| Moonlight, Morning Sun, Synthesis, Shore Up | 2/3 in sun (sandstorm for Shore Up), 1/4 in another weather, else 1/2 |

| Abilities | Hooks |
|---|---|
| Drizzle, Drought, Sand Stream, Snow Warning (snow / hail by `B_SNOW_WARNING`) | `ENTRY_WEATHER`, 5 turns or KEEP (`B_ABILITY_WEATHER`) |
| Cloud Nine, Air Lock | `WEATHER_SUPPRESS` (+ entry notice) |
| Swift Swim, Chlorophyll, Sand Rush, Slush Rush | speed ×2 record (before paralysis) |
| Sand Veil, Snow Cloak | immunity + accuracy ×0.8 against the holder |
| Sand Force | immunity + Rock/Ground/Steel power ×1.3 |
| Overcoat | sandstorm and hail immunity (powder moves are unsupported in V15) |
| Rain Dish, Ice Body, Dry Skin, Solar Power | residual hooks (Ice Body instead of hail damage, not underground); Dry Skin also Water absorb and Fire power ×1.25; Solar Power Sp. Atk ×1.5 |

## 5. Not supported (refused, never silently inert)

UNSUPPORTED abilities (refused under `RULE_STRICT_ABILITIES`, shown as "not
simulated" otherwise): primal weather (Desolate Land, Primordial Sea, Delta
Stream), Forecast, Flower Gift, Protosynthesis, Orichalcum Pulse, Mega Sol,
Hydration, Leaf Guard, Harvest, Ice Face. Unsupported moves (not
selectable; refused under `RULE_STRICT_MOVES`): Hydro Steam, Aurora Veil,
Chilly Reception, Growth, Electro Shot (its charge-turn boost), every powder
move (V15), weather moves of darkness / fog. Map weather is not exported: a
V15 battle on a map whose weather this build brings into battles is refused
by the GBA. Items (Damp Rock and other rocks, Utility Umbrella, Safety
Goggles) are not simulated (M4-B). Other limits: Magic Guard, Mold Breaker
on weather abilities, underwater, Forecast-type form changes.

## 6. Order and rounding (V15)

- Damage: base → spread ×0.75 → **weather step** → random → STAB → type →
  burn → final chain; every step a 4.12 factor rounded half down, as V14.
- Power chain: move records (Solar Beam ×0.5), then abilities (Sand Force,
  defender's Dry Skin), half-up chaining as V14.
- Attack chain: Solar Power with the attacker's records. Defense chain: V14
  then the field records.
- Speed: stages → speed records (×2) → paralysis; turn order recomputed
  after each action (dynamic speed rule).
- Accuracy: weather accuracy (0: never misses, no draw) → stage table →
  V12 accuracy hook → attacker records → defender records (Sand Veil).
- End of turn: countdown (continue / end), then per Pokémon in speed
  order its weather residual (ability or weather), then Shed Skin, status
  residuals and Speed Boost as before.
- Entries: the leads, and the replacements of one round, in speed order
  after all are in (ties by a draw); a single switch or drag enters at once.

## 7. Verification

| Check | Result |
|---|---|
| Native (`battle_test.c`, -O2 and ASan + UBSan) | all suites pass; new `test_v15_*`: wire and validation, set / same / replace / end / KEEP / start / locked, entry order and deferred replacements, suppression, damage step, accuracy, speed order, residuals and faint, Solar Beam / Moonlight / Weather Ball, no freeze, strict moves, 240 random weather battles in four formats run twice (identical events, state, RNG) |
| Mutations (17 changes to the weather code: step position, countdown, suppression, same weather, underground, first-only records, speed order, accuracy, deferred entries, no freeze, charge, heal, type, version gates) | all 17 caught |
| Frontier `make check` | 6,828 tests; the 6,817 (name, status) pairs of the M4-P run unchanged, 11 new PASS; FAILED 2,455 in both (pre-existing, mostly Japanese-text MESSAGE checks) |
| Expansion reference (`make check TESTS="GC V15 reference"`) | 11 tests, 33 of 33 values equal to the GC (rain/sun factors, spread order, sandstorm Sp. Def, snow Def, Solar Beam, Weather Ball, Sand Force, Solar Power, Dry Skin, Cloud Nine) |
| V12 / V13 / V14 traces (1,200 battles each) | identical to M4-P (V12 also to M4-A2) |
| V15 trace corpus (`battle_trace 15`) | 1,200 battles, two runs equal; total `efa20edd` |
| Dolphin Lab L30–L36 (V15, four formats) | all ran; each replays natively with 0 mismatches; L34 refused (strict, primal); L35 (Gen 5 rules) starts hail with no end where L32 starts 5 turns of snow |
| Dolphin Lab L01–L29 | request bytes, events, actions and results line for line equal to the M4-P runs |
| Debug menu V1–V12, Cube Tester V12 (4 formats) | all reached a result; requests 925 / 1,717 / 1,701 / 1,701 B as in M4-A2 |
| Result contract (V15) | link cut 3 s on the result screen → "result already final … not restarting" |
| Save | Frontier save AFF1EB7B… before and after |
| JoyBus (56 sessions) | GBA 164,024 requests, CRC 0, overflows 0, 96 stale drops, 104 NACKs; GC 111 retries, 4 single-frame timeouts (recovered); the one link loss is the deliberate cut |

## 8. Memory, link and time

| Item | M4-P | M4-W |
|---|---:|---:|
| `BattleRequest` (GC and GBA heap) | 5,776 B | 5,852 B |
| `Battle` (GC, x86-64 host build) | 4,176 B | 4,184 B |
| `EventLog` | 45,556 B | 46,612 B |
| Request buffer `BATTLE_REQUEST_MAX_BYTES(6, 21)` | 2,789 B | 2,877 B |
| ELF text / data / bss | 257,310 / 61,920 / 2,989,504 | 271,750 / 67,156 / 2,995,392 |
| DOL | 319,488 B | 339,168 B |
| Frontier ROM used | 26,345,828 B | 26,354,912 B |
| EWRAM | 231,984 B | 231,984 B |
| A V15 request | — | V14 size + 88 B (L30 1,397 B; L31 1,773 B) |
| GC core per turn (Dolphin) | 26–89 µs | V12–V14 21–96 µs; V15 up to 210 µs (Triple) |
| Native, 1,200 battles | 41 ms (V14) | 51 ms (V15 corpus) |

The DOL growth is mostly the V15 branch of the effect-list validator
(`battle_move_effects_valid_for`, +8.4 KB), unwind tables and the larger
event / animation buffers.
