# Ability hooks (Milestone 4-A, request V11)

Milestone 4-A adds abilities to the GameCube battle core. As with moves
(request V8–V10), the GBA resolves each Pokémon's ability from its own game
data and configuration into a small generic descriptor; the GC never looks
up game ability ids, it only runs descriptor kinds. The game's battle code
was read for behaviour only (pokeemerald-expansion `battle_util.c`,
`battle_move_resolution.c`, `battle_stat_change.c`, `battle_end_turn.c`); the
engine is an independent implementation.

Base: release 4 / request V10. V11 adds the ability block and one move flag;
V1–V10 requests decode and run exactly as before (no ability runs).

## Wire format (request V11)

- Move flag `BMF_CONTACT` (bit 6): the move makes contact (the game's
  `makesContact`). Struggle counts as a contact move in V11.
- After the move effect lists and before the volatile rules: one 8-byte
  ability descriptor per entry, players then opponents, in entry order.

```
 0 u8 kind (BABIL_*)   1 u8 arg0   2 s8 arg1   3 u8 arg2
 4 u8 chance (0 = certain, 1..100 percent)   5 u8 percent (multiplier or share)
 6 u16 source ability id (must equal the record's ability id; diagnostic)
```

`RULE_IGNORE_ABILITIES` still means what it says: with the rule set, a V11
request carries descriptors but none of them runs (V10 behaviour).

| Kind | Args | Representative abilities |
|---|---|---|
| `NONE` 0 | — | Abilities without a battle effect in this model (Run Away, Pickup, Illuminate before Gen 9 …) |
| `UNSUPPORTED` 1 | — | Any ability with a battle effect the core does not run. Shown, never silently dropped (below) |
| `ATTACK_MULT` 2 | arg0 category (physical/special), percent | Huge Power, Pure Power (physical, 200) |
| `PINCH` 3 | arg0 type, arg2 HP divisor, percent | Blaze, Torrent, Overgrow, Swarm (÷3, 150) |
| `LOW_POWER` 4 | arg0 max power, percent | Technician (60, 150) |
| `RESIST` 5 | arg0 type, arg2 second type or 0xff, percent | Thick Fat (Fire, Ice, 50) |
| `TYPE_IMMUNE` 6 | arg0 type | Levitate (Ground) |
| `TYPE_ABSORB` 7 | arg0 type, percent of max HP | Volt Absorb, Water Absorb (25) |
| `STATUS_IMMUNE` 8 | arg0 mask of `1 << BST_*` | Limber, Insomnia, Vital Spirit, Water Veil, Immunity |
| `VOLATILE_IMMUNE` 9 | arg0 mask: flinch 1, confusion 2, Intimidate 4 | Inner Focus, Own Tempo |
| `STAT_DROP_IMMUNE` 10 | arg0 mask of `1 << BSTAT_*`, arg2 bit 0 ignores target evasion | Clear Body, Hyper Cutter, Keen Eye |
| `ACCURACY` 11 | percent | Compound Eyes (130) |
| `NO_GUARD` 12 | — | No Guard |
| `CONTACT_STATUS` 13 | arg0 status, chance | Static, Flame Body, Poison Point (30) |
| `CONTACT_DAMAGE` 14 | arg2 divisor of the attacker's max HP | Rough Skin, Iron Barbs (8) |
| `ENTRY_STAT` 15 | arg0 stat, arg1 delta | Intimidate (Atk −1) |
| `END_TURN_STAT` 16 | arg0 stat, arg1 delta | Speed Boost (Spe +1) |
| `STURDY` 17 | — | Sturdy |
| `NO_RECOIL` 18 | — | Rock Head |
| `SHIELD_DUST` 19 | — | Shield Dust |
| `SECONDARY_CHANCE` 20 | percent | Serene Grace (200) |
| `INFILTRATOR` 21 | — | Infiltrator (Gen 6+ substitute rule) |

The GBA decides game-generation questions (for Frontier, from the
expansion's `B_*` configs) and expresses the answer in the descriptor:
whether Inner Focus / Own Tempo block Intimidate (`B_UPDATED_INTIMIDATE`),
the contact chance (`B_ABILITY_TRIGGER_CHANCE`), Rough Skin's divisor
(`B_ROUGH_SKIN_DMG`), whether Sturdy is the Gen 5 version (`B_STURDY`;
otherwise `UNSUPPORTED`), whether Infiltrator passes substitutes
(`B_INFILTRATOR_SUBSTITUTE`; otherwise `NONE`, since screens and Mist do not
exist here).

## Unsupported abilities

An ability whose battle effect the core does not implement is sent as
`UNSUPPORTED` with its source id. The battle still runs; the ability is
inert. The GC:

- emits `EV_ABILITY_UNSUPPORTED` for every such entry when the battle begins
  (before the first command), so the event log and the screen say
  "<Pokémon>'s <ability> is not simulated";
- logs `PH6 ability <side>/<entry> <id> unsupported` in Dolphin's log;
- never treats `UNSUPPORTED` like `NONE` in text: `NONE` is silent because
  the ability does nothing here.

A descriptor with an unknown kind or invalid arguments makes the request
malformed (as with effect descriptors), not "unsupported".

## Hook points and firing order

The existing move order (battle_effects.c) with the hooks inserted. Every
hook names its unit; units are the V10 effect units.

```
battle begin            ENTRY: unsupported notices, then entry hooks of the leads in speed order
action checks           (unchanged: recharge, sleep/freeze, flinch*, confusion*, paralysis)
move used, PP, targets
per target:
  Protect -> out of reach (No Guard reaches semi-invulnerable targets) ->
  ABILITY IMMUNITY        PER_TARGET  TYPE_IMMUNE, TYPE_ABSORB (before substitute and accuracy)
  substitute (status moves; Infiltrator passes) ->
  accuracy                PER_TARGET  NO_GUARD, ACCURACY, STAT_DROP_IMMUNE bit 0 (ignore evasion)
  per hit:
    damage                PER_HIT     ATTACK_MULT, PINCH, LOW_POWER, RESIST
    substitute or Pokémon             INFILTRATOR decides who takes it
    STURDY                PER_HIT     only when the Pokémon itself takes the hit
    PER_HIT secondaries   PER_HIT     (V10)  SHIELD_DUST / *_IMMUNE / SECONDARY_CHANCE inside
    CONTACT abilities     PER_HIT     only when the Pokémon itself took the hit
  multi-hit message, thaw
  drain, PER_TARGET / ON_DAMAGE secondaries (V10, with the same effect-level hooks), pivot, drag
  PER_USE secondaries     once, first landing
AFTER_MOVE secondaries    once
recoil                    AFTER_MOVE  NO_RECOIL (move recoil only, not Struggle)
recharge
send-out (any reason)     ENTRY       entry hooks of the entering Pokémon
end of turn               residual damage, then END_TURN_STAT in speed order
```

`*` Effect-level hooks run wherever the effect runs: `VOLATILE_IMMUNE`
inside the flinch and confusion handlers, `STATUS_IMMUNE` inside the status
handler, `STAT_DROP_IMMUNE` inside the stat handler, `SHIELD_DUST` before a
secondary effect's chance roll, `SECONDARY_CHANCE` in the chance roll. They
therefore inherit the effect's V10 unit (per hit, per target, on damage,
after move, per use) instead of having a unit of their own. This is the rule
that keeps ability units and move units from contradicting each other: an
ability that modifies an effect never decides how often the effect runs.

### Units, hook by hook

| Hook | Unit | Condition | Reference behaviour |
|---|---|---|---|
| Attack/power modifiers | per hit (each damage roll) | holder attacks | attack-stat / base-power modifiers in the damage calculation |
| `RESIST` | per hit | holder is hit by a matching type | Thick Fat halves the attacker's attack stat |
| `TYPE_IMMUNE` | per target, before accuracy | damaging move of the type, or a status move that uses the type chart (`BEFL_TYPE_CHART`); not the user's own move | Levitate is part of effectiveness; status moves ignore it except type-chart ones (Thunder Wave) |
| `TYPE_ABSORB` | per target, before accuracy | any move of the type from another battler | `CanAbilityAbsorbMove` canceler: heal 1/4, or "unaffected" at full HP; a multi-hit or spread move heals each holder once |
| `NO_GUARD` | per target | either side has it | skips accuracy and reaches Fly/Dig |
| `ACCURACY` | per target | holder attacks | accuracy × 1.3 after stages |
| `STAT_DROP_IMMUNE` bit 0 | per target | holder attacks | Keen Eye ignores target evasion stages |
| `STURDY` | per hit | holder at full HP before this hit, hit would knock it out, the Pokémon (not its substitute) takes it; also the confusion self-hit | ends at 1 HP; a multi-hit's next hit can still knock it out |
| `CONTACT_STATUS` | per hit | contact move, the holder itself was struck (holder may have fainted), attacker standing and able to get the status | expansion `ABILITYEFFECT_MOVE_END` runs per strike (moveend restarts per hit) and needs `EXCLUDING_SUBSTITUTES` damage |
| `CONTACT_DAMAGE` | per hit | same; attacker loses max HP / 8 | Rough Skin |
| `SHIELD_DUST` | the effect's unit | secondary effect (chance > 0) on the holder from another battler's damaging move | `!primary && !affectsUser` |
| `SECONDARY_CHANCE` | the effect's unit | holder's move, chance > 0 | chance × 2 (100 % or more always succeeds; a roll is still drawn) |
| `STATUS_IMMUNE` | the effect's unit; also on entry | status in the mask | blocks the status; a holder entering with such a status is cured |
| `VOLATILE_IMMUNE` | the effect's unit; Intimidate on entry | flinch / confusion / Intimidate bit | Inner Focus, Own Tempo |
| `STAT_DROP_IMMUNE` | the effect's unit; Intimidate | negative stage on the holder caused by another battler (not self-inflicted) | Clear Body, Hyper Cutter, Keen Eye |
| `INFILTRATOR` | per target | holder attacks | the target's substitute does not take the hit nor block status moves |
| `NO_RECOIL` | after move | move recoil (`BEFF_RECOIL`) | Struggle recoil is not prevented |
| `ENTRY_STAT` | on entry | every standing foe on the field | blocked by substitute, the Intimidate bit, `STAT_DROP_IMMUNE`, or stage −6 |
| `END_TURN_STAT` | end of turn, after residuals | holder standing and on the field since the start of the turn | Speed Boost: `isFirstTurn` check; leads boost at the end of turn 1 |

### Substitute interactions

| Ability | With a substitute up |
|---|---|
| Damage modifiers | Apply to the damage dealt, whoever takes it |
| `TYPE_IMMUNE`, `TYPE_ABSORB` | Checked before the substitute: Volt Absorb heals behind a substitute |
| `STURDY`, `CONTACT_*` | Only when the Pokémon itself takes the hit, never for a substitute hit |
| Effect-level hooks | Target effects already need the Pokémon itself to be struck |
| `ENTRY_STAT` | A foe behind a substitute is not affected |
| `INFILTRATOR` | The holder's moves pass the target's substitute (damage and status moves) |
| `NO_RECOIL` | Recoil from substitute damage is also prevented |

### RNG order

All draws use the core's xorshift in this order, so fixed-seed tests pin
it: accuracy (skipped by No Guard), damage random per hit, multi-hit count
after the first damage roll (as before), then per hit: PER_HIT secondary
rolls, then the contact ability's chance roll, drawn only when every other
condition holds. Abilities never draw from the RNG in V1–V10 requests.

## Events

- `EV_ABILITY` (user = holder, target = affected, `ability` = source id,
  `condition` = descriptor kind): shown as "<holder>'s <ability>" with the
  game-rendered ability label, before its consequence (`EV_STAGE`,
  `EV_STATUS`, `EV_HEAL`, `EV_RECOIL`-like `EV_ABILITY_DAMAGE`, …).
- `EV_ABILITY_BLOCK`: an ability prevented something (immunity, absorb at
  full HP, status/volatile/stat-drop immunity, Shield Dust is silent as in
  the game). Shown as "<holder>'s <ability>: no effect".
- `EV_ABILITY_DAMAGE`: Rough Skin damage on the attacker.
- `EV_ABILITY_UNSUPPORTED`: battle start notice.

## Out of scope (stated, not silently ignored)

- Every ability not in the table is `UNSUPPORTED` (sent, shown, inert):
  Mold Breaker, weather and terrain setters, Trace, Pressure (no PP effect
  here), form changes, Moxie-like and HP-threshold abilities, Lightning Rod /
  Storm Drain redirection, Cute Charm, Effect Spore, Poison Touch,
  Aftermath, Magic Guard, Wonder Guard, … (Moxie, Flash Fire, Shed Skin,
  Synchronize and others became supported in V12, see below.)
- Ability changes during battle (Skill Swap, Gastro Acid) do not exist.
- Triple (V11): Intimidate affects every standing foe on the field. V12
  limits it to the adjacent foes (see "Ranges" below); V11 requests keep
  the M4-A behaviour.
- OHKO immunity of Sturdy is not reachable (OHKO moves are refused).

## Verification (release 5)

Native (`-Wall -Wextra -Werror`, -O2 and ASan + UBSan): `test_ability_*`
in `battle_test.c` cover the codec (block position, source id check,
argument rules, later-version fields refused below V11), every hook kind
and its unit (for example: Rough Skin three times on a three-hit move,
each right after that hit's per-hit secondary; Sturdy on the first hit of
a multi-hit only; nothing for a substitute hit; Volt Absorb once per
target, also behind a substitute; Speed Boost not on the turn of a
switch-in), and a random 4-format run of every representative ability
(60 seeds per format, invariants on HP and stages, peak 27 of 1,898 event
slots). Every hook was mutation-checked: disabling or loosening it fails a
test. V1–V10 suites are unchanged and pass.

Dolphin 2609b, release 5 GC + Frontier `frontier-gc-m4a`:

| Run | Result | Ability events seen |
|---|---|---|
| Cube Tester Single (V11) | win in 3 turns | Intimidate at the start (Geodude Atk −1) |
| Cube Tester Double | loss in 14 turns | Levitate blocked Earthquake 4×; Ninjask sent in at turn 6, Speed Boost from turn 7 |
| Cube Tester Triple | win in 9 turns | silent modifiers only (Technician, Huge Power, Compound Eyes) |
| Cube Tester Rotation | win in 30 turns | Guts "not simulated" notice, Intimidate on all three, Clear Body block shown with the GBA-rendered names |
| Debug V11 (abilities), level-5 foes | 22 turns, forfeit | (no ability-relevant moves in that fixture) |
| Debug V10 / V9 / V8 regressions | 6-turn forfeit / win / win | V10 request has `RULE_IGNORE_ABILITIES` |

Not seen in Dolphin (native tests only): contact abilities, Sturdy,
absorb, status/volatile immunity, Shield Dust, Serene Grace, Rock Head,
Infiltrator, No Guard against a semi-invulnerable target. The automated
Cube Tester inputs did not create those situations.

# Milestone 4-A2 (request V12, release 6)

## Why the descriptor changed

The V11 descriptor (one kind, three arguments, a percent chance) covers
simple modifiers and immunities. The M4-A2 abilities do not fit it:

| Need | Example | V12 answer |
|---|---|---|
| Several hooks for one ability | Scrappy (type rule and Intimidate block), Guts (stat and burn rule), Flash Fire (absorb and boost) | an ability is a list of up to 4 hooks |
| A chance that is not a percentage | Shed Skin 1/3, contact abilities 1/3 before Gen 4 | chance numerator / denominator |
| A condition on the holder | Guts and Marvel Scale only while statused, Flash Fire's boost only after absorbing | hook flags `REQUIRES_STATUS`, `REQUIRES_STATE` |
| State that lives with the battler | Flash Fire's boost | `ability_state` in the battler's volatiles: cleared on switch-out and faint, kept by Rotation and Triple Shift (the volatile lifetime table) |
| A reach that depends on position | Intimidate in a Triple | a range argument checked by the shared `battle_in_range` |
| An option for the game's generation | Synchronize's toxic rule, Flash Fire while frozen | hook flags set by the GBA from `B_*` |

The GC still never reads a game ability id: the GBA lists hooks, the core
runs hook kinds. V11 descriptors decode into the same structure (one hook,
chance/100), so there is one code path; the V11 encoder refuses an ability
it cannot express, and every V12-only field (kind, flag, second hook, rule
bit) makes an earlier request malformed.

```
V12 ability block, per entry, after the move effect lists:
  0 u8 class (NONE 0 / UNSUPPORTED 1 / HOOKS 2)   1 u8 hook count (0, or 1..4 for HOOKS)
  2 u16 source ability id (= the record's ability id)
  then count x 8 bytes: kind, arg0, s8 arg1, arg2, chance num, chance den (both 0 = certain), percent, flags
```

New hook kinds: `DEFENSE_MULT`, `IGNORE_BURN`, `STAB_MULT`, `KO_STAT`,
`END_TURN_CURE`, `ABSORB_STATE`, `TYPE_PASS`, `STATUS_MIRROR`; `ATTACK_MULT`
may apply to any category and to one type (`TYPE_FILTER`); `ENTRY_STAT` has
a range. `BREAKABLE` is copied from the game's ability data for a later
Mold Breaker and has no effect yet. The decoder refuses a block that claims
more than four hooks before reading any of them (a safety bound: the later
validity check also refuses it, so no test can tell the two apart).

## Designed, not implemented (needs a review before it is built)

| Topic | Design |
|---|---|
| Abilities that ignore others (Mold Breaker, Teravolt) | an attacker hook `IGNORE_BREAKABLE`: during that attacker's move, target hooks with `BREAKABLE` are skipped by the lookup (`bc_ability_next` gets the acting battler). No new wire field: the flag is already sent |
| Abilities that change (Trace, Skill Swap, Role Play, Entrainment) | each battler gets an ability reference (an index into the request's ability blocks) instead of reading its own entry; a change copies the reference and clears `ability_state`. Abilities that come from a move (Worry Seed, Simple Beam) need their blocks in the request: a small "ability catalog" after the entries |
| Suppression (Gastro Acid, Neutralizing Gas) | a volatile "ability suppressed" bit checked by the lookup; Neutralizing Gas is a field condition |
| Order between battlers | hooks at one point run in the existing speed order (entry, end of turn) or in event order (per hit, per target); inside one ability, in hook list order |
| Recursion | hooks that react to events (Synchronize now; later Color Change, Cursed Body) run with a guard so an effect they cause cannot trigger the same reaction again (`ability_mirroring` now; a depth counter when there are more) |
| Lifetime | `ability_state` follows the volatile table (switch, drag, pivot, faint clear it; Shift and rotation keep it); an ability change clears it |

## Ranges

`battle_in_range(battle, holder, other, range)` is the one place that
decides who an effect with several targets reaches. `BRANGE_ALL_FOES`:
every foe on the field. `BRANGE_ADJACENT_FOES`: in a Triple, the foes
adjacent to the holder (the same adjacency table moves use: left reaches
left and center, center reaches all three, right reaches center and right);
in Single, Double and Rotation every foe on the field (no change from
M4-A). The Frontier exporter gives Intimidate the adjacent range in V12.

Other abilities whose reach depends on position in a Triple (none of them
implemented yet, all UNSUPPORTED): Supersweet Syrup, Frisk, Anticipation,
Forewarn, Healer, Friend Guard, Flower Veil, Plus/Minus, Lightning Rod and
Storm Drain redirection, Trace's choice of foe. They will use the same
function with new range values.

## Strict and development modes

`NONE` means the ability does nothing in this model; `UNSUPPORTED` means it
does something the core does not run. The request rule
`RULE_STRICT_ABILITIES` (V12) makes `battle_init` refuse a battle in which
any Pokemon has an UNSUPPORTED ability (`BI_UNSUPPORTED_ABILITY`, shown as
"strict rules: an ability is not simulated"; the result is "unsupported"
and nothing is applied). Without the rule (development, the Cube Tester and
the debug fixtures) the battle runs with the notice, as before. A future
public battle mode should send the strict rule; `NONE` never blocks.

## New abilities (V12)

| Ability | Hooks | Unit / order | Reference behaviour checked |
|---|---|---|---|
| Guts | `ATTACK_MULT` physical 150 while statused; `IGNORE_BURN` | per hit (damage) | Guts stat modifier; burn halving skipped, frostbite still halves special moves |
| Marvel Scale | `DEFENSE_MULT` physical 150 while statused | per hit | defender modifier when Defense is used |
| Adaptability | `STAB_MULT` 200 | per hit | same-type bonus 2.0; Struggle has none (typeless) |
| Moxie | `KO_STAT` Atk +1 per knock-out | after the move, after recoil | every battler the move damaged (not a substitute) that is down, allies included; not when either side has no Pokemon left; not when the holder is down (no observable difference: stages reset on faint) |
| Shed Skin | `END_TURN_CURE` any status, 1/3 (30% only under the Gen 4 setting) | end of turn, before residual damage | the first end-of-turn block, before poison and burn; the chance is drawn only when the holder has a status |
| Flash Fire | `ABSORB_STATE` Fire; `ATTACK_MULT` any category 150 for Fire while the state is set | per target, before the substitute and accuracy (like Volt Absorb); boost per hit | absorbs every Fire move (status moves too), also when already boosted; a frozen holder too from Gen 5 (`B_FLASH_FIRE_FROZEN`); the boost leaves with the battler |
| Scrappy | `TYPE_PASS` Normal and Fighting into Ghost; `VOLATILE_IMMUNE` Intimidate (Gen 8+) | per hit (effectiveness) | only the Ghost part of the multiplier becomes neutral |
| Oblivious | `VOLATILE_IMMUNE` Intimidate (Gen 8+); NONE before Gen 8 | entry | its other effects (infatuation, Taunt, Captivate) belong to moves and abilities the GC refuses or runs inert, so nothing else can differ |
| Synchronize | `STATUS_MIRROR` poison, bad poison, paralysis, burn | right after the status is set, at the effect's own unit | the source gets the same status if it can (type, existing status, its own immunity); the ability shows even when it cannot; bad poison stays bad poison from Gen 5 (`B_SYNCHRONIZE_TOXIC`); a status given back never reflects again (the game's synchronize state); a contact ability's holder is the source of its status |

## Ability lab (Dolphin fixtures)

Debug menu > Utilities > **GC Ability Lab...**: 20 fixtures with fixed
species, ability slot, level, moves, HP and status. Each request carries
`RULE_FIXED_SEED` and a fixed battle id (`0x4C4142nn`), which the GC uses as
its RNG seed, so the same inputs replay the same battle. The Dolphin log
prints every hook (`PH8 ability ... hookN`) and, after each turn, every
battler's HP, status, stages, substitute, confusion and ability state
(`PH8 STATE`), so a run is checked against the core's state, not only its
events.

| Fixture | Shows (fires / does not fire) |
|---|---|
| L01 Sturdy | 1 HP from full; next turn knocked out (not at full HP) |
| L02 Sturdy multi-hit | the first Double Kick hit leaves 1 HP, the second knocks out |
| L03 Rough Skin | once per contact hit (also on the knocking-out hit); knocks out an attacker at 5% HP; no damage for Water Gun |
| L04 Static | paralysis from a contact hit; never for the Electric attacker |
| L05 Flame Body | burn chance per contact hit; never for the Fire attacker |
| L06 Poison Point | poison chance per contact hit; never for the Poison attacker |
| L07 Volt / Water Absorb | heals 1/4; "no effect" at full HP; Surf hits the ally and the other foe |
| L08 Limber | cured on entry; Thunder Wave blocked with a message; Body Slam's chance silent |
| L09 Insomnia / Immunity / Water Veil | each blocks its status; Will-O-Wisp still burns the Immunity holder |
| L10 Intimidate (Triple) | from the left: left foe blocked by Inner Focus, center −1, right untouched |
| L11 Own Tempo / Oblivious / Scrappy | Intimidate blocked by both; Confuse Ray blocked; Scrappy's Tackle hits Gastly, another Tackle has no effect |
| L12 Serene Grace / Shield Dust | 60% Body Slam paralysis; never on the Shield Dust holder |
| L13 Rock Head | no Double-Edge recoil; the partner takes it |
| L14 Infiltrator / No Guard | Bite through a substitute (the substitute keeps its HP); a 50% move hits a flying foe |
| L15 Guts / Marvel Scale / Adaptability | runs; the multipliers are checked natively (damage equivalence tests) |
| L16 Flash Fire | Ember absorbed each turn; the state is set |
| L17 Moxie | +1 per knock-out; nothing after the last one |
| L18 Shed Skin | cures before the burn damage |
| L19 Synchronize | paralysis back to Machop; shown but not set on the Electric Pikachu |
| L20 Strict rules | the GC refuses the battle (an UNSUPPORTED ability) |

## Verification (release 6)

Native (`-Wall -Wextra -Werror`, -O2 and ASan + UBSan): all suites, V1–V11
unchanged and passing. V12 adds wire/validation tests, the Triple range,
damage equivalences for Guts / Marvel Scale / Adaptability, Moxie, Shed
Skin, Flash Fire, Scrappy, Synchronize, strict mode, and a same-seed
determinism run (4 formats × 40 seeds, two cores compared event by event).
Every new hook and codec rule was mutation-checked; the one survivor is the
decoder's hook-count bound, which the validity check also enforces.

Dolphin 2609b, release 6 GC + Frontier `frontier-gc-m4a2` (fixed seeds):

| Fixture | Observed |
|---|---|
| L01 | Sturdy at 1/29 HP after a 28-damage hit; knocked out the next turn |
| L02 | Double Kick: first hit Sturdy (1 HP), second hit knocks out (2 hits) |
| L03 | Rough Skin after each of 2 contact hits (also the knocking-out one); Machop at 5% knocked out by it; replacement's Water Gun: none |
| L04 | Static paralysed Machop on the first contact hit; no roll once paralysed; the Electric Raichu never |
| L05 | Flame Body burned Hitmonlee on turn 4 (then burn damage each turn); the Fire Ponyta never |
| L06 | Poison Point poisoned Hitmonlee on turn 2; the Poison Nidoran♂ never |
| L07 | Volt Absorb +29 HP twice; Water Absorb at full HP "no effect"; Surf hit the ally Pikachu |
| L08 | Limber cured paralysis on entry and blocked Thunder Wave with a message |
| L09 | Insomnia, Immunity, Water Veil each blocked their status; Will-O-Wisp burned the Immunity Snorlax |
| L10 | Triple Intimidate from the left: Inner Focus block (left), −1 (center), right foe untouched |
| L11 | Intimidate blocked by Own Tempo and Oblivious; Confuse Ray blocked by Own Tempo; Scrappy Tackle hit Gastly (62→25 HP), Pikachu's Tackle had no effect |
| L12 | Serene Grace Body Slam paralysed on turn 1; the Shield Dust Dustox never in 3 turns |
| L13 | Rock Head: no Double-Edge recoil; the partner's recoil 10 HP |
| L14 | Infiltrator Bite hit Crobat behind its substitute (substitute kept 31 HP); No Guard Zap Cannon hit Pidgeot in the air |
| L15 | runs with the hooks; the multipliers are verified natively |
| L16 | Flash Fire absorbed Ember on all 3 turns (state 1) |
| L17 | Moxie +1 after each of two knock-outs, nothing after the last |
| L18 | Shed Skin cured the burn at the end of turn 1, before the burn damage |
| L19 | Synchronize: Machop paralysed back; shown but not set on the Electric Pikachu |
| L20 | strict rules: the GC refused (init error, result "unsupported") |

The same fixture run in two Dolphin sessions gave identical events and
state lines (L05 turns 1–3, 48 lines). L04 was first run with a Hitmonlee
attacker, whose Limber correctly stopped Static; the fixture now uses a
Machop.

Regressions (Dolphin, release 6): debug entries V12 and V11 ability
endurance, V10 endurance, V9, V8, V7 conditions, V6 pivot, V5 drag, V4
Struggle (run on its own: the 0-PP lead sends Struggle, hits for 9, the
max-HP/4 recoil of 14 knocks out the 1-HP user; identical in two runs),
V3 switch, V2 reserve, V1 show-only; Cube Tester V12 in all four formats
(Single win in 3 turns, Double loss in 20, Triple loss in 8, Rotation win in 16; 12 ability events, no link loss); result contract on V12 (link cut on the result
screen → "result already final … not restarting", GBA back to the field);
Phase 2 party viewer on Frontier (42 assets, reconnect 42/42 cache hits)
and on the generic connector (23/23); Phase 1 at 160 µs, with faults and at
250 µs. Save SHA-256 unchanged after every run.

Frontier `make check`: `frontier-gc-m4a` (a7e7d6cb9f, the M4-A2 parent)
and `frontier-gc-m4a2` (f8c02476e1) both 3,890 passed / 2,455 failed of
6,806, with the same 2,407 failing test names (no new failure).

| Build | M4-A (release 5) | M4-A2 (release 6) |
|---|---:|---:|
| GC DOL | 306,880 B | 315,008 B |
| GC ELF text / data / BSS | 246,766 / 59,816 / 2,988,544 B | 253,510 / 61,232 / 2,988,928 B |
| GC `BattleRequest` / `Battle` / `EventLog` | 5,152 / 3,736 / 45,556 B | 5,488 / 3,784 / 45,556 B |
| Frontier ROM used | 26,334,336 B | 26,340,772 B |
| Frontier EWRAM (static) | 231,984 B | 231,984 B |
| Frontier request buffer (heap) | 2,565 B | 2,789 B (see known issues) |
