# Build

Use Windows PowerShell 5.1 or PowerShell 7 and devkitPro with devkitARM, libgba, devkitPPC, libogc, gbafix and elf2dol. The build invokes native toolchain executables directly, so it does not require MSYS make. Windows defaults to C:/devkitPro. Set DEVKITPRO to override the installation path.

```powershell
./build.ps1                         # Full suite, 160 µs interval
./build.ps1 -Auto 0                 # Short round trip and framed smoke check
./build.ps1 -Auto 2                 # Reliability benchmark only, after handshake
./build.ps1 -Auto 2 -Faults         # Inject one invalid CRC and one dropped ACK
./build.ps1 -Auto 3 -Interval 50    # Bulk sweep at a specific interval
```

Intervals: 50, 100, 160 or 250 µs. Mode 4 is the full sequence. Mode 3 is for repeating the already validated bulk phase. Use the same `build/client.gba` and `build/host.dol` from each build. ELF and linker maps are also under `build/`.

The native wire codec can be tested with a desktop C compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror protocol/wire.c protocol/wire_test.c -o build/wire-test
```

Run the resulting test executable. It covers every legal payload length, little-endian word order, a known CRC-32 vector, corruption, and invalid headers.

## Phase 2

`./build.ps1` also builds `build/companion.dol` with the same `-Interval`; it has no `-Auto` modes.

The GBA side lives in a pokeemerald-expansion checkout (branch `gc-extension`). Copy the shared protocol code there, then check it:

```powershell
./tools/sync-pokeemerald.ps1 -Target C:/path/to/pokeemerald-expansion
./tools/sync-pokeemerald.ps1 -Target C:/path/to/pokeemerald-expansion -Check
```

The script writes `include/gc_ext/*.h` and `src/gc_ext/*.c`; its include paths point to `gc_ext/`. Build the ROM with that project's normal `make` (on the reference machine, `make -j8` in WSL Ubuntu).

The protocol unit tests run with any desktop C compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror -Iprotocol protocol/wire.c protocol/message.c protocol/pkmn_protocol.c protocol/message_test.c -o build/message-test
```
