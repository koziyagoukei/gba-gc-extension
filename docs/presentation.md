# Native GC battle presentation

This document records the Single presentation milestone. The current Double,
Triple and Rotation layouts and their formation events are described in
[multi-format battles](multiformat.md).

The companion owns all TV coordinates, UI textures and animation timing. The GBA owns the party and action input. The GC core owns HP, PP, validation, turn order, damage and outcomes. The core emits ordered events without GX types or coordinates.

| Module | Responsibility |
|---|---|
| `gamecube/battle/battle_core.*` | Existing SDK-free SINGLE mechanics and logical `Position {side, slot}` |
| `gamecube/companion/battle_view.c` | JoyBus, request/result, core scheduling and presentation lifecycle |
| `gamecube/presentation/layout.*` | Format registry and position → anchor lookup |
| `gamecube/presentation/animation.*` | SDK-free event playback and independently displayed HP |
| `gamecube/presentation/scene.*` | Field, battlers, HUD, position cursor, messages, effects and GX draw order |
| `gamecube/presentation/assets.*` | Original GC art uploaded at startup; lookup of ROM-specific sprites and labels in the existing cache |

## Layout and events

`BattleLayout` contains the native TV viewport, field and its center, command strip, message panel and an anchor table indexed by side and active slot. Each occupied slot has separate anchors for sprite feet and scale, platform, HUD, HP bar, nickname, HP value, status, party marker, cursor, attack origin and hit center. Layouts share the renderer's 640×480 logical canvas. In SINGLE, slot 0 means center. The player is lower left and the opponent upper right; the logical position and party slot remain distinct. Transparent rows at the bottom of the cached frame are measured once and removed when drawing the sprite at its feet anchor. The player's feet are at y=348 on the arena's near edge.

The scene opens with two local enter cues and a fade. During command selection it highlights the acting position and marks the opponent by `Position`. When the core resolves the turn, the timeline consumes its events in order. The existing JoyBus keepalive remains active during intro and playback.

| Core event | Presentation |
|---|---|
| `EV_MOVE_USED` | Nickname and move label, lunge from the user's origin toward the target's anchor |
| `EV_MISS`, `EV_NO_EFFECT` | Local message |
| `EV_DAMAGE` | Hit spark and blink, then drain to `hp_after`, with effectiveness text where applicable |
| `EV_FAINT` | Sink below feet anchor, hide battler/HUD, faint message |
| `EV_FORFEIT`, `EV_NO_MOVES` | Local cancellation or no-move message |
| `EV_END` | WIN/LOSE message from the outcome |

No SHIFT, ROTATE, switching, status effects or new battle formats are implemented. STATUS is a display-only marker for nonzero status supplied in the record. The queue stores visual progress separately from the authoritative state, so the HUD does not jump to the final HP before its event plays.

The V1 request lengths and offsets are unchanged. Former sprite-placement bytes 4–7 within each BattleMon record are reserved. The encoder writes zero; the decoder ignores nonzero bytes from old ROMs. The old C member names remain deprecated for source compatibility if the codec is vendored into an older producer. No TV placement or cursor pixels travel over the wire. Legacy UI asset IDs stay reserved for older host/client combinations.

## Original art and transferred art

Meadow Arena and Link Plaza share an original pixel-art hillside and a bright cream, mint, sky-blue and coral palette. GC-local textures include rounded panels, HP frames, a target pointer, status and party marks, battle platforms, wallpaper, link crest, and a hit spark. The party roster uses the same panel and selection palette. The generated textures can be replaced behind the `UiArt` IDs. The console font comes from libogc.

The battle still requests GBA front/back sprites (including the correct palette/variant) and game-font nickname and move labels, using the existing CRC-checked, optionally compressed asset transfer and build-keyed cache. The host does not fetch the old GBA battle background, textbox, healthbox, HP bar, digits or pre-rendered messages. In the apply-mode win check, the cold battle fetched 10 sprite/label keys totaling 7,040 B decoded / 3,403 B on the wire. Nothing in the graphics layer runs on the GBA.

## Regression

- Windows devkitPro build and native `wire-test`, `message-test`, `battle-test` and `presentation-test` passed. The last test moves an anchor and checks that the hit effect follows it; it also checks staged HP, the faint transition and final outcome. The codec test verifies acceptance of an old request containing placement bytes.
- Dolphin 2609b Integrated GBA, 160 µs. Phase 1 completed the raw exchange, reset/reconnect, 10,000 framed round trips, 64 KiB with CRC and zero retries, and the original GX sprite. The Phase 1 DOL and GBA outputs match the pre-change build byte-for-byte.
- Phase 2 party mode on the unchanged Frontier ROM received real records and assets, followed the cursor from slot 0 to 1, showed selection and completed BYE.
- Phase 3 on the unchanged Frontier ROM: apply-mode WIN (one turn, opponent 0/18 and move PP 20 → 19 in the accepted result), show-only LOSE vs Steven (three turns, player 0/301), and GBA B/CANCELLED (zero turns, unchanged HP/PP). Each completed RESULT and BYE. Field return was observed after the display-only win. The GC confirmed in-session party, bag, flags, vars and SaveBlock1 digests; apply policy runs on the GBA after BYE. The copied save files and source save have identical SHA-256 values after the tests. The follow-up input did not reach a second battle in the apply-mode session, so the updated PP in the next request was not verified. No physical hardware was tested.
- TV visual checks used Direct3D 11. Link Plaza, the Party Roster and the Single field were checked for the brighter palette and readable labels. The Party Roster was checked for its selection frame, HP and PP, and the Single field for both sprites, the cursor and near-edge back sprite. Tests used GBA button pulses in a private Dolphin profile.

## Footprint and future layouts

The companion DOL grew from 218,048 B to 223,008 B (+4,960 B). ELF text/data/BSS grew by 3,096 / 1,876 / 35,424 B (+40,396 B total); the largest addition is the generated 160×96 RGB5A3 backdrop (30,720 B). The cache arena budget remains 2 MiB and the GX framebuffer count is unchanged. No GBA rebuild or new GBA RAM/ROM allocation is required for this presentation.

Double needs a second anchor per side and shared message/HUD placement that leaves room for four battlers. Triple needs three anchors per side, distinct HUD and target positions, and a view that keeps center/edge reach readable. Add each table to the layout registry after its logical slot order is specified by the core and protocol. Rotation can use the same six position anchors plus a separate foreground/wing placement and a presentation event for the rotation. Adding a table alone does not enable a new battle format.
