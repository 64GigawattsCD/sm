# BlockBox Migration Plan

## Goal

Move Super Metroid room and level parsing into BlockBox without breaking the current runtime. The first milestone is a read-only parser that can load the same room header, room-state, decompressed level-data, BTS, background payload, and scroll metadata that `sm` currently derives at runtime.

Current submodule target: `BlockBox/` in this repo, sourced from `64GigawattsCD/BlocksBox` on branch `SuperMetroid`.

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

1. Add the BlockBox submodule once the exact repository URL is confirmed.
2. Create the `SuperMetroid` branch inside the submodule.
3. Implement read-only room/header/state parsing plus decompression in BlockBox.
4. Add a small comparison harness in `sm` that dumps or asserts parsed room slices for a curated test room list.
