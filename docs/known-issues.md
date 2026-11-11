# Known issues and limits

- Hardware, standalone mGBA/TCP, and third-party cables have not been validated. The Dolphin numbers are not real-cable throughput or latency.
- Disconnect validation deliberately disables the client SIO mode in the emulator. No physical unplug or power-loss test has been performed.
- GC initiates one request at a time. The bulk suite transfers GBA → GC. The reverse direction is exercised by short/framed requests.
- CRC and ACK loss are recoverable by reusing the same sequence number. A failed SI word or malformed header can leave an unknown boundary. The host re-handshakes and abandons that test run instead of resuming a partial asset.
- No persistent state or session resume is implemented. The automatic suite starts once per DOL boot; restart it for a new benchmark.
- The fixture is one raw 64×64 image and 16-color palette. The PoC does not handle Pokémon assets, compression or multiple simultaneous GBA ports.

## Phase 2

- Dolphin + Integrated GBA is the only target. Real hardware is not a requirement and was not tested.
- The game answers one request per GBA frame, so assets move at about 3.7 KiB/s. A 6-member party with labels takes about 9.4 s cold and about 0.35 s once the GameCube cache is warm.
- Only the first animation frame of front sprites is sent. Back sprites, icons, shiny sparkle, animations, cries, and Pokédex/type data are not transferred.
- The GameCube shows the National Dex number, nature id and status from the record but has no names for natures or types. Moves, abilities, items, species and nicknames appear as GBA-rendered labels.
- Labels are cached per id. The same text under different ids (e.g. an un-nicknamed Pokémon's nickname and species) is sent twice.
- The cache is RAM only and is lost when `companion.dol` restarts. It is bounded at 1 MiB / 128 entries and does no eviction; a full cache logs `CACHE FULL`.
- The entry point is the debug menu only (Utilities > GC Extension Mode), which exists in non-release builds.
- The raw SaveBlock1 CRC differs between two visits because the game re-keys encryption on map load. Party, bag (decrypted), flags and vars are compared across visits.
