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
