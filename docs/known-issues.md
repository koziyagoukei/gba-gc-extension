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

## Milestone 3.5 (release 3, development only)

- The Cube Tester battles use the V9 volatile fixtures (level 23 against level 23). The V8 fixtures are under the debug menu entry **GC Double V8 (effects)**; **GC Double V9 (endurance)** runs the V9 fixtures against level-5 foes for long battles.
- GC battles have no weather or terrain: Solar Beam always takes two turns, and Earthquake is never weakened by Grassy Terrain.
- Not supported yet (the move is refused, never sent incomplete): Dive, Phantom Force, Shadow Force, Sky Drop, Skull Bash's charge-turn boost, self-confusing moves (Thrash, Outrage), and any move whose effect list would need more than six descriptors.
- Frontier sessions often show 1–3 CRC errors seen by the GBA, answered with NACK and recovered by a host retry (up to about 0.55 s), mostly while the battle UI loads. The cause is not known; no request has failed because of it. The `DIAG` lines in the Dolphin log show the counters. (Release 4 update: these were requests that lost a word; see below.)
- ~~Opening the Frontier debug menu changes in-memory party, flag and variable data between sessions.~~ Wrong attribution, corrected in release 4: the change comes from field time on the test save's start map (see below).

## Milestone 3.5 audit fixes (release 4, development only)

- Link: a request that loses a word on its way to the GBA is now detected within two GBA frames (the GBA drops the partial frame and NACKs) and resent at once; a resend no longer lands on the stale partial frame. In Dolphin this happens a few times per session, mostly while assets load; recovery takes about 40–70 ms instead of about 0.55 s. **Why the word is lost is still not known**: the GC's SI layer reports every write as successful, the GBA's receive queue never overflows, and the Phase 1 PoC link (other GBA code, same GC code) shows none. The leading hypothesis (not confirmed) is that the GBA does not service the serial IRQ before the next word replaces JOY_RECV, e.g. during DMA or a BIOS call in the asset/redraw path.
- The between-session party digest change has one cause in the test save: the save starts on Frontier's party-bath map, where `Task_PartyBathTimeEffects` lowers each Shadow Pokemon's heart gauge by 10 every 5 seconds (`ShadowPokemon_OnBathTick`). The save's Kyogre is a Shadow Pokemon; its gauge (stored in the bytes that also hold Pokerus) is the only field that changed. The debug menu and the GC modes do not write the party; the save file never changed.
- V8 and V9 requests keep the per-target effect behaviour (a spread move with a self stat change applies it per target). Only V10 carries the effect application unit.
- The cache still has no eviction (128 entries or 2 MiB per power-on) and nickname label keys do not include the name text; left for a later milestone (audit M-01, second half).
- A V7 battle where the opponent only has PP left in a move V7 cannot run (e.g. Ember) ends as unsupported; this is the existing limit, seen in a 36-turn regression run.

## Milestone 4-A (release 5, development only)

- Abilities run only in V11 requests. The core runs 20 generic kinds (see
  [abilities](abilities.md)); the Frontier exporter maps 40 abilities to them
  and 5 ids (none, Run Away, Pickup, Honey Gather, Ball Fetch) to NONE. Every other ability with a battle effect is sent as
  UNSUPPORTED: the battle runs with it inert, the GC says "Ability not
  simulated" with the Pokémon and ability names at the battle start, and
  the Dolphin log marks it `NOT SIMULATED`. Examples: Guts, Pressure,
  Mold Breaker, Trace, weather and terrain setters, Flash Fire,
  Lightning Rod / Storm Drain, Shed Skin, Synchronize, Cute Charm, Moxie,
  Magic Guard, Wonder Guard, Scrappy, Oblivious, Dry Skin, Magma Armor.
- Silent modifiers (Huge Power, Technician, Thick Fat, pinch abilities,
  Compound Eyes, Keen Eye's evasion rule, Serene Grace, Shield Dust,
  Infiltrator) change numbers or skip effects without a message.
- Intimidate affects every standing foe on the field, also in a Triple
  Battle (the reference code has no Triple rules; whether a Gen 5 Triple
  Battle limits it to adjacent foes is not confirmed here).
- Static, Flame Body and Poison Point need the Gen 4+ 30% chance; with the
  older 1/3 the exporter sends them as UNSUPPORTED. Sturdy needs the Gen 5
  rule, Infiltrator the Gen 6 substitute rule (otherwise UNSUPPORTED).
- Rotation: only the front Pokémon takes end-of-turn effects (as residual
  damage already did), so Speed Boost works for the front only.
- Unchanged from release 4: the first lost JoyBus word is still not
  explained (diagnostics and regression runs kept); the asset cache still
  has no eviction and nickname labels are keyed by id (audit M-01).
- Fixed on the M4-A Frontier branch: the debug menu's Utilities list
  (with the GC test entries) was longer than the menu's 21-entry arrays,
  so opening it wrote past a heap allocation and garbled the first rows.
  This existed in every Frontier GC build with more than 21 entries
  (34 in public-alpha-r1, 37 in release 4); those builds are unchanged.

## Milestone 4-A2 (release 6, development only)

- Abilities run with V12 hooks; V11 requests keep the M4-A behaviour
  (Intimidate reaches every Triple foe there). The new abilities are
  UNSUPPORTED in a V11 request.
- Still UNSUPPORTED (shown, inert): Mold Breaker and similar, Trace and
  other ability changes, weather and terrain setters and the weather
  abilities, Pressure, Lightning Rod / Storm Drain, Cute Charm, Effect
  Spore, Poison Touch, Aftermath, Magic Guard, Wonder Guard, Early Bird,
  Hustle, Super Luck, Steadfast, form changes, among others. The designs
  for ability changes and Mold Breaker are in abilities.md, the weather and
  terrain design in weather-terrain.md; none is implemented.
- Guts', Marvel Scale's and Adaptability's multipliers are verified by the
  native tests; in Dolphin the lab fixture only shows the battle running
  with them (damage values alone do not isolate the multiplier).
- Some lab fixtures depend on a chance (Static, Flame Body, Poison Point,
  Serene Grace, Shed Skin): with the fixed seed they show the effect at the
  turn recorded in the M4-A2 report; a code change that moves RNG draws can
  move it to another turn.
- The JoyBus first-word loss is unchanged: still recovered (stale drop,
  NACK, one resend), still not explained. The cache eviction and nickname
  label key (audit M-01) are still open.
- Fixed on `frontier-gc-m4a2`: the Frontier request buffer
  (`REQUEST_CAPACITY`) did not count the V9 volatile rules or the ability
  blocks. A worst-case V12 request (every move with six effects, every
  ability with four hooks) is 2,789 bytes against 2,565; the encoder would
  have refused it and the battle would not have started (no overflow).
  Requests built so far are about 1.7 KB, so no run was affected. The
  buffer now counts both blocks at their maximum (+224 bytes, heap).
- Decided after the M4-A2 design review (2026-11-18): V12 stays the base;
  hooks, conditions and typed operations are to be consolidated step by
  step. An independent rule engine, separate definition sets, a per-ROM
  manifest and a rule RAM cache are on hold; GC memory card persistence is
  deferred. Measured for that review: a battle request takes 0.40–0.63 s
  over the link (about 1.4–2.2 KB/s effective), the battle sprites and
  labels 1.1–4.0 s cold.
- V13 modifier PoC (branch `battle-rule-poc`, release 7, not merged): see
  [rule-poc](rule-poc.md). Hex/Venoshock are supported only in V13; Facade
  stays unsupported (its burn exception is not a multiplier); Hex does not
  treat Comatose as sleep. The decoded record grows `BattleRequest` and
  `Battle` by about 2 KB each (GC) and the GBA's temporary request
  structure by 2,352 bytes.
