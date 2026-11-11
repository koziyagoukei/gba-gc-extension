# Benchmarks

Measured on Dolphin 2609b with integrated mGBA on GC port 2, no external GBA BIOS, devkitARM/devkitPPC and libogc. The normal speed limit was disabled. Durations and RTTs use the emulated GC time base. These are emulator observations, not a physical cable benchmark.

## Round trip and reliability

The raw 32-bit exchange passed for zero, all ones and 0x1234A5C7. An intentional 30-frame SIO interruption was detected. A subsequent JoyBus RESET and raw handshake re-established the connection.

| 160 µs run | Successful exchanges | Sent / received frames | CRC failures (GC / GBA) | NACKs | Retries | Duplicates (GC / GBA) | GBA queue overflows | GBA resets | Timeouts | SI errors during test |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean | 10,000 | 10,000 / 10,000 | 0 / 0 | 0 | 0 | 0 / 0 | 0 | 2 | 0 | 0 |
| Injected CRC and missing ACK | 10,000 | 10,002 / 10,002 | 0 / 1 | 1 | 2 | 0 / 1 | 0 | 2 | 1 | 0 |

Both resets occurred during the initial raw test, before the measured run. Frames received in the injected run include the NACK and the valid ACK intentionally discarded by the fault injector. GBA cumulative counters are read from the final PING response. GC counters are scoped to the measured test. The clean RTT was **7.037 ms mean, 7.038 ms median / p95 / p99**. The two recovered faults increased the fault-run mean to approximately **7.05 ms**.

## Bulk transfer from GBA to GameCube

Each cell is elapsed emulated seconds / effective KiB/s, from BEGIN through the ACK containing the final CRC. Each size and interval was measured once in the dedicated sweep. The frame size was at most 224 bytes.

| Requested interval | 1 KiB | 4 KiB | 16 KiB | 64 KiB |
|---:|---:|---:|---:|---:|
| 50 µs | 0.152 / 6.574 | 0.581 / 6.879 | 2.295 / 6.970 | 9.147 / 6.997 |
| 100 µs | 0.166 / 6.016 | 0.634 / 6.309 | 2.501 / 6.398 | 9.963 / 6.423 |
| 160 µs | 0.185 / 5.391 | 0.704 / 5.679 | 2.778 / 5.760 | 11.068 / 5.782 |
| 250 µs | 0.214 / 4.678 | 0.814 / 4.916 | 3.208 / 4.987 | 12.779 / 5.008 |

All 16 runs verified the byte pattern and object CRC, with zero CRC failures, NACKs, retries, timeouts, unrecovered frame loss or SI errors during the transfers. The 64 KiB pattern's CRC was `4E5E3311`. The 160 µs full suite additionally transferred the original sprite and palette (2,080 bytes, CRC `C78144A6`) in 0.393 s. The GBA showed `GC CONNECTED` and the diamond-shaped beacon was visually checked in the GC window with the Direct3D 11 backend.

Before the readiness fix, 160 µs bulk tests sometimes read the frame magic twice. Discarding READ values for which JOYSTAT.TRANS was clear removed that failure in the tested runs. The interval controls the requested minimum gap; the measured rate also includes transport overhead, generation, validation, and any polling.
