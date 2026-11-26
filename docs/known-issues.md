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

## M4-P (release 8, development only)

- V14 matches Expansion for the modelled parts (checked hit by hit in the
  Frontier's test runner). Still outside the GC model: critical hits,
  weather, terrain, items, screens, Parental Bond, Tera. Weather needs its
  own step in the formula (between the spread and the random factor).
- A record converts its percent to the nearest 4.12 value; Expansion
  converts some item percents with a floor (Muscle Band, Wise Glasses), and
  its Paradox defense modifiers chain half up. Items will need a flag.
- A stat or the power stays at least 1 in V14, as before; Expansion has no
  minimum there (reachable only with factors below 1/4096).
- Brine, Toxic Boost, Flare Boost and Defeatist are exported only in V14
  requests; below V14 they stay unsupported, so V13 requests are unchanged.
- During the M4-P Dolphin runs, 8 of 46 sessions had one frame time out
  and recover on its first retry (no link loss). The link code did not
  change; heavy native builds ran on the same machine at the time. The
  JoyBus first-word loss is still unexplained.

## M4-W (release 9, development only)

- Weather only (terrain: M4-T below); items (M4-B: rocks, Utility Umbrella,
  Safety Goggles) are not simulated. Primal weather, Forecast, Flower Gift,
  Hydration, Leaf Guard, Protosynthesis and the other weather abilities in
  [weather](weather.md) stay UNSUPPORTED; so do the moves listed there and,
  in V15, every powder move.
- The overworld weather is not exported: the GBA refuses a V15 battle on a
  map whose weather this build brings into battles.
- V15 changes two V14 behaviours for V15 requests only: accuracy stages use
  the game's ratio table, and replacements made after the same turn run
  their entry abilities together in speed order. V7–V14 keep the old rules.
- The GC core takes up to about 0.2 ms per turn in a V15 Triple (the weather
  lookups are not cached); V12–V14 are unchanged.
- During the M4-W Dolphin runs, 4 of 56 sessions had one frame time out and
  recover on its first retry; the JoyBus first-word loss is still unexplained.

## M4-T (release 10, development only)

- Terrain only as listed in [terrain](terrain.md). Terrain Extender, the
  seeds, Air Balloon and Iron Ball are items (not simulated, M4-B); a V16
  request must normalise items away or the GC refuses it, so no item can
  silently ground or lift a Pokemon. Gravity, Magnet Rise, Telekinesis,
  Ingrain, Smack Down, Thousand Arrows and Roost are not run (unsupported
  moves), so the grounded rule only knows the Flying type and Levitate.
- Terrain-dependent moves (Rising Voltage, Expanding Force, Misty
  Explosion, Psyblade, Terrain Pulse, Grassy Glide, Steel Roller, Ice
  Spinner, Nature Power, Camouflage, Secret Power, Floral Healing) and
  terrain abilities other than the four surges, Surge Surfer and Grass Pelt
  (Mimicry, Quark Drive, Hadron Engine, Seed Sower ...) stay UNSUPPORTED.
- Psychic Terrain's exemption of moves on all battlers or the opponents'
  field cannot happen: those target classes are refused by the exporter.
- During the M4-T Dolphin runs, 10 of 65 sessions had one request time out
  and recover on a retry (no link loss other than the deliberate cut); the
  JoyBus first-word loss is still unexplained.

## M4-B0 (release 11, development only)

- Items only as listed in [items](items.md): power items and durations. A
  battle with any other item, or with an item ability (Klutz, Pickpocket,
  Magician, Frisk, Pickup ...), runs with every item off (shown on the GBA
  and the GC) or, under strict item rules, does not start. Most Battle
  Frontier sets hold items of later stages (Leftovers, berries, Choice items,
  Focus Sash), so for now they run normalised.
- The speed items (Macho Brace, power items) were left for later by decision;
  they are UNSUPPORTED.
- The GBA's pre-battle "items off" lines are hidden behind the connector's
  transfer screen; the notice the player sees is the GBA command screen's
  last line ("ALL ITEMS OFF: <item or ability>") from the first prompt on,
  and the GC's battle-start message and "ITEMS OFF" marker.
- V1-V15 requests still ignore `held_item` without checking
  `RULE_IGNORE_ITEMS` (unchanged; the GBA always sends the rule there).
- No 1,200-battle V17 trace corpus (the 240 random V17 battles of the native
  suite and the Lab replays stand in for it).

## M4-B1a (release 12, development only)

- End-of-turn items are Leftovers and Black Sludge only. Every other item of
  M4-B0's unsupported list still turns every item off (or, strict, refuses
  the battle). About 15% of the Battle Frontier sets now hold an item that
  takes part (items section 10.8); Z-Crystals, Life Orb, Choice items, Focus
  Sash and berries are the next large groups.
- A holder of Leftovers or Black Sludge with Magic Guard, a type-changing
  ability (Color Change, Protean, Libero, Imposter), a max-HP-changing one
  (Power Construct, Tera Shift) or an ability copier (Trace, Receiver, Power
  of Alchemy, Wandering Spirit) turns every item off: the GC does not run
  those abilities.
- V18 orders equal end-of-turn speeds as the reference does (battler order);
  V17 and earlier keep the player's Pokemon first.
- The GBA shows no item activation; the GC shows it with the item's name.

## M4-B1a+ (release 12 kept, development only)

- Audited Z-Crystals (34) and Mega Stones (92) are held with no effect only
  in a V18 battle without gimmicks in which no Pokémon changes form by itself
  or knows a move that reads held items (Knock Off, Poltergeist ...);
  otherwise they still turn every item off (strict: refuse). Ultranecrozium Z
  and the Primal orbs are never accepted.
- The removal and transfer properties of these items are not sent yet; no
  move that could use them is run.
- Sunsteel Strike, Moongeist Beam, Sacred Sword, Chip Away, Darkest Lariat
  and the Shadow moves are unsupported moves in every version now (they were
  run as plain hits before).
- About 42% of the Battle Frontier sets now hold an item that takes part,
  5% with every move and the ability simulated too (items 11.9); Life Orb,
  Choice items, Focus Sash, Assault Vest and berries are the next groups.

## M4-B1b (release 13, development only)

- Consumable items are Oran, Sitrus, Cheri, Chesto, Pecha, Rawst, Aspear,
  Persim and Lum Berry and Focus Sash. Every other berry, Berry Juice, Focus
  Band, the herbs, Air Balloon, Weakness Policy, Red Card, Eject Button and
  the like still turn every item off (strict: refuse).
- A used-up item is back after the battle: the result carries no item and
  the GBA save is not changed (items 12.6 has the plan for a later stage).
- Cud Chew on a berry holder turns every item off (the GC does not eat a
  berry twice); Recycle, Harvest, Belch and the item-moving moves stay
  unsupported.
- The move-end check runs before a pivot's switch. (An action lost to
  recharge now runs the move-end item checks: ACT-002, items 12.11.)
- On every lost turn the GC checks the user's items before the chosen
  target's pending status; Expansion checks the target first. No supported
  effect leaves a status pending, so no battle shows the difference.
- The GBA shows no item activation; the GC shows it with the item's name.

## Ability audit fixes (release 13 kept, development only)

- ABL-G05: an ability with a contact status and a contact damage hook, or
  with a type reaction another of the same type always pre-empts, is now a
  malformed request (V12-V19; [abilities](abilities.md) "Hook sets"). The
  exporter never wrote one. Two hooks of one kind keep the V12 first-hook
  rule; a later request version could make that one hook per kind.
- ABL-G04: when a contact hit knocks the target out, the GC's events put
  the target's faint before the contact ability (Rough Skin); Expansion
  shows the ability, the attacker's damage (and its faint) and then the
  target's faint. HP, who faints, the replacements and the outcome (a draw
  when both last Pokemon faint) are the same. The order is kept for V1-V19;
  moving the faint after the hit's reactions needs a new request version
  ([ability mechanics](ability-mechanics/gc-compatibility.md)).
- Expansion draws a contact status's chance before its other conditions; the
  GC draws only when the status could be set. The chance is the same and the
  RNG streams are not shared, so no result differs.

## M4-B1c-0b (release 13 kept, development only)

- Life Orb is the only after-move item (request V20, [items](items.md)
  section 14). Shell Bell, Rocky Helmet, Weakness Policy, the Choice items
  and every other item of B1c stay UNSUPPORTED.
- Magic Guard, Sheer Force or Klutz on a Life Orb holder turns every item
  off (strict: refused); the GC runs neither of those abilities.
- The defender's item has no final-damage record yet.
- Only Lab-style fixtures and the exporter tests use APP V20: the debug and
  Cube Tester battles keep their APP versions, and the Lab list is full.
  Dolphin was not run for V20.
- When the hit knocks out the last foe and Life Orb then knocks out its
  holder, the GC calls it a draw; the reference was not run for that case.

## M4-B1c-0c (release 13 kept, development only)

- Fixed in V20: a Pokemon's HP berry made due by a Life Orb holder's move
  (the target's Sitrus Berry) came after the Life Orb loss; it now comes
  before it, as in the reference ([items](items.md) section 15.4).
- V20 reaches Dolphin only through the GC V20 Lab list (L73-L83): the normal
  and Cube Tester battles keep their APP versions, and V20 results are shown,
  not applied (as every roster battle's).
- Triple: when a side's Pokemon has no foe in reach, the GBA offers only
  Shift for it (L76 turn 2); this is the existing Triple rule, not V20.
- Not run in Dolphin: Struggle with Life Orb, and a pivot that switches
  after the loss (L78's U-turn knocked out the last foe, so the battle
  ended); both are covered by the native and Expansion tests of section 14.
- The draw when the last foe and then the holder faint is the GC's result;
  the reference's was not run.
