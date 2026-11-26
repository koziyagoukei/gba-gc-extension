# Shell Bell: specification, reference and design (M4-B1c-1a)

Stage M4-B1c-1a: investigation, Expansion reference tests, failing-first GC
tests and the implementation design. **No battle logic, exporter or
request/APP change** in this stage. Branches `milestone4b1c1a` (GC) and
`frontier-gc-m4b1c1a` (Frontier), based on M4-B1c-0c (`60e7517` /
`ceb867b5d6`).

Evidence words: `SOURCE_CONFIRMED` (read in code), `TEST_CONFIRMED` (run:
Expansion tests in this build, or GC native tests), `INFERRED` (a design or a
reading not run), `UNKNOWN`. Section 1-4 are what Expansion does (the
reference); sections 5-9 are the GC: what it has today (measured) and what is
proposed (`INFERRED` until M4-B1c-1b implements it).

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

## 6. Design for M4-B1c-1b (`INFERRED`)

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

`battle_test.c`, `#ifdef GC_TEST_SHELL_BELL`, run by
`tools/shell-bell-tests.sh` (left out of the default build; it uses the
proposed names, or the real ones once the protocol defines
`BATTLE_REQUEST_VERSION_AFTER_MOVE_HEAL`):

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

Today: 17 cases, 0 started, 18 checks reached (17 start failures and the
V21 acceptance), 18 failed; refusals 12 of 12 hold; exit 18. When V21 is
implemented every case must start and reach its own assertion.

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
- A heal-blocking effect in the GC: none today; it would add a holder
  condition.
