# Dolphin integrated mGBA test

1. Use [Dolphin 2609b](https://dolphin-emu.org/download/) or a newer build with integrated mGBA. Use a separate Dolphin user directory with `Dolphin.exe -u <test-profile>` if you want to keep your normal settings separate.
2. Under Controllers, select **GBA (Integrated)** on GameCube port **2**. Configure its ROM as `build/client.gba`. Do not select GBA (TCP).
3. Enable **OSReport EXI** and file logging at Notice level if you want machine-readable statistics. Then open `build/host.dol` in Dolphin. A graphics backend is required to see the sprite. Null is suitable for link-only runs.
4. The short exchange runs automatically and verifies zero, all ones and 0x1234A5C7. The test client disables JoyBus for 30 GBA frames once. The host detects that interruption, resets the link, repeats the handshake and displays `GC CONNECTED`.
5. With the default build, the host runs 10,000 PING/ACK exchanges, transfers and checks 1/4/16/64 KiB, then renders the original 64×64 test image. Check for `POC2 PASS`, `POC3 PASS` for 65,536 bytes, and `POC4 GX PASS`. Leave both windows open until the sprite appears.
6. To repeat with fault injection or a different interval, close the running DOL, rebuild with the desired flags, and reopen both new binaries. `-Faults` corrupts the CRC on one request and deliberately discards another valid ACK; expect one NACK, one timeout, two retries and one GBA duplicate.

Dolphin 2412 requires a valid GBA BIOS for this integrated path. In the tested 2609b build the ROM booted without an external BIOS despite a BIOS-not-found log entry. Neither the BIOS nor the emulator is part of this repository.

Log records use the emulated GC time base. The elapsed time displayed in a log timestamp or on a host stopwatch depends on emulation speed. Count a frame as delivered only after the expected reply passes CRC and content checks.
