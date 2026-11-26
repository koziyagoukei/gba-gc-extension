# Dolphin integrated mGBA test

1. Use [Dolphin 2609b](https://dolphin-emu.org/download/) or a newer build with integrated mGBA. Use a separate Dolphin user directory with `Dolphin.exe -u <test-profile>` if you want to keep your normal settings separate.
2. Under Controllers, select **GBA (Integrated)** on GameCube port **2**. Configure its ROM as `build/client.gba`. Do not select GBA (TCP).
3. Enable **OSReport EXI** and file logging at Notice level if you want machine-readable statistics. Then open `build/host.dol` in Dolphin. A graphics backend is required to see the sprite. Null is suitable for link-only runs.
4. The short exchange runs automatically and verifies zero, all ones and 0x1234A5C7. The test client disables JoyBus for 30 GBA frames once. The host detects that interruption, resets the link, repeats the handshake and displays `GC CONNECTED`.
5. With the default build, the host runs 10,000 PING/ACK exchanges, transfers and checks 1/4/16/64 KiB, then renders the original 64×64 test image. Check for `POC2 PASS`, `POC3 PASS` for 65,536 bytes, and `POC4 GX PASS`. Leave both windows open until the sprite appears.
6. To repeat with fault injection or a different interval, close the running DOL, rebuild with the desired flags, and reopen both new binaries. `-Faults` corrupts the CRC on one request and deliberately discards another valid ACK; expect one NACK, one timeout, two retries and one GBA duplicate.

Dolphin 2412 requires a valid GBA BIOS for this integrated path. In the tested 2609b build the ROM booted without an external BIOS despite a BIOS-not-found log entry. Neither the BIOS nor the emulator is part of this repository.

Log records use the emulated GC time base. The elapsed time displayed in a log timestamp or on a host stopwatch depends on emulation speed. Count a frame as delivered only after the expected reply passes CRC and content checks.

## Phase 2: pokeemerald-expansion GC Extension Mode

1. Build the ROM from the pokeemerald-expansion `gc-extension` branch and `build/companion.dol` here.
2. In Dolphin, set port 2 to **GBA (Integrated)** with that ROM and a save that has a party. Copy your save into the profile's `GBA/Saves/<rom name>-2.sav`; the mode never writes it.
3. Give GC pad 1 and the GBA different keyboard keys. Otherwise one key press reaches both devices. The reference test profile uses:
   - GBA: A = X, B = Z, Start = Enter, D-pad = T/F/G/H.
   - GC: A = M, Start = B, Z = D.
4. Use a graphics backend and start `companion.dol`. The TV shows `PROBE / Waiting for GBA`.
5. On the GBA, load the save, then open START > debug menu > Utilities and choose **GC Extension Mode** (the last entry). The GBA shows progress (`Sending party`, `Sending sprites n/m`), then `GC CONNECTED > PARTY`. The TV shows the party list and the slot details.
6. On the GBA, A opens the party list. Up/Down moves the cursor and the TV follows. A marks the selection (`SEL` on the TV). B goes back. B in the menu closes the session and returns to the field after `Party/bag/flags/vars unchanged.`.
7. Host controls: START ends the session (the GBA shows `The GameCube ended the session`, with A: reconnect, B: exit). A reconnects after an end. Z stops the link for 3 s; the GBA times out and the host reconnects automatically, using the cache.
8. Cancel checks: B on the GBA while it waits for the GC, or during `Sending sprites`, returns to the field. The host logs `LINK LOST` and keeps probing.

Machine-readable `PH2 ...` lines go to the OSReport log:

- `CAPS`
- `RECORD` with every field
- `ASSET` with key, size, CRC and time
- `READY` with totals and cache use
- `UI` for every GBA input change
- `DIGEST start/end` with match bits
- `BYE`


## Native GC presentation check

Use the unchanged Frontier test ROM and a copy of its save. Start `build/companion.dol` with Integrated GBA on port 2. On the field, use R+START → Utilities → the existing GC Extension / GC Battle debug entries. Check Link Plaza, party selection and the selected slot on TV, Single target highlighting, HP drain/faint, WIN/LOSE and cancellation. After exit check field input and the save copy's hash. See [measured results](presentation.md#regression).

For the SDK-free animation regression:

```sh
cc -std=c11 -Wall -Wextra -Werror -Iprotocol -Igamecube/battle \
  protocol/wire.c protocol/message.c protocol/pkmn_protocol.c protocol/battle_protocol.c \
  gamecube/battle/battle_core.c gamecube/presentation/layout.c \
  gamecube/presentation/animation.c gamecube/presentation/presentation_test.c -o build/presentation-test
```


## Multi-format battle checks

Use a Frontier non-release ROM with the vendored battle protocol and a copy of a
save. In the GBA Utilities debug menu, choose **GC Double (test)**, **GC Triple
(test)** or **GC Rotation (test)**. The mode allocates test teams for both sides
and uses display-only results. See [multi-format mechanics and test cases](multiformat.md).

## Milestone 3.5 checks

The Cube Tester battles are the V9 volatile fixtures. The debug menu adds
**GC Double V8 (effects)** (the Milestone 3 fixtures) and **GC Double V9
(endurance)** (level-5 foes, non-damaging player moves, for 20+ turn runs;
end it with B to forfeit). During a battle, the GC pad's Z button makes the
host silent for 3 s to force a link loss; the battle restarts from its
request after the reconnect. Each session prints `DIAG` lines (link
counters, peaks, the GBA's PING counters and a ring buffer of anomalies)
at battle end, on a GC-side abort and on link loss.

## Release 4 (audit fixes)

Native: `protocol/wire_test.c` (every lost and duplicated word position),
`gamecube/battle/battle_test.c`, `gamecube/presentation/presentation_test.c`
and `gamecube/companion/asset_check_test.c`. The GBA connector is tested
natively too: `tools/connector-test.sh <pokeemerald checkout>` builds the
checkout's real `gc_companion_link.c` (IO registers as variables, the test
raises the serial IRQ per word) and `gc_companion_protocol.c` (RESULT
handling through the frame handler) with ASan and UBSan.

Dolphin: the Cube Tester battles are V10. Debug menu: **GC Double V8
(effects)**, **GC Double V9 (volatiles)** and **GC Double V10 (endurance)**.
To check the result contract, forfeit a battle and press GC Z (3 s
silence) on the result screen: after the reconnect the GC must report
"Battle already finished" and the GBA must return to the field.

## Milestone 4-A (release 5, abilities)

Native: `battle_test.c` adds `test_ability_*` (wire, damage modifiers,
immunity/absorb, accuracy, Sturdy, contact per hit, effect-level hooks,
entry, end of turn, not-simulated notices, and a 4-format random run of
every representative ability checking HP/stage invariants and the event
bound). Each hook was checked by mutation: removing or loosening it makes a
test fail. `presentation_test.c` checks the ability messages.

Dolphin: the Cube Tester battles are V11 (ability fixtures: Single
Intimidate vs Sturdy; Double Serene Grace, No Guard, Speed Boost vs
Levitate, Rough Skin, Own Tempo; Triple Technician, Huge Power, Compound
Eyes vs Limber, Water Absorb, Shield Dust; Rotation Clear Body, Immunity,
Infiltrator vs Static, Intimidate and Guts, which is shown as not
simulated). Debug menu: **GC Double V10 (endurance)** keeps V10, **GC
Double V11 (abilities)** runs the V11 fixtures against level-5 foes. The
Dolphin log prints each entry's descriptor as `PH8 ability ...` and the
ability id with each `PH3 EVENT`.

## Milestone 4-A2 (release 6)

Native: `battle_test.c` adds the V12 tests (wire, Triple range, Guts /
Marvel Scale / Adaptability equivalences, Moxie, Shed Skin, Flash Fire,
Scrappy, Synchronize, strict mode, and a 4-format same-seed determinism
run comparing two cores event by event). Each new hook and codec rule was
mutation-checked.

Dolphin: the Cube Tester battles are V12. Debug menu: **GC Double V11
(abilities)** and **GC Double V12 (abilities)** (level-5 foes), and **GC
Ability Lab...** with 20 fixed-seed fixtures (see
[abilities](abilities.md), "Ability lab"). A fixture replays exactly with
the same inputs: the log's `PH8 STATE` lines after each turn are the
core's state. Inputs per turn: A (fight), the move (DOWN to the next one),
A, then the target (RIGHT to the next legal foe) and A; spread and self
moves have no target step.

## V13 modifier PoC (release 7, [rule-poc](rule-poc.md))

Native: `battle_test.c` adds `test_v13_*`: record validity (every reserved
bit, unknown predicate and stage, packing, per-place rules), the wire
(round trip, malformed bytes, V12-labelled bytes refused, the encoder
refusing V13 records in V12), the largest V12 and V13 requests against
`BATTLE_REQUEST_MAX_BYTES` (fits exactly, one byte short is refused and
nothing is written), Hex and the custom ability against plain moves of the
expected power, one record used from a move and from an ability, order and
rounding inside each stage and the minimum rules, the V12 multiplier kinds
against the same rules written as records (1,296 cases), predicates that
never hold changing nothing (events, state and RNG), and a 4-format
same-seed determinism run.

V12 trace equivalence: `gamecube/battle/battle_trace.c` builds 1,200 V12
battles (every multiplier kind and unusual hook combinations, four
formats) and prints a hash per battle of the wire bytes, every event and
the final state and RNG. Build it against the tree before the change and
after it and compare the outputs (`cmp`); they must be identical.

Dolphin: Utilities → GC Ability Lab... L21–L25 are V13. A fixed-seed run
logs its request (`PH9 REQ`); `gamecube/battle/battle_replay.c` replays the
log on the native core and compares every `PH3 EVENT` / `PH8 STATE` line,
and `-without` replays the same bytes with the V13 records removed:

```sh
cc -std=c11 -O2 -Iprotocol -Igamecube/battle protocol/wire.c protocol/message.c \
  protocol/pkmn_protocol.c protocol/battle_protocol.c gamecube/battle/battle_core.c \
  gamecube/battle/battle_effects.c gamecube/battle/battle_abilities.c \
  gamecube/battle/battle_modifiers.c gamecube/battle/battle_replay.c -o build/battle-replay
build/battle-replay .dolphin/Logs/dolphin.log
build/battle-replay .dolphin/Logs/dolphin.log -without
```

The same replay source builds against the M4-A2 tree (without
`battle_modifiers.c`), so a V12 fixture log can be checked on both cores.

## M4-P (release 8, [modifiers](modifiers.md))

Native: `battle_test.c` adds `test_v14_arithmetic`, `test_v14_order`,
`test_v14_battle`, `test_v14_union_decode` and
`test_v14_expansion_reference` (the GC's V14 values for eleven hits that
the Frontier's own test runner checks against Expansion). The
largest-request test covers V12 to V14.

Expansion reference (Frontier branch `frontier-gc-m4p`):

```sh
make check TESTS="GC V14 reference"
```

V12 / V13 trace equivalence as for the PoC (`battle_trace.c` against
`milestone4a2` and `battle-rule-poc`; arguments 12, 13 and 14).

Dolphin: Utilities -> GC Ability Lab... L26-L29 are V14 (Single, Double,
Triple, Rotation). `battle-replay <log> -version 13` replays a V14 run with
the V13 arithmetic (same bytes, seed and commands) and prints its damage.

## M4-W (release 9, [weather](weather.md))

Native: `battle_test.c` adds `test_v15_*` (wire and validation, weather
set / end / KEEP / start / lock, entry order and deferred replacements,
suppression, the weather damage step, accuracy, speed order, residuals,
moves in a weather, no freeze, strict moves, 240 random weather battles in
four formats run twice, and `test_v15_expansion_reference`).

Expansion reference (Frontier branch `frontier-gc-m4w`):

```sh
make check TESTS="GC V15 reference"
```

Trace: `battle_trace 15` plays the same corpus with weather added (field
rules, weather abilities and moves); 12 to 14 are unchanged and compare
with M4-P. Compile the trace main inside each tree when comparing trees
(its quoted includes resolve next to the source file).

Dolphin: Utilities -> GC Ability Lab... L30-L36 are V15 (L30 Single, L31
Double, L32 Triple, L33 Rotation, L34 strict refusal of primal weather, L35
= L32 with the Gen 5 weather rules and the same seed, L36 a battle-start
weather). The companion logs the field rules as `PH10` lines.

## M4-T (release 10, [terrain](terrain.md))

Native: `battle_test.c` adds `test_v16_*` (wire and validation, set /
same / replace / end / start, surges, the grounded rule, status / sleep /
confusion order, Psychic Terrain per target, damage records, Surge Surfer,
the orders the reference fixes, the item rule, 240 random terrain battles
in four formats run twice, and `test_v16_expansion_reference`).

Expansion reference (Frontier branch `frontier-gc-m4t`):

```sh
make check TESTS="GC V16 reference"
```

Trace: 12 to 15 are unchanged and compare with M4-W (`battle_trace 15`
total `efa20edd`).

Dolphin: Utilities -> GC Ability Lab... L37-L42 are V16 (L37 Single
Electric then Psychic, L38 Double Grassy, L39 Triple Misty, L40 Rotation
Psychic, L41 = L38 with the Gen 7 x1.5 terrain records and the same seed,
L42 strict refusal of Rising Voltage on the GBA). The companion logs the
terrain rules as `PH11` lines. Keys for the scripted runs: a move in the
second slot is `A,DOWN,A,A`; a spread move in a Double is `A,A` (no target).

## M4-B0 (release 11, [items](items.md))

Native: `battle_test.c` adds `test_v17_wire` (blocks, validation, V16
unchanged byte for byte, stage bit 6 gates, malformed bytes), `test_v17_init`
(V16 `BI_ITEMS`, `BI_UNSUPPORTED_ITEM`, normalisation and its notice, strict
`BI_ITEMS_NORMALIZED`, reserves, the item state model), `test_v17_power`
(type items, truncated Muscle Band / Wise Glasses, order after the abilities,
another GBA definition, Struggle, normalised), `test_v17_field_turns` (rocks,
Terrain Extender, surges, Drizzle, no-end / KEEP / start rules, same weather,
normalised), `test_v17_formats` (240 random battles with items in four
formats run twice; every normalised one is event for event the battle
without items) and `test_v17_expansion_reference`.

Expansion reference (Frontier branch `frontier-gc-m4b0`):

```sh
make check TESTS="GC V17 reference"
```

Trace: 12 to 16 are unchanged and compare with M4-T (`battle_trace 15` and
`16` total `efa20edd`).

Dolphin: Utilities -> GC Ability Lab... L43-L50 are V17 (L43 Single, L44
Double, L45 Triple, L46 Rotation, L47 = L43 with Leftovers: every item off,
same seed, L48 = L47 under strict item rules: refused on the GBA, L49 = L43
with the Gen 3 type item records, same seed, L50 Klutz: every item off). The
companion logs the items as `PH12` lines. The debug menu list holds up to 56
entries (`DEBUG_MAX_MENU_ITEMS`).

## M4-B1a (release 12, [items](items.md) section 10)

Native: `battle_test.c` adds `test_v18_wire` (the `BITEM_HP` record, every
byte checked, V17 refuses it, the V18 layout is V17's), `test_v18_init`
(in-memory validation, all-or-nothing normalisation with no end-of-turn item,
strict), `test_v18_end_turn_reference` (26 cases, the table of the Expansion
reference), `test_v18_state` (`item_from`, timings), `test_v18_positions`
(switch, Shift, Rotation), `test_v18_versions` (the V17 tie order kept) and
`test_v18_formats` (240 random battles with Leftovers and Black Sludge).

Expansion reference (Frontier branch `frontier-gc-m4b1a`):

```sh
make check TESTS="GC V18 reference"
```

Trace: 12 to 17 are unchanged and compare with M4-B0 (`battle_trace 17`
total `203feab5`).

Dolphin: Utilities -> GC Ability Lab... L51-L57 are V18 (L51 Single, L52
Double with Grassy Surge and a Black Sludge faint, L53 Triple, L54 Rotation,
L55 = L51 with Life Orb: every item off, same seed, L56 = L55 under strict
item rules: refused on the GBA, L57 Magic Guard with Black Sludge: every item
off). The debug menu list holds up to 64 entries (`DEBUG_MAX_MENU_ITEMS`).

## M4-B1a+ (release 12 kept, [items](items.md) section 11)

GC: no code change; the release-12 `companion.dol` of this branch is the
M4-B1a binary byte for byte. Native: `battle_test.c` adds
`test_v18_gimmick_items` (an audited Z-Crystal / Mega Stone as `BICLASS_NONE`
with its id: kept on the wire, held, inert, normalised and strict as in
items 11.6).

Frontier branch `frontier-gc-m4b1aplus`, `test/battle/gc_gimmick_item_reference.c`:

```sh
make check TESTS="GC V18 gimmick"   # this build's battles (Expansion facts)
make check TESTS="GC V18 export"    # the exporter: audit, conditions, move flags, fixtures, tally
```

`GC V18 export tally` prints one `TALLY` line per chunk of 120 Battle
Frontier sets; add them up for the coverage of items 11.9. The fixture tests
print `FIXTURE` lines (request CRC now and with the M4-B1a export).

Dolphin: Utilities -> GC Ability Lab... L58-L64 are V18 (L58 Single, L59
Double, L60 Triple, L61 Rotation with Z-Crystals and Mega Stones beside
Leftovers / Black Sludge, L62 = L58 with Life Orb: every item off, same seed,
L63 Ultranecrozium Z under strict item rules: refused on the GBA, L64 the
Red Orb beside a Z-Crystal: every item off). The Lab list is full at 64
entries (`DEBUG_MAX_MENU_ITEMS`).

## M4-B1b (release 13, [items](items.md) section 12)

Native: `battle_test.c` adds `test_v19_reference` and `test_v19_more` (the
Expansion facts of items 12.1 as GC battles: thresholds, amounts, every
check point, spread and multi-hit order, Focus Sash, switches), `test_v19_wire`
(records, flags, versions, refusals) and `test_v19_formats` (240 random
battles in four formats, run twice: identical events, state and RNG; the
normalised ones event for event the battle without items; every used-up
item checked against the battler state) and `test_v19_gaps` (the cases the
mutation run found missing: history, a fainted holder, the lost-turn and
per-target order, battler order across sides). `presentation_test.c` adds
the V19 messages and plays a log without `EV_ITEM_CONSUMED` in the same frames.

Frontier branch `frontier-gc-m4b1b`:

```sh
make check TESTS="GC V19 reference"   # this build's battles (Expansion facts), 41 tests
make check TESTS="GC V19 export"      # the exporter: records, unsupported berries, abilities, strict, tally
```

`GC V19 export tally` prints one `TALLY19` line per chunk of Battle Frontier
sets (V18 and V19 side by side); add them up for items 12.9.

Dolphin: Utilities -> GC Ability Lab... L65-L71 are V19 (L65 Single: Lum
against Thunder Wave, Sitrus after a hit, Focus Sash; L66 Double: a spread
hit, Oran, Chesto, Pecha, Focus Sash; L67 Triple: Rawst, Cheri, Persim; L68
Rotation: only the front; L69 = L65 with Life Orb: every item off, same
seed; L70 Cud Chew with Sitrus: every item off; L71 Figy Berry under strict
item rules: refused on the GBA). The Lab list holds 72 entries
(`DEBUG_MAX_MENU_ITEMS`).

ACT-002 repair (branches `milestone4b1b-act002`, `frontier-gc-m4b1b-act002`,
[items](items.md) 12.11): `battle_test.c` adds `test_v19_recharge`; the
Frontier file `test/battle/gc_recharge_item_reference.c` adds 6 tests under
the same prefix (`GC V19 reference`, 47 tests). Lab L72 (`V19 recharge
Sitrus`): Charizard (Solar Power, Sitrus Berry) uses Hyper Beam in sun; on
the recharge turn the GC shows the recharge, then the berry, then the foe's
move.

Ability audit fixes (branches `milestone4b1b-ability-fixes`,
`frontier-gc-m4b1b-ability-fixes`): `battle_test.c` adds `test_abl_g05`
(hook lists the codec refuses or keeps, per version; single contact hooks
with their RNG draws; the strict rule) and `test_abl_g04` (the contact
knock-out order). The Frontier files `test/battle/gc_contact_faint_reference.c`
(8 tests: Rough Skin with a target, an attacker or both knocked out, a
multi-hit move, a substitute, a miss, a Double) and
`test/battle/gc_ability_hooks_export.c` (2 tests: every exported ability for
V11-V19 passes the vendored validity check) use the same prefix (`GC V19
reference`, 57 tests). The faint messages are matched in Japanese (this
build's text): the name codes give `{JPN}{JPN}相手の {JPN}キバニアは 倒れた！`.
No new Lab fixture: the refused lists cannot be exported and the knock-out
order is unchanged.

M4-B1c-0a (branches `milestone4b1c0a`, `frontier-gc-m4b1c0a`, [items](items.md)
section 13): the Frontier file `test/battle/gc_life_orb_reference.c` adds 22
tests under `GC V20 reference: Life Orb` (no MESSAGE checks; the measured
damage is printed as `LO_*` lines). `battle_test.c` adds
`test_c0a_life_orb_arithmetic` to the V19 suite and keeps the request V20
cases in `test_v20_life_orb`, built only by `tools/v20-tests.sh`
(`-DGC_TEST_V20`); until the core implements V20 that script reports 14 of
14 checks failed and exits 1. No core, codec or exporter change. Frontier
`make check`: 6,983 tests, 4,067 PASS, 2,455 FAILED (no test changed status
against the ability fixes; the 22 additions pass). Native -O2/-Werror,
ASan/UBSan and presentation pass; traces V12-V19 and the release-13 DOL
(362,624 B) are byte for byte those of the ability fixes.

M4-B1c-0b (branches `milestone4b1c0b`, `frontier-gc-m4b1c0b`, [items](items.md)
section 14): request V20 and Life Orb. `battle_test.c` runs the saved V20
cases in the default suite (`test_v20_life_orb`: 14 cases, 14 started, 26
effect checks) and adds `test_v20_damage`, `test_v20_wire`, `test_v20_rules`
and `test_v20_paths`; `tools/v20-tests.sh` still runs the saved cases alone
(exit 0). Frontier: `test/battle/gc_life_orb_reference.c` (27 tests: Struggle,
the last foe and Moxie added) and `test/battle/gc_life_orb_export.c` (4 tests
under `GC V20 export`, with the Battle Frontier tally `TALLY20`). Frontier
`make check`: 6,992 tests, 4,076 PASS, 2,455 FAILED (no test changed status
against M4-B1c-0a; the ability-hook export test now covers V11-V20). GC
mutations 20 of 22 killed (2 equivalent), exporter mutations 3 of 3.

M4-B1c-0c (branches `milestone4b1c0c`, `frontier-gc-m4b1c0c`, [items](items.md)
section 15): request V20 on the GBA → GC path. The Frontier debug menu has
Utilities → GC V20 Lab... (L73-L83; the GC Ability Lab list L01-L72 is
unchanged). Each V20 Lab battle logs the request bytes (`PH9 REQ`), and
`battle_replay` replays them on the native core; `battle_replay
-without-items` replays the same battle without the held items' modifier
records (Life Orb's factor) for the counterfactual damage. Frontier tests:
`test/battle/gc_v20_lab_export.c` (L73-L83 built through `Init`),
`test/battle/gc_v20_lab_reference.c` (6 tests: the fixtures' damage at all
16 rolls, the Sitrus-before-Life-Orb order) and
`test/battle/gc_v20_coverage.c` (V19 and V20 coverage of the Battle
Frontier sets by indicator, `COVER` lines). `test_v20_paths` adds the order
of a foe's Sitrus Berry and the loss. Frontier `make check`: 7,000 tests: 4,084 PASS, 2,455 FAILED, 19 KNOWN_FAILING, 419 TO_DO, 9 EXPECT_FAILING, 14 ASSUMPTION_FAIL (no SKIP; -j8 as for M4-B1c-0b: 6,992 / 4,076 PASS / 2,455 FAILED); the 8 new tests pass and no other test changed status. A -j4 run gave 6,980 / 4,084 / 2,435: two Frontier music tests and one contest animation test change with the run order (they pass alone), not with this stage.
Dolphin: the V20 Lab twice (before and after the fix), L01-L72 on the final
DOL and ROM (the 67 fixtures that start a battle are line for line those of the ability fixes' run (requests, item and ability lines, events, states, actions and results; nonces and timings aside); the five GBA refusals (L42, L48, L56, L63, L71) refuse as before and send nothing to the GC), the result contract, a V19 DOL against a V20
GBA. GC mutations 27: 22 killed, 5 equivalent (the version gate and the holder identity check, as in M4-B1c-0b; the loss after the all-items pass or after the whole move end: nothing that pass checks can become due from the holder's own loss; the loss only with consumable items: V20 implies them).

M4-B1c-1a (branches `milestone4b1c1a`, `frontier-gc-m4b1c1a`, [Shell
Bell](shell-bell.md)): investigation only. Frontier
`test/battle/gc_shell_bell_reference.c` adds 34 tests under `GC Shell Bell
reference:` (no MESSAGE checks; the measured values are printed as `SB_*`
lines): 34/34 PASS, twice with identical values; each of nine wrong
expectations (divisor 4, no minimum, overkill counted, per-hit or
per-target rounding, the ally left out, the substitute uncapped, the berry
or the drain after the heal) makes its test fail. The existing
`hold_effect/shell_bell.c` gives 9 PASS and 6 English MESSAGE failures.
`battle_test.c` adds `test_c1a_lost` (default build, read-only: the move's
HP sum through a 100% recoil) and the Shell Bell cases under
`-DGC_TEST_SHELL_BELL` (`tools/shell-bell-tests.sh`: 17 cases, 0 started,
18 checks failed, 12 refusals hold, exit 18 until V21). No core change:
traces V11-V20 are those of M4-B1c-0c. Frontier `make check` (-j8): 7,034 tests, 4,118
PASS, 2,455 FAILED, 19 KNOWN_FAILING, 419 TO_DO, 9 EXPECT_FAILING, 14
ASSUMPTION_FAIL; the 34 new tests pass and no other test changed status
against M4-B1c-0c.

M4-B1c-1b (branches `milestone4b1c1b`, `frontier-gc-m4b1c1b`, [Shell
Bell](shell-bell.md) section 11): request/APP V21. `battle_test.c` runs the
17 failing-first Shell Bell cases in the default suite (17 started, 20 effect
checks, 0 failed, 12 refusals; `tools/shell-bell-tests.sh` exits 0) and adds
`test_v21_wire`, `test_v21_rules`, `test_v21_amounts` (every HP taken from 1
to 100, the cap), `test_v21_paths`, `test_v21_formats` (Single, Double,
Triple, Rotation) and `test_v21_life_orb` (V20 Life Orb paths equal in V21).
-O2 -Werror, ASan/UBSan and the presentation test pass; traces V11-V20 are
those of M4-B1c-1a and the corpus relabelled V21 is V20's. Frontier adds
`test/battle/gc_shell_bell_export.c` (7, `GC V21 export`) and
`test/battle/gc_v21_reference.c` (2, `GC V21 reference`); the 34 `GC Shell
Bell reference` tests: 34/34 PASS twice, the 75 measured `SB_*` values the same in both runs and the same as M4-B1c-1a's. Frontier `make check`: 7,043 tests, PASS 4,127 (+9), FAILED 2,455, KNOWN_FAILING 19, TO_DO 419, EXPECT_FAILING 9, ASSUMPTION_FAIL 14, no status changed against M4-B1c-1a. Not run in
Dolphin (M4-B1c-1c).

M4-B1c-1b-fix (branches `milestone4b1c1b-fix`, `frontier-gc-m4b1c1b-fix`,
[Shell Bell](shell-bell.md) section 12): `battle_test.c` adds
`test_fix_contact_drain` (the review's 322 and the reference's 324 from 1 HP,
Iron Barbs, Static, Life Orb, multi-hit, substitute, knock-out, cap, recoil,
Double spread, four formats; V20 order kept) and `test_fix_item_init`; one
V17 expectation now refuses a decoder-refused record. Frontier adds
`gc_contact_drain_reference.c` (6, `GC contact drain reference`) and
`gc_v21_fix_export.c` (4, `GC V21 fix export`). Expansion: Shell Bell 34/34, V21 references 2/2, Life Orb (`GC V20 reference`) 27/27, the new contact drain references 6/6, all `GC V21` exporter and reference tests 13/13. `make
check`: 7,053 tests: PASS 4,137 (+10, the new ones), FAILED 2,455, KNOWN_FAILING 19, TO_DO 419, EXPECT_FAILING 9, ASSUMPTION_FAIL 14; against M4-B1c-1b no test changed status. Traces V11-V21 unchanged.
