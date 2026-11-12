# Multi-format battle PoC

Frontier owns the test teams and input. GameCube owns the temporary battle,
ordering, HP/PP, faint, result and TV layout. The generic JoyBus connector
provides the mode, APP, asset and result channels. Multi battles are
display-only. The original Single result policy and duplicate-result checks
remain in use.

## Formation and commands

| Format | Field per side | Turn input | Legal foe targets |
|---|---|---|---|
| Single | 1 center | One move | Center |
| Double | 2, left/right | One move per live battler | Either live foe |
| Triple | 3, left/center/right | One move or Shift per live battler | Left → L/C, center → L/C/R, right → C/R |
| Rotation | 3, left wing/front/right wing | Keep or rotate, plus move, once per side | The opposing front after rotations |

The compact Double slot IDs are 0 for left and 1 for right. Triple and
Rotation use 0/1/2 for left/center/right. Side slots index stable battlers. Party slot IDs belong to battlers and remain
stable during shifts and rotations. Triple adjacency also applies within a
side, excluding the user. An explicit distance flag permits a far target; the
Frontier fixture uses Wing Attack. Shift exchanges an edge battler and its
center. The displaced battler keeps its action. Its new location controls
the target range at execution. Rotation permutes the three occupants before
move resolution. The newly rotated battler supplies priority and speed. Its
HP, PP, status and stat-stage storage travel with the individual.

Moves are ordered by signed priority, speed and a seeded RNG tie-break.
Formation changes resolve before damaging moves. An action from a fainted
battler is skipped. If a selected position is empty, fainted, or beyond
the new range after Shift, the PoC chooses the first live legal foe in field
order. A side loses when all its entrants faint. A fainted Rotation front
can be replaced on the next turn by rotating a live wing into the front.
Normal replacement from the bench is outside this test.

Target classes are one foe, all adjacent foes, all adjacent including allies,
and self. The last three resolve as a set. Surf exercises multiple targets and
the adjacent ally. The core consumes one PP per action, makes independent
accuracy checks and applies the PoC 0.75 damage modifier if there is more than
one target. Self-target status moves are unsupported. The Frontier exporter
only allows ordinary single-hit damaging moves and still marks nonzero-priority
game moves unsupported. AI chooses among legal moves and targets; it chooses
from available Rotation candidates and can Shift when it has no valid move.

## Wire and presentation

Request and Result V1 keep their existing layouts. Requests use the reserved
format and active count fields, with exactly 2 or 3 entries per side. Bit 7 of
the move target byte declares the distance override. The GC echoes party slot
IDs in original request order, regardless of field swaps. Single uses the
original 16-byte STATE and 4-byte ACTION. Multi uses APP opcodes 3/4 with a
61-byte state and 8-byte action. State carries field slot mappings, HP,
max HP, player PP, legal target masks and a 16-bit command token. The action
echoes the token and acting position with a move slot, target and rotation.
GC rejects stale tokens, illegal target/rotation choices and wrong actor IDs
before collecting the turn. The result ID still guards duplicate results.

GBA presents the acting position and move list, D-Pad target selection and
legal foe slots. SELECT issues Shift from the Triple edge command screen. In
Rotation, use L/R on the command screen to choose the wing, then A to open
moves and A to confirm the combined command. The front is marked on the TV.
Positions map to GC-only layout anchors. The visual actors follow Shift and
Rotation events before damage playback and keep the correct HP animation.
Sprites and labels come from the GBA asset cache. Field and UI textures use
the GC's local art.

## Test teams and boundaries

The level 23 player fixture is Taillow, Marshtomp, and Grovyle. The level 13
opponent fixture is Makuhita, Geodude, and Numel. Double uses the first two
from each side. The Double strong test raises the foes to level 45. The fixture uses mode-owned Pokémon with fixed IVs, generated
inside the Frontier ROM without modifying its party or save.

Supported mechanics are move PP, accuracy, type and STAB, basic damage and
spread, faint and outcome. Deferred mechanics include bench replacement,
Struggle, critical hits, stat-stage effects, status behavior, move side
effects, abilities, items, weather, terrain, gimmicks, growth and rewards.
Future priorities are bench replacement and forced actions, then stat stages
and status, then effects, abilities and items. Multi HP/PP writeback can be
added after the test rules have been exercised.

BW reference points for the position model are
[BTL_SERVER_FLOW_ProcShift / ShiftPokes](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L2761),
[ProcRotation](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L2057),
and [BuildActionOrder](https://github.com/Project-Holon/aletheia/blob/main/src/game/ov093/btl_server_flow.c#L2142).
The PoC uses independent implementation and simplified retarget, AI and
damage rules.


## Validation

Dolphin 2609b used the integrated GBA, standard JoyBus at 160 µs and
Direct3D 11. Inputs used the normal GBA/GC buttons in private test profiles.

| Check | Result |
|---|---|
| Double | Four moves executed on turn 1. Surf applied damage to an ally and an opponent in the winning run. WIN in 2 turns, LOSE in 2 against the strong team. GBA accepted the results. Field return was observed. |
| Triple | Three commands per side, left edge Shift, positions and targeting updated before moves, WIN in 3 turns. The TV displayed six ROM-supplied sprites and separate HUDs. GBA accepted the result and field return was observed. |
| Rotation | GBA submitted both left + move and right + move. The core applied the position changes before attacking the current front. WIN in 5 turns, followed by accepted result and BYE. |
| Phase 1 | Raw reset/reconnect, 10,000 pings with injected corruption and dropped reply (2 retries, 0 loss), 64 KiB with CRC 4e5e3311, 5.781 KiB/s, GX sprite. |
| Phase 2 | Real party/sprites, cursor selection, forced disconnect, automatic reconnect, 42/42 cached assets, BYE with unchanged digests. |
| Phase 3 | Single GBA move input, WIN/LOSE/cancel, display-only and HP/PP policies. After applying a win, a second battle exported 19/20 PP instead of 20/20. After applying a loss to Steven, the party viewer exported 0/301 HP and 17/20 PP. |
| Native | Wire, message, battle and presentation tests passed with warnings as errors. Includes four/six actions, priority and ties, faint skip, spread, both Shift directions, distance rules, both rotations, retained HP/status/stages, post-rotation speed and rejection of fainted candidates. |

The multi battles completed with matching party, bag, flags, vars and
SaveBlock1 digests at the start and end of the link.
The test runs used private save copies. No hardware run was performed.

The GC DOL is 237,376 B (+14,368 B); ELF text/data/BSS increased by
10,256 / 4,136 / 6,976 B. Frontier ELF text/data increased by 3,615 /
124 B. Static BSS is unchanged and the padded GBA image remains 32 MiB.
The final link uses 231,928 B EWRAM and 28,532 B IWRAM. The mode's
heap allocation for its own state is 2,192 B while it is open.
