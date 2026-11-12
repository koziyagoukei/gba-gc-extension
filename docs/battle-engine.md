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

Struggle, drag/phazing and pivots remain pending. A player-selected pivot
needs a resumable turn boundary so the queued actions can continue after
the reserve choice. Stat stages, status, residual effects, abilities,
items, field effects and gimmicks follow the lifecycle work.
