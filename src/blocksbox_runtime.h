#ifndef ZELDA3_BLOCKSBOX_RUNTIME_H_
#define ZELDA3_BLOCKSBOX_RUNTIME_H_

#include <stdbool.h>
#include <stdint.h>

bool BlocksBoxRuntime_LoadFromBootstrapManifest(const char *manifest_path);
void BlocksBoxRuntime_Unload(void);
bool BlocksBoxRuntime_IsLoaded(void);
const char *BlocksBoxRuntime_GetGameId(void);
int BlocksBoxRuntime_GetLevelCount(void);
int BlocksBoxRuntime_GetTilesetCount(void);
const char *BlocksBoxRuntime_GetLevelIdByIndex(int index);
const char *BlocksBoxRuntime_GetLevelPreviewPathByIndex(int index);
const char *BlocksBoxRuntime_GetTilesetIdByIndex(int index);
const char *BlocksBoxRuntime_GetTilesetPreviewPathByIndex(int index);
int BlocksBoxRuntime_GetTilesetAreaIndexByIndex(int index);
int BlocksBoxRuntime_GetTilesetGraphicsSetByIndex(int index);
void BlocksBoxRuntime_OnRoomStateLoaded(uint16_t room_address, uint16_t state_address);
bool BlocksBoxRuntime_HasCurrentRoom(void);
const char *BlocksBoxRuntime_GetCurrentLevelId(void);
const char *BlocksBoxRuntime_GetCurrentLevelPath(void);
const char *BlocksBoxRuntime_GetCurrentTilesetId(void);
const char *BlocksBoxRuntime_GetCurrentPalettePath(void);
const char *BlocksBoxRuntime_GetCurrentArtManifestPath(void);

#endif  // ZELDA3_BLOCKSBOX_RUNTIME_H_
