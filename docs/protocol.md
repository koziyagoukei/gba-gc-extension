# JoyBus PoC wire protocol, version 1

All words travel as four bytes, least-significant first. The GC sends 0xff RESET, 0x00 STATUS, 0x15 WRITE and 0x14 READ over SI. The initial raw test precedes framing and verifies 0, 0xffffffff and 0x1234a5c7; it treats zero as data once the response marker is found.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic 0x31434247 (GBC1 in byte order) |
| 4 | 1 | Version = 1 |
| 5 | 1 | Type |
| 6 | 2 | Payload length, 0–224 |
| 8 | 4 | Sequence number |
| 12 | 4 | CRC-32/ISO-HDLC over bytes 0–11 and the exact payload |
| 16 | variable | Payload; zero-padded to the next word, outside the CRC |

The GC assigns contiguous sequences beginning with 1 (HELLO). Replies echo the request sequence; NACK with sequence 0 means the header/CRC could not be trusted. ACK acknowledges the request and can carry response data. The following request implicitly confirms the prior response. The GBA caches the last accepted request and its response. An identical retransmission replays the response without advancing the bulk cursor. A different request that reuses the sequence is rejected. The GC holds the encoded request until completion and sends it up to four times. An attempt ends on a NACK, a reply that fails its header or CRC with nothing else buffered, a failed word write (the outcome on the GBA is unknown; the resend is safe, see below) or the reply timeout. A READ that finds JOY_TRANS not refilled is retried until the attempt's deadline, and a failed READ costs only that word. The session is reset only when every attempt failed or the peer answers out of protocol (release 4; earlier releases reset on the first failed SI transfer, partial frame or bad header).

Frame boundaries (release 4): a receiver that rejects a candidate frame (bad header or CRC) re-checks any later magic word it already holds, so a frame that lost a word does not swallow the start of the next one; a candidate is accepted only with a valid header, length and CRC, so a magic value inside a payload cannot pass as a frame. The GBA responder also drops a partial request that gets no new word for two frames and sends NACK 0 at once, so the host resends without waiting for its reply timeout. The NACK after a failed candidate is skipped while a later candidate is in progress.

| Type | Message | Request and ACK payload |
|---:|---|---|
| 1 | HELLO | Empty request and ACK |
| 2 | ACK | Reply, variable length |
| 3 | NACK | Empty reply |
| 4 | PING | Request u32 pattern; ACK pattern plus cumulative GBA reset, overflow, CRC error and duplicate counters (u32 each). Release 4 GBA connectors append NACKs sent, stale partial requests dropped, resyncs, peak receive-queue depth and requests handled (40 bytes in all; hosts read what they know) |
| 5 | BULK_BEGIN | u32 total size, u32 kind (0 generated pattern, 1 test sprite); empty ACK |
| 6 | BULK_DATA | u32 offset and u32 length; ACK carries the requested bytes |
| 7 | BULK_END | Empty request; ACK carries u32 CRC-32 of the complete object |

The sprite object contains 2,048 bytes of 8×8 tiled 4bpp indices, followed by a 32-byte BGR555 palette. Even x uses the low nibble. Index zero is transparent. The bulk test pattern is `((offset*29) ^ (offset>>8) ^ 0xa5) & 255`.

## Message layer (version 2) and Pokémon application

Frame types 8–12 (CAPS, APP, ASSET_INFO, ASSET_CHUNK, BYE) use the same frame format. A client that does not know them NACKs them, as the Phase 1 client does. The layouts, the Pokémon APP opcodes and the 100-byte PokemonRecordV1 are in [Phase 2](phase2.md). The C definitions are in `protocol/message.h` and `protocol/pkmn_protocol.h`, with tests in `protocol/message_test.c`.
