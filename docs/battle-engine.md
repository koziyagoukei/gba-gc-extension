# GC Battle Engine

The GBA owns persistent Pokémon, saves, game data, and result application.
The generic connector provides JoyBus, mode discovery, resolved data and asset
export, second-screen hooks and result delivery. Frontier selects trainers,
rules and rewards. The GC owns transient battle state, deterministic turn
resolution, legal commands, AI and presentation. Layout anchors and GX remain
in the presentation layer.

Behavioral references are used to specify rules. The engine is independently
written. GC packages include original UI, backgrounds and effects. The GBA
supplies game-specific Pokémon sprites, palettes and labels at run time.

## Baseline

The remote tag `gc-battle-minimal-formats` identifies GC `b6f12a5` and
Frontier `efc5406b31`. Dolphin 2609b booted the tagged builds. Rotation won
in three turns, sent an accepted result and completed BYE. The GBA returned to
the field. D-Pad Right moved the player by one tile, confirmed by the scene
scroll. A second entry through the debug menu opened another Rotation battle
with a new ID. The second run cancelled and completed BYE.

## Lifecycle, stage 1

The request contains up to six records per side. The party slot identifies a
record on the GBA. The GC uses its index in the request as a stable battler
ID. A field position maps to one battler ID or an empty sentinel. Initial
send-out uses the first living entrants. The Rotation roster request assigns
the lead to FRONT and the next two to the wings. Shift and Rotation change
the position mapping. The entrants' HP, PP, status and stat stages stay
with their records.

After the last action, fainted battlers leave vacancies. The battle ends
when a side has no living roster member. Before the next turn, replacement
processes vacancies in side and position order. The GBA selects among the
GC's legal reserve IDs, with one token per prompt. The GC validates the
token, position and index against current state. The opponent takes the
first legal reserve. When reserves run out, the remaining positions stay
empty. A simultaneous spread knockout can create several vacancies.

Triple performs an automatic center move when both sides have exactly one
living Pokémon, each is on the field, and their positions are out of range.
It emits position-change events and preserves the battlers' state. The
position indices use the TV-facing convention of the existing GC engine.
BW reference: [BTL_SERVER_FLOW_ProcTripleShift](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L6664).

Rotation wings are occupants, not reserves. A reserve fills a vacant FRONT.
With no reserve, the next command may rotate an eligible wing into FRONT
before the move. This changes formation and emits a rotation event. A
replacement emits a send-out event and does not rotate the other positions.
BW reference: [CollectChangeRequests and ProcRotation](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L2057).

The presentation receives an explicit send-out event with the battler index,
position and HP. It replaces the visual occupant at that position before the
entry animation. The host keeps polling JoyBus while playing events.

## Protocol

Battle request V2 preserves the record encoding and allows a roster wider
than the active formation. It pairs with battle APP V2, which uses opcodes
5/6. The 92-byte state extends the 61-byte field state with a legal reserve
mask and six 5-byte player records (party slot, HP, max HP). REPLACE replies
use the original 8-byte layout with a reserve index and a fresh command
token. V1 rejects V2 state and commands. The GC checks the advertised APP
version against the request version. Older hosts close an unrecognized mode
through the existing BYE path.

The existing Result V1 already holds up to six records per side. It returns
them in request order. Frontier's V2 test uses display-only policy. The
GBA retains its validation and duplicate-result handling, including the
existing V1 Single HP/PP application.

## Verification

Unit tests cover V1/V2 decoding, Single reserves, simultaneous Double
vacancies, exhausted reserves, Triple automatic centering, Rotation
FRONT replacement, wing promotion without replacement and send-out
presentation. The Frontier debug entry **GC Double (reserve test)** starts
two on the field and one on the bench per side. In Dolphin, Surf caused
vacancies, the GBA chose Grovyle for the left position, the opponent sent
out its reserve, and the battle finished in a two-turn WIN with an
accepted result and field return.

The native wire, message, battle and presentation tests passed. On Dolphin
2609b at 160 µs, the regressions covered 10,000 fault-injected pings with
two retries and zero loss, 64 KiB CRC 4e5e3311 and the GX sprite, Party
Viewer reconnect with 42 cache hits, Single PP apply/readback and cancel,
Double, Triple Shift and Rotation. The roster run reported 11 µs peak
core resolution, 9 events in the largest batch and a 50.05 ms maximum
poll interval during presentation. The cold asset load used 21 keys,
7,655 wire bytes and 66,304 B in the cache arena.

The GC DOL is 242,912 B (+5,536 B from the tag). ELF text/data/BSS grew
by 4,744 / 800 / 224 B. Frontier link use is 26,315,396 B ROM (+1,544 B),
231,980 B EWRAM (+52 B) and 28,532 B IWRAM (unchanged). The padded GBA ROM
remains 32 MiB. Hardware has not been tested for this stage.

## Lifecycle, stage 2

Voluntary switching uses battle request and APP V3. The roster state keeps
the V2 layout and also supplies the legal reserve mask in CHOOSE. SWITCH
uses the roster index in the action target byte. V1 and V2 continue to
decode their own commands. The GBA opens the reserve selector with START,
uses the GC mask and echoes the current actor and token. It can return
to the command screen with B.

A turn reserves each selected bench index once per side. The host removes
reserved indices from later prompts and the core checks duplication
independently. Recall and send-out resolve before Shift and attacks.
Other actions aimed at the affected position see the entrant. The
outgoing Pokémon keeps HP, PP and major status, and its stat-stage storage
is cleared. Shift and Rotation keep their own state and events. Rotation
permits a voluntary switch of FRONT with a bench member. A rotation plus
a switch in one command and a direct wing switch are rejected.

The native suite covers switching in all four formats, simultaneous
distinct choices, duplicate reserve rejection, hit resolution against
the entrant, stage cleanup, wire version rejection, and seeded replay.
The core suite also passed ASan/UBSan. On Dolphin, the player recalled
Taillow at 1 HP, sent Grovyle into the left slot before attacks, and won in
three turns. Taillow retained 1 HP and its original PP. The GBA accepted
the result and returned to the field. V2 replacement and the tagged V1
Triple/Rotation ROM were exercised against the V3 host. The JoyBus
fault-injection, Party Viewer reconnect and Single apply/readback/cancel
regressions also passed.

The switch battle logged 11 µs peak core resolution, 8 events in the
largest batch and a 50.05 ms maximum interval between presentation polls.
The GC DOL is 244,256 B (+1,344 B since stage 1), with +1,248 B text,
+92 B data and unchanged BSS. Frontier link use is 26,315,644 B ROM
(+248 B), 231,980 B EWRAM and 28,532 B IWRAM.

## Lifecycle, stage 3

The GC offers Struggle when the battler has exhausted the PP of every
known move and can reach an opponent. This uses the same legality check
for the player and AI. A move with PP that the GC does not implement
stays unsupported. Struggle takes no PP, chooses a random legal foe using
the battle RNG, and resolves physical damage without type effectiveness
or STAB. Recoil can knock out the user, and the core checks both sides for
defeat after the attack and recoil. Double knockouts can result in a draw.

Request and APP V4 carry resolved Struggle power, accuracy and the recoil
rounding rule. The state adds one byte for eligible field positions.
STRUGGLE uses the same eight-byte action envelope. This version handles
the typeless, max-HP-based Gen IV+ rule: one quarter of max HP rounded
down (Gen IV) or to nearest with ties up (Gen V+), with a minimum of one.
The Frontier exporter explicitly rejects earlier recoil rules or a typed
Struggle for V4. V1 through V3 retain their state lengths and commands.

The native tests cover all four formats, exhausted vs unsupported moves,
type immunity bypass, both recoil rounding modes, Rotation with a wing
using Struggle, and simultaneous faint with a draw. The Dolphin Double
fixture began with Taillow at 1 HP and zero PP. Struggle caused 14 recoil
damage, the GBA sent out Grovyle to fill its position, and the battle won
in two turns and returned to the field. Phase 1 fault injection, 64 KiB
and GX, Party Viewer reconnect and cache reuse, Single apply/readback
and cancel, V3 switch, Triple Shift and Rotation all passed.

The V4 battle reported 10 µs peak core resolution, 9 events in the largest
batch and a 33.366 ms maximum presentation poll gap. The GC DOL is
246,656 B (+2,400 B since stage 2), with +2,216 B text, +188 B data and
unchanged BSS. Frontier link use is 26,316,212 B ROM (+568 B), 231,980 B
EWRAM and 28,532 B IWRAM.

## Lifecycle, stage 4

The Frontier exporter maps game move data to resolved trainer-battle
effects. Roar and Whirlwind attempt a status drag. Dragon Tail and Circle
Throw attempt it only after damage lands and the target survives.
The target must have an eligible reserve. Selection uses the battle RNG
and excludes fainted Pokémon and all current field occupants, including
Rotation wings. The entrant inherits the field position. A move still
queued for the outgoing battler is skipped, and later attacks can hit
the entrant. Drag clears the outgoing stat stages and emits a separate
event. Abilities and volatile blockers are covered by later stages.

Request/APP V5 uses the four formerly reserved BattleMon bytes for the
resolved effect of each move. Its state and action layouts match V4.
The legacy requests still ignore those bytes. Unknown V5 effect IDs are
rejected. These effects apply to single foe targets and trainer battles.

Normal switching resolves before Rotation and regular moves. The BW
reference distinguishes both stages in
[BuildActionOrder](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L2197).
The eligibility reference is
[TryForceSwitchTarget / PickRandomBenchPoke](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L6174).

Dolphin confirmed a Dragon Tail hit and a Roar in the same turn.
Each dragged in a legal reserve at its target position. The first trial
also confirmed a Dragon Tail miss with no forced switch. The successful
battle won in three turns, accepted the result and returned to the field.
The native matrix covered all four formats, queued-action cancellation,
miss/immunity, a knockout leading to replacement, and no eligible reserve.
ASan/UBSan, the Phase 1 transport stress run, Party Viewer reconnect,
Single apply/readback and cancel, V4 Struggle, Triple Shift and Rotation
all passed.

The successful battle logged 15 µs peak core resolution, 11 events in the
largest batch and a 50.05 ms maximum presentation poll gap. The GC DOL
is 247,936 B (+1,280 B). GC text/data/BSS grew by 1,136 / 168 / 192 B.
Frontier ROM link use is 26,316,684 B (+472 B), with RAM unchanged.

## Lifecycle, stage 5

Request/APP V6 adds the resolved damaging pivot effect. The Frontier
exporter maps its hit-and-switch move data, including U-turn, Volt Switch
and Flip Turn. The turn stores its ordered actions and current cursor.
After a successful hit, a player with a legal reserve receives a new
mid-turn prompt. The original move and its PP cost have already resolved.
When a valid reserve is selected, the actor withdraws, the entrant appears
in the same position, and the remaining actions resume at the stored
cursor. The turn only increments once. Opponent pivots choose a legal
reserve through the battle RNG.

Immunity, a miss, no legal reserve, and a terminal knockout each bypass
the prompt. The GBA can forfeit while choosing a reserve. Input from
another prompt is ignored through the command token and position checks.
All partial state remains on the GameCube and V6 fixture results are
display-only.

The V6 state remains 93 bytes and adds the PIVOT phase. Its action uses
the eight-byte envelope with a reserve index. Older decoders refuse
the new phase and command. The presentation draws the hit, waits for
the reserve, then plays recall, send-out and the rest of the turn,
continuing to poll during the wait and animations.

In V6 Triple, manual Shift shares priority zero with moves and is
ordered by speed and the RNG tie break. V1 through V5 retain their earlier
Shift ordering. The BW references are
[BuildActionOrder](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L2197)
and [ProcAction](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L2497).
[RunActionOrder](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L1855)
is the reference for turn interruption and continuation.

Dolphin confirmed a U-turn hit, a separate reserve prompt, the selected
Grovyle send-out, and the original turn's remaining actions. The battle
won in three turns and returned to the field. A separate run forfeited
during reserve selection and also returned to the field. Both runs
kept the persistent-state digests unchanged. The native matrix covers
all formats, action replay and invalid reserve rejection, misses and
immunity, no reserve, battle-ending hits, AI pivot and Triple order.
The core passed ASan/UBSan. Phase 1 fault injection and 64 KiB/GX,
Party Viewer reconnect and cache, Single apply/readback and cancel,
Double force effects, Triple Shift and Rotation all passed on Dolphin.

The V6 winning run logged 10 µs peak core time, 8 events in the largest
batch and a 33.366 ms maximum interval between presentation polls.
The GC DOL is 249,472 B (+1,536 B since stage 4). Its text/data/BSS
grew by 1,128 / 344 / 192 B. Frontier uses 26,316,980 B ROM (+296 B),
231,980 B EWRAM and 28,532 B IWRAM. No physical hardware was tested.

Milestone 1 covers initial deployment, voluntary and forced switching,
damaging pivots, faint/replacement, empty positions, Struggle and battle
outcome. The current GBA entry points are debug fixtures. Non-damaging
pivot variants, switch-blocking abilities and volatile effects, and
persistent results for the new formats depend on later milestones.
Next come stat stages, major status and the end-of-turn phase.


## Milestone 2 — stages, major status and the end of turn

The GBA supplies resolved condition rules with Battle Request V7. The
Frontier build exports its paralysis speed and interruption divisors,
burn residual divisor, sleep duration range, thaw probability, type
IDs and rule flags. Its default profile uses half speed and a 1-in-4
paralysis check, burn at 1/16 max HP, 1–3 blocked sleep actions and a
20% chance to thaw. Frostbite uses a capability in the rules. The
Frontier producer rejects rules it cannot represent, including Sleep
Clause and the Champions sleep/freeze profile. The GC owns all rolls.

| State | Identity and lifetime |
| --- | --- |
| HP, PP and major status | Pokémon in a roster entry. HP, PP and the status kind persist through switch, Shift and Rotation. |
| Toxic counter | Pokémon in a roster entry. Starts at zero, advances during eligible poison ticks and resets on normal withdrawal. Shift and Rotation preserve it. |
| Sleep counter | Pokémon in a roster entry. Counts down on eligible actions. Frontier preserves it on normal withdrawal. |
| Stat stages | Field lifetime of the battler. Normal switch, forced switch, pivot and faint clear the stages. Shift and Rotation carry stages with the same battler. |
| Position and front | Formation state. The field position selects a roster index. Rotation makes only the new FRONT eligible for actions and residuals. Wings retain their state. |
| Volatile, side and field effects | Future mechanics will own their state at their corresponding scope. |

All seven stages use signed values capped at −6 and +6. Stats use
`(2 + stage) / 2` for a nonnegative stage and `2 / (2 − stage)` for a
negative one. Accuracy uses the difference between accuracy and evasion
stages capped to the same range and a ratio with base 3. Stages affect
physical and special damage, hit checks and speed. The V7 dynamic
speed rule updates order among pending actions after speed changes.
Priority and the original seeded tie values still determine order.
Rotation uses the selected Pokémon's speed and priority.

Primary condition effects support eligible single-stat moves, poison,
toxic, burn, paralysis, sleep and Refresh. Freeze and optional frostbite
can enter via normalized roster state. Frozen Pokémon can thaw on their
action or when struck by a supported fire attack with the rule enabled.
Major statuses cannot stack. Type immunity is evaluated using the IDs
provided by the game. Powder moves, ability/item immunities, complex
thawing moves, secondary effects and compound stage effects need later
mechanics. The producer marks unsupported moves unavailable. Status
interruption precedes PP consumption.

The turn resumes any pending pivot before it reaches the residual
phase. The residual phase snapshots the eligible field Pokémon by
effective speed, with ties in field order, then applies poison and toxic,
burn, and frostbite as distinct groups. Poison uses 1/8 max HP. Toxic
uses the floored 1/16 max HP unit, with minimum 1, multiplied by its
counter up to 15. The burn divisor also applies to frostbite. A hit
that ends the battle finishes it during actions. At the end of a
residual phase, all residual faints in that batch are resolved before
outcome and replacement, allowing a draw if both sides run out.
Vacant positions are filled through the existing replacement path.

The references for the Frontier profile are `include/config/battle.h`,
`src/battle_move_resolution.c` (sleep, freeze and paralysis),
`src/battle_stat_change.c`, and `src/battle_end_turn.c`
(`HandleEndTurnOrder`, `HandleEndTurnPoison`, `HandleEndTurnBurn`
and `HandleEndTurnFrostbite`). The Rotation action order follows the
selected Pokémon as in [BW BuildActionOrder](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L2095).
The GC rules are implemented independently.

### V7 wire changes

Request V7 appends 12 bytes after the type chart. In order: rule
flags, poison/steel/fire/electric/ice type IDs, paralysis speed divisor,
paralysis block divisor, burn residual divisor, minimum/maximum sleep
counter and thaw percent. The existing four resolved effect bytes
carry the single-stat and primary condition IDs documented in
`battle_protocol.h`. In the Pokémon record, the low byte of status
is the normalized condition kind and the next byte is its counter.
Upper bytes are reserved and zero. V1–V6 retain their previous layouts
and status semantics.

APP V7 adds 48 bytes to the 93-byte roster state. Each position in
side/slot order contains the major status ID and seven signed stage
bytes (Atk, Def, SpA, SpD, Spe, Acc, Eva). It keeps the V6 action and
token layout. The V1 result envelope carries the normalized V7 status
for diagnostics. V7 requests use display-only results. The V1 Single
HP/PP application path continues to use the existing validation.

The TV plays status, cure, blocked action, residual HP and stage events
on the presentation timeline. The GBA shows a condition badge per
position and the selected Pokémon's nonzero stages. The GC polls
during all presentation steps. The GBA's four V7 debug fixtures use
temporary export slots for both parties.


### Milestone 2 verification

The desktop wire, message, battle and presentation suites passed,
including the V7 codec bounds, all stage modifiers, status immunity,
cures, paralysis and sleep interruption, toxic reset on switch,
Rotation retention, both sides fainting at end of turn, replacement,
and pivot continuation before residuals. The Battle Core also passed
ASan and UBSan.

Dolphin 2609b with Integrated GBA ran the V7 fixtures to a win, accepted
result and BYE in 2, 2, 6 and 9 turns for Single, Double, Triple and
Rotation. The Double fixture applied Growl to both opposing positions,
processed two residual faints in turn one and filled one position with
the available reserve. Triple exercised residuals and empty positions.
Rotation retained the burn badge and Attack stage when the same
Pokémon returned to the front. The GBA field was observed after Double
and Rotation. The test profiles used copies of a test save and verified
SHA-256 after exit. The V7 timeout test forced a three-second link
silence mid-battle, reconnected, restarted with the original fixture
HP and PP, then cancelled and returned safely.

Phase 1 passed 10,000 round trips with fault injection, CRC rejection,
two retries and duplicate detection. The 64 KiB transfer completed in
11.070 s at 5.781 KiB/s and GX rendered the transferred sprite. Phase 2
reconnected with 42 cache hits and returned to the field. Phase 3
applied HP/PP, read the decremented PP on reentry, and cancelled safely.
The V1 Triple Shift and Rotation runs also completed. The lifecycle
regression exercised replacement, voluntary switch, Struggle,
Roar and Dragon Tail, and U-turn resumption in the original turn.

| V7 format | Peak core | Peak event batch | Longest presentation poll gap |
| --- | ---: | ---: | ---: |
| Single | 10 µs | 5 | 33.366 ms |
| Double | 23 µs | 13 | 33.366 ms |
| Triple | 37 µs | 15 | 33.366 ms |
| Rotation | 11 µs | 7 | 50.050 ms |

One concurrent V2 run logged a 533.866 ms gap and finished with a
normal result and BYE. A separate V2 run recorded 50.050 ms, with the
same 16 µs peak core time. These are emulator measurements.

| Build measurement | Milestone 1 | Milestone 2 |
| --- | ---: | ---: |
| GC DOL | 249,472 B | 259,232 B |
| GC ELF text | 199,238 B | 208,014 B |
| GC ELF data | 49,936 B | 50,920 B |
| GC ELF BSS | 2,738,944 B | 2,741,568 B |
| Frontier linked ROM | 26,316,980 B | 26,320,076 B |
| Frontier EWRAM | 231,980 B | 231,980 B |
| Frontier IWRAM | 28,532 B | 28,532 B |

Physical hardware and persistent V7 results remain outside this
verification. The Move Effect System can build on the primary status
and stat-stage paths and the existing mid-turn action suspension.


## Milestone 3 — move effect system

Request/APP V8 replaces the single resolved effect byte with an effect list
per move. The GBA resolves it from its own move data; the GameCube runs it
in `gamecube/battle/battle_effects.c` without asking the GBA during a turn.

### What each side owns

| GBA (Frontier exporter) | GameCube (battle core) |
| --- | --- |
| Move ID, power, type, category, accuracy, priority, target class | Accuracy and evasion rolls, type immunity |
| Effect list: kind, subject, chance, arguments | Damage, hit count, secondary-effect rolls |
| Hit-count rule (`B_MULTI_HIT_CHANCE`), Protect base (`B_PROTECT_FAILURE_RATE`), Recover's half heal | Stat stages, statuses, heal/drain/recoil amounts |
| `ignoresProtect`, Thunder Wave's type-chart exception | Protect state, action order, faint, replacement, pivot prompts |
| Unsupported reasons and the game effect ID (diagnostic) | Refusing unusable lists; logging them (`PH6` lines) |

### Wire format

After the V7 condition rules, for every entry (player entries, then
opponent entries) and each of its four moves: `u8 count, u8 move flags,
u16 source effect`, then `count` 8-byte descriptors
`kind, who, chance, arg0, s8 arg1, arg2, flags, reserved`. At most six
effects per move and ten hits. The decoder rejects truncated or trailing
data, nonzero reserved bytes, a nonzero legacy effect byte, and lists that
break the structural rules in `battle_move_effects_valid` (one leading
DAMAGE exactly for damaging moves, chances only on hits, Protect alone,
argument ranges). The request version must match the APP version, and
release numbers keep a release-1 GBA away from a release-2 DOL.

### Effect handlers

| Kind | Arguments | Behaviour |
| --- | --- | --- |
| DAMAGE | min/max hits, hit rule | Accuracy once, then hits; stops when the target faints; MULTI_HIT event for more than one hit |
| STAT | stat, delta | Clamped to ±6; no change emits nothing (status moves fall back to NO_EFFECT) |
| STATUS | status, type-chart flag | Fails on an existing status or a type immunity; sleep counter from the rules |
| CURE | status mask | Refresh-style |
| HEAL | numerator/denominator of max HP | Rounded down, at least 1; FAIL at full HP |
| DRAIN | percent | Of the HP the target lost, at least 1 |
| RECOIL | percent | Once per move, of the HP all targets lost, at least 1 |
| FORCE_OUT | — | Status move or after a landed hit (existing drag logic) |
| PIVOT | — | After a landed hit (existing mid-turn reserve prompt) |
| PROTECT | consecutive base (2 or 3) | Fails for the last mover; odds 1/base^n (n ≤ 3); the chain resets after any other move, a status block or a switch |

V5–V7 effect bytes are translated into the same lists when a battler is
loaded, so all request versions use one path. The legacy RNG order is
kept: the Milestone 1–2 suite passes unchanged.

### Order within one move

1. PP is spent and MOVE_USED is emitted (status blocks were checked before).
2. Targets come from the format rules (Triple distance, Rotation front).
3. PROTECT runs on the user alone and ends the move.
4. Per target: Protect check (unless the move ignores it) → accuracy →
   type immunity → hits → drain → remaining effects in list order (chance
   per target; effects on a fainted target are skipped, self effects still
   apply) → force-out / pivot. Status moves apply their list and emit
   NO_EFFECT if nothing changed.
5. Recoil from the HP all targets lost.
6. Faint resolution, outcome, replacement and pivot prompts stay in the
   turn loop; end-of-turn residuals follow the Milestone 2 order.

New events: HEAL, DRAIN, PROTECT, PROTECTED, FAIL, MULTI_HIT. The TV shows
them as messages; the HP bar now animates upward for healing.

### Exported moves

The exporter supports `EFFECT_HIT`, `EFFECT_STAT_CHANGE`,
`EFFECT_NON_VOLATILE_STATUS` (not powder moves), `EFFECT_RECOIL`,
`EFFECT_RESTORE_HP`, `EFFECT_PROTECT` (normal protection only),
`EFFECT_HIT_ESCAPE`, `EFFECT_HIT_SWITCH_TARGET`, `EFFECT_ROAR` and
`EFFECT_REFRESH`, plus these additional effects: stat changes (status-move
and secondary), the major statuses (freeze or frostbite per config) and
absorb. `multiHit` and `strikeCount` set the hit count. Exported and
exercised: Swords Dance, Agility, Screech, Growl, Thunder Wave,
Will-O-Wisp, Toxic, Hypnosis, Recover, Giga Drain, Thunderbolt,
Flamethrower, Ice Beam, Icy Wind, Heat Wave, Quick Attack, Protect, Fury
Swipes, Double Kick, Double-Edge, U-turn, Dragon Tail and Surf.

Still unsupported (shown as unusable on the GBA, with the effect ID in the
host log): flinch, confusion and other volatile effects, two-turn and
recharge moves, fixed or variable damage, OHKO, explosion, always-crit,
Protect variants (King's Shield etc.), weather, terrain, field and side
effects, item and ability interactions, powder moves.

### Verification

The native suite adds the V8 codec and its rejections, stat and status
moves with immunities, heal cap, drain and recoil amounts, 10% secondary
odds over 2,000 seeds, the 35/35/15/15 hit split over 4,000 seeds,
priority, Protect odds (1/3 on a second use over 900 seeds), last-mover
failure, ignores-Protect, Double spread with a protected foe, Triple
distance, Rotation stage retention, U-turn/Dragon Tail lists, unsupported
lists and seeded replay. Wire, message, battle and presentation suites
pass with warnings as errors; the battle suite passes ASan and UBSan.

Dolphin 2609b, release-2 pair, from the Cube Tester:

| Format | Observed |
| --- | --- |
| Single | Swords Dance +2, Screech −2, Hypnosis miss and then blocked by Protect, Recover capped at max HP, Giga Drain draining 1 of 3, Quick Attack first at +1 priority, WIN |
| Double | Protect blocking Icy Wind and Thunderbolt, Icy Wind super effective with Speed −1 on the other foe, U-turn reserve prompt, Fury Swipes 3 hits, Dragon Tail KO (no drag on a fainted target) and replacement, Toxic and its residual, WIN |
| Triple | Screech limited to reachable foes, Thunderbolt NO_EFFECT on Ground, Surf hitting allies and three foes, WIN in one turn |
| Rotation | Swords Dance then rotation keeping the stage, Double-Edge recoil 10 of 33, Hypnosis sleep blocking the foe, WIN |

Every battle returned to the field with unchanged digests, and the test
saves kept their SHA-256. Peak core time was 13–39 µs per turn with up to
14 events; the longest poll gap during playback was 33–50 ms, with one
534 ms gap in a V7 regression run (as recorded in Milestone 2). One link
loss happened during a stalling Single (430 SI errors, 7 retries); the
host reconnected and restarted the battle from its request as designed.

Regression: Phase 1 normal and fault injection (10,000 pings, 64 KiB CRC
4e5e3311, GX), the Phase 2 party viewer with a forced 3 s silence,
reconnect and 23/23 cache hits, Phase 3 apply/readback (PP 20 → 19 → 18),
V1 Triple and Rotation, V2 reserve, V4 Struggle, V5 drag, V6 pivot and the
V7 Double and Rotation condition fixtures all passed through the new
engine. The Frontier `make check` results are identical to `frontier-hack`
before the GC work (same 2,413 failing test names).

| Build | Release 1 | Milestone 3 |
| --- | ---: | ---: |
| GC DOL | 261,344 B | 269,952 B |
| GC ELF text / data / BSS | 209,518 / 51,560 / 2,741,696 B | 216,782 / 52,876 / 2,797,824 B |
| Frontier linked ROM | 26,322,912 B | 26,326,480 B |
| Frontier EWRAM | 231,984 B | 231,984 B |

### Next

Volatile states (flinch, confusion, substitute, two-turn and recharge
moves) use the same effect list with new kinds and per-battler volatile
storage. Abilities and held items then hook into the stages named above:
before accuracy, on damage, after a hit and at the end of the turn.

## Milestone 3.5 — volatile status, multi-turn actions and link diagnostics

Request/APP V9 (release 3, `m3.5-dev`) adds five effect kinds, five move
flags and a rules block, and gives every battler a volatile state with one
lifetime table. The GameCube still never asks the GBA during a turn.

### Volatile state

`BattlerVolatiles` (`battle_core.h`) holds what lives only while a
Pokemon is on the field, apart from HP/PP/major status (the Pokemon) and
stat stages: confusion counter, substitute HP, charging move slot and
stored target, semi-invulnerable state, recharge counter, flinch, "acted
this turn", and the Protect state that Milestone 3 kept on the battler.
All transitions go through `bc_volatiles_on`:

| | turn end | switch / drag / pivot | Triple Shift | Rotation | faint | new battle (reconnect) |
| --- | --- | --- | --- | --- | --- | --- |
| confusion | keep | clear | keep | keep | clear | clear |
| substitute | keep | clear | keep | keep | clear | clear |
| charge, semi-invulnerable | keep | clear | keep | front cannot rotate | clear | clear |
| recharge | ticks down | clear | keep | front cannot rotate | clear | clear |
| flinch | clear | clear | keep | keep | clear | clear |
| Protect this turn | (cleared at turn start) | clear | keep | keep | clear | clear |

Rotation follows Gen 5 behaviour, studied (not copied) from the Black/White
decompilation: a rotation does not run the switch-out reset, so wings keep
their volatiles (`ProcRotation` vs `ProcMemberOutCore` in the server flow),
and a Pokemon with a forced action skips action selection entirely
(`IsActionForced` / `StepSelectAction` in the client), so a charging or
recharging front can neither choose nor rotate. Behaviour not confirmed
there was left out rather than guessed. A battle restarted after a link
loss runs `battle_init` again, so no volatile survives a reconnect.

### Action states

`battle_action_state` reports READY, CHARGING, SEMI_INVULNERABLE,
RECHARGING, FAINTED or EMPTY for a position; within a turn every queued
action is PENDING, DONE, SKIPPED (the actor fainted first) or INTERRUPTED.
A locked position (charging, semi-invulnerable or recharging) is not
prompted: `battle_forced_action` supplies its action, `battle_run_turn`
ignores any command for it, and the AI path returns the same action. When
every player position is locked, the turn runs without a GBA prompt.
Forfeit still works while locked. Pivot resumption is unchanged.

Before each action, in order: recharge (the turn is spent), sleep/freeze,
flinch, confusion (counter down; at 0 it ends and the Pokemon acts; else a
self-hit by the rules' chance, which replaces the move and spends no PP),
paralysis. Any lost action resets the Protect chain and cancels a charging
move (`EV_CHARGE_END`, PP already spent stays spent).

### Effect handlers (V9)

| Kind | Behaviour |
| --- | --- |
| FLINCH | Secondary effect of a hit; only on a target that has not acted this turn and that was struck itself (not its substitute) |
| CONFUSE | Status move or secondary; counter = min..max from the rules; fails (NO_EFFECT) if already confused |
| SUBSTITUTE | Costs max HP / divisor (at least 1); fails if one exists or HP ≤ cost; status and stat moves aimed at the Pokemon then fail unless the move ignores substitutes; damage goes to the substitute first |
| CHARGE | Must lead the list, before DAMAGE. First turn: PP, MOVE_USED, CHARGE (with AIR/UNDERGROUND), target stored; second turn: forced, no PP, stored target (retargeted by the normal rules if it is gone) |
| RECHARGE | After a hit that landed: the next action is a forced recharge; not after a miss, a Protect block or the user's faint; after a KO only if the rules do not skip it |

Move flags: ignores substitute; hits / doubles against an airborne target;
hits / doubles against an underground target. A semi-invulnerable target
is missed by everything else (EV_MISS), except a sure hit (Toxic from a
Poison type under the always-hits rule). Drain and recoil count the HP the
substitute lost; a multi-hit move keeps hitting after the substitute breaks
when the rules say so; Dragon Tail does not drag a Pokemon whose substitute
took the hit, while U-turn still pivots; the confusion self-hit ignores the
user's own substitute.

### Wire format (V9)

The V8 effect lists are unchanged; the new kinds are 11–15 and the new
move flags are bits 1–5 (`BMF_V9_MASK`). An 8-byte `BattleVolatileRules`
block follows the effect lists: flags (multi-hit continues after a
substitute, recharge skipped after a KO, burn halves the confusion
self-hit), self-hit percent, confusion min/max, self-hit power, substitute
divisor, two reserved zero bytes. The decoder refuses out-of-range values
and nonzero reserved bytes. APP V9 state = the V7 condition state + one
`BVF_*` byte per field position (confused, substitute, charging,
semi-invulnerable, recharging, locked). V9 kinds and flags are refused in a
V8 request, and V8 requests encode and decode exactly as before.

### GBA exporter (Frontier)

| Game data | Descriptor |
| --- | --- |
| `MOVE_EFFECT_FLINCH` (Bite, Rock Slide, Air Slash, Twister …) | FLINCH with the move's chance |
| `EFFECT_CONFUSE` (Confuse Ray, Supersonic), `MOVE_EFFECT_CONFUSION` | CONFUSE |
| `EFFECT_SWAGGER` | STAT Attack +2, then CONFUSE |
| `EFFECT_SUBSTITUTE` | SUBSTITUTE |
| `EFFECT_SOLAR_BEAM`, `EFFECT_TWO_TURNS_ATTACK` | CHARGE (none) + DAMAGE |
| `EFFECT_SEMI_INVULNERABLE` with `STATE_ON_AIR` / `STATE_UNDERGROUND` (Fly, Bounce, Dig) | CHARGE (air / underground) + DAMAGE |
| `MOVE_EFFECT_RECHARGE` (Hyper Beam, Giga Impact) | RECHARGE |
| `EFFECT_EARTHQUAKE` | plain hit (its only extra is Grassy Terrain) |
| `ignoresSubstitute`, `damagesAirborne`, `damagesAirborneDoubleDamage`, `damagesUnderground` | move flags (underground hits double in this build) |

No battle script is passed; anything that cannot be expressed exactly stays
unsupported: underwater, Phantom Force and Sky Drop states, charge-turn
effects (Skull Bash), self-confusion (Thrash), and any list that would need
more than six descriptors (refused, never truncated). GC battles have no
weather, so Solar Beam always charges and is never weakened.

The rules come from this build's code and config, not fixed guesses:
confusion lasts 2..`B_CONFUSION_TURNS` (5), self-hit 33% with
`B_CONFUSION_SELF_DMG_CHANCE` ≥ Gen 7 (else 50%), 40 power, typeless, no
random factor, and halved by burn (the self-hit uses `MOVE_NONE`, which is
physical); Substitute costs max HP / 4 and fails at HP ≤ cost; a multi-hit
move only stops on a faint (`MoveEndMultihitMove`); the recharge is
skipped after a KO only when `B_SKIP_RECHARGE == GEN_1`.

The GBA shows `c` (confused), `s` (substitute), `!` (charging / out of
reach) and `r` (recharging) after each position's HP, and CNF/SUB for the
acting Pokemon. The GameCube shows SUB/CNF/CHG badges, hides a
semi-invulnerable sprite, and has messages for every new event.

### Link diagnostics

`gamecube/companion/diag.c` sees every request through one `exchange()`
and records: requests and failures, time since the last good exchange,
the longest gap between requests (idle time before a new HELLO excluded),
new SI errors, retries, peak core time per turn, peak presentation (draw)
time, event-log and animation-queue use (and whether either filled), the
session number and battle ID. The GBA's own JoyBus counters (resets,
overflows, CRC errors, duplicates) are read from its existing PING reply
at session start and once per turn, so the connector did not change.
Anomalies go into a 64-entry ring buffer; counters, peaks and the ring are
printed as `DIAG` lines on link loss, on a GC-side abort and at battle end.

### Verification

Native suite: V9 codec and rejections (reserved bytes, ranges, truncation,
structure), V8 compatibility, flinch order in Single and Double, confusion
duration distribution, end, 33% self-hit over 600 seeds with exact damage,
no PP on a self-hit and the burn rule, substitute cost/failure/blocking/
bypass, multi-hit with and without the continue rule, drain and recoil
through a substitute, Dragon Tail and U-turn, Solar Beam / Fly / Dig
(PP once, forced second turn, ignored command, interruption by flinch and
sleep, air/underground reach and doubling, sure-hit Toxic, retargeting
after the stored target fainted, a locked Triple position), recharge
(forced turn, no PP, miss, KO rule, Rotation front), lifetimes through
Shift, faint and a fresh init, forfeit while locked, and a 25-turn
volatile-heavy deterministic replay. Mutating eight key conditions in the
engine makes the suite fail. Clean with `-Wall -Wextra -Werror`, at -O2,
and under ASan + UBSan.

Dolphin 2609b, release-3 pair (Cube Tester V9 fixtures, equal levels):

| Format | Observed |
| --- | --- |
| Single | Substitute 13 of 54 HP, Bite into it, substitute broken exactly; Confuse Ray, self-hit 18 with no move; Hyper Beam |
| Double | Fly: Tackle and Supersonic missed it, sprite hidden, `L:54!` on the GBA; next turn not prompted (SEMI_INVULNERABLE) and Fly hit for 78; Giga Impact, then a forced RECHARGE turn without a prompt; Dig underground, Rock Slide missing it and hitting a substitute, Confuse Ray failing on the substitute, Dig with no effect on a Flying type, second Substitute failing; forfeit → CANCELLED |
| Triple | Hyper Beam, Dig and Solar Beam on turn 1; turn 2 had all three positions locked and ran with no prompt; a confused Pokemon passing its check and firing Solar Beam; WIN on turn 4 |
| Rotation | Charging front locked (no prompt, no rotation) while the foe rotated; the beam hit the new foe front (position target); a confused foe rotated to a wing and kept `c`; Dig locked the front and released for a KO |

Endurance (debug menu **GC Double V9 (endurance)**): 24 turns with
Protect chains, 7 confusions, 18 confusion checks, 15 substitute hits,
32 failed moves; then a forced 3 s host silence: one request failed after
retries, `LINK LOST` printed the DIAG dump, the GBA reconnected, the same
request was replayed from the asset cache and the battle restarted at turn
1 with clean volatiles; forfeit and return to the field. An 18-turn V9 and
a 21-turn V7 battle also completed. The GBA save kept its SHA-256 through
every run, and every session's start and end state digests matched.

Regression: Phase 1 normal and fault injection (10,000 pings, bulk up to
64 KiB, GX), Phase 2 party viewer with a forced silence and 42/42 cache
hits on reconnect, Phase 3 apply (PP 20 → 19 → 18), V2 reserve, V6
pivot, V7 conditions, V8 effects (debug menu **GC Double V8 (effects)**)
and the link check (both sides release 3). Frontier `make check`: 3,890
passed / 2,455 failed, the same 2,408 distinct failing tests as before
the GC work.

Observed but not fixed (cause not found, recovered by design): in most
Frontier sessions the GBA saw 1–3 CRC errors in incoming frames, answered
with NACKs, and the host retried (1–3 timeouts, peak gap about 0.55 s),
mostly during `ASSET_INFO` at session start and once in a while on a turn
poll. No request failed except during the intentional silence. The Phase 1
PoC link had none of these.

| Build | Milestone 3 | Milestone 3.5 |
| --- | ---: | ---: |
| GC DOL | 269,952 B | 286,368 B |
| GC ELF text / data / BSS | 216,782 / 52,876 / 2,797,824 B | 230,358 / 55,740 / 2,850,240 B |
| Frontier linked ROM | 26,326,480 B | 26,329,800 B |
| Frontier EWRAM | 231,984 B | 231,984 B |

### Next

Abilities and held items (Milestone 4) hook into the same stages: before
the action (e.g. Inner Focus, Own Tempo), before accuracy, on damage
(substitute-aware), after a hit and at the end of the turn.
