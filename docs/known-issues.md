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

## Phase 3

- In Dolphin, the first SI write after a pause of several frames with no JoyBus traffic can fail. Hosts must keep traffic continuous (the battle and party views poll every frame).
- The battle core is SINGLE with one Pokémon per side.
  - **Moves:** only plain damaging moves without additional effects, at priority 0, are supported. Everything else is marked unsupported and cannot be chosen.
  - **Not simulated:** abilities, items, status, crits and gimmicks are normalised away by request rules. There is no switching and no Struggle.
- A reconnect in the middle of a battle restarts it from the request. Only the final result of a battle id is ever applied.
- The frontier test picks the first able party member with a supported move, not a player-chosen one.
- Party digests are compared within one mode visit. Walking in the field legitimately changes party bytes (step counters), so digests from different visits or boots differ.
- (Fixed in release 1) The lobby no longer draws the previous party behind the ENDED and PROBE screens.

## Public alpha (release 1, v0.1.0-alpha)

- **No GBA patch is distributed.** A BPS patch of the Frontier ROM against Japanese Emerald would be 20.6 MB and mostly new data derived from the official games (see [releases](releases.md)). Until a distributable form exists, only developers can build the GBA side from source.
- A release DOL only runs with a GBA build of the same release, and refuses development builds (release 0). Development DOLs accept any peer.
- The Cube Tester battles use fixed temporary teams (level 23 vs level 13) and the V7 condition rules. Results are display-only; the HP/PP apply path stays in the debug menu.
- Abilities, items, critical hits, weather, terrain, gimmicks, most secondary effects, priority moves and multi-hit moves are not simulated. Unsupported moves cannot be chosen.
- Move animations are not implemented; the menu entry says so.
- After a session, the GC waits for the next one on its own after 3 seconds (A does it at once). A version mismatch stays on screen until A.
- A link loss during a battle restarts the battle from its request (turn 1, original HP/PP).
- The Frontier test suite (`make check`) reports 2,455 failing tests; the same failures occur on `frontier-hack` before the GC integration and are not caused by it.
