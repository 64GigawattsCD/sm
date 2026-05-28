# BlockBox Migration Plan

## Goal

Move Super Metroid room and level parsing into BlockBox without breaking the current runtime. The first milestone is a read-only parser that can load the same room header, room-state, decompressed level-data, BTS, background payload, and scroll metadata that `sm` currently derives at runtime.

Current submodule target: `BlockBox/` in this repo, sourced from `64GigawattsCD/BlocksBox` on branch `SuperMetroid`.

## Host project role

This `sm` repository is now also the Super Metroid host/test project for guiding BlocksBox development. The runtime can still read directly from the ROM while parity work is in progress, but the long-term target is that all replaceable/runtime-consumed assets and level data come from the BlocksBox dump instead of direct ROM reads. That includes room blocks, BTS/collision data, background layers, PLMs, doors, enemies, title/cinematic props, character frames, palettes, audio/cinematics, and any convenience objects we define for editing or rendering.

BlocksBox should remain the place where game-specific extraction happens. The host project should increasingly act as a consumer of normalized dumped assets, with direct ROM access kept as a fallback or verification path until the dump is complete enough to stand on its own.

## Current parsing seam in `sm`

- `src/ida_types.h`
  Defines the ROM-facing room structures, especially `RoomDefHeader` and `RoomDefRoomstate`.
- `src/sm_82.c`
  `LoadRoomHeader()` and `LoadStateHeader()` select the room/state metadata.
- `src/sm_82.c`
  The room hydration path clears `level_data`, calls `DecompressToMem(Load24(&room_compr_level_data_ptr), ...)`, then splits the decompressed blob into level blocks, BTS, custom background, scrolls, and room PLMs.
- `src/sm_80.c`
  `GetLevelOrBackgroundRoomBlockForStreaming()` plus the row/column upload helpers are the narrow read-only consumers that prove the parsed level format.
- `src/sm_84.c`
  `WriteLevelDataBlockTypeAndBts()` and related PLM helpers are the main in-place mutation layer over parsed room data.
- `src/sm_86.c`, `src/sm_91.c`, and `src/sm_94.c`
  Collision, x-ray, and projectile systems are high-volume consumers of `level_data` and `BTS`.

## Data model to lift into BlockBox

- `RoomHeader`
  Room id, area, map position, width/height in scrolls, scroller metadata, door list pointer.
- `RoomState`
  Tileset pointers, compressed level-data pointer, music metadata, FX/layer3 hooks, enemy population pointers, PLM header pointer, scroll definition pointer, and room setup hooks.
- `LevelBlob`
  The decompressed room payload as three logical regions:
  1. foreground level blocks
  2. BTS bytes
  3. custom background blocks
- `ScrollData`
  Either rectangular fill behavior for positive `rdf_scroll_ptr` or explicit table data for negative/scripted cases.
- `MutableRoomOverlay`
  A separate patch/mutation layer for PLM-driven edits so BlockBox can remain read-only by default.

## Recommended migration order

1. Copy the room structure definitions and decompression routine into BlockBox with no behavioral changes.
2. Build a read-only `SuperMetroidRoomReader` that resolves:
   - room header
   - selected room state
   - decompressed level blob
   - BTS slice
   - custom background slice
   - scroll table
3. Add golden tests against a small room set from `sm.smc`.
   - Include at least one ordinary room, one room with positive scroll fill behavior, and one room with explicit scroll table data.
4. Add a compatibility view in `sm` that can compare BlockBox parse results against the current in-engine loader before switching any runtime callers.
5. Migrate the read-only consumers first.
   - Streaming/tile fetch helpers from `src/sm_80.c`
   - X-ray lookups from `src/sm_91.c`
6. Add a mutation overlay API for PLM edits instead of porting direct raw-array writes first.
   - Start with `WriteLevelDataBlockTypeAndBts()` semantics from `src/sm_84.c`
7. Move collision/query helpers once the mutation overlay exists.
   - Samus collision in `src/sm_94.c`
   - Enemy/projectile collision in `src/sm_86.c`
8. Only after parity is stable, consider making BlockBox the authoritative parser for editor/export tooling and for optional runtime validation.

## Suggested BlockBox deliverables

- `SuperMetroidRom`
  Minimal ROM access wrapper with banked pointer helpers matching the room/state tables.
- `SuperMetroidDecompressor`
  Port of `DecompressToMem`.
- `SuperMetroidRoomReader`
  Produces a normalized parsed room object.
- `SuperMetroidRoomOverlay`
  Applies PLM-style edits without mutating the base parsed data.
- `tests/super_metroid_rooms/*`
  Golden fixtures for known room outputs.

## Integration notes for this repo

- Keep `sm` as the source of truth until BlockBox matches the current loader byte-for-byte.
- Favor a bridge layer over broad refactors at first.
  A thin adapter that converts BlockBox output into the existing `level_data`, `BTS`, and `scrolls` shapes will let us verify correctness before changing gameplay systems.
- Defer editor-only niceties until parity is proven.
  The risky part is faithfully reproducing room-state selection, decompression, and PLM mutation semantics.

## Immediate next steps on this branch

1. Stabilize the native `Level Editor` browser path in `sm`.
   The main menu entry exists, and the browser is intended to list imported Super Metroid tilesets with hover previews, but the fallback tileset/runtime-package path still needs hardening until launches from the built executable reliably enumerate previews without crashing.
2. Promote the runtime import index from bootstrap metadata to a first-class bridge API.
   `sm` already loads bootstrap/runtime manifests and can resolve room/state to exported metadata; the next step is making those paths authoritative and resilient enough that runtime callers can trust them outside the repo-root development environment.
3. Start moving read-only room/graphics consumers onto imported assets.
   The highest-value early target remains read-only consumers such as streaming/tile fetch helpers and x-ray/collision lookups, with the ROM loader still available for parity checks.
4. Preserve replacement-pack seams as the runtime bridge grows.
   Tileset-scoped art IDs, palette metadata, per-tileset art manifests, tileset preview sheets, and assembled Samus frame exports are now in place; future runtime/editor work should keep those identifiers stable so replacement packs can override graphics cleanly.

## Progress snapshot

- `BlockBox/` is now an active submodule sourced from `64GigawattsCD/BlocksBox`, on branch `SuperMetroid`.
- BlockBox can parse Super Metroid room headers, room states, decompressed level data, BTS, custom backgrounds, scroll metadata, palettes, and tilesets into normalized export data.
- Bootstrap export now emits:
  - `blocksbox-index.json`
  - per-room JSON level files
  - per-tileset palette JSON
  - raw indexed tile art payloads
  - runtime manifests/index TSVs for levels and tilesets
  - tileset preview BMP sheets
  - assembled Samus frame PNG exports plus metadata
- `sm` now has a bootstrap bridge layer that can locate a Super Metroid ROM, invoke BlockBox on first run, and load the exported runtime manifest/indexes.
- The native main menu now includes a `Level Editor` entry with an in-progress tileset browser UI intended to show imported tilesets on the left and preview imagery on the right.
