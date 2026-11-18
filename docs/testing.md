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
