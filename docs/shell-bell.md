# Shell Bell: specification, reference, design and implementation (M4-B1c-1a, M4-B1c-1b)

Stage M4-B1c-1a: investigation, Expansion reference tests, failing-first GC
tests and the implementation design. **No battle logic, exporter or
request/APP change** in this stage. Branches `milestone4b1c1a` (GC) and
`frontier-gc-m4b1c1a` (Frontier), based on M4-B1c-0c (`60e7517` /
`ceb867b5d6`).

Stage M4-B1c-1b implements it: request/APP V21, the GC core and the Frontier
exporter, checked natively and against the reference (section 11).
Branches `milestone4b1c1b` (GC) and `frontier-gc-m4b1c1b` (Frontier), based
on M4-B1c-1a (`7c2a16c` / `ce3b8de214`). Dolphin and JoyBus are M4-B1c-1c:
nothing in section 11 was run in Dolphin.

Evidence words: `SOURCE_CONFIRMED` (read in code), `TEST_CONFIRMED` (run:
Expansion tests in this build, or GC native tests), `INFERRED` (a design or a
reading not run), `UNKNOWN`. Section 1-4 are what Expansion does (the
reference); sections 5-9 are the GC: what it had in M4-B1c-1a (measured) and
what was proposed (`INFERRED` there); section 11 is what M4-B1c-1b built and
ran (`TEST_CONFIRMED` natively, not in Dolphin).

Sources (Frontier `ceb867b5d6`):
[TryShellBell](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/ceb867b5d69eb7cbd9e01fac213fd9d911375f9d/src/battle_hold_effects.c#L494) ·
[TryMoveDamageUpdate](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/ceb867b5d69eb7cbd9e01fac213fd9d911375f9d/src/battle_move_resolution.c#L3120) ·
[MoveEndSetValues](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/ceb867b5d69eb7cbd9e01fac213fd9d911375f9d/src/battle_move_resolution.c#L3343) ·
[MoveEndSheerForce](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/ceb867b5d69eb7cbd9e01fac213fd9d911375f9d/src/battle_move_resolution.c#L4358) ·
[MoveEndLifeOrbShellBell](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/ceb867b5d69eb7cbd9e01fac213fd9d911375f9d/src/battle_move_resolution.c#L4876) ·
[ClearDamageCalcResults](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/ceb867b5d69eb7cbd9e01fac213fd9d911375f9d/src/battle_util.c#L10289) ·
[move end order](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/ceb867b5d69eb7cbd9e01fac213fd9d911375f9d/include/constants/battle_move_resolution.h#L126) ·
reference tests `test/battle/gc_shell_bell_reference.c` (this stage).

## 1. The heal (Expansion)

`SOURCE_CONFIRMED`, every row `TEST_CONFIRMED` by the reference tests (34,
section 4).

| Point | Rule |
|---|---|
| Base | `gBattleScripting.savedDmg`: reset per move (`ClearDamageCalcResults`), increased at `MOVEEND_SET_VALUES` by `moveDamage[target]` for each target of each strike |
| What `moveDamage` holds then | after `TryMoveDamageUpdate`: the HP the target lost (cut to the HP it had when it faints; reduced by Sturdy / Focus Sash before), or what its substitute took (cut to the substitute's HP when it breaks); 0 for a blocked, missed, immune or absorbed target, for Disguise and for False Swipe at 1 HP |
| Amount | `savedDmg / 8` (holdEffectParam 8), rounded down, **at least 1** (`SetHealAmount` turns 0 into 1) |
| Cap | the heal stops at max HP (`passiveHpUpdate`); at max HP the item does not act at all (`IsBattlerAtMaxHp`) |
| Conditions | `savedDmg > 0`, the move was used (`!unableToUseMove`), not Future Sight's hit, the holder is the battler that started the move (`originalBattlerPartyId == PARTY_SIZE`), not at max HP, not under Heal Block (`B_HEAL_BLOCKING` Gen 5+, this build); `ItemBattleEffects` needs the holder alive; the hold effect is read when the stage runs (`DoMoveEnd` rebuilds `holdEffects`) |
| When | once per move, at `MOVEEND_LIFE_ORB_SHELL_BELL` (the stage Life Orb uses), after every strike and target |
| Not used up | the item stays |

Measured (`SB_*` lines of the reference tests):

| Case | Dealt / lost | Heal |
|---|---|---|
| Seismic Toss 1, 7, 8, 9, 15, 16, 17, 50, 100 | the same | 1, 1, 1, 1, 1, 2, 2, 6, 12 |
| Strength, three rolls | 90, 83, 76 | 11, 10, 9 |
| Seismic Toss 100 into a foe with 7, 10, 20, 63, 64, 99, 100, 150 HP | 7, 10, 20, 63, 64, 99, 100, 100 lost | 1, 1, 2, 7, 8, 12, 12, 12 (the calculated 100 would give 12 each time) |
| Holder at 400 / 399 / 395 of 400, 12 due | — | none / 1 / 5 |
| False Swipe into a foe at 1 HP | 0 lost | none |
| Seismic Toss into a foe at 1 HP | 1 lost | 1 |

## 2. Several strikes and targets (Expansion)

`TEST_CONFIRMED`. The base is one sum over every strike and every target of
the move; the heal is computed once from it, after the last strike and
target. Rounding comes after the sum.

| Case | Each strike / target (lost) | Sum | Heal | "/ 8 per strike or target" would give |
|---|---|---|---|---|
| Bullet Seed, 2 / 3 / 4 / 5 hits | 18 each | 36 / 54 / 72 / 90 | 4 / 6 / 9 / 11 | 4 / 6 / 8 / 10 |
| Bullet Seed, knock-out at hit 2 (foe at 30) | 18, 12 (cut) | 30 | 3 | (36 would give 4) |
| Bullet Seed, substitute (21 HP) breaks at hit 2, three more hits | 18, 3 (cut), 18, 18, 18 | 75 | 9 | (90 would give 11) |
| Bullet Seed into Disguise (Gen 7: no chip) | 0, 18, 18, 18, 18 | 72 | 9 | — |
| Rock Slide into two foes, six rolls | e.g. 52 + 38, 47 + 35 | 90, 82 | 11, 10 | 10, 9 |
| Earthquake: a foe, the ally, a Flying foe | 53, 53, 0 | 106 | 13 | (the foe alone: 6) |
| Rock Slide, one foe protected | 0, 39 | 39 | 4 | — |
| Rock Slide, a substitute and a foe that faints at 11 HP | 39 (substitute), 11 | 50 | 6 | — |
| Rock Slide, both foes faint at 9 and 14 HP | 9, 14 | 23 | 2 | — |

No heal comes between the strikes (the test fails if a holder HP bar
appears there). The ally's HP counts; a protected, missed, immune or
absorbing target adds 0.

## 3. No heal, order, abilities and moving items (Expansion)

`TEST_CONFIRMED` unless marked.

| Case | Result |
|---|---|
| Miss, Protect, Normal into Ghost, Volt Absorb, a status move | none |
| Heal Block (Gen 5+), Embargo, Klutz | none |
| Sheer Force with a move it boosts (Flamethrower) | none: `MOVEEND_SHEER_FORCE` jumps to `MOVEEND_ITEMS_EFFECTS_ALL` |
| Sheer Force with a move it does not boost (Strength) | heals |
| Magic Guard | heals (it stops damage, not this) |
| Hyper Beam | heals on its turn; nothing on the recharge turn |
| Solar Beam (no sun) | nothing on the charging turn; heals on the hit |
| Struggle | its recoil (max HP / 4 = 50), then 1/8 of the HP taken (22 → 2); a recoil that knocks the holder out: none |
| The holder knocked out first (Rough Skin at 1 HP) | none |
| Future Sight's hit, the user on the field or switched out | none |
| Red Card forces the user out before the stage | none (the user is no longer the battler that started the move) |
| Fixed damage (Seismic Toss, Dragon Rage) | heals like any other damage |
| The user switched out mid-move by other means (Eject Pack, Emergency Exit) | `INFERRED` from `originalBattlerPartyId`: none |

Order (each a test; the M4-B1c-0c contract holds for it as for Life Orb):

| Before the heal | After it |
|---|---|
| the strikes and their HP bars; the drain (Giga Drain 71 → +35, then +8); Rough Skin (−50, then the heal); the move's recoil (Double-Edge 113 → −37, then +14); Moxie; **the target's Sitrus Berry** (`MOVEEND_KEE_MARANGA_HP_THRESHOLD_ITEM_TARGET`); Red Card | U-turn's switch (the holder heals, then leaves); Pickpocket (heals, then the item is taken); `MOVEEND_ITEMS_EFFECTS_ALL` |

Life Orb and Shell Bell share the stage; one Pokemon cannot hold both, and
the stage reads only the attacker's item, so their relative order never
shows. A drain that brings the holder to max HP leaves nothing for the bell
(399/400: drain +1, no heal). Knocking out the last foe still heals.

Moving items (reference only; the GC has no item transfer, B2):
a Shell Bell taken this move by Thief or Covet (52 → 6, 26 → 3) or by
Magician (34 → 4) heals for that same move: the item is read when the stage
runs. Pickpocket takes it after it healed.

## 4. Reference tests (Expansion)

`test/battle/gc_shell_bell_reference.c`: 34 tests under `GC Shell Bell
reference:` (33 battle tests and the Battle Frontier count), no `MESSAGE`
checks: HP bars (`captureDamage`), substitute hits, the item animation,
`NONE_OF`, the final HP and items. 34/34 PASS, twice with the same `SB_*`
values. Each wrong expectation listed in testing.md makes its test fail
(divisor 4, no minimum, overkill counted, per-hit or per-target rounding,
the ally left out, the substitute uncapped, the berry after the heal, the
drain after the heal).

The existing `test/battle/hold_effect/shell_bell.c` (15 tests): 9 PASS; the
6 failures are all English `MESSAGE` matches against this Japanese build
(Future Sight x4, Thief/Covet, Magician), not behaviour. The new file covers
each of the six without text.

## 5. The GC today: `MoveContext.lost`

`SOURCE_CONFIRMED` and `TEST_CONFIRMED` (`test_c1a_lost`, read-only, through
a recoil of 100% of `lost`).

| Question | Answer |
|---|---|
| What it holds | per target: `before - after` HP of the Pokemon (`strike`), or what the substitute took (`min(amount, substitute_hp)`); summed into `MoveContext.lost` in `damaging_results` for each landed target |
| Each strike | yes: `strike` adds each hit's loss; a knock-out ends the hits (`five hits 124`; a knock-out at hit 2 counts what was left) |
| Spread targets | yes, every target (the ally included: `spread with the ally 85`) |
| Substitute | yes, cut to its HP when it breaks; the later hits on the Pokemon add theirs |
| Overkill | no: `before - after` (the event's amount is the calculated damage, larger) |
| Moves with no damage | 0: `MoveContext` is a local, zeroed for every move |
| User and target | only targets; the user's own recoil, drain, item or ability HP are not in it |
| The ally | yes when the move hits it |
| Until the move end | yes: `execute_move` reads it for the recoil after all hits |
| Other turns, other Pokemon | no: a new `MoveContext` per move (the second turn's recoil is that turn's HP only) |
| Struggle | `bc_struggle_strike` builds its own `MoveContext` and **discards** `strike`'s return: the value exists but is not passed out |
| Endure (Sturdy, Focus Sash) | `before - after` after the endure: the same as Expansion's adjusted `moveDamage` |
| A protected, missed, immune or absorbing target | not landed: 0 |

So `lost` is the same quantity as Expansion's `savedDmg` for every case the
GC can produce (Disguise, False Swipe and Future Sight have no GC path).
The recoil already uses it as the reference does (`savedDmg * recoil%`).

## 6. Design for M4-B1c-1b (`INFERRED` in M4-B1c-1a; built as planned, section 11)

| Option | Verdict |
|---|---|
| A: only a new `BITEM_HP` operation | not enough by itself: the item code has no access to the move's damage (`bc_item_hp` sees max HP) |
| B: a new common per-move damage accumulator (state in `Battle`) | not needed: `MoveContext.lost` is that accumulator, per move, with the reference's rules; a second one would duplicate it |
| **C: reuse `MoveContext.lost`, add the operation and pass the value to the existing after-move point** | **recommended** |

Changes proposed (no persistent state, no dispatcher):

1. Protocol: request/APP **V21** (`BATTLE_REQUEST_VERSION_AFTER_MOVE_HEAL` /
   `BATTLE_APP_VERSION_AFTER_MOVE_HEAL = 21`), V20 layout. `enum
   BattleItemHpOperation` gains `BIHP_HEAL_DEALT = 4` (the HP the move took
   / divisor, at least 1, up to max HP).
2. `battle_item_valid` (encoder, decoder, `bc_items_check`): `BIHP_HEAL_DEALT`
   only from V21, only at `BITIME_AFTER_MOVE`, divisor 2..255, predicate
   none (argument 0), reserved 0, no `BIREC_CONSUMED`; still one
   after-move record per item (so Life Orb and Shell Bell never share an
   item). V20 and older refuse it: the encoder writes nothing, the decoder
   and `battle_init` refuse.
3. Core: the value travels with the Life Orb pointer of M4-B1c-0c. `execute_move`
   gives `bc_items_after_action` `{origin, landed, lost}` instead of
   `origin`; `bc_struggle_strike` returns its `strike` loss; the after-move
   call of `bc_items_after_action` (between the other Pokemon's HP pass and
   the all-items pass, before the pivot's switch) passes them to
   `bc_item_after_move`, and `bc_item_hp` gets the move's loss for the new
   operation: `lost > 0`, the holder is `origin`, alive and below max HP;
   heal `lost / divisor` (at least 1, at most the room), `EV_ITEM_HEAL` with
   the item id, not used up. Life Orb keeps its condition (`landed`).
4. Negotiation: `party_view.c` mode bound and `battle_view.c` action layout
   accept APP V21; a V20 GC refuses a V21 GBA at the mode check.
5. Exporter (`ResolveItem`): `HOLD_EFFECT_SHELL_BELL` → `HP(AFTER_MOVE,
   HEAL_DEALT, holdEffectParam 8)` from roster V21; UNSUPPORTED before.
   `HpItemAbility` already turns every item off for Sheer Force with an
   after-move record and only reacts to Magic Guard for damage records
   (Shell Bell stays simulated with Magic Guard, as in the reference);
   Klutz stays `ItemAbility`. Moves that give Heal Block are not exported
   (no GC effect).

Size: a Shell Bell block is 12 bytes (one record) against 4: +8 per holder,
+96 at most; `BATTLE_REQUEST_MAX_BYTES(6, 21)` unchanged. Presentation: the
existing "Restored HP with its held item." with the item name.

## 7. Request version

The operation is a new meaning, so it needs a version the peers announce.
A V20 host already refuses an unknown operation at decode (safe, but late:
the battle would fail after the mode check), and the exporter can only know
what the host runs from the APP version. V21 keeps every V20 meaning; V1-V20
requests are unchanged and a V21 record never reaches a V20 peer.

## 8. Failing-first GC tests

`battle_test.c`, run alone by `tools/shell-bell-tests.sh`
(`-DGC_TEST_SHELL_BELL`). In M4-B1c-1a they were left out of the default
build and used the proposed names; since M4-B1c-1b they use the protocol's
names and run in the default suite too. The cases are unchanged.

| # | Case | Today |
|---|---|---|
| 1 | basic: 1/8 of the HP taken, after the hit | refused at the start |
| 2 | at least 1 (a foe with 5 HP) | refused |
| 3 | full HP (none); one below (1, up to max) | refused (2 cases) |
| 4 | overkill: the HP the foe had | refused |
| 5 | multi-hit: one heal of the sum, after the last hit | refused |
| 6 | spread with the ally: one heal of the sum | refused |
| 7 | substitute broken mid move | refused |
| 8 | items off (`RULE_IGNORE_ITEMS`); the GC has no Heal Block | refused |
| 9 | recoil, then the heal; recoil knock-out: none | refused (2) |
| 10 | drain, then the heal; drain to full: none | refused (2) |
| 11 | holder knocked out mid move (Rough Skin) | refused |
| 12 | the foe's Sitrus Berry, then the heal | refused |
| 13 | Struggle: recoil, then 1/8 | refused |
| 14 | pivot: the heal, then the prompt | refused |
| 15 | records: the valid one accepted in V21 | fails (V21 unknown); 6 bad records refused: hold |
| 16 | V19 and V20 refuse the record (`battle_item_valid`, encoder, `battle_init`) | 6 refusals: hold |

M4-B1c-1a: 17 cases, 0 started, 18 checks reached (17 start failures and
the V21 acceptance), 18 failed; refusals 12 of 12 hold; exit 18.

M4-B1c-1b (`TEST_CONFIRMED`, GC native): 17 cases, 17 started (0 refused at
the start), 20 effect checks, 0 failed, 0 started without reaching an effect
check; refusals 12 of 12 hold; exit 0, alone and in the default suite. A
start is now counted apart from the effect checks, and a case that starts
but reaches no effect check fails, so passing the request alone is not
counted as a pass.

## 9. Battle Frontier

The 2,040 `gBattleFrontierMons` sets hold no Shell Bell (the reference test
counts them at run time: 0; Life Orb 142 as the control), so supporting it
adds no Frontier set. It is held once in the Slateport Battle Tent data
(Mightyena) and 8 times in the Trainer Hill data, which the GC exporter
does not use. No set data is changed. The value of this stage is the
move-damage sum and the after-move heal for later items.

## 10. Open

- Emergency Exit with Shell Bell (`shellBellEmergencyExit`), Eject Pack,
  Dynamax HP: no GC path; reference rules recorded only.
- Thief, Covet, Magician, Pickpocket and other item transfers: B2.
- A heal-blocking effect in the GC: none (Heal Block, Psychic Noise and
  Embargo are moves the exporter does not send, `TEST_CONFIRMED` in
  M4-B1c-1b); it would add a holder condition.
- Red Card, Eject Button, Emergency Exit: no GC path. The core keeps the
  origin check (the holder must be the Pokemon that started the move) for
  them; today nothing can replace the user inside its move, so the check
  never decides (section 11.6).
- Dolphin and JoyBus with V21: M4-B1c-1c.

## 11. M4-B1c-1b: implementation and verification

`TEST_CONFIRMED` natively (GC `battle_test.c`, Frontier tests); **not run in
Dolphin** (M4-B1c-1c). Release 13 is kept.

### 11.1 Request / APP V21

| Point | V21 |
|---|---|
| Versions | `BATTLE_REQUEST_VERSION_AFTER_MOVE_HEAL = 21` (`_LATEST`), `BATTLE_APP_VERSION_AFTER_MOVE_HEAL = 21`; V20 request, action, result and event layout |
| New operation | `BIHP_HEAL_DEALT = 4` (`BIHP_COUNT` 5) |
| Record | `BITEM_HP`, timing `BITIME_AFTER_MOVE`, operation `BIHP_HEAL_DEALT`, divisor 2..255, predicate `BPRED_NONE` with argument 0, reserved 0, flags 0 (no `BIREC_CONSUMED`); still one after-move record per item (with Life Orb's, two are refused). Bytes `04 05 04 08 00 00 00 00` for Shell Bell |
| Validity | `item_record_valid` (shared by the encoder, the decoder and `bc_items_check` / `battle_init`): `BIHP_DAMAGE` at the after-move timing from V20, `BIHP_HEAL_DEALT` from V21; the operation at any other timing is refused |
| Older peers | a V20 or older request with the record: the encoder writes nothing, the decoder refuses the bytes (also a V21 request relabelled V20), `battle_init` refuses it from memory (`BI_CONDITIONS`); no V22 |
| Size | `BATTLE_REQUEST_MAX_BYTES(6, 21)` = 3,165 unchanged; a Shell Bell block is 12 B against the 4 B UNSUPPORTED block (+8 per holder, +96 at most) |
| GC companion | `party_view.c` accepts APP 1..21 at the mode check; `battle_view.c` keeps the V6 action layout for V21; the PH12 diagnostic line names `heal dealt 1/8` |
| V20 GC, V21 GBA | `SOURCE_CONFIRMED`: the V20 DOL accepts APP 1..20, so a V21 mode falls through to "MODE 2 not supported by this host" / "Unsupported mode" before any request is fetched (the M4-B1c-0c check of a V19 DOL against V20 showed this path in Dolphin; V21 itself is M4-B1c-1c) |

### 11.2 Core

The design of section 6 (option C), unchanged:

- `BcMoveEnd {origin, landed, lost}` (`battle_internal.h`) replaces the Life
  Orb pointer of M4-B1c-0c. `execute_move` fills it at the end of a move
  that reached its hits (`landed` = `MoveContext.landed`, `lost` =
  `MoveContext.lost`); early ends (charge, Protect, Substitute, no target)
  leave it empty. `bc_struggle_strike` now returns `strike`'s loss and
  `execute_struggle` passes `{origin, true, lost}`. No state is added to
  `Battle` or `Battler` (the value lives on the stack of one action).
- `bc_items_after_action` calls `bc_item_after_move` at the same point as in
  M4-B1c-0c: after the actor's checks and every other Pokemon's HP check,
  before every Pokemon's checks; it runs before the pivot's switch.
- `bc_item_after_move` (V20+, the holder is `origin` and stands) runs the
  item's after-move record through `item_hp`, the shared `BITEM_HP` code
  (`bc_item_hp` is `item_hp` without a move): Life Orb's damage needs
  `landed` (as in V20); Shell Bell's heal needs `lost > 0` and heals
  `lost / divisor`, at least 1, up to max HP, nothing at full HP, through the
  existing `EV_ITEM_HEAL` (amount, `hp_after`, item id) and its message; the
  record is not used up and the item stays held after the battle (no save
  is written).

Order in one action (`TEST_CONFIRMED`): the hits on every target (per strike
since M4-B1c-1b-fix: the damage, the drain, the contact abilities and faints;
then the target's status check) → the move's own recoil → the actor's item checks → Moxie → every other Pokemon's
HP berry → **Shell Bell** → every Pokemon's checks → the pivot's switch.

### 11.3 Exporter

| Point | Rule |
|---|---|
| Record | `HOLD_EFFECT_SHELL_BELL` → `HP(AFTER_MOVE, HEAL_DEALT, GetItemHoldEffectParam(item))` (8, the item's own parameter, not the power cap) from roster V21; outside 2..255 it would stay UNSUPPORTED |
| Before V21 | UNSUPPORTED as before: every item off (`RULE_IGNORE_ITEMS`, notice), strict refused |
| Magic Guard | simulated (a heal is not damage; `HpItemAbility` reacts to damage records only) |
| Sheer Force | every item off (strict: refused), as for Life Orb: it skips the after-move stage for the moves it boosts, which the GC does not model |
| Trace, Receiver, Power of Alchemy, Wandering Spirit | every item off beside any after-move record (new: they could give a Shell Bell holder Sheer Force; Life Orb was already off through its damage record) |
| Klutz, Pickpocket, Magician and the other item abilities | every item off (`ItemAbility`, unchanged) |
| Neutralizing Gas | an ability the GC does not run: UNSUPPORTED, strict abilities refused by the GC (`BI_UNSUPPORTED_ABILITY`); the bell is kept |
| Heal Block, Psychic Noise, Embargo, Gastro Acid, Thief, Covet, Knock Off, Trick, Switcheroo, Future Sight | not run by the GC in any format (`FrontierGcBattle_TestMoveSupport`) |
| Life Orb | the same block and state as V20 |

### 11.4 Verification

| Check | Result |
|---|---|
| Failing-first cases (section 8) | 17 cases, 17 started, 20 effect checks, 0 failed; 12 of 12 refusals; `tools/shell-bell-tests.sh` exit 0 |
| GC native, new (`test_v21_*`) | wire (versions, every edge of the record, the bytes, V20/V22 refused, a V20 request relabelled V21 is byte-identical but the version byte), rules (items off with the notice, strict, UNSUPPORTED before V21), amounts (every HP taken 1..100: 1:1 7:1 8:1 9:1 15:1 16:2 17:2 50:6 100:12; the damage when the foe stands; cap 400/399/395/388/387 → 0/1/5/12/12), paths (miss, immune, Protect, a status move, a charge turn, the foe's and the partner's moves, each move apart, a multi-hit knock-out, a substitute's 10 HP, the foe's Focus Sash, a hit that takes no HP, Struggle recoil knock-out, Moxie first), formats (Double: Protect, both foes knocked out 9 + 14 → 2, a substitute 39 + a knock-out 11 → 6; Triple centre spread; the lead in Single, Double, Triple and Rotation; Life Orb and Shell Bell holders in one turn) |
| V20 Life Orb | the 14 saved cases (26 effect checks) and `test_v20_*` unchanged; 7 Life Orb paths (one hit, multi-hit, Struggle, pivot, the foe's Sitrus, recoil, spread) give the same events, HP and RNG as a V20 and as a V21 request |
| Native builds | -O2 -Wall -Wextra -Werror, ASan/UBSan, presentation test, `battle_replay` build: pass |
| Fixed-seed traces | V11-V20 identical to M4-B1c-1a (V12 `36e5712c` … V20 `65a3b885`); the corpus relabelled V21 is V20's |
| Expansion reference | the 34 tests of section 4: 34/34 PASS twice, the 75 measured `SB_*` values the same in both runs and the same as M4-B1c-1a's; new `test/battle/gc_v21_reference.c` (2): a hit that takes no HP (the foe's Focus Sash at 1 max HP) heals nothing (savedDmg 0) and still costs Life Orb 40 of 400 (damagedByAttack): the GC's `lost` and `landed` |
| Frontier exporter | `test/battle/gc_shell_bell_export.c` (7): the block on both sides and in reserve, V19/V20 UNSUPPORTED and strict refusal, the abilities above, Neutralizing Gas, the moves above, Life Orb in V21, the set tally |
| Frontier `make check` | 7,043 tests: PASS 4,127 (+9, the new ones), FAILED 2,455, KNOWN_FAILING 19, TO_DO 419, EXPECT_FAILING 9, ASSUMPTION_FAIL 14; against M4-B1c-1a no test changed status (the 2,455 failures are the same tests, the baseline of earlier stages; ACT-002, ABL-G05, ABL-G04 and ITM-TIME-003 tests as before) |
| Mutations | GC core and protocol 26: 23 killed, 3 equivalent today (11.6); exporter 8: 8 killed (Shell Bell from V20, no record in V21, a fixed divisor 4, the damage operation, used up, Magic Guard off, the Sheer Force gate for damage only, the copiers without the after-move gate) |

### 11.5 Size and time

| Measure | Result |
|---|---|
| `Battle`, `Battler`, `BattleRequest`, `EventLog`, `BattleEvent`, `BattleItem` | 4,472 / 352 / 6,344 / 47,812 / 24 / 36 B (host), unchanged; `BcMoveEnd` 16 B on the stack only |
| Max request | 3,165 B unchanged; the V21 test request with Shell Bell and Life Orb is 844 B |
| Event log | at most one after-move item event per action, as for Life Orb (`BC_MAX_EVENTS` 1,992 unchanged); the busiest Triple turn measured (every Pokemon holding Shell Bell, multi-hit spread moves, 200 seeds): 100 events (91 without items) |
| DOL (release 13) | 363,712 B (+384 over M4-B1c-1a's 363,328): text +224, data +128, bss unchanged |
| ROM | text + data 26,374,605 B (+192 over M4-B1c-1a, which is M4-B1c-0c's 26,374,413: the exporter's Shell Bell case and the V21 checks); Labs and debug menu unchanged |
| Host time of one turn | V21 Shell Bell 1.62-1.68 µs against 1.55-1.77 µs without an item and 1.64-1.77 µs with Life Orb (three runs, noise level) |
| Link | +8 B per Shell Bell holder in the request; nothing else changes. Measured on the link in M4-B1c-1c |

### 11.6 Mutations that survive (equivalent today)

- The after-move call moved after the all-items pass: every check that pass
  makes after a Shell Bell move has run already (the target's checks after
  its hit, the actor's after the recoil, the others' HP), and the holder has
  no second item, so no observable order changes.
- No origin check: nothing in the GC replaces the user inside its own move
  (no Red Card, Eject Button or Emergency Exit; a fainted holder is caught by
  the faint check).
- No version check in `bc_item_after_move`: before V20 no item can have an
  after-move record (`battle_item_valid`).

The two that M4-B1c-1b's first tests missed (Shell Bell on `landed`, Life
Orb on `lost`) are killed by the hit that takes no HP (11.4).

## 12. M4-B1c-1b-fix: the independent review

Codex's review of M4-B1c-1b (NO-GO) reported three findings; all three are
fixed on branches `milestone4b1c1b-fix` (GC) and `frontier-gc-m4b1c1b-fix`
(Frontier), based on M4-B1c-1b (`c60fd6d` / `0acd7f71b3`). Request/APP V21,
release 13 and every layout are kept. Not run in Dolphin (M4-B1c-1c).

### 12.1 MAJOR: a contact drain move drained after the contact abilities

| | Order | Result |
|---|---|---|
| Expansion (review) | 322 dealt → drain 161 → Rough Skin 50 → Shell Bell 40 | 152 HP, stands |
| GC V21 before the fix (`TEST_CONFIRMED`, reproduced: Attack 457 deals 322) | 322 dealt → Rough Skin 50 → the holder faints; no drain, no heal | 0 HP |
| GC V21 after the fix (`TEST_CONFIRMED`) | 322 → drain 161 → Rough Skin 50 → Shell Bell 40 | 152 HP |
| Expansion in this build (`TEST_CONFIRMED`, `gc_contact_drain_reference.c`: Machamp level 72, damage roll 13; 322 is not reachable with this build's species) | 324 → drain 162 → Rough Skin 50 → Shell Bell 40 | 153 HP |
| GC V21 after the fix, the same 324 (Attack 460) | 324 → 162 → 50 → 40 | 153 HP |

Cause (`SOURCE_CONFIRMED`): the reference's drain is the move's additional
effect (`MOVE_EFFECT_ABSORB`, `HandleSetEffectAbsorb`), run in the hit script
for each strike: this strike's `moveDamage` (cut at the target's or the
substitute's HP) × percent / 100, at least 1 (`SetHealAmount`), the attacker
alive, nothing at max HP. Rough Skin, Iron Barbs and the contact statuses run
later, at `MOVEEND_ABILITIES`. The GC ran the contact abilities inside
`strike` (`hit_aftermath`) and the drain afterwards in `damaging_results`,
from the target's summed loss. Shell Bell's own condition was right (a
fainted holder never heals).

Fix (V21 only, `battle_effects.c`): `strike_drain` runs each strike's drain
in `hit_aftermath`, after Sturdy / Focus Sash and the per-hit effects and
before `bc_ability_contact`; a substitute's strike drains what the
substitute took (no contact ability follows); a deferred single-hit spread
target (V19+) drains in its results pass, before its own contact abilities;
the old drain in `damaging_results` is skipped in V21, so each strike drains
once. `MoveContext.lost`, the recoil, Life Orb and Shell Bell are untouched.
V20 and older keep the old order (fixed-seed traces V11-V20 unchanged).

Covered (`TEST_CONFIRMED`; Expansion for Drain Punch against Rough Skin,
Iron Barbs, Static, Life Orb, too little drain from 1 HP, and Giga Drain
without contact; GC natively for those and for the rest):

| Case | V21 |
|---|---|
| Rough Skin, Iron Barbs | damage → drain → contact damage → Shell Bell |
| Static (contact status) | the drain before the paralysis |
| Life Orb | damage → drain → Rough Skin → the loss (same HP as V20; V20 has Rough Skin first) |
| Too little drain from 1 HP | the drain, then Rough Skin knocks the holder out; no Shell Bell |
| No contact (Giga Drain), no contact ability | the same events as V20 |
| Multi-hit contact drain (GC only: Expansion has no such move) | each strike: damage → its own drain (rounded per strike) → its Rough Skin; one drain per strike |
| Substitute | the drain from what it took; no Rough Skin |
| The target knocked out | the drain from the HP it had (its `EV_FAINT` stays where ABL-G04 has it, before the drain) |
| HP cap | 10 below the max drains 10; none at full HP |
| Recoil on the same move | the drain per strike, the recoil after the hits |
| Double, single-hit spread (deferred) | each target's drain before its own Rough Skin |
| Single, Double, Triple, Rotation | the lead's contact drain into a Rough Skin lead: the drain first, the holder stands |

### 12.2 MAJOR: Imposter could give a Shell Bell holder Sheer Force

Imposter copies its target's ability on entry, Sheer Force included, and
Sheer Force skips Shell Bell after the moves it boosts (section 3). The GC
runs neither, but the exporter accepted such a holder (`ITEMS_SIMULATED`).
Fix (`HpItemAbility`): Imposter turns every item off (strict items:
refused) beside a heal-dealt record (`BIHP_HEAL_DEALT`, which only V21 has).
Its V20 rule (a type changer beside a typed record) is kept, so Imposter with
no item, Leftovers or Life Orb keeps its verdict in V20 and V21; Magic Guard
still keeps the bell; Sheer Force and Trace are still gated. Transform, Skill
Swap, Role Play, Entrainment and Doodle are not run by the GC
(`TEST_CONFIRMED`, `gc_v21_fix_export.c`).

### 12.3 MINOR: battle_init did not check V17 item blocks

`bc_items_check` ran `battle_item_valid` from V18 only, so a V21 record in a
request built in memory as V17 started (the encoder and the decoder refused
it). It now runs from V17 (the first version with item blocks); V1-V16 are
untouched. One V17 test that ran a decoder-refused record through
`battle_init` (an attack-stage item record) now expects `BI_CONDITIONS`.
`test_fix_item_init`: for V17 to V21 the encoder, the decoder (V21 bytes
relabelled) and `battle_init` agree on the V21 record (15 checks); V17
items start in V17, the V19-V21 records do not.

### 12.4 Verification

| Check | Result |
|---|---|
| GC native | -O2 -Werror, ASan/UBSan, presentation, `tools/shell-bell-tests.sh` (17 cases, 20 effect checks, 12 refusals), `tools/v20-tests.sh` (14), `test_fix_contact_drain`, `test_fix_item_init`, every earlier test (ACT-002, ABL-G05, ABL-G04, the four formats): pass |
| Fixed-seed traces | V11-V20 identical to M4-B1c-1b; V21 identical too (the trace corpus has contact moves and contact abilities but no drain move) |
| Expansion | Shell Bell 34/34, V21 references 2/2, Life Orb (`GC V20 reference`) 27/27, the new contact drain references 6/6, all `GC V21` exporter and reference tests 13/13 |
| Frontier `make check` | 7,053 tests: PASS 4,137 (+10, the new ones), FAILED 2,455, KNOWN_FAILING 19, TO_DO 419, EXPECT_FAILING 9, ASSUMPTION_FAIL 14; against M4-B1c-1b no test changed status |
| Mutations | GC: the 9 of the fix killed (the drain back after the contact abilities, a double drain, no per-strike drain, the new order from V20, no drain from a substitute, the target's whole loss per strike, no early drain for a deferred target, battle_init from V18 again, no item check); B1c-1b's 26 rerun: 23 killed, the same 3 equivalent (11.6). Exporter: 4 killed (no Imposter gate, the gate on every after-move record, the gate on any item, heal-dealt read from the timing) |
| Sizes | Battle, BattleRequest, EventLog and the max request unchanged; DOL (release 13) 364,384 B (+672 over M4-B1c-1b: text +608, data +96); ROM text + data 26,374,637 B (+32 over M4-B1c-1b); Labs and debug menu unchanged; the busiest V21 turn measured with 10-hit contact drain spread moves into Rough Skin: 58 events (V20 49) of 1,992 |

### 12.5 Not covered

- Life Orb with Imposter (V20 and V21): Imposter could copy Sheer Force,
  which skips Life Orb's loss; the V20 verdict (simulated) was kept here.
  Closed in M4-B1c-1b-fix2 ([items](items.md) section 19): Imposter beside
  an after-move damage record now turns every item off (strict: refused).
- The target's faint is announced before the drain when a contact drain
  knocks it out (the ABL-G04 order of `EV_FAINT`); HP and outcome are the
  reference's.
- Liquid Ooze, Big Root, Strength Sap and Dream Eater have no GC path.
