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
  Storm Drain redirection, Flash Fire, Shed Skin, Synchronize, Cute Charm,
  Effect Spore, Poison Touch, Aftermath, Magic Guard, Wonder Guard, …
- Ability changes during battle (Skill Swap, Gastro Acid) do not exist.
- Triple: Intimidate affects every standing foe on the field, as in the
  reference code (which has no Triple rules). Whether a Gen 5 Triple Battle
  limits it to adjacent foes is not confirmed here.
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
