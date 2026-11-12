# Phase 4: GBA-style battle presentation

The GameCube battle screen now uses the GBA game's own battle graphics and text. The scene is laid out in GBA screen coordinates (240×160) and scaled to the TV (×8/3, letterboxed).

## What the GBA sends

The battle mode (frontier-hack, `frontier_gc_battle_gfx.c`) serves these assets through `GcModeHandler.resolveAsset`:

| Asset | Type | Content |
|---|---|---|
| Terrain background | `ASSET_BATTLE_BG` 0x0201 (id = environment) | the battle environment BG, 30×20 tilemap, opaque |
| Textbox | `ASSET_BATTLE_TEXTBOX` 0x0202 | the message box frame, screen rows 14–19 |
| Healthbox | `ASSET_BATTLE_HEALTHBOX` 0x0203 (id = side, variant = level, extra = personality) | the singles healthbox frame with name, gender and level printed by the game's healthbox code |
| HP bar | `ASSET_BATTLE_HPBAR` 0x0204 | the H/P label and the green/yellow/red bar tiles (0–8 px fill) |
| Digits | `ASSET_BATTLE_DIGITS` 0x0205 | healthbox digits 0–9 and `/` (FONT_SMALL glyphs) |
| Messages | `ASSET_BATTLE_MESSAGE` 0x0206 (id = `BMSG_KEY(msg, side)`) | 26×4-tile message window rendered from the game's battle string templates |
| Pokémon | `ASSET_PKMN_FRONT` 0x0101, variant `PKMN_SPRITE_BACK` for the back picture | front picture for the opponent, back picture for the player |

The generic connector (pokeemerald-expansion `gc-extension`) only adds the mechanism: `resolveAsset`, `GcExtension_RenderTextAsset`, back sprites and RLE. No battle content is in it.

## Protocol additions (message layer, backward compatible)

- `CAP_RLE` (32). When the host advertises it, the client may RLE-encode asset data longer than 64 bytes.
- `AssetInfo` grows to 40 bytes: byte 27 = encoding, bytes 36–39 = decoded length. The 36-byte form is still decoded.
- `PIXEL_4BPP_TILEMAP` (2): 8-byte header (map w/h, tile count, palette banks, flags), u16 GBA map entries (tile, h/v flip, bank), 4bpp tiles, palette banks.
- `BattleRequestV1` byte 20 = environment; `BattleMonV1` bytes 4–6 = front Y offset, back Y offset, elevation.

## Layout (from the game source)

- Pokémon: opponent centre (176, 40 + front_y − elevation), player centre (72, min(80 + back_y + 8, 104)), 64×64.
- Healthboxes: top-left (12, 14) opponent, (126, 72) player. The healthbox sprites are created 64×32 and the player's are switched to 64×64 without recomputing the corner vector, so both tops are centre y − 16.
- HP bar: healthbox + (24, 16) opponent, + (32, 16) player; tiles [H][P][6 bar tiles], colour by > 50% / > 20%.
- Player HP text right-aligned at healthbox x + 96, y + 21.
- Message window at (16, 120).

## Animations

The host runs a step timeline per intro and per turn. These are the GBA's generic battle effects, not per-move animation scripts:

- fade in, trainer intro message, opponent send-out (sprite grows from its base, healthbox slides in), "Go!" and player send-out;
- attacker nudge toward the target;
- hit blink with a small shake;
- HP bar drain at about one bar pixel per frame;
- faint: the sprite sinks below its base and the healthbox is hidden;
- messages from the game text for move use, miss, effectiveness, no effect, faint, win, lose and forfeit.

Messages needed by a turn are fetched after the GBA chooses its move (about 0.7 s for a new turn); the intro assets are fetched up front (20 assets, 33.8 KB raw, 12.3 KB on the wire with RLE, about 5.2 s).

## Verified (Dolphin 2609b)

- Frontier debug battle: intro, move, faint, win message, RESULT and BYE; state digests unchanged within the visit; apply-mode PP 19 → 18 once.
- Unit tests: `message-test` (RLE, 40/36-byte AssetInfo, tilemap header), `battle-test` (environment and offsets round trip).
- Phase 2 party viewer on the master ROM with the new host; save hash unchanged.
- Phase 1 sources (host, session, wire, client) are unchanged in this phase.

## Not done

- Per-move animation scripts, trainer pictures and ball throw are not reproduced.
- The player's EXP bar is drawn empty (the frame's own graphic).
