#include "blocksbox_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct BlocksBoxRuntimeTilesetEntry {
  char *tileset_id;
  int area_index;
  int graphics_set;
  char *palette_id;
  char *palette_path;
  char *art_manifest_path;
  char *preview_image_path;
  int tile_count;
} BlocksBoxRuntimeTilesetEntry;

typedef struct BlocksBoxRuntimeLevelEntry {
  uint16_t room_address;
  uint16_t state_address;
  char *level_id;
  char *level_path;
  char *tileset_id;
  int area_index;
  int graphics_set;
  bool has_custom_background;
} BlocksBoxRuntimeLevelEntry;

typedef struct BlocksBoxRuntimeState {
  bool loaded;
  char *game_id;
  char *runtime_manifest_path;
  BlocksBoxRuntimeTilesetEntry *tilesets;
  int tilesets_count;
  int tilesets_capacity;
  BlocksBoxRuntimeLevelEntry *levels;
  int levels_count;
  int levels_capacity;
  const BlocksBoxRuntimeTilesetEntry *current_tileset;
  const BlocksBoxRuntimeLevelEntry *current_level;
} BlocksBoxRuntimeState;

static BlocksBoxRuntimeState g_blocksbox_runtime;

static void BlocksBoxRuntime_ClearCurrentSelection(void);
static void BlocksBoxRuntime_FreeTilesetEntry(BlocksBoxRuntimeTilesetEntry *entry);
static void BlocksBoxRuntime_FreeLevelEntry(BlocksBoxRuntimeLevelEntry *entry);
static bool BlocksBoxRuntime_ReadManifestValue(const char *manifest_path, const char *key, char **value_out);
static bool BlocksBoxRuntime_LoadTilesetsIndex(const char *path);
static bool BlocksBoxRuntime_LoadLevelsIndex(const char *path);
static bool BlocksBoxRuntime_EnsureTilesetCapacity(void);
static bool BlocksBoxRuntime_EnsureLevelCapacity(void);
static const BlocksBoxRuntimeTilesetEntry *BlocksBoxRuntime_FindTilesetById(const char *tileset_id);
static const BlocksBoxRuntimeLevelEntry *BlocksBoxRuntime_FindLevelByRoomState(uint16_t room_address, uint16_t state_address);
static void BlocksBoxRuntime_GetDirectoryName(const char *path, char *dst, size_t dst_size);
static char *BlocksBoxRuntime_ResolvePackagePath(const char *index_path, const char *value);
static int BlocksBoxRuntime_ParseIntField(const char *value);
static uint16_t BlocksBoxRuntime_ParseHex16Field(const char *value);
static bool BlocksBoxRuntime_ParseBoolField(const char *value);

bool BlocksBoxRuntime_LoadFromBootstrapManifest(const char *manifest_path) {
  char *runtime_manifest_path = NULL;
  char *game_id = NULL;
  char *levels_index_path = NULL;
  char *tilesets_index_path = NULL;

  BlocksBoxRuntime_Unload();
  if (!manifest_path || !manifest_path[0])
    return false;
  if (!BlocksBoxRuntime_ReadManifestValue(manifest_path, "runtime_manifest_path", &runtime_manifest_path))
    return false;
  if (!BlocksBoxRuntime_ReadManifestValue(runtime_manifest_path, "game_id", &game_id) ||
      !BlocksBoxRuntime_ReadManifestValue(runtime_manifest_path, "levels_index", &levels_index_path) ||
      !BlocksBoxRuntime_ReadManifestValue(runtime_manifest_path, "tilesets_index", &tilesets_index_path)) {
    free(runtime_manifest_path);
    free(game_id);
    free(levels_index_path);
    free(tilesets_index_path);
    return false;
  }
  if (!BlocksBoxRuntime_LoadTilesetsIndex(tilesets_index_path) ||
      !BlocksBoxRuntime_LoadLevelsIndex(levels_index_path)) {
    free(runtime_manifest_path);
    free(game_id);
    free(levels_index_path);
    free(tilesets_index_path);
    BlocksBoxRuntime_Unload();
    return false;
  }

  g_blocksbox_runtime.game_id = game_id;
  g_blocksbox_runtime.runtime_manifest_path = runtime_manifest_path;
  g_blocksbox_runtime.loaded = true;

  free(levels_index_path);
  free(tilesets_index_path);
  return true;
}

void BlocksBoxRuntime_Unload(void) {
  for (int i = 0; i < g_blocksbox_runtime.tilesets_count; i++)
    BlocksBoxRuntime_FreeTilesetEntry(&g_blocksbox_runtime.tilesets[i]);
  free(g_blocksbox_runtime.tilesets);
  g_blocksbox_runtime.tilesets = NULL;
  g_blocksbox_runtime.tilesets_count = 0;
  g_blocksbox_runtime.tilesets_capacity = 0;

  for (int i = 0; i < g_blocksbox_runtime.levels_count; i++)
    BlocksBoxRuntime_FreeLevelEntry(&g_blocksbox_runtime.levels[i]);
  free(g_blocksbox_runtime.levels);
  g_blocksbox_runtime.levels = NULL;
  g_blocksbox_runtime.levels_count = 0;
  g_blocksbox_runtime.levels_capacity = 0;

  free(g_blocksbox_runtime.runtime_manifest_path);
  g_blocksbox_runtime.runtime_manifest_path = NULL;
  free(g_blocksbox_runtime.game_id);
  g_blocksbox_runtime.game_id = NULL;
  g_blocksbox_runtime.loaded = false;
  BlocksBoxRuntime_ClearCurrentSelection();
}

bool BlocksBoxRuntime_IsLoaded(void) {
  return g_blocksbox_runtime.loaded;
}

const char *BlocksBoxRuntime_GetGameId(void) {
  return g_blocksbox_runtime.game_id;
}

int BlocksBoxRuntime_GetLevelCount(void) {
  return g_blocksbox_runtime.levels_count;
}

int BlocksBoxRuntime_GetTilesetCount(void) {
  return g_blocksbox_runtime.tilesets_count;
}

const char *BlocksBoxRuntime_GetTilesetIdByIndex(int index) {
  if (index < 0 || index >= g_blocksbox_runtime.tilesets_count)
    return NULL;
  return g_blocksbox_runtime.tilesets[index].tileset_id;
}

const char *BlocksBoxRuntime_GetTilesetPreviewPathByIndex(int index) {
  if (index < 0 || index >= g_blocksbox_runtime.tilesets_count)
    return NULL;
  return g_blocksbox_runtime.tilesets[index].preview_image_path;
}

int BlocksBoxRuntime_GetTilesetAreaIndexByIndex(int index) {
  if (index < 0 || index >= g_blocksbox_runtime.tilesets_count)
    return -1;
  return g_blocksbox_runtime.tilesets[index].area_index;
}

int BlocksBoxRuntime_GetTilesetGraphicsSetByIndex(int index) {
  if (index < 0 || index >= g_blocksbox_runtime.tilesets_count)
    return -1;
  return g_blocksbox_runtime.tilesets[index].graphics_set;
}

void BlocksBoxRuntime_OnRoomStateLoaded(uint16_t room_address, uint16_t state_address) {
  const BlocksBoxRuntimeLevelEntry *level = BlocksBoxRuntime_FindLevelByRoomState(room_address, state_address);
  BlocksBoxRuntime_ClearCurrentSelection();
  if (!level)
    return;
  g_blocksbox_runtime.current_level = level;
  g_blocksbox_runtime.current_tileset = BlocksBoxRuntime_FindTilesetById(level->tileset_id);
}

bool BlocksBoxRuntime_HasCurrentRoom(void) {
  return g_blocksbox_runtime.current_level != NULL;
}

const char *BlocksBoxRuntime_GetCurrentLevelId(void) {
  return g_blocksbox_runtime.current_level ? g_blocksbox_runtime.current_level->level_id : NULL;
}

const char *BlocksBoxRuntime_GetCurrentLevelPath(void) {
  return g_blocksbox_runtime.current_level ? g_blocksbox_runtime.current_level->level_path : NULL;
}

const char *BlocksBoxRuntime_GetCurrentTilesetId(void) {
  return g_blocksbox_runtime.current_tileset ? g_blocksbox_runtime.current_tileset->tileset_id : NULL;
}

const char *BlocksBoxRuntime_GetCurrentPalettePath(void) {
  return g_blocksbox_runtime.current_tileset ? g_blocksbox_runtime.current_tileset->palette_path : NULL;
}

const char *BlocksBoxRuntime_GetCurrentArtManifestPath(void) {
  return g_blocksbox_runtime.current_tileset ? g_blocksbox_runtime.current_tileset->art_manifest_path : NULL;
}

static void BlocksBoxRuntime_ClearCurrentSelection(void) {
  g_blocksbox_runtime.current_level = NULL;
  g_blocksbox_runtime.current_tileset = NULL;
}

static void BlocksBoxRuntime_FreeTilesetEntry(BlocksBoxRuntimeTilesetEntry *entry) {
  free(entry->tileset_id);
  free(entry->palette_id);
  free(entry->palette_path);
  free(entry->art_manifest_path);
  free(entry->preview_image_path);
  memset(entry, 0, sizeof(*entry));
}

static void BlocksBoxRuntime_FreeLevelEntry(BlocksBoxRuntimeLevelEntry *entry) {
  free(entry->level_id);
  free(entry->level_path);
  free(entry->tileset_id);
  memset(entry, 0, sizeof(*entry));
}

static bool BlocksBoxRuntime_ReadManifestValue(const char *manifest_path, const char *key, char **value_out) {
  size_t key_len;
  size_t file_size;
  char *text;
  char *cursor;
  char *line;

  *value_out = NULL;
  if (!manifest_path || !manifest_path[0] || !key || !key[0])
    return false;
  text = (char *)ReadWholeFile(manifest_path, &file_size);
  if (!text)
    return false;
  (void)file_size;
  key_len = strlen(key);
  cursor = text;
  while ((line = NextLineStripComments(&cursor)) != NULL) {
    if (line[0] == 0)
      continue;
    if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {
      *value_out = strdup(line + key_len + 1);
      free(text);
      return *value_out != NULL;
    }
  }
  free(text);
  return false;
}

static bool BlocksBoxRuntime_LoadTilesetsIndex(const char *path) {
  size_t file_size;
  char *text = (char *)ReadWholeFile(path, &file_size);
  char *cursor;
  char *line;

  if (!text)
    return false;
  (void)file_size;
  cursor = text;
  while ((line = NextLineStripComments(&cursor)) != NULL) {
    char *fields;
    BlocksBoxRuntimeTilesetEntry *entry;
    if (line[0] == 0)
      continue;
    fields = line;
    if (!BlocksBoxRuntime_EnsureTilesetCapacity()) {
      free(text);
      return false;
    }
    entry = &g_blocksbox_runtime.tilesets[g_blocksbox_runtime.tilesets_count];
    entry->tileset_id = strdup(NextDelim(&fields, '\t'));
    entry->area_index = BlocksBoxRuntime_ParseIntField(NextDelim(&fields, '\t'));
    entry->graphics_set = BlocksBoxRuntime_ParseIntField(NextDelim(&fields, '\t'));
    entry->palette_id = strdup(NextDelim(&fields, '\t'));
    entry->palette_path = BlocksBoxRuntime_ResolvePackagePath(path, NextDelim(&fields, '\t'));
    entry->art_manifest_path = BlocksBoxRuntime_ResolvePackagePath(path, NextDelim(&fields, '\t'));
    entry->preview_image_path = BlocksBoxRuntime_ResolvePackagePath(path, NextDelim(&fields, '\t'));
    entry->tile_count = BlocksBoxRuntime_ParseIntField(NextDelim(&fields, '\t'));
    if (!entry->tileset_id || !entry->palette_id || !entry->palette_path ||
        !entry->art_manifest_path || !entry->preview_image_path) {
      free(text);
      return false;
    }
    g_blocksbox_runtime.tilesets_count++;
  }
  free(text);
  return true;
}

static bool BlocksBoxRuntime_LoadLevelsIndex(const char *path) {
  size_t file_size;
  char *text = (char *)ReadWholeFile(path, &file_size);
  char *cursor;
  char *line;

  if (!text)
    return false;
  (void)file_size;
  cursor = text;
  while ((line = NextLineStripComments(&cursor)) != NULL) {
    char *fields;
    BlocksBoxRuntimeLevelEntry *entry;
    if (line[0] == 0)
      continue;
    fields = line;
    if (!BlocksBoxRuntime_EnsureLevelCapacity()) {
      free(text);
      return false;
    }
    entry = &g_blocksbox_runtime.levels[g_blocksbox_runtime.levels_count];
    entry->room_address = BlocksBoxRuntime_ParseHex16Field(NextDelim(&fields, '\t'));
    entry->state_address = BlocksBoxRuntime_ParseHex16Field(NextDelim(&fields, '\t'));
    entry->level_id = strdup(NextDelim(&fields, '\t'));
    entry->level_path = BlocksBoxRuntime_ResolvePackagePath(path, NextDelim(&fields, '\t'));
    entry->tileset_id = strdup(NextDelim(&fields, '\t'));
    entry->area_index = BlocksBoxRuntime_ParseIntField(NextDelim(&fields, '\t'));
    entry->graphics_set = BlocksBoxRuntime_ParseIntField(NextDelim(&fields, '\t'));
    entry->has_custom_background = BlocksBoxRuntime_ParseBoolField(NextDelim(&fields, '\t'));
    if (!entry->level_id || !entry->level_path || !entry->tileset_id) {
      free(text);
      return false;
    }
    g_blocksbox_runtime.levels_count++;
  }
  free(text);
  return true;
}

static void BlocksBoxRuntime_GetDirectoryName(const char *path, char *dst, size_t dst_size) {
  size_t len;
  if (!dst || dst_size == 0)
    return;
  dst[0] = 0;
  if (!path || !path[0])
    return;
  snprintf(dst, dst_size, "%s", path);
  len = strlen(dst);
  while (len > 0) {
    char c = dst[len - 1];
    if (c == '/' || c == '\\') {
      dst[len - 1] = 0;
      break;
    }
    len--;
  }
  if (len == 0)
    snprintf(dst, dst_size, ".");
}

static char *BlocksBoxRuntime_ResolvePackagePath(const char *index_path, const char *value) {
  char runtime_dir[1024];
  char output_root[1024];
  char candidate[1024];
  if (!value || !value[0])
    return strdup("");
  if ((strlen(value) >= 2 && value[1] == ':') || value[0] == '/' || value[0] == '\\')
    return strdup(value);
  BlocksBoxRuntime_GetDirectoryName(index_path, runtime_dir, sizeof(runtime_dir));
  BlocksBoxRuntime_GetDirectoryName(runtime_dir, output_root, sizeof(output_root));
#ifdef _WIN32
  snprintf(candidate, sizeof(candidate), "%s\\%s", output_root, value);
#else
  snprintf(candidate, sizeof(candidate), "%s/%s", output_root, value);
#endif
  return strdup(candidate);
}

static bool BlocksBoxRuntime_EnsureTilesetCapacity(void) {
  if (g_blocksbox_runtime.tilesets_count < g_blocksbox_runtime.tilesets_capacity)
    return true;
  int new_capacity = g_blocksbox_runtime.tilesets_capacity ? g_blocksbox_runtime.tilesets_capacity * 2 : 32;
  BlocksBoxRuntimeTilesetEntry *new_entries = (BlocksBoxRuntimeTilesetEntry *)realloc(
      g_blocksbox_runtime.tilesets, sizeof(BlocksBoxRuntimeTilesetEntry) * new_capacity);
  if (!new_entries)
    return false;
  memset(new_entries + g_blocksbox_runtime.tilesets_capacity, 0,
         sizeof(BlocksBoxRuntimeTilesetEntry) * (new_capacity - g_blocksbox_runtime.tilesets_capacity));
  g_blocksbox_runtime.tilesets = new_entries;
  g_blocksbox_runtime.tilesets_capacity = new_capacity;
  return true;
}

static bool BlocksBoxRuntime_EnsureLevelCapacity(void) {
  if (g_blocksbox_runtime.levels_count < g_blocksbox_runtime.levels_capacity)
    return true;
  int new_capacity = g_blocksbox_runtime.levels_capacity ? g_blocksbox_runtime.levels_capacity * 2 : 256;
  BlocksBoxRuntimeLevelEntry *new_entries = (BlocksBoxRuntimeLevelEntry *)realloc(
      g_blocksbox_runtime.levels, sizeof(BlocksBoxRuntimeLevelEntry) * new_capacity);
  if (!new_entries)
    return false;
  memset(new_entries + g_blocksbox_runtime.levels_capacity, 0,
         sizeof(BlocksBoxRuntimeLevelEntry) * (new_capacity - g_blocksbox_runtime.levels_capacity));
  g_blocksbox_runtime.levels = new_entries;
  g_blocksbox_runtime.levels_capacity = new_capacity;
  return true;
}

static const BlocksBoxRuntimeTilesetEntry *BlocksBoxRuntime_FindTilesetById(const char *tileset_id) {
  int i;
  if (!tileset_id)
    return NULL;
  for (i = 0; i < g_blocksbox_runtime.tilesets_count; i++) {
    if (strcmp(g_blocksbox_runtime.tilesets[i].tileset_id, tileset_id) == 0)
      return &g_blocksbox_runtime.tilesets[i];
  }
  return NULL;
}

static const BlocksBoxRuntimeLevelEntry *BlocksBoxRuntime_FindLevelByRoomState(uint16_t room_address, uint16_t state_address) {
  int i;
  for (i = 0; i < g_blocksbox_runtime.levels_count; i++) {
    const BlocksBoxRuntimeLevelEntry *entry = &g_blocksbox_runtime.levels[i];
    if (entry->room_address == room_address && entry->state_address == state_address)
      return entry;
  }
  return NULL;
}

static int BlocksBoxRuntime_ParseIntField(const char *value) {
  return value && value[0] ? (int)strtol(value, NULL, 10) : 0;
}

static uint16_t BlocksBoxRuntime_ParseHex16Field(const char *value) {
  return (uint16_t)(value && value[0] ? strtoul(value, NULL, 0) : 0);
}

static bool BlocksBoxRuntime_ParseBoolField(const char *value) {
  return value && (StringEqualsNoCase(value, "true") || strcmp(value, "1") == 0);
}
