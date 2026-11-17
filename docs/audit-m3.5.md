# Milestone 3.5 independent audit: response (release 4)

The audit of `milestone3.5` (14ab73b) / `frontier-gc-m3.5` (347079d3f9) /
`gc-extension` (4fd4228869) reported no P0, five P1 and eight P2 findings.
The fixes are on `m3.5-audit-fixes` (gba-gc-extension),
`frontier-gc-m3.5-audit` (Frontier) and `gc-extension-audit` (generic
connector). Release 4 (`m3.5-audit`) pairs them; battle request/APP V10 adds
the effect application unit. Release 1, `main`, `frontier-hack`,
`gc-extension` and the Milestone 3.5 heads are unchanged.

Every fix that changes behaviour has a regression test that fails on the
code before the fix (checked by building the test against the previous
code).

## Findings

| ID | Sev. | Finding | Status | Reproduction / test | Fix |
|---|---|---|---|---|---|
| J-01 | P1 | A request that lost a word left a partial frame on the GBA; the resend's magic word completed it (BAD_CRC) and the resend was swallowed | **Fixed** | `wire_test.c`: every lost and duplicated word position, six frame sizes, magic inside a payload. `tests/connector/link_test.c`: real GBA responder, immediate resend after any lost word, stale partial NACK, no false drop. Both fail on the old code | Parser re-checks later buffered magic words after a failed candidate (still length + CRC validated); GBA drops a partial request after 2 quiet frames and NACKs at once; no NACK while a candidate is in progress |
| J-02 | P1 | One failed SI read/write or a bad header ended the whole session | **Fixed** | Static (no Dolphin fault for this); behaviour covered by the session code paths and Dolphin runs | READ not ready → retried to the attempt deadline; READ error → word lost only; WRITE error → attempt ends and the frame is resent; bad CRC/header → attempt ends only if nothing else is buffered; a transfer waits for a pending callback; session lost only after 4 attempts or an out-of-protocol reply |
| J-03 | P2 | DIAG mixed request time and idle time | **Fixed** | Dolphin logs | Separate request duration and idle gap; per-attempt end reason and wait; SI counters (submits, failures, busy, completion timeouts, status errors, not-ready polls); GBA stale drops, resyncs, NACKs, peak queue depth via PING |
| B-01 | P1 | Scale Noise's self Defense drop applied once per target | **Fixed (V10)** | `test_effect_timing_spread_self`, `test_effect_timing_units`; Dolphin V10 Double: Clanging Scales hit both foes, one Defense −1 after both | Request V10 effect timing: per hit, per target, on damage, after move (user), per use (user). Exporter: self effects after the move, multi-strike secondaries per hit, else per target. V8/V9 keep the old behaviour |
| B-02 | P1 | Struggle bypassed Protect (also substitute, Fly/Dig) | **Fixed** | `test_struggle_shared_checks` (fails on the old core at the Protect case) | Struggle goes through the shared Protect → out-of-reach → strike path; typeless, no PP, random target, recoil after a hit that landed |
| P-02 | P1 | A second result with another id could replace an accepted one; a reconnect after RESULT restarted the battle | **Fixed** | `tests/connector/result_test.c` (old connector: two ids both accepted, second replaces first). Dolphin: forfeit, GC Z on the result screen → reconnect → "Battle already finished", GBA back to the field | Result id = MODE nonce; one final result per mode start (resend → DUPLICATE, other → REJECTED, mode not called); `MODE_FLAG_RESULT_FINAL`; host BYE then ends the mode normally; GC shows the result status |
| B-03 | P2 | A refused turn (pending pivot) cleared Protect and acted flags | **Fixed** | `test_refused_turn_keeps_state` (whole Battle byte-compared; fails on the old core) | Check pending pivot/replacement before turn-start state |
| B-04 | P2 | AUTO-target actions kept a raw target value that reached events when nothing was left to hit | **Fixed** | `test_auto_target_normalized` (slot 200 in an event on the old core) | Canonical target stored when queued; defensive clamp in execute |
| P-01 | P2 | V7/V8 state decode left the volatile bytes uninitialised on the GBA | **Fixed** | `test_state_decode_defaults` (0xA5-filled receiver; fails on the old decoder) | Roster decoders clear later-version fields |
| P-03 | P2 | Layer checks: ACK seq 0 accepted, APP reply app id unchecked, MODE length/CRC only logged, V9 fields decodable in V8 | **Fixed** | `test_version_schema`; session code | ACK must carry the request seq; reply app id checked; request must match MODE length and CRC; later-version fields make the request malformed |
| M-01 | P2 | Texture sizes checked after pointers were formed (32-bit products); no cache eviction; nickname-blind label key | **Partly fixed** | `asset_check_test.c` (wrap-around sizes, empty maps, bank limits, trailing bytes) | Sizes validated in 64-bit before any pointer. **Deferred**: cache eviction and nickname key (risk: a long run of many different Pokemon fills the 128-entry / 2 MiB cache and later assets are not cached; labels of a renamed Pokemon keep the old picture within one power-on) |
| T-01 | P2 | `presentation_test.c:83` failed since M3.5 (a badge step took its own tick) | **Fixed** | Original 18-tick expectation kept; new badge assertions | Volatile badge steps are instant like messages. Dolphin: no visible change (one frame shorter) |
| D-01 | P2 | V7 Rotation debug script fell through into the link check | **Fixed** | Dolphin: no link-check session after the V7 Rotation battle | `releaseall` / `end` added |

## Link behaviour before and after (Dolphin 2609b)

| Run | Before (M3.5, release 3) | After (release 4) |
|---|---|---|
| Phase 1, 160 µs | PASS, 0 errors | PASS, 0 errors (10,000 round trips, 64 KiB `4e5e3311`, GX) |
| Phase 1 fault injection | PASS | PASS (injected CRC → 1 NACK, dropped ACK → 1 timeout + 1 duplicate) |
| Phase 1, 250 µs | — | PASS, 0 errors |
| Frontier party viewer, 160 µs | ASSET_INFO resends with ~0.55 s gaps (GBA counted CRC errors) | 4 ASSET_INFO resends, each NACK after ~38 ms + OK ~28 ms; GBA: 0 CRC, 4 stale drops |
| Frontier V10 battle, 160 µs, 12 turns | — | 8,734 requests: 1 NACK/1 retry (37 + 19 ms), 0 timeouts, 0 CRC; GBA: 1 stale drop, 0 overflow, peak queue 39 |
| Same, 250 µs | (audit: 1 GBA CRC, 1 NACK, 2 retries, 1 timeout) | 5,990 requests: 1 NACK/1 retry (37 + 19 ms), 0 timeouts, 0 CRC; GBA: 1 stale drop |
| Link check worst round trip | 525 ms | 43.6 ms |

The first data loss is **not identified**. What is established: the lost
word never reaches the GBA's receive queue (no overflow), the GC's SI
layer reports every write as successful (status 0, no completion
timeouts), the resend recovers, and the Phase 1 PoC client (other GBA code
on the same GC code) shows none. The leading hypothesis, not confirmed, is
that the GBA does not service the serial IRQ before the next word replaces
JOY_RECV (DMA or BIOS calls in the asset/redraw path). One run per interval
cannot compare 160 and 250 µs rates.

## Between-session digest change (audit section "Frontier Debug digest")

Found with the new `PKMN_STATE_DETAIL` diagnostics op: between two party
viewer sessions only one Pokemon changed (the save's Kyogre, a Shadow
Pokemon in this Frontier build), and only its heart gauge (the bytes that
also hold Pokerus): 0x2526 → 0x24F4, −50. Frontier's party bath
(`Task_PartyBathTimeEffects` → `ShadowPokemon_OnBathTick`) lowers it by 10
every 5 s while the player is on that map, and the test save starts there:
five ticks in the ~25 s spent on the field. Cube Tester sessions (another
map) keep the digest. The debug menu and the GC modes do not write the
party; GC sessions start and end with the same digest; the save file
SHA-256 never changed. The Milestone 3.5 report attributed the change to
the debug menu; that was wrong.

## Verification

- Native, `-Wall -Wextra -Werror`, -O2 and ASan + UBSan: wire, message,
  battle, presentation, asset check; connector link and result tests
  against `frontier-gc-m3.5-audit` and `gc-extension-audit`.
- Dolphin, release 4: V1–V9 debug entries (V1 show-only, V2 reserve, V3
  switch, V4 Struggle, V5 drag, V6 pivot, V7 Double and Rotation, V8, V9);
  Cube Tester V10 Single, Double (Clanging Scales), Triple and Rotation;
  P-02 reconnect; Phase 1 normal, faults, 250 µs; Phase 2 party viewer with
  a forced silence and 42/42 cache hits; link check (release 4 both sides).
- Frontier `make check`: see the release 4 report.
- Save SHA-256 `aff1eb7b…0b0566` before and after every Frontier run.

| Build | Milestone 3.5 | Release 4 |
|---|---:|---:|
| GC DOL | 286,368 B | 292,512 B |
| GC ELF text / data / BSS | 230,358 / 55,740 / 2,850,240 B | 234,838 / 57,404 / 2,850,400 B |
| Frontier linked ROM | 26,329,800 B | 26,331,088 B |
| Frontier EWRAM | 231,984 B | 231,984 B |
