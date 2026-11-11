# Known issues and limits

- Hardware, standalone mGBA/TCP, and third-party cables have not been validated. The Dolphin numbers are not real-cable throughput or latency.
- Disconnect validation deliberately disables the client SIO mode in the emulator. No physical unplug or power-loss test has been performed.
- GC initiates one request at a time. The bulk suite transfers GBA → GC. The reverse direction is exercised by short/framed requests.
- CRC and ACK loss are recoverable by reusing the same sequence number. A failed SI word or malformed header can leave an unknown boundary. The host re-handshakes and abandons that test run instead of resuming a partial asset.
- No persistent state or session resume is implemented. The automatic suite starts once per DOL boot; restart it for a new benchmark.
- The fixture is one raw 64×64 image and 16-color palette. The PoC does not handle Pokémon assets, compression or multiple simultaneous GBA ports.
