#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <SDL.h>
#ifdef _WIN32
#include "platform/win32/blocksbox_bootstrap.h"
#include "platform/win32/volume_control.h"
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "snes/ppu.h"

#include "types.h"
#include "sm_rtl.h"
#include "sm_cpu_infra.h"
#include "ida_types.h"
#include "variables.h"
#include "variables_extra.h"
#include "funcs.h"
#include "config.h"
#include "util.h"
#include "spc_player.h"
#include "enemy_types.h"
#include "blocksbox_runtime.h"

#ifdef __SWITCH__
#include "switch_impl.h"
#endif

static void playAudio(Snes *snes, SDL_AudioDeviceID device, int16_t *audioBuffer);
static void renderScreen(Snes *snes, SDL_Renderer *renderer, SDL_Texture *texture);
static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len);
static void SwitchDirectory();
static bool EnsureBlocksBoxImport(const char *preferred_rom_path, char *resolved_rom_path, size_t resolved_rom_path_size);
static bool ResolveBlocksBoxPath(const char *workspace_relative_path, const char *exe_relative_path, char *dst, size_t dst_size);
static void RenderNumber(uint8 *dst, size_t pitch, int n, uint8 big);
static void OpenOneGamepad(int i);
static int GetCurrentVolumePercent(void);
static void SetCurrentVolumePercent(int new_volume);
static void HandleVolumeAdjustment(int volume_adjustment);
static void HandleAspectRatioSelection(int direction);
static void ApplyRuntimeAspectRatio(void);
static void SaveNativeOptionsConfig(void);
static void SetModernLayerRendererEnabled(bool enabled);
static void ToggleNativeLevelRenderMode(void);
static void HandleGamepadAxisInput(int gamepad_id, int axis, int value);
static int RemapSdlButton(int button);
static void HandleGamepadInput(int button, bool pressed);
static void HandleInput(int keyCode, int keyMod, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
static uint16 GetInputBitForControlCommand(uint32 j);
static bool IsDirectionalControlCommand(uint16 cmd);
static bool IsGameplayMovementState(void);
static uint16 GetNativeMenuInputs(void);
static void UpdateOpeningIntroSkipState(uint16 inputs);
static void MaybeActivateNativeMainMenu(void);
static void OpenNativeMainMenuScene(void);
void StartNativePlayFromMainMenu(void);
static void OpenNativeOptionsFromMainMenu(uint16 held_inputs);
static void OpenNativeFileSelect(uint16 held_inputs);
static void CloseNativeFileSelect(void);
static void OpenNativeLevelEditor(uint16 held_inputs);
static void CloseNativeLevelEditor(void);
static void UnloadNativeLevelEditorPreview(void);
static bool LoadNativeLevelEditorPreviewFromPath(const char *preview_path, int loaded_key);
static bool LoadNativeLevelEditorPreview(int tileset_index);
static bool LoadNativeLevelEditorLevelPreview(int level_index);
static void UnloadNativeLevelEditorTilesets(void);
static bool LoadNativeLevelEditorTilesetsFromManifest(const char *bootstrap_manifest_path);
static bool EnsureNativeLevelEditorTilesetsLoaded(void);
static int GetNativeLevelEditorTilesetCount(void);
static int GetNativeLevelEditorTilesetAreaIndex(int index);
static int GetNativeLevelEditorTilesetGraphicsSet(int index);
static const char *GetNativeLevelEditorTilesetPreviewPath(int index);
static void GetDirectoryNameFromPath(const char *path, char *dst, size_t dst_size);
static void JoinPath2(char *dst, size_t dst_size, const char *a, const char *b);
static char *ResolveIndexRelativePath(const char *index_path, const char *value);
static const char *NextTsvFieldOrEmpty(char **fields);
static void DrawScaledPreviewImage(uint8 *pixel_buffer, size_t pitch, int width, int height,
                                   int dst_x, int dst_y, int dst_w, int dst_h,
                                   const uint32 *src_pixels, int src_w, int src_h);
static void UpdateNativeMainMenu(uint16 inputs);
static uint16 MaskNativeMainMenuInputs(uint16 inputs);
static void UpdateNativeOptionsOverlay(uint16 inputs);
static void UpdateNativeFileSelect(uint16 inputs);
static void UpdateNativeLevelEditor(uint16 inputs);
static uint16 MaskNativeLevelEditorInputs(uint16 inputs);
static uint16 GetMenuAnalogDirectionalInputs(void);
static void RenderNativeMainMenu(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void RenderNativeFileSelect(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void RenderNativeLevelEditor(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void RenderNativeOptionsOverlay(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void DrawNativeOptionsSlider(uint8 *pixel_buffer, size_t pitch, int width, int height,
                                    int x, int y, int w, int scale, int value, bool selected, bool enabled);
static void RenderBuildTimestampOverlay(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void ShowExitToMainMenuPrompt(void);
static void UpdateExitToMainMenuPrompt(uint16 inputs);
static uint16 MaskExitToMainMenuPromptInputs(uint16 inputs);
static void RenderExitToMainMenuPrompt(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void RenderOpeningIntroSkipPrompt(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void RenderAnalogDebugOverlay(uint8 *pixel_buffer, size_t pitch, int width, int height);
static bool NativeMainMenuCanOpenOnTitleScene(void);
static void ClearTransientMenuInputs(void);
static void RenderModernFrontCustomLayer(uint32 *pixels, int width, int height);
static void RenderModernGunshipCustomLayerLine(int custom_slot, int y, uint32 *pixels, int width, int height);
static void CompositeModernFrontCustomLayer(uint8 *pixel_buffer, size_t pitch, int width, int height);
static bool TryStartTitleVideoPlayback(void);
static void StopTitleVideoPlayback(bool open_title_scene);
static bool UpdateTitleVideoPlayback(uint16 inputs);
static void RenderTitleVideoFrame(void);
static void SeekTitleVideoPlayback(int frame_index);
static bool LoadTitleVideoAudio(const char *audio_path);
static void MixTitleVideoAudio(Uint8 *stream, int len);
static bool LoadTitleVideoOverlays(void);
static void RenderTitleVideoOverlays(void);
static void DrawTitleVideoSubtitleText(const char *text);
static void DrawTitleVideoOverlayImage(const struct TitleVideoOverlayImage *image, int dst_x, int dst_y);
static void FreeTitleVideoOverlayImage(struct TitleVideoOverlayImage *image);
static void DrawText5x7(uint8 *pixel_buffer, size_t pitch, int width, int height, int x, int y, const char *text, int scale, uint32 color);
static void SaveDebugScreenshot(uint8 *pixel_buffer, size_t pitch, int width, int height, const char *prefix, int *counter);
static void CaptureCinematicFrame(uint8 *pixel_buffer, size_t pitch, int width, int height, int render_scale);
static void CaptureCinematicAudioForFrame(void);
static void FinalizeCinematicAudioCapture(void);
static void WidescreenDebugLog(const char *fmt, ...);
void OpenGLRenderer_Create(struct RendererFuncs *funcs);

typedef struct NativeLevelEditorPreview {
  uint32 *pixels;
  int width;
  int height;
  int loaded_index;
} NativeLevelEditorPreview;

typedef struct {
  int area_number;
  int graphics_number;
  char *preview_file_path;
} NativeLevelEditorTilesetEntry;

typedef enum TitleVideoSubtitleKind {
  kTitleVideoSubtitleKind_Text,
  kTitleVideoSubtitleKind_RedText,
  kTitleVideoSubtitleKind_NintendoLogo,
} TitleVideoSubtitleKind;

typedef struct TitleVideoSubtitleCue {
  int start_frame;
  int end_frame;
  TitleVideoSubtitleKind kind;
  char text[32];
} TitleVideoSubtitleCue;

typedef struct TitleVideoOverlayImage {
  uint32 *pixels;
  int width;
  int height;
} TitleVideoOverlayImage;

typedef struct TitleVideoPlayback {
  bool active;
  FILE *frames_file;
  uint8 *rgb_frame;
  uint32 *argb_frame;
  uint8 *audio;
  size_t audio_size;
  size_t audio_offset;
  int width;
  int height;
  int frame_count;
  int frame_index;
  size_t frame_bytes;
  SDL_Texture *texture;
  bool changed_logical_size;
  bool show_menu;
  TitleVideoOverlayImage title_logo;
  TitleVideoOverlayImage nintendo_logo;
  TitleVideoSubtitleCue *subtitle_cues;
  int subtitle_cue_count;
} TitleVideoPlayback;

typedef enum NativeLevelEditorPage {
  kNativeLevelEditorPage_PackList,
  kNativeLevelEditorPage_PackHome,
  kNativeLevelEditorPage_LevelList,
  kNativeLevelEditorPage_TileSets,
} NativeLevelEditorPage;

bool g_debug_flag;
bool g_is_turbo;
bool g_is_turbo;
bool g_want_dump_memmap_flags;
bool g_new_ppu;
bool g_new_ppu = true;
bool g_other_image;
bool g_cinematic_capture_active;
struct SpcPlayer *g_spc_player;
static uint32_t button_state;

static uint8_t g_pixels[kPpuXPixels * 4 * 240];
static uint8_t g_my_pixels[kPpuXPixels * 4 * 240];
static uint32 *g_modern_front_custom_layer;
static size_t g_modern_front_custom_layer_size;
static bool g_shinespark_screenshot_requested;
static bool g_manual_screenshot_requested;
static bool g_room_load_screenshot_requested;
static bool g_main_menu_screenshot_requested;
static bool g_toggle_screenshot_requested;
static bool g_native_level_render_saved_modern_layer_renderer;
static int g_shinespark_screenshot_counter;
static int g_manual_screenshot_counter;
static int g_room_load_screenshot_counter;
static int g_main_menu_screenshot_counter;
static int g_toggle_screenshot_counter;
static int g_room_load_screenshot_timer;
static int g_main_menu_screenshot_timer;
static int g_toggle_screenshot_timer;
static char g_toggle_screenshot_prefix[64];
static int g_startup_screenshot_counter;
static int g_startup_screenshot_remaining = 24;
static int g_startup_screenshot_timer = 1;
static const char *g_cinematic_capture_path;
static FILE *g_cinematic_capture_file;
static const char *g_cinematic_audio_capture_path;
static FILE *g_cinematic_audio_capture_file;
static uint32_t g_cinematic_audio_capture_bytes;
static int64_t g_cinematic_audio_capture_sample_accum;
static int g_cinematic_capture_frame_limit;
static int g_cinematic_capture_frames_written;
static bool g_cinematic_capture_quit_when_done;
static bool g_cinematic_capture_done;
static bool g_native_main_menu_active;
static bool g_native_main_menu_dismissed;
static bool g_native_main_menu_return_from_options;
static bool g_native_main_menu_scene_pending;
static bool g_native_options_active;
static bool g_native_file_select_active;
static int g_native_main_menu_selection;
static uint16 g_native_main_menu_prev_inputs;
static bool g_native_main_menu_quit_requested;
static int g_native_options_selection;
static uint16 g_native_options_prev_inputs;
static int g_native_file_select_selection;
static uint16 g_native_file_select_prev_inputs;
static int g_native_file_select_status_slot;
static int g_native_file_select_status_timer;
static bool g_native_level_editor_active;
static NativeLevelEditorPage g_native_level_editor_page;
static int g_native_level_editor_pack_selection;
static int g_native_level_editor_pack_menu_selection;
static int g_native_level_editor_level_selection;
static int g_native_level_editor_level_scroll;
static int g_native_level_editor_selection;
static int g_native_level_editor_scroll;
static uint16 g_native_level_editor_prev_inputs;
static NativeLevelEditorPreview g_native_level_editor_preview = { NULL, 0, 0, -2 };
static NativeLevelEditorTilesetEntry *g_native_level_editor_tilesets;
static int g_native_level_editor_tilesets_count;
static bool g_exit_to_main_menu_prompt_active;
static bool g_exit_to_main_menu_prompt_selection_yes;
static uint16 g_exit_to_main_menu_prompt_prev_inputs;
static TitleVideoPlayback g_title_video;

int g_got_mismatch_count;


enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
  kCinematicCaptureAudioFreq = 44100,
  kCinematicCaptureAudioChannels = 2,
  kSnesNativeWidth = 256,
  kSnesNativeHeight = 240,
  kTitleVideoWidth = 398,
  kTitleVideoHeight = 224,
  kTitleVideoLoopFrame = 1893,
  kPathBufferSize = 1024,
};

static const char kWindowTitle[] = "SuperMet";
static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
static SDL_Window *g_window;

static uint8 g_paused, g_turbo, g_replay_turbo = true, g_cursor = true;
static uint8 g_current_window_scale;
static uint8 g_gamepad_analog_buttons;
static uint8 g_gamepad_dpad_buttons;
static int g_gamepad_button_inputs;
static int g_input1_state;
uint8 g_dedicated_missile_fire_pressed;
uint8 g_dedicated_missile_toggle_pressed;
uint8 g_dedicated_beam_fire_held;
uint8 g_dedicated_grapple_fire_pressed;
uint8 g_dedicated_grapple_fire_held;
static bool g_display_perf;
static int g_curr_fps;
static int g_ppu_render_flags = 0;
static int g_snes_width, g_snes_height;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static struct RendererFuncs g_renderer_funcs;
static uint32 g_gamepad_modifiers;
static uint16 g_gamepad_last_cmd[kGamepadBtn_Count];
static uint8 g_intro_skip_hold_frames;
extern Snes *g_snes;

enum {
  kOpeningIntroSkipHoldFrames = 60,
  // main.c feeds RtlRunFrame a packed 12-button frontend bitfield.
  // The SNES Start mask is produced later after SwapInputBits(), so use the
  // frontend bit here instead of kButton_Start.
  kInputBit_B = 1 << 0,
  kInputBit_Y = 1 << 1,
  kInputBit_Select = 1 << 2,
  kInputBit_Start = 1 << 3,
  kInputBit_Up = 1 << 4,
  kInputBit_Down = 1 << 5,
  kInputBit_Left = 1 << 6,
  kInputBit_Right = 1 << 7,
  kInputBit_A = 1 << 8,
  kInputBit_X = 1 << 9,
  kInputBit_PageUp = 1 << 10,
  kInputBit_PageDown = 1 << 11,
};

static const int kNativeSpriteSizes[8][2] = {
  {8, 16}, {8, 32}, {8, 64}, {16, 32},
  {16, 64}, {32, 64}, {16, 32}, {16, 32}
};

enum {
  kMenuConfirmInputs = kInputBit_A | kInputBit_Start,
  kMenuCancelInputs = kInputBit_B,
};

static bool MenuHasConfirmInput(uint16 new_inputs) {
  return (new_inputs & kMenuConfirmInputs) != 0;
}

static bool MenuHasCancelInput(uint16 new_inputs) {
  return (new_inputs & kMenuCancelInputs) != 0 && !MenuHasConfirmInput(new_inputs);
}

enum {
  kNativeOptionsRow_Controls = 0,
  kNativeOptionsRow_Aspect,
  kNativeOptionsRow_Master,
  kNativeOptionsRow_Bgm,
  kNativeOptionsRow_Sfx,
  kNativeOptionsRow_Count,
};

static uint16 GetMenuAnalogDirectionalInputs(void) {
  const float deadzone = 0.675f;
  float abs_x = fabsf(g_left_stick_x);
  float abs_y = fabsf(g_left_stick_y);
  if (abs_x < deadzone && abs_y < deadzone)
    return 0;
  if (abs_y >= abs_x)
    return g_left_stick_y < 0.0f ? kInputBit_Up : kInputBit_Down;
  return g_left_stick_x < 0.0f ? kInputBit_Left : kInputBit_Right;
}

static bool IsNativeOptionsOverlayVisible(void) {
  return g_native_options_active;
}

static void UpdateNativeOptionsOverlay(uint16 menu_inputs) {
  if (!IsNativeOptionsOverlayVisible()) {
    g_native_options_prev_inputs = 0;
    return;
  }

  uint16 new_inputs = menu_inputs & ~g_native_options_prev_inputs;
  g_native_options_prev_inputs = menu_inputs;

  if (MenuHasCancelInput(new_inputs)) {
    SaveNativeOptionsConfig();
    g_native_options_active = false;
    g_native_main_menu_active = true;
    g_native_options_prev_inputs = 0;
    ClearTransientMenuInputs();
    return;
  }

  if (new_inputs & kInputBit_Up)
    g_native_options_selection = (g_native_options_selection + kNativeOptionsRow_Count - 1) % kNativeOptionsRow_Count;
  if (new_inputs & kInputBit_Down)
    g_native_options_selection = (g_native_options_selection + 1) % kNativeOptionsRow_Count;
  if (g_native_options_selection == kNativeOptionsRow_Master && (new_inputs & kInputBit_Left))
    HandleVolumeAdjustment(-1);
  if (g_native_options_selection == kNativeOptionsRow_Master && (new_inputs & kInputBit_Right))
    HandleVolumeAdjustment(1);
  if (g_native_options_selection == kNativeOptionsRow_Aspect && (new_inputs & (kInputBit_Left | kInputBit_Right)))
    HandleAspectRatioSelection((new_inputs & kInputBit_Right) ? 1 : -1);
}

static void CopyPath(char *dst, size_t dst_size, const char *src) {
  if (!dst_size)
    return;
  snprintf(dst, dst_size, "%s", src ? src : "");
}

static bool FileExists(const char *path) {
  FILE *f;
  if (!path || !path[0])
    return false;
  f = fopen(path, "rb");
  if (!f)
    return false;
  fclose(f);
  return true;
}

static bool ResolveBlocksBoxPath(const char *workspace_relative_path, const char *exe_relative_path, char *dst, size_t dst_size) {
  if (!dst || dst_size == 0)
    return false;
  dst[0] = 0;
  if (workspace_relative_path && workspace_relative_path[0] && FileExists(workspace_relative_path)) {
    if (!BlocksBoxBootstrap_GetAbsolutePath(workspace_relative_path, dst, dst_size))
      CopyPath(dst, dst_size, workspace_relative_path);
    return true;
  }
  if (exe_relative_path && exe_relative_path[0] &&
      BlocksBoxBootstrap_GetExecutableRelativePath(exe_relative_path, dst, dst_size) &&
      FileExists(dst)) {
    return true;
  }
  if (workspace_relative_path && workspace_relative_path[0]) {
    if (!BlocksBoxBootstrap_GetAbsolutePath(workspace_relative_path, dst, dst_size))
      CopyPath(dst, dst_size, workspace_relative_path);
    return true;
  }
  if (exe_relative_path && exe_relative_path[0] &&
      BlocksBoxBootstrap_GetExecutableRelativePath(exe_relative_path, dst, dst_size)) {
    return true;
  }
  return false;
}

static bool ReadBootstrapManifestValue(const char *manifest_path, const char *key, char *dst, size_t dst_size) {
  FILE *f = fopen(manifest_path, "rb");
  if (!f)
    return false;
  char line[2048];
  size_t key_len = strlen(key);
  bool found = false;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {
      char *value = line + key_len + 1;
      size_t len = strcspn(value, "\r\n");
      value[len] = 0;
      CopyPath(dst, dst_size, value);
      found = true;
      break;
    }
  }
  fclose(f);
  return found;
}

static void GetDirectoryNameFromPath(const char *path, char *dst, size_t dst_size) {
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
    if (c == '\\' || c == '/') {
      dst[len - 1] = 0;
      return;
    }
    len--;
  }
  snprintf(dst, dst_size, ".");
}

static void JoinPath2(char *dst, size_t dst_size, const char *a, const char *b) {
  if (!dst || dst_size == 0)
    return;
  if (!a || !a[0]) {
    CopyPath(dst, dst_size, b);
    return;
  }
  if (!b || !b[0]) {
    CopyPath(dst, dst_size, a);
    return;
  }
#ifdef _WIN32
  snprintf(dst, dst_size, "%s\\%s", a, b);
#else
  snprintf(dst, dst_size, "%s/%s", a, b);
#endif
}

static char *ResolveIndexRelativePath(const char *index_path, const char *value) {
  char runtime_dir[kPathBufferSize];
  char output_root[kPathBufferSize];
  char combined[kPathBufferSize];
  if (!value || !value[0])
    return strdup("");
  if ((strlen(value) >= 2 && value[1] == ':') || value[0] == '\\' || value[0] == '/')
    return strdup(value);
  GetDirectoryNameFromPath(index_path, runtime_dir, sizeof(runtime_dir));
  GetDirectoryNameFromPath(runtime_dir, output_root, sizeof(output_root));
  JoinPath2(combined, sizeof(combined), output_root, value);
  return strdup(combined);
}

static const char *NextTsvFieldOrEmpty(char **fields) {
  char *value = NextDelim(fields, '\t');
  return value ? value : "";
}

static bool EnsureBlocksBoxImport(const char *preferred_rom_path, char *resolved_rom_path, size_t resolved_rom_path_size) {
  char manifest_path[kPathBufferSize];
  char output_dir[kPathBufferSize];
  char manifest_rom_path[kPathBufferSize];
  char runtime_manifest_path[kPathBufferSize];
  ResolveBlocksBoxPath("build\\blocksbox_import\\bootstrap.manifest",
                       "..\\blocksbox_import\\bootstrap.manifest",
                       manifest_path, sizeof(manifest_path));
  if (FileExists("build\\blocksbox_import\\bootstrap.manifest")) {
    if (!BlocksBoxBootstrap_GetAbsolutePath("build\\blocksbox_import", output_dir, sizeof(output_dir)))
      CopyPath(output_dir, sizeof(output_dir), "build\\blocksbox_import");
  } else if (!BlocksBoxBootstrap_GetExecutableRelativePath("..\\blocksbox_import", output_dir, sizeof(output_dir))) {
    if (!BlocksBoxBootstrap_GetAbsolutePath("build\\blocksbox_import", output_dir, sizeof(output_dir)))
      CopyPath(output_dir, sizeof(output_dir), "build\\blocksbox_import");
  }
  bool has_manifest_rom = ReadBootstrapManifestValue(manifest_path, "rom_path", manifest_rom_path, sizeof(manifest_rom_path)) &&
                          FileExists(manifest_rom_path);
  bool has_runtime_manifest = ReadBootstrapManifestValue(manifest_path, "runtime_manifest_path", runtime_manifest_path, sizeof(runtime_manifest_path)) &&
                              FileExists(runtime_manifest_path);
  if (has_manifest_rom && has_runtime_manifest) {
    if (!BlocksBoxBootstrap_GetAbsolutePath(manifest_rom_path, resolved_rom_path, resolved_rom_path_size))
      CopyPath(resolved_rom_path, resolved_rom_path_size, manifest_rom_path);
    return true;
  }

  char rom_path[kPathBufferSize];
  if (has_manifest_rom) {
    CopyPath(rom_path, sizeof(rom_path), manifest_rom_path);
  } else if (!BlocksBoxBootstrap_ResolveRomPath(preferred_rom_path, rom_path, sizeof(rom_path))) {
    return false;
  }

  if (!BlocksBoxBootstrap_RunImporter(rom_path, output_dir)) {
    WidescreenDebugLog("blocksbox-import: importer failed for %s", rom_path);
    CopyPath(resolved_rom_path, resolved_rom_path_size, rom_path);
    return true;
  }
  WidescreenDebugLog("blocksbox-import: importer succeeded for %s", rom_path);

  if (ReadBootstrapManifestValue(manifest_path, "rom_path", manifest_rom_path, sizeof(manifest_rom_path)) &&
      FileExists(manifest_rom_path)) {
    if (!BlocksBoxBootstrap_GetAbsolutePath(manifest_rom_path, resolved_rom_path, resolved_rom_path_size))
      CopyPath(resolved_rom_path, resolved_rom_path_size, manifest_rom_path);
  } else {
    CopyPath(resolved_rom_path, resolved_rom_path_size, rom_path);
  }
  return true;
}

static void UnloadNativeLevelEditorTilesets(void) {
  for (int i = 0; i < g_native_level_editor_tilesets_count; i++)
    free(g_native_level_editor_tilesets[i].preview_file_path);
  free(g_native_level_editor_tilesets);
  g_native_level_editor_tilesets = NULL;
  g_native_level_editor_tilesets_count = 0;
}

static bool LoadNativeLevelEditorTilesetsFromManifest(const char *bootstrap_manifest_path) {
  char runtime_manifest_path[kPathBufferSize];
  char tilesets_index_path[kPathBufferSize];
  FILE *f;
  char line_storage[4096];

  UnloadNativeLevelEditorTilesets();
  WidescreenDebugLog("level-editor: loading fallback tilesets from %s", bootstrap_manifest_path ? bootstrap_manifest_path : "(null)");
  if (!ReadBootstrapManifestValue(bootstrap_manifest_path, "runtime_manifest_path",
                                  runtime_manifest_path, sizeof(runtime_manifest_path)))
    return false;
  if (!ReadBootstrapManifestValue(runtime_manifest_path, "tilesets_index",
                                  tilesets_index_path, sizeof(tilesets_index_path)))
    return false;

  f = fopen(tilesets_index_path, "rb");
  if (!f)
    return false;
  while (fgets(line_storage, sizeof(line_storage), f) != NULL) {
    char *fields;
    char *line;
    NativeLevelEditorTilesetEntry *entry;
    char *comment;

    line = line_storage;
    comment = strchr(line, '#');
    if (comment)
      *comment = 0;
    {
      size_t len = strcspn(line, "\r\n");
      line[len] = 0;
    }
    while (*line == ' ' || *line == '\t')
      line++;
    if ((uint8)line[0] == 0xef && (uint8)line[1] == 0xbb && (uint8)line[2] == 0xbf)
      line += 3;
    while (*line == ' ' || *line == '\t')
      line++;
    if (line[0] == '#')
      continue;
    if (line[0] == 0)
      continue;
    fields = line;
    (void)NextTsvFieldOrEmpty(&fields);
    entry = (NativeLevelEditorTilesetEntry *)realloc(
        g_native_level_editor_tilesets,
        sizeof(NativeLevelEditorTilesetEntry) * (g_native_level_editor_tilesets_count + 1));
    if (!entry) {
      fclose(f);
      UnloadNativeLevelEditorTilesets();
      return false;
    }
    g_native_level_editor_tilesets = entry;
    entry = &g_native_level_editor_tilesets[g_native_level_editor_tilesets_count];
    memset(entry, 0, sizeof(*entry));
    entry->area_number = (int)strtol(NextTsvFieldOrEmpty(&fields), NULL, 10);
    entry->graphics_number = (int)strtol(NextTsvFieldOrEmpty(&fields), NULL, 10);
    (void)NextTsvFieldOrEmpty(&fields);
    (void)NextTsvFieldOrEmpty(&fields);
    (void)NextTsvFieldOrEmpty(&fields);
    entry->preview_file_path = ResolveIndexRelativePath(tilesets_index_path, NextTsvFieldOrEmpty(&fields));
    if (!entry->preview_file_path) {
      fclose(f);
      UnloadNativeLevelEditorTilesets();
      return false;
    }
    if (!entry->preview_file_path[0]) {
      free(entry->preview_file_path);
      entry->preview_file_path = NULL;
      continue;
    }
    g_native_level_editor_tilesets_count++;
  }
  fclose(f);
  WidescreenDebugLog("level-editor: fallback tilesets loaded=%d", g_native_level_editor_tilesets_count);
  return g_native_level_editor_tilesets_count > 0;
}

static bool EnsureNativeLevelEditorTilesetsLoaded(void) {
  char bootstrap_manifest_path[kPathBufferSize];
  if (BlocksBoxRuntime_GetTilesetCount() > 0)
    return true;
  if (g_native_level_editor_tilesets_count > 0)
    return true;
  ResolveBlocksBoxPath("build\\blocksbox_import\\bootstrap.manifest",
                       "..\\blocksbox_import\\bootstrap.manifest",
                       bootstrap_manifest_path, sizeof(bootstrap_manifest_path));
  {
    bool loaded = LoadNativeLevelEditorTilesetsFromManifest(bootstrap_manifest_path);
    WidescreenDebugLog("level-editor: ensure fallback loaded=%d count=%d", loaded ? 1 : 0, g_native_level_editor_tilesets_count);
    return loaded;
  }
}

static int GetNativeLevelEditorTilesetCount(void) {
  return BlocksBoxRuntime_GetTilesetCount() > 0 ? BlocksBoxRuntime_GetTilesetCount() : g_native_level_editor_tilesets_count;
}

static int GetNativeLevelEditorTilesetAreaIndex(int index) {
  if (BlocksBoxRuntime_GetTilesetCount() > 0)
    return BlocksBoxRuntime_GetTilesetAreaIndexByIndex(index);
  if (index < 0 || index >= g_native_level_editor_tilesets_count)
    return -1;
  return g_native_level_editor_tilesets[index].area_number;
}

static int GetNativeLevelEditorTilesetGraphicsSet(int index) {
  if (BlocksBoxRuntime_GetTilesetCount() > 0)
    return BlocksBoxRuntime_GetTilesetGraphicsSetByIndex(index);
  if (index < 0 || index >= g_native_level_editor_tilesets_count)
    return -1;
  return g_native_level_editor_tilesets[index].graphics_number;
}

static const char *GetNativeLevelEditorTilesetPreviewPath(int index) {
  if (BlocksBoxRuntime_GetTilesetCount() > 0)
    return BlocksBoxRuntime_GetTilesetPreviewPathByIndex(index);
  if (index < 0 || index >= g_native_level_editor_tilesets_count)
    return NULL;
  return g_native_level_editor_tilesets[index].preview_file_path;
}

static void WidescreenDebugLog(const char *fmt, ...) {
  FILE *f = fopen("debug_widescreen.log", "ab");
  if (!f)
    return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

void NORETURN Die(const char *error) {
  WidescreenDebugLog("DIE: %s", error);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, error, NULL);
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

void Warning(const char *error) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, kWindowTitle, error, NULL);
  fprintf(stderr, "Warning: %s\n", error);
}


void ChangeWindowScale(int scale_step) {
  if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
    return;
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_scale = kMaxWindowScale;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a scale level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + g_snes_width / 4) / g_snes_width;
    int mh = (bounds.h - bt - bb + g_snes_height / 4) / g_snes_height;
    max_scale = IntMin(mw, mh);
  }
  int new_scale = IntMax(IntMin(g_current_window_scale + scale_step, max_scale), 1);
  g_current_window_scale = new_scale;
  int w = new_scale * g_snes_width;
  int h = new_scale * g_snes_height;

  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#define RESIZE_BORDER 20
static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *pt, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 || (flags & SDL_WINDOW_FULLSCREEN) != 0)
    return SDL_HITTEST_NORMAL;

  if ((SDL_GetModState() & KMOD_CTRL) != 0)
    return SDL_HITTEST_DRAGGABLE;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (pt->y < RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPLEFT :
      (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
  } else if (pt->y >= h - RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMLEFT :
      (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
  } else {
    if (pt->x < RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_LEFT;
    } else if (pt->x >= w - RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_RIGHT;
    }
  }
  return SDL_HITTEST_NORMAL;
}

void RtlDrawPpuFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags) {
  static int draw_log_budget = 16;
  uint8 *ppu_pixels = g_other_image ? g_my_pixels : g_pixels;
  int src_x = IntMax((kPpuXPixels - g_snes_width) / 2, 0);
  int copy_width = IntMin(g_snes_width, kPpuXPixels);
  if (draw_log_budget-- > 0) {
    WidescreenDebugLog("RtlDrawPpuFrame: frame=%u dst_width=%d height=%d pitch=%zu ppu_width=%d src_x=%d copy_width=%d other=%d modern=%d",
                       (unsigned)snes_frame_counter, g_snes_width, g_snes_height, pitch,
                       kPpuXPixels, src_x, copy_width, g_other_image ? 1 : 0,
                       g_modern_layer_renderer ? 1 : 0);
  }
  for (size_t y = 0; y < kSnesNativeHeight; y++) {
    uint8_t *dst = (uint8_t *)pixel_buffer + y * pitch;
    if (copy_width != g_snes_width)
      memset(dst, 0, g_snes_width * 4);
    memcpy(dst, ppu_pixels + (y * kPpuXPixels + src_x) * 4, copy_width * 4);
  }
}

void DebugRequestShinesparkScreenshot(void) {
  g_shinespark_screenshot_requested = true;
}

static void QueueToggleScreenshot(const char *prefix) {
  snprintf(g_toggle_screenshot_prefix, sizeof(g_toggle_screenshot_prefix), "%s", prefix);
  g_toggle_screenshot_requested = true;
  g_toggle_screenshot_timer = 3;
}

static bool IsNativeTitleLogoPixel(uint32 color) {
  uint8 r = (uint8)((color >> 16) & 0xff);
  uint8 g = (uint8)((color >> 8) & 0xff);
  uint8 b = (uint8)(color & 0xff);
  return (r > 70 && (g > 35 || b < 90)) ||
         (r > 170 && g > 120 && b > 80);
}

static void ApplyNativeTitleSceneBackdropCompensation(uint8 *pixel_buffer, size_t pitch, int width, int height, int scale) {
  if (!g_native_level_render_enabled ||
      game_state != kGameState_1_OpeningCinematic ||
      scale <= 0)
    return;

  int shift = scale;
  if (shift >= height)
    return;

  size_t row_bytes = (size_t)width * sizeof(uint32);

  int top_end_y = IntMin(82 * scale, height - shift);
  int logo_left = 40 * scale;
  int logo_right = IntMin(218 * scale, width);
  int logo_top = 10 * scale;
  int logo_bottom = 84 * scale;
  int trademark_left = 216 * scale;
  int trademark_right = IntMin(230 * scale, width);
  int trademark_top = 60 * scale;
  int trademark_bottom = 78 * scale;
  for (int y = 0; y < top_end_y; y++) {
    uint32 *dst = (uint32 *)(pixel_buffer + (size_t)y * pitch);
    uint32 *src = (uint32 *)(pixel_buffer + (size_t)(y + shift) * pitch);
    bool in_logo_y = y >= logo_top && y < logo_bottom;
    bool in_trademark_y = y >= trademark_top && y < trademark_bottom;
    for (int x = 0; x < width; x++) {
      if (in_trademark_y && x >= trademark_left && x < trademark_right)
        continue;
      if (in_logo_y && x >= logo_left && x < logo_right &&
          (IsNativeTitleLogoPixel(dst[x]) || IsNativeTitleLogoPixel(src[x])))
        continue;
      dst[x] = src[x];
    }
  }

  int start_y = 96 * scale;
  if (start_y < 0 || start_y + shift >= height)
    return;

  for (int y = start_y; y < height - shift; y++)
    memmove(pixel_buffer + (size_t)y * pitch,
            pixel_buffer + (size_t)(y + shift) * pitch,
            row_bytes);
}

static void DrawPpuFrameWithPerf(void) {
  int render_scale = PpuGetCurrentRenderScale(g_snes->ppu, g_ppu_render_flags);
  uint8 *pixel_buffer = 0;
  int pitch = 0;

  g_renderer_funcs.BeginDraw(g_snes_width * render_scale,
                             g_snes_height * render_scale,
                             &pixel_buffer, &pitch);
  if (g_display_perf || g_config.display_perf_title) {
    static float history[64], average;
    static int history_pos;
    uint64 before = SDL_GetPerformanceCounter();
    RtlDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
    uint64 after = SDL_GetPerformanceCounter();
    float v = (double)SDL_GetPerformanceFrequency() / (after - before);
    average += v - history[history_pos];
    history[history_pos] = v;
    history_pos = (history_pos + 1) & 63;
    g_curr_fps = average * (1.0f / 64);
  } else {
    RtlDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
  }
  ApplyNativeTitleSceneBackdropCompensation(pixel_buffer, pitch,
                                            g_snes_width * render_scale,
                                            g_snes_height * render_scale,
                                            render_scale);
  if (g_display_perf)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);

  if (!g_cinematic_capture_path) {
    RenderAnalogDebugOverlay(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderOpeningIntroSkipPrompt(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderNativeMainMenu(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderNativeFileSelect(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderNativeLevelEditor(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderNativeOptionsOverlay(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderExitToMainMenuPrompt(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderBuildTimestampOverlay(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
  }
  CaptureCinematicFrame(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale);
  if (g_shinespark_screenshot_requested) {
    g_shinespark_screenshot_requested = false;
    SaveDebugScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale,
                        "shinespark_down_diag", &g_shinespark_screenshot_counter);
  }
  if (g_manual_screenshot_requested) {
    g_manual_screenshot_requested = false;
    SaveDebugScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale,
                        "manual", &g_manual_screenshot_counter);
  }
  if (!g_cinematic_capture_path &&
      g_room_load_screenshot_requested && --g_room_load_screenshot_timer <= 0 &&
      game_state == kGameState_8_MainGameplay) {
    g_room_load_screenshot_requested = false;
    SaveDebugScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale,
                        "room_load", &g_room_load_screenshot_counter);
  }
  if (!g_cinematic_capture_path &&
      g_main_menu_screenshot_requested && --g_main_menu_screenshot_timer <= 0 &&
      g_native_main_menu_active) {
    g_main_menu_screenshot_requested = false;
    SaveDebugScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale,
                        "main_menu", &g_main_menu_screenshot_counter);
  }
  if (!g_cinematic_capture_path &&
      g_toggle_screenshot_requested && --g_toggle_screenshot_timer <= 0) {
    g_toggle_screenshot_requested = false;
    SaveDebugScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale,
                        g_toggle_screenshot_prefix, &g_toggle_screenshot_counter);
  }
  if (!g_cinematic_capture_path && g_startup_screenshot_remaining > 0 && --g_startup_screenshot_timer <= 0) {
    g_startup_screenshot_timer = 15;
    g_startup_screenshot_remaining--;
    SaveDebugScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale,
                        "startup", &g_startup_screenshot_counter);
  }

  g_renderer_funcs.EndDraw();
}

void DebugRequestRoomLoadScreenshot(uint16 room_address, uint16 state_address) {
  g_room_load_screenshot_requested = true;
  g_room_load_screenshot_timer = 45;
  WidescreenDebugLog("room-load-screenshot queued: room=%04x state=%04x",
                     room_address, state_address);
}

static void WriteLe16(FILE *f, uint16_t value) {
  fputc(value & 0xff, f);
  fputc((value >> 8) & 0xff, f);
}

static void WriteLe32(FILE *f, uint32_t value) {
  fputc(value & 0xff, f);
  fputc((value >> 8) & 0xff, f);
  fputc((value >> 16) & 0xff, f);
  fputc((value >> 24) & 0xff, f);
}

static void SaveDebugScreenshot(uint8 *pixel_buffer, size_t pitch, int width, int height, const char *prefix, int *counter) {
#ifdef _WIN32
  _mkdir("debug_screenshots");
#else
  mkdir("debug_screenshots", 0755);
#endif

  char filename[160];
  snprintf(filename, sizeof(filename), "debug_screenshots/%s_%04d.bmp", prefix, ++*counter);
  FILE *f = fopen(filename, "wb");
  if (!f) {
    printf("Failed to save shinespark screenshot: %s\n", filename);
    return;
  }

  int row_bytes = width * 3;
  int padded_row_bytes = (row_bytes + 3) & ~3;
  uint32_t image_size = (uint32_t)(padded_row_bytes * height);
  uint32_t file_size = 14 + 40 + image_size;

  fputc('B', f);
  fputc('M', f);
  WriteLe32(f, file_size);
  WriteLe16(f, 0);
  WriteLe16(f, 0);
  WriteLe32(f, 14 + 40);

  WriteLe32(f, 40);
  WriteLe32(f, (uint32_t)width);
  WriteLe32(f, (uint32_t)height);
  WriteLe16(f, 1);
  WriteLe16(f, 24);
  WriteLe32(f, 0);
  WriteLe32(f, image_size);
  WriteLe32(f, 2835);
  WriteLe32(f, 2835);
  WriteLe32(f, 0);
  WriteLe32(f, 0);

  uint8 padding[3] = { 0, 0, 0 };
  for (int y = height - 1; y >= 0; y--) {
    const uint32_t *src = (const uint32_t *)(pixel_buffer + (size_t)y * pitch);
    for (int x = 0; x < width; x++) {
      uint32_t color = src[x];
      fputc(color & 0xff, f);
      fputc((color >> 8) & 0xff, f);
      fputc((color >> 16) & 0xff, f);
    }
    fwrite(padding, 1, padded_row_bytes - row_bytes, f);
  }

  fclose(f);
  printf("Saved debug screenshot: %s\n", filename);
}

static void CaptureCinematicFrame(uint8 *pixel_buffer, size_t pitch, int width, int height, int render_scale) {
  if (!g_cinematic_capture_path || g_cinematic_capture_done)
    return;
  if (g_cinematic_capture_frame_limit > 0 &&
      g_cinematic_capture_frames_written >= g_cinematic_capture_frame_limit) {
    g_cinematic_capture_done = true;
    return;
  }
  if (!g_cinematic_capture_file) {
    g_cinematic_capture_file = fopen(g_cinematic_capture_path, "wb");
    if (!g_cinematic_capture_file) {
      printf("Failed to open cinematic capture output: %s\n", g_cinematic_capture_path);
      g_cinematic_capture_done = true;
      return;
    }
  }

  int out_width = g_snes_width;
  int out_height = IntMin(g_snes_height, 224);
  if (render_scale <= 0)
    render_scale = 1;
  uint8 rgb[3];
  for (int y = 0; y < out_height; y++) {
    int source_y = y * render_scale;
    if (source_y >= height)
      source_y = height - 1;
    const uint32_t *src = (const uint32_t *)(pixel_buffer + (size_t)source_y * pitch);
    for (int x = 0; x < out_width; x++) {
      int source_x = x * render_scale;
      if (source_x >= width)
        source_x = width - 1;
      uint32_t color = src[source_x];
      rgb[0] = (uint8)((color >> 16) & 0xff);
      rgb[1] = (uint8)((color >> 8) & 0xff);
      rgb[2] = (uint8)(color & 0xff);
      fwrite(rgb, 1, 3, g_cinematic_capture_file);
    }
  }
  g_cinematic_capture_frames_written++;
  if (g_cinematic_capture_frame_limit > 0 &&
      g_cinematic_capture_frames_written >= g_cinematic_capture_frame_limit) {
    fflush(g_cinematic_capture_file);
    g_cinematic_capture_done = true;
  }
}

static void WriteCinematicAudioWavHeader(FILE *f, uint32_t data_bytes) {
  uint16_t channels = kCinematicCaptureAudioChannels;
  uint32_t sample_rate = kCinematicCaptureAudioFreq;
  uint16_t bits_per_sample = 16;
  uint16_t block_align = channels * bits_per_sample / 8;
  uint32_t byte_rate = sample_rate * block_align;
  uint32_t riff_size = 36 + data_bytes;

  fwrite("RIFF", 1, 4, f);
  WriteLe32(f, riff_size);
  fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f);
  WriteLe32(f, 16);
  WriteLe16(f, 1);
  WriteLe16(f, channels);
  WriteLe32(f, sample_rate);
  WriteLe32(f, byte_rate);
  WriteLe16(f, block_align);
  WriteLe16(f, bits_per_sample);
  fwrite("data", 1, 4, f);
  WriteLe32(f, data_bytes);
}

static void FinalizeCinematicAudioCapture(void) {
  if (!g_cinematic_audio_capture_file)
    return;
  fseek(g_cinematic_audio_capture_file, 0, SEEK_SET);
  WriteCinematicAudioWavHeader(g_cinematic_audio_capture_file, g_cinematic_audio_capture_bytes);
  fclose(g_cinematic_audio_capture_file);
  g_cinematic_audio_capture_file = NULL;
}

static void CaptureCinematicAudioForFrame(void) {
  if (!g_cinematic_audio_capture_path || g_cinematic_capture_done)
    return;
  if (!g_cinematic_audio_capture_file) {
    g_cinematic_audio_capture_file = fopen(g_cinematic_audio_capture_path, "wb");
    if (!g_cinematic_audio_capture_file) {
      printf("Failed to open cinematic audio capture output: %s\n", g_cinematic_audio_capture_path);
      return;
    }
    WriteCinematicAudioWavHeader(g_cinematic_audio_capture_file, 0);
  }

  int next_frame = g_cinematic_capture_frames_written + 1;
  int64_t target_samples = (int64_t)((double)next_frame * kCinematicCaptureAudioFreq / 60.0988138974405 + 0.5);
  int samples = (int)(target_samples - g_cinematic_audio_capture_sample_accum);
  if (samples <= 0)
    return;

  int16 *buffer = (int16 *)malloc((size_t)samples * kCinematicCaptureAudioChannels * sizeof(int16));
  if (!buffer)
    return;
  RtlRenderAudio(buffer, samples, kCinematicCaptureAudioChannels);
  uint32_t bytes = (uint32_t)((size_t)samples * kCinematicCaptureAudioChannels * sizeof(int16));
  fwrite(buffer, 1, bytes, g_cinematic_audio_capture_file);
  free(buffer);
  g_cinematic_audio_capture_sample_accum = target_samples;
  g_cinematic_audio_capture_bytes += bytes;
}

static SDL_mutex *g_audio_mutex;
static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;
static SDL_AudioDeviceID g_audio_device;

void RtlApuLock(void) {
  SDL_LockMutex(g_audio_mutex);
}

void RtlApuUnlock(void) {
  SDL_UnlockMutex(g_audio_mutex);
}

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  if (g_title_video.active && g_title_video.audio != NULL) {
    MixTitleVideoAudio(stream, len);
    SDL_UnlockMutex(g_audio_mutex);
    return;
  }
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      RtlRenderAudio((int16 *)g_audiobuffer, g_frames_per_block, g_audio_channels);
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }
  SDL_UnlockMutex(g_audio_mutex);
}


// State for sdl renderer
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Rect g_sdl_renderer_rect;

static bool SdlRenderer_Init(SDL_Window *window) {

  if (g_config.shader)
    fprintf(stderr, "Warning: Shaders are supported only with the OpenGL backend\n");

  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1,
                                              g_config.output_method == kOutputMethod_SDLSoftware ? SDL_RENDERER_SOFTWARE :
                                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(renderer, &renderer_info);
  if (kDebugFlag) {
    printf("Supported texture formats:");
    for (Uint32 i = 0; i < renderer_info.num_texture_formats; i++)
      printf(" %s", SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
    printf("\n");
  }
  g_renderer = renderer;
  if (!g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(renderer, g_snes_width, g_snes_height);
  if (g_config.linear_filtering)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

  int tex_mult = PpuGetCurrentRenderScale(g_snes->ppu, g_ppu_render_flags);
  WidescreenDebugLog("SDL texture create: base=%dx%d tex_mult=%d texture=%dx%d",
                     g_snes_width, g_snes_height, tex_mult, g_snes_width * tex_mult, g_snes_height * tex_mult);
  g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                g_snes_width * tex_mult, g_snes_height * tex_mult);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return false;
  }
  return true;
}

static void SdlRenderer_Destroy(void) {
  SDL_DestroyTexture(g_texture);
  g_texture = NULL;
  SDL_DestroyRenderer(g_renderer);
  g_renderer = NULL;
}

static bool SdlRenderer_RecreateTexture(void) {
  int tex_mult;
  SDL_Texture *texture;
  if (!g_renderer)
    return true;
  tex_mult = PpuGetCurrentRenderScale(g_snes->ppu, g_ppu_render_flags);
  texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                              g_snes_width * tex_mult, g_snes_height * tex_mult);
  if (!texture) {
    WidescreenDebugLog("SDL texture recreate failed: %s", SDL_GetError());
    return false;
  }
  SDL_DestroyTexture(g_texture);
  g_texture = texture;
  WidescreenDebugLog("SDL texture recreate: base=%dx%d tex_mult=%d texture=%dx%d",
                     g_snes_width, g_snes_height, tex_mult, g_snes_width * tex_mult, g_snes_height * tex_mult);
  return true;
}

static void ApplyRuntimeAspectRatio(void) {
  int old_width = g_snes_width;
  int old_height = g_snes_height;
  g_config.extended_aspect_ratio = 0;
  g_snes_width = kSnesNativeWidth;
  g_snes_height = kSnesNativeHeight;

  if (old_width == g_snes_width && old_height == g_snes_height)
    return;

  WidescreenDebugLog("aspect apply: extended=%u size=%dx%d old=%dx%d",
                     g_config.extended_aspect_ratio, g_snes_width, g_snes_height, old_width, old_height);
  if (g_renderer && !g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(g_renderer, g_snes_width, g_snes_height);
  if (g_renderer)
    SdlRenderer_RecreateTexture();
  if (g_window && g_config.fullscreen == 0 && g_config.window_width == 0 && g_config.window_height == 0)
    SDL_SetWindowSize(g_window, g_current_window_scale * g_snes_width, g_current_window_scale * g_snes_height);
}

static void SetModernLayerRendererEnabled(bool enabled) {
  if (enabled)
    g_ppu_render_flags |= kPpuRenderFlags_ModernLayerRenderer;
  else
    g_ppu_render_flags &= ~kPpuRenderFlags_ModernLayerRenderer;
  g_modern_layer_renderer = enabled;
}

static void ToggleNativeLevelRenderMode(void) {
  if (!g_native_level_render_enabled) {
    g_native_level_render_saved_modern_layer_renderer = g_modern_layer_renderer;
    g_native_level_render_enabled = true;
    SetModernLayerRendererEnabled(true);
  } else {
    g_native_level_render_enabled = false;
    SetModernLayerRendererEnabled(g_native_level_render_saved_modern_layer_renderer);
  }
  QueueToggleScreenshot(g_native_level_render_enabled ? "toggle_native_on" : "toggle_native_off");
  printf("[Native level renderer]=%s\n", g_native_level_render_enabled ? "on" : "off");
}

static void SdlRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  g_sdl_renderer_rect.w = width;
  g_sdl_renderer_rect.h = height;
  if (SDL_LockTexture(g_texture, &g_sdl_renderer_rect, (void **)pixels, pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }
}

static void SdlRenderer_EndDraw(void) {
  static int sdl_end_log_budget = 12;
  bool should_log = sdl_end_log_budget-- > 0;
  if (should_log)
    WidescreenDebugLog("SdlRenderer_EndDraw begin: rect=%dx%d pitch_texture_copy", g_sdl_renderer_rect.w, g_sdl_renderer_rect.h);
  //  uint64 before = SDL_GetPerformanceCounter();
  SDL_UnlockTexture(g_texture);
  //  uint64 after = SDL_GetPerformanceCounter();
  //  float v = (double)(after - before) / SDL_GetPerformanceFrequency();
  //  printf("%f ms\n", v * 1000);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, &g_sdl_renderer_rect, NULL);
  SDL_RenderPresent(g_renderer); // vsyncs to 60 FPS?
  if (should_log)
    WidescreenDebugLog("SdlRenderer_EndDraw end");
}

static bool ReadWavLe16(const uint8 *p, uint16 *value) {
  *value = (uint16)(p[0] | (p[1] << 8));
  return true;
}

static bool ReadWavLe32(const uint8 *p, uint32 *value) {
  *value = (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
  return true;
}

static bool LoadTitleVideoAudio(const char *audio_path) {
  FILE *f = fopen(audio_path, "rb");
  uint8 header[12];
  uint8 chunk_header[8];
  bool saw_fmt = false;
  uint16 audio_format = 0;
  uint16 channels = 0;
  uint32 sample_rate = 0;
  uint16 bits_per_sample = 0;
  uint8 *audio = NULL;
  uint32 audio_size = 0;

  if (!f)
    return false;
  if (fread(header, 1, sizeof(header), f) != sizeof(header) ||
      memcmp(header, "RIFF", 4) != 0 ||
      memcmp(header + 8, "WAVE", 4) != 0) {
    fclose(f);
    return false;
  }

  while (fread(chunk_header, 1, sizeof(chunk_header), f) == sizeof(chunk_header)) {
    uint32 chunk_size;
    ReadWavLe32(chunk_header + 4, &chunk_size);
    if (memcmp(chunk_header, "fmt ", 4) == 0) {
      uint8 fmt[32];
      size_t to_read = IntMin((int)chunk_size, (int)sizeof(fmt));
      if (fread(fmt, 1, to_read, f) != to_read) {
        fclose(f);
        return false;
      }
      if (chunk_size > to_read)
        fseek(f, (long)(chunk_size - to_read), SEEK_CUR);
      if (to_read >= 16) {
        ReadWavLe16(fmt + 0, &audio_format);
        ReadWavLe16(fmt + 2, &channels);
        ReadWavLe32(fmt + 4, &sample_rate);
        ReadWavLe16(fmt + 14, &bits_per_sample);
        saw_fmt = true;
      }
    } else if (memcmp(chunk_header, "data", 4) == 0) {
      audio = (uint8 *)malloc(chunk_size);
      if (!audio) {
        fclose(f);
        return false;
      }
      if (fread(audio, 1, chunk_size, f) != chunk_size) {
        free(audio);
        fclose(f);
        return false;
      }
      audio_size = chunk_size;
      break;
    } else {
      fseek(f, (long)chunk_size, SEEK_CUR);
    }
    if (chunk_size & 1)
      fseek(f, 1, SEEK_CUR);
  }
  fclose(f);

  if (!saw_fmt || !audio ||
      audio_format != 1 ||
      channels != 2 ||
      sample_rate != 44100 ||
      bits_per_sample != 16) {
    free(audio);
    return false;
  }

  free(g_title_video.audio);
  g_title_video.audio = audio;
  g_title_video.audio_size = audio_size;
  g_title_video.audio_offset = 0;
  return true;
}

static bool LoadTitleVideoOverlayImage(const char *path, TitleVideoOverlayImage *image) {
  SDL_Surface *loaded = SDL_LoadBMP(path);
  if (!loaded)
    return false;
  SDL_Surface *surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(loaded);
  if (!surface)
    return false;

  image->pixels = (uint32 *)malloc((size_t)surface->w * surface->h * sizeof(uint32));
  image->width = surface->w;
  image->height = surface->h;
  if (!image->pixels) {
    SDL_FreeSurface(surface);
    memset(image, 0, sizeof(*image));
    return false;
  }

  for (int y = 0; y < surface->h; y++) {
    const uint32 *src = (const uint32 *)((const uint8 *)surface->pixels + (size_t)y * surface->pitch);
    for (int x = 0; x < surface->w; x++) {
      Uint8 r, g, b, a;
      SDL_GetRGBA(src[x], surface->format, &r, &g, &b, &a);
      if (a == 0 || (r == 0 && g == 0 && b == 0))
        image->pixels[y * surface->w + x] = 0;
      else
        image->pixels[y * surface->w + x] = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | b;
    }
  }

  SDL_FreeSurface(surface);
  return true;
}

static void FreeTitleVideoOverlayImage(TitleVideoOverlayImage *image) {
  free(image->pixels);
  memset(image, 0, sizeof(*image));
}

static int SplitTsvPreserveEmptyFields(char *line, char **columns, int max_columns) {
  int count = 0;
  char *cursor = line;

  while (count < max_columns) {
    columns[count++] = cursor;
    char *tab = strchr(cursor, '\t');
    if (!tab)
      break;
    *tab = 0;
    cursor = tab + 1;
  }

  for (int i = 0; i < count; i++) {
    char *end = columns[i] + strlen(columns[i]);
    while (end > columns[i] && (end[-1] == '\r' || end[-1] == '\n')) {
      end--;
      *end = 0;
    }
  }

  return count;
}

static bool ParseTitleVideoSubtitleCue(char *line, TitleVideoSubtitleCue *cue) {
  char *columns[10] = { 0 };
  int column_count = SplitTsvPreserveEmptyFields(line, columns, 10);
  if (column_count < 5)
    return false;

  cue->start_frame = atoi(columns[0]);
  cue->end_frame = atoi(columns[1]);
  cue->kind = kTitleVideoSubtitleKind_RedText;
  cue->text[0] = 0;

  const char *kind = NULL;
  const char *text = NULL;
  if (column_count >= 7) {
    kind = columns[4];
    text = columns[5];
  } else {
    text = columns[4];
  }

  if (kind) {
    if (strcmp(kind, "nintendo-logo") == 0)
      cue->kind = kTitleVideoSubtitleKind_NintendoLogo;
    else if (strcmp(kind, "red-text") == 0)
      cue->kind = kTitleVideoSubtitleKind_RedText;
    else
      cue->kind = kTitleVideoSubtitleKind_Text;
  }

  if (text)
    snprintf(cue->text, sizeof(cue->text), "%s", text);
  return cue->end_frame > cue->start_frame;
}

static bool LoadTitleVideoSubtitleCues(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return false;

  int capacity = 32;
  TitleVideoSubtitleCue *cues = (TitleVideoSubtitleCue *)calloc((size_t)capacity, sizeof(*cues));
  if (!cues) {
    fclose(f);
    return false;
  }

  char line[512];
  int count = 0;
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == 0)
      continue;
    if (count == capacity) {
      capacity *= 2;
      TitleVideoSubtitleCue *new_cues = (TitleVideoSubtitleCue *)realloc(cues, (size_t)capacity * sizeof(*cues));
      if (!new_cues)
        break;
      cues = new_cues;
    }
    char parse_line[512];
    snprintf(parse_line, sizeof(parse_line), "%s", line);
    if (ParseTitleVideoSubtitleCue(parse_line, &cues[count]))
      count++;
  }

  fclose(f);
  free(g_title_video.subtitle_cues);
  g_title_video.subtitle_cues = cues;
  g_title_video.subtitle_cue_count = count;
  return count > 0;
}

static bool LoadTitleVideoOverlays(void) {
  char path[kPathBufferSize];
  bool loaded_any = false;

  if (ResolveBlocksBoxPath("BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\overlays\\super-metroid-title-logo.bmp",
                           "..\\..\\BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\overlays\\super-metroid-title-logo.bmp",
                           path, sizeof(path)) &&
      FileExists(path)) {
    loaded_any |= LoadTitleVideoOverlayImage(path, &g_title_video.title_logo);
  }

  if (ResolveBlocksBoxPath("BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\overlays\\nintendo-logo.bmp",
                           "..\\..\\BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\overlays\\nintendo-logo.bmp",
                           path, sizeof(path)) &&
      FileExists(path)) {
    loaded_any |= LoadTitleVideoOverlayImage(path, &g_title_video.nintendo_logo);
  }

  if (ResolveBlocksBoxPath("BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\plan\\title-subtitles.tsv",
                           "..\\..\\BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\plan\\title-subtitles.tsv",
                           path, sizeof(path)) &&
      FileExists(path)) {
    loaded_any |= LoadTitleVideoSubtitleCues(path);
  }

  WidescreenDebugLog("title-video: overlays loaded title=%dx%d nintendo=%dx%d cues=%d",
                     g_title_video.title_logo.width, g_title_video.title_logo.height,
                     g_title_video.nintendo_logo.width, g_title_video.nintendo_logo.height,
                     g_title_video.subtitle_cue_count);
  return loaded_any;
}

static bool TryStartTitleVideoPlayback(void) {
  char frames_path[kPathBufferSize];
  char audio_path[kPathBufferSize];
  FILE *frames_file;
  long frame_file_size;

  if (g_title_video.active)
    return true;
  if (g_cinematic_capture_path)
    return false;
  if (!ResolveBlocksBoxPath("BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\frames.rgb24",
                            "..\\..\\BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\frames.rgb24",
                            frames_path, sizeof(frames_path)) ||
      !ResolveBlocksBoxPath("BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\audio.wav",
                            "..\\..\\BlockBox\\out\\cinematic-bb-title-16x9-full\\super-metroid\\audio.wav",
                            audio_path, sizeof(audio_path)) ||
      !FileExists(frames_path) ||
      !FileExists(audio_path)) {
    WidescreenDebugLog("title-video: BB export files missing; using runtime title scene");
    return false;
  }

  frames_file = fopen(frames_path, "rb");
  if (!frames_file)
    return false;
  fseek(frames_file, 0, SEEK_END);
  frame_file_size = ftell(frames_file);
  fseek(frames_file, 0, SEEK_SET);

  memset(&g_title_video, 0, sizeof(g_title_video));
  g_title_video.width = kTitleVideoWidth;
  g_title_video.height = kTitleVideoHeight;
  g_title_video.frame_bytes = (size_t)g_title_video.width * g_title_video.height * 3;
  if (frame_file_size <= 0 || (size_t)frame_file_size < g_title_video.frame_bytes) {
    fclose(frames_file);
    return false;
  }
  g_title_video.frame_count = (int)((size_t)frame_file_size / g_title_video.frame_bytes);
  g_title_video.frames_file = frames_file;
  g_title_video.rgb_frame = (uint8 *)malloc(g_title_video.frame_bytes);
  g_title_video.argb_frame = (uint32 *)malloc((size_t)g_title_video.width * g_title_video.height * sizeof(uint32));
  if (!g_title_video.rgb_frame || !g_title_video.argb_frame || !LoadTitleVideoAudio(audio_path)) {
    StopTitleVideoPlayback(false);
    return false;
  }
  LoadTitleVideoOverlays();

  if (g_renderer) {
    g_title_video.texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                              g_title_video.width, g_title_video.height);
    if (g_title_video.texture) {
      SDL_RenderSetLogicalSize(g_renderer, g_title_video.width, g_title_video.height);
      g_title_video.changed_logical_size = true;
    }
  }

  g_title_video.active = true;
  g_title_video.show_menu = false;
  game_state = kGameState_3_Unused;
  g_native_main_menu_active = true;
  g_native_main_menu_dismissed = false;
  g_native_main_menu_return_from_options = false;
  g_native_main_menu_scene_pending = false;
  g_native_options_active = false;
  g_native_file_select_active = false;
  g_native_level_editor_active = false;
  g_exit_to_main_menu_prompt_active = false;
  demo_timer = 900;
  WidescreenDebugLog("title-video: started frames=%d path=%s", g_title_video.frame_count, frames_path);
  return true;
}

static void StopTitleVideoPlayback(bool open_title_scene) {
  if (g_audio_mutex)
    SDL_LockMutex(g_audio_mutex);
  g_title_video.active = false;
  if (g_audio_mutex)
    SDL_UnlockMutex(g_audio_mutex);

  if (g_title_video.changed_logical_size && g_renderer) {
    if (g_config.ignore_aspect_ratio)
      SDL_RenderSetLogicalSize(g_renderer, 0, 0);
    else
      SDL_RenderSetLogicalSize(g_renderer, g_snes_width, g_snes_height);
  }
  if (g_title_video.texture)
    SDL_DestroyTexture(g_title_video.texture);
  if (g_title_video.frames_file)
    fclose(g_title_video.frames_file);
  free(g_title_video.rgb_frame);
  free(g_title_video.argb_frame);
  free(g_title_video.audio);
  free(g_title_video.subtitle_cues);
  FreeTitleVideoOverlayImage(&g_title_video.title_logo);
  FreeTitleVideoOverlayImage(&g_title_video.nintendo_logo);
  memset(&g_title_video, 0, sizeof(g_title_video));
  if (open_title_scene)
    OpenNativeMainMenuScene();
}

static void MixTitleVideoAudio(Uint8 *stream, int len) {
  size_t remaining = g_title_video.audio_size > g_title_video.audio_offset
    ? g_title_video.audio_size - g_title_video.audio_offset
    : 0;
  int n = (int)IntMin(len, (int)remaining);
  if (n > 0) {
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_title_video.audio + g_title_video.audio_offset, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_title_video.audio + g_title_video.audio_offset, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_title_video.audio_offset += (size_t)n;
    stream += n;
    len -= n;
  }
  if (len > 0)
    SDL_memset(stream, 0, len);
}

static bool UpdateTitleVideoPlayback(uint16 inputs) {
  if (!g_title_video.active)
    return false;
  bool menu_just_shown = false;

  if (!g_title_video.show_menu && inputs != 0) {
    SeekTitleVideoPlayback(kTitleVideoLoopFrame);
    g_title_video.show_menu = true;
    ClearTransientMenuInputs();
    g_native_main_menu_prev_inputs = inputs;
    menu_just_shown = true;
  }

  if (!g_title_video.show_menu && g_title_video.frame_index >= kTitleVideoLoopFrame) {
    g_title_video.show_menu = true;
    ClearTransientMenuInputs();
    g_native_main_menu_prev_inputs = inputs;
    menu_just_shown = true;
  }

  if (g_title_video.show_menu && g_native_options_active && !menu_just_shown)
    UpdateNativeOptionsOverlay(inputs);
  else if (g_title_video.show_menu && g_native_file_select_active && !menu_just_shown)
    UpdateNativeFileSelect(inputs);
  else if (g_title_video.show_menu && g_native_level_editor_active && !menu_just_shown)
    UpdateNativeLevelEditor(inputs);
  else if (g_title_video.show_menu && g_native_main_menu_active && !menu_just_shown)
    UpdateNativeMainMenu(inputs);

  if (!g_native_main_menu_active && !g_native_options_active && !g_native_file_select_active && !g_native_level_editor_active) {
    StopTitleVideoPlayback(false);
    return true;
  }

  if (g_title_video.frame_index >= g_title_video.frame_count) {
    SeekTitleVideoPlayback(kTitleVideoLoopFrame);
    g_title_video.show_menu = true;
  }
  RenderTitleVideoFrame();
  g_title_video.frame_index++;
  return true;
}

static void SeekTitleVideoPlayback(int frame_index) {
  if (!g_title_video.active || !g_title_video.frames_file)
    return;

  frame_index = IntMin(IntMax(frame_index, 0), IntMax(g_title_video.frame_count - 1, 0));
  if (fseek(g_title_video.frames_file, (long)((size_t)frame_index * g_title_video.frame_bytes), SEEK_SET) != 0)
    return;
  g_title_video.frame_index = frame_index;

  if (g_audio_mutex)
    SDL_LockMutex(g_audio_mutex);
  if (g_title_video.audio_size != 0 && g_title_video.frame_count > 0) {
    size_t audio_offset = ((size_t)frame_index * g_title_video.audio_size) / (size_t)g_title_video.frame_count;
    audio_offset &= ~(size_t)3;
    g_title_video.audio_offset = audio_offset < g_title_video.audio_size ? audio_offset : g_title_video.audio_size;
  }
  if (g_audio_mutex)
    SDL_UnlockMutex(g_audio_mutex);
}

static const TitleVideoSubtitleCue *FindTitleVideoSubtitleCue(int frame_index) {
  for (int i = 0; i < g_title_video.subtitle_cue_count; i++) {
    const TitleVideoSubtitleCue *cue = &g_title_video.subtitle_cues[i];
    if (frame_index >= cue->start_frame && frame_index < cue->end_frame)
      return cue;
  }
  return NULL;
}

static void DrawTitleVideoOverlayImage(const TitleVideoOverlayImage *image, int dst_x, int dst_y) {
  if (!image || !image->pixels || !g_title_video.argb_frame)
    return;
  for (int y = 0; y < image->height; y++) {
    int py = dst_y + y;
    if ((unsigned)py >= (unsigned)g_title_video.height)
      continue;
    uint32 *dst = g_title_video.argb_frame + (size_t)py * g_title_video.width;
    const uint32 *src = image->pixels + (size_t)y * image->width;
    for (int x = 0; x < image->width; x++) {
      int px = dst_x + x;
      if ((unsigned)px >= (unsigned)g_title_video.width)
        continue;
      uint32 color = src[x];
      uint32 alpha = color >> 24;
      if (alpha == 0)
        continue;
      if (alpha == 255) {
        dst[px] = color;
      } else {
        uint32 dst_color = dst[px];
        uint32 inv = 255 - alpha;
        uint32 r = (((color >> 16) & 0xff) * alpha + ((dst_color >> 16) & 0xff) * inv) / 255;
        uint32 g = (((color >> 8) & 0xff) * alpha + ((dst_color >> 8) & 0xff) * inv) / 255;
        uint32 b = ((color & 0xff) * alpha + (dst_color & 0xff) * inv) / 255;
        dst[px] = 0xff000000u | (r << 16) | (g << 8) | b;
      }
    }
  }
}

static void DrawTitleVideoSubtitleText(const char *text) {
  if (!text || !text[0])
    return;
  int scale = 1;
  int text_width = ((int)strlen(text) * 6 - 1) * scale;
  int x = (g_title_video.width - text_width) / 2;
  int y = 112 - (7 * scale) / 2;
  DrawText5x7((uint8 *)g_title_video.argb_frame,
              (size_t)g_title_video.width * sizeof(uint32),
              g_title_video.width,
              g_title_video.height,
              x,
              y,
              text,
              scale,
              0xF6241C);
}

static void RenderTitleVideoOverlays(void) {
  if (g_title_video.show_menu) {
    if (g_title_video.title_logo.pixels) {
      int x = (g_title_video.width - g_title_video.title_logo.width) / 2;
      DrawTitleVideoOverlayImage(&g_title_video.title_logo, x, 14);
    }
    return;
  }

  const TitleVideoSubtitleCue *cue = FindTitleVideoSubtitleCue(g_title_video.frame_index);
  if (!cue)
    return;
  if (cue->kind == kTitleVideoSubtitleKind_NintendoLogo) {
    if (g_title_video.nintendo_logo.pixels) {
      int x = (g_title_video.width - g_title_video.nintendo_logo.width) / 2;
      int y = (g_title_video.height - g_title_video.nintendo_logo.height) / 2;
      DrawTitleVideoOverlayImage(&g_title_video.nintendo_logo, x, y);
    }
    return;
  }

  DrawTitleVideoSubtitleText(cue->text);
}

static void RenderTitleVideoFrame(void) {
  if (!g_title_video.active || !g_title_video.frames_file)
    return;

  if (fread(g_title_video.rgb_frame, 1, g_title_video.frame_bytes, g_title_video.frames_file) != g_title_video.frame_bytes) {
    StopTitleVideoPlayback(true);
    return;
  }

  for (int i = 0, n = g_title_video.width * g_title_video.height; i < n; i++) {
    uint8 r = g_title_video.rgb_frame[i * 3 + 0];
    uint8 g = g_title_video.rgb_frame[i * 3 + 1];
    uint8 b = g_title_video.rgb_frame[i * 3 + 2];
    g_title_video.argb_frame[i] = 0xff000000u | ((uint32)r << 16) | ((uint32)g << 8) | b;
  }

  RenderTitleVideoOverlays();

  if (g_title_video.show_menu)
    RenderNativeMainMenu((uint8 *)g_title_video.argb_frame, g_title_video.width * sizeof(uint32),
                         g_title_video.width, g_title_video.height);
  if (g_title_video.show_menu)
    RenderNativeFileSelect((uint8 *)g_title_video.argb_frame, g_title_video.width * sizeof(uint32),
                           g_title_video.width, g_title_video.height);
  if (g_title_video.show_menu)
    RenderNativeLevelEditor((uint8 *)g_title_video.argb_frame, g_title_video.width * sizeof(uint32),
                            g_title_video.width, g_title_video.height);
  if (g_title_video.show_menu)
    RenderNativeOptionsOverlay((uint8 *)g_title_video.argb_frame, g_title_video.width * sizeof(uint32),
                               g_title_video.width, g_title_video.height);

  if (g_renderer && g_title_video.texture) {
    SDL_UpdateTexture(g_title_video.texture, NULL, g_title_video.argb_frame, g_title_video.width * (int)sizeof(uint32));
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_title_video.texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
    return;
  }

  uint8 *pixel_buffer = NULL;
  int pitch = 0;
  g_renderer_funcs.BeginDraw(g_title_video.width, g_title_video.height, &pixel_buffer, &pitch);
  for (int y = 0; y < g_title_video.height; y++) {
    memcpy(pixel_buffer + (size_t)y * pitch,
           g_title_video.argb_frame + (size_t)y * g_title_video.width,
           (size_t)g_title_video.width * sizeof(uint32));
  }
  g_renderer_funcs.EndDraw();
}

static const struct RendererFuncs kSdlRendererFuncs = {
  &SdlRenderer_Init,
  &SdlRenderer_Destroy,
  &SdlRenderer_BeginDraw,
  &SdlRenderer_EndDraw,
};



#undef main
int main(int argc, char** argv) {
  remove("debug_widescreen.log");
  WidescreenDebugLog("startup: argc=%d ppu_width=%d extra=%d", argc, kPpuXPixels, kPpuExtraLeftRight);
#ifdef __SWITCH__
  SwitchImpl_Init();
#endif
  argc--, argv++;
  const char *config_file = NULL;
  if (argc >= 2 && strcmp(argv[0], "--config") == 0) {
    config_file = argv[1];
    argc -= 2, argv += 2;
  } else {
    SwitchDirectory();
  }
  WidescreenDebugLog("working-directory-ready: config_file=%s", config_file ? config_file : "(default)");
  if (argc >= 1 && strcmp(argv[0], "--debug") == 0) {
    g_debug_flag = true;
    argc -= 1, argv += 1;
  }
  while (argc >= 1) {
    if (argc >= 2 && strcmp(argv[0], "--capture-cinematic-frames") == 0) {
      g_cinematic_capture_path = argv[1];
      g_cinematic_capture_active = true;
      argc -= 2, argv += 2;
    } else if (argc >= 2 && strcmp(argv[0], "--capture-cinematic-audio") == 0) {
      g_cinematic_audio_capture_path = argv[1];
      argc -= 2, argv += 2;
    } else if (argc >= 2 && strcmp(argv[0], "--capture-frames") == 0) {
      g_cinematic_capture_frame_limit = atoi(argv[1]);
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--capture-quit") == 0) {
      g_cinematic_capture_quit_when_done = true;
      argc -= 1, argv += 1;
    } else {
      break;
    }
  }
  char bootstrap_rom_path[kPathBufferSize];
  char bootstrap_manifest_path[kPathBufferSize];
  bool have_bootstrap_rom = EnsureBlocksBoxImport(argc >= 1 ? argv[0] : NULL,
                                                  bootstrap_rom_path, sizeof(bootstrap_rom_path));
  ResolveBlocksBoxPath("build\\blocksbox_import\\bootstrap.manifest",
                       "..\\blocksbox_import\\bootstrap.manifest",
                       bootstrap_manifest_path, sizeof(bootstrap_manifest_path));
  if (BlocksBoxRuntime_LoadFromBootstrapManifest(bootstrap_manifest_path)) {
    WidescreenDebugLog("blocksbox-runtime: loaded levels=%d tilesets=%d",
                       BlocksBoxRuntime_GetLevelCount(), BlocksBoxRuntime_GetTilesetCount());
  } else {
    WidescreenDebugLog("blocksbox-runtime: unavailable");
  }
  ParseConfigFile(config_file);

  ApplyRuntimeAspectRatio();
  g_ppu_render_flags = g_config.new_renderer * kPpuRenderFlags_NewRenderer |
    g_config.enhanced_mode7 * kPpuRenderFlags_4x4Mode7 |
    g_config.extend_y * kPpuRenderFlags_Height240 |
  g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits |
    g_config.modern_layer_renderer * kPpuRenderFlags_ModernLayerRenderer;
  g_modern_layer_renderer = (g_ppu_render_flags & kPpuRenderFlags_ModernLayerRenderer) != 0;
  WidescreenDebugLog("config: fullscreen=%d extended=%u snes=%dx%d flags=0x%x modern=%d",
                     g_config.fullscreen, g_config.extended_aspect_ratio, g_snes_width, g_snes_height,
                     g_ppu_render_flags, g_modern_layer_renderer ? 1 : 0);

  if (g_config.fullscreen == 1)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else if (g_config.fullscreen == 2)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN;

  // Window scale (1=100%, 2=200%, 3=300%, etc.)
  g_current_window_scale = (g_config.window_scale == 0) ? 2 : IntMin(g_config.window_scale, kMaxWindowScale);

  // audio_freq: Use common sampling rates (see user config file. values higher than 48000 are not supported.)
  if (g_config.audio_freq < 11025 || g_config.audio_freq > 48000)
    g_config.audio_freq = kDefaultFreq;

  // Currently, the SPC/DSP implementation only supports up to stereo.
  if (g_config.audio_channels < 1 || g_config.audio_channels > 2)
    g_config.audio_channels = kDefaultChannels;

  // audio_samples: power of 2
  if (g_config.audio_samples <= 0 || ((g_config.audio_samples & (g_config.audio_samples - 1)) != 0))
    g_config.audio_samples = kDefaultSamples;

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }
  WidescreenDebugLog("SDL initialized");

  bool custom_size = g_config.window_width != 0 && g_config.window_height != 0;
  int window_width = custom_size ? g_config.window_width : g_current_window_scale * g_snes_width;
  int window_height = custom_size ? g_config.window_height : g_current_window_scale * g_snes_height;
  WidescreenDebugLog("window-request: %dx%d scale=%u custom=%d flags=0x%x",
                     window_width, window_height, g_current_window_scale, custom_size ? 1 : 0, g_win_flags);

  if (g_config.output_method == kOutputMethod_OpenGL) {
    g_win_flags |= SDL_WINDOW_OPENGL;
    OpenGLRenderer_Create(&g_renderer_funcs);
  } else {
    g_renderer_funcs = kSdlRendererFuncs;
  }

  // init snes, load rom
  const char* filename = have_bootstrap_rom ? bootstrap_rom_path : (argv[0] ? argv[0] : "sm.smc");
  Snes *snes = SnesInit(filename);
  WidescreenDebugLog("SnesInit: filename=%s snes=%p", filename, (void *)snes);

  if(snes == NULL) {
  #ifdef __SWITCH__
    ThrowMissingROM();
  #else
    char buf[256];
    snprintf(buf, sizeof(buf), "unable to load rom: %s", filename);
    Die(buf);
  #endif
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, g_win_flags);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  g_window = window;
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);
  WidescreenDebugLog("SDL window created: window=%p", (void *)window);

  if (!g_renderer_funcs.Initialize(window))
    return 1;
  WidescreenDebugLog("renderer initialized");

  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);

  bool enable_audio = g_cinematic_audio_capture_path == NULL;
  if (enable_audio) {
    SDL_AudioSpec want = { 0 }, have;
    want.freq = 44100;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = 2048;
    want.callback = &AudioCallback;
    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio_device == 0) {
      printf("Failed to open audio device: %s\n", SDL_GetError());
      return 1;
    }
    g_audio_channels = 2;
    g_frames_per_block = (534 * have.freq) / 32000;
    g_audiobuffer = (uint8 *)malloc(g_frames_per_block * have.channels * sizeof(int16));
  } else {
    g_audio_channels = kCinematicCaptureAudioChannels;
  }

  PpuBeginDrawing(snes->snes_ppu, g_pixels, kPpuXPixels * 4, 0);
  PpuBeginDrawing(snes->my_ppu, g_my_pixels, kPpuXPixels * 4, 0);
  WidescreenDebugLog("PpuBeginDrawing done: pitch=%d buffer_bytes=%zu", kPpuXPixels * 4, sizeof(g_pixels));

#if defined(_WIN32)
  _mkdir("saves");
#else
  mkdir("saves", 0755);
#endif

  RtlReadSram();

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    OpenOneGamepad(i);

  if (g_config.autosave)
    HandleCommand(kKeys_Load + 0, true);

  bool running = true;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;
  uint8 audiopaused = true;
  int main_loop_log_budget = 32;
  TryStartTitleVideoPlayback();

  while (running) {
    bool log_loop = main_loop_log_budget-- > 0;
    if (log_loop)
      WidescreenDebugLog("main-loop begin: frameCtr=%u snes_frame=%u game_state=%u cinematic=%04x",
                         frameCtr, (unsigned)snes_frame_counter, game_state, cinematic_function);
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERAXISMOTION:
        HandleGamepadAxisInput(event.caxis.which, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int b = RemapSdlButton(event.cbutton.button);
        if (b >= 0)
          HandleGamepadInput(b, event.type == SDL_CONTROLLERBUTTONDOWN);
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0 ? SDL_TRUE : SDL_FALSE);
          }
        }
        break;
      case SDL_KEYDOWN:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        break;
      case SDL_KEYUP:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_QUIT:
        running = false;
        break;
      }
    }

    if (g_paused != audiopaused) {
      audiopaused = g_paused;
      if (g_audio_device)
        SDL_PauseAudioDevice(g_audio_device, audiopaused);
    }

    if (g_paused) {
      SDL_Delay(16);
      continue;
    }

    // Gameplay uses analog-derived directions. Native menus use one
    // physical/menu-action input path so face buttons behave consistently
    // across overlays regardless of gameplay remaps.
    static uint32 previous_gamepad_modifiers;
    uint32 gamepad_new_modifiers = g_gamepad_modifiers & ~previous_gamepad_modifiers;
    previous_gamepad_modifiers = g_gamepad_modifiers;
    if (gamepad_new_modifiers & (1u << kGamepadBtn_L3))
      ToggleNativeLevelRenderMode();
    g_dedicated_missile_fire_pressed = 0;
    g_dedicated_missile_toggle_pressed = 0;
    g_dedicated_beam_fire_held = 0;
    g_dedicated_grapple_fire_pressed = 0;
    g_dedicated_grapple_fire_held = 0;

    int menu_inputs = GetNativeMenuInputs();
    int inputs = g_input1_state | g_gamepad_button_inputs;
    if (g_gamepad_modifiers & (1u << kGamepadBtn_Start))
      inputs |= kInputBit_Start;
    if (g_gamepad_modifiers & (1u << kGamepadBtn_Back))
      inputs |= kInputBit_Select;
    if (g_gamepad_modifiers & (1u << kGamepadBtn_R1))
      inputs &= ~kInputBit_PageDown;
    uint8 analog_buttons = g_gamepad_analog_buttons;
    if (g_input1_state & 0xf0)
      analog_buttons = 0;
    inputs |= analog_buttons;

    if (UpdateTitleVideoPlayback((uint16)(inputs | menu_inputs))) {
      if (g_native_main_menu_quit_requested) {
        running = false;
        continue;
      }
      frameCtr++;
      curTick = SDL_GetTicks();
      if (!g_config.disable_frame_delay) {
        static const uint8 title_video_delays[3] = { 17, 17, 16 };
        lastTick += title_video_delays[frameCtr % 3];
        if (lastTick > curTick) {
          uint32 delta = lastTick - curTick;
          if (delta > 500) {
            lastTick = curTick - 500;
            delta = 500;
          }
          SDL_Delay(delta);
        } else if (curTick - lastTick > 500) {
          lastTick = curTick;
        }
      }
      continue;
    }

    MaybeActivateNativeMainMenu();
    uint8 is_replay = 0;
    if (g_exit_to_main_menu_prompt_active) {
      uint16 game_inputs = MaskExitToMainMenuPromptInputs((uint16)menu_inputs);
      UpdateExitToMainMenuPrompt((uint16)menu_inputs);
      is_replay = RtlRunFrame(game_inputs);
      if (g_native_main_menu_scene_pending) {
        g_native_main_menu_scene_pending = false;
        OpenNativeMainMenuScene();
      }
      g_snes->disableRender = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0;
    } else if (g_native_options_active) {
      uint16 game_inputs = MaskNativeMainMenuInputs((uint16)menu_inputs);
      UpdateNativeOptionsOverlay((uint16)menu_inputs);
      is_replay = RtlRunFrame(game_inputs);
      if (game_state == kGameState_1_OpeningCinematic)
        demo_timer = 900;
      g_snes->disableRender = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0;
    } else if (g_native_file_select_active) {
      uint16 game_inputs = MaskNativeMainMenuInputs((uint16)menu_inputs);
      UpdateNativeFileSelect((uint16)menu_inputs);
      is_replay = RtlRunFrame(game_inputs);
      g_snes->disableRender = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0;
    } else if (g_native_level_editor_active) {
      uint16 game_inputs = MaskNativeLevelEditorInputs((uint16)menu_inputs);
      UpdateNativeLevelEditor((uint16)menu_inputs);
      is_replay = RtlRunFrame(game_inputs);
      if (g_native_level_editor_active && game_state == kGameState_1_OpeningCinematic)
        demo_timer = 900;
      g_snes->disableRender = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0;
    } else if (g_native_main_menu_active) {
      uint16 game_inputs = MaskNativeMainMenuInputs((uint16)menu_inputs);
      UpdateNativeMainMenu((uint16)menu_inputs);
      if (g_native_main_menu_quit_requested) {
        running = false;
        continue;
      }
      if (log_loop)
        WidescreenDebugLog("main-loop RtlRunFrame menu: inputs=0x%x game_inputs=0x%x", inputs, game_inputs);
      is_replay = RtlRunFrame(game_inputs);
      if (log_loop)
        WidescreenDebugLog("main-loop RtlRunFrame menu done: replay=%u game_state=%u cinematic=%04x",
                           is_replay, game_state, cinematic_function);
      if (g_native_main_menu_active && game_state == kGameState_1_OpeningCinematic)
        demo_timer = 900;
      g_snes->disableRender = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0;
    } else {
      g_dedicated_missile_fire_pressed = (gamepad_new_modifiers & (1u << kGamepadBtn_R1)) != 0;
      g_dedicated_missile_toggle_pressed = (gamepad_new_modifiers & (1u << kGamepadBtn_DpadUp)) != 0;
      g_dedicated_beam_fire_held = (g_gamepad_modifiers & (1u << kGamepadBtn_R2)) != 0;
      g_dedicated_grapple_fire_pressed = (gamepad_new_modifiers & (1u << kGamepadBtn_R3)) != 0;
      g_dedicated_grapple_fire_held = (g_gamepad_modifiers & (1u << kGamepadBtn_R3)) != 0;
      UpdateOpeningIntroSkipState(inputs);
      if (log_loop)
        WidescreenDebugLog("main-loop RtlRunFrame gameplay: inputs=0x%x", inputs);
      is_replay = RtlRunFrame(inputs);
      g_dedicated_missile_fire_pressed = 0;
      g_dedicated_missile_toggle_pressed = 0;
      g_dedicated_beam_fire_held = 0;
      g_dedicated_grapple_fire_pressed = 0;
      g_dedicated_grapple_fire_held = 0;
      if (log_loop)
        WidescreenDebugLog("main-loop RtlRunFrame gameplay done: replay=%u game_state=%u cinematic=%04x",
                           is_replay, game_state, cinematic_function);
      g_snes->disableRender = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0;
    }

    if (g_cinematic_capture_path && NativeMainMenuCanOpenOnTitleScene())
      demo_timer = 900;

    frameCtr++;

    if (!g_snes->disableRender) {
      CaptureCinematicAudioForFrame();
      if (log_loop)
        WidescreenDebugLog("main-loop DrawPpuFrameWithPerf begin");
      DrawPpuFrameWithPerf();
      if (log_loop)
        WidescreenDebugLog("main-loop DrawPpuFrameWithPerf end");
    }

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    if (!g_snes->disableRender && !g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      lastTick += delays[frameCtr % 3];

      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
        //        printf("Sleeping %d\n", delta);
        SDL_Delay(delta);
        if (log_loop)
          WidescreenDebugLog("main-loop delay: %u", delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
    if (log_loop)
      WidescreenDebugLog("main-loop end");
    if (g_cinematic_capture_done && g_cinematic_capture_quit_when_done)
      running = false;
  }

  StopTitleVideoPlayback(false);

  if (g_cinematic_capture_file) {
    fclose(g_cinematic_capture_file);
    g_cinematic_capture_file = NULL;
  }
  FinalizeCinematicAudioCapture();

  if (g_config.autosave)
    HandleCommand(kKeys_Save + 0, true);

  // clean sdl
  if (g_audio_device) {
    SDL_PauseAudioDevice(g_audio_device, 1);
    SDL_CloseAudioDevice(g_audio_device);
  }
  SDL_DestroyMutex(g_audio_mutex);
  free(g_audiobuffer);

  g_renderer_funcs.Destroy();

#ifdef __SWITCH__
  SwitchImpl_Exit();
#endif

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

static void RenderDigit(uint8 *dst, size_t pitch, int digit, uint32 color, bool big) {
  static const uint8 kFont[] = {
    0x1c, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1c,
    0x18, 0x1c, 0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e,
    0x3e, 0x63, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x63, 0x7f,
    0x3e, 0x63, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x30, 0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x30, 0x30, 0x78,
    0x7f, 0x03, 0x03, 0x03, 0x3f, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x1c, 0x06, 0x03, 0x03, 0x3f, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x7f, 0x63, 0x60, 0x60, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c,
    0x3e, 0x63, 0x63, 0x63, 0x3e, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x3e, 0x63, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x60, 0x30, 0x1e,
  };
  const uint8 *p = kFont + digit * 10;
  if (!big) {
    for (int y = 0; y < 10; y++, dst += pitch) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1)
          ((uint32 *)dst)[x] = color;
      }
    }
  } else {
    for (int y = 0; y < 10; y++, dst += pitch * 2) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1) {
          ((uint32 *)dst)[x * 2 + 1] = ((uint32 *)dst)[x * 2] = color;
          ((uint32 *)(dst + pitch))[x * 2 + 1] = ((uint32 *)(dst + pitch))[x * 2] = color;
        }
      }
    }
  }
}


static void RenderNumber(uint8 *dst, size_t pitch, int n, uint8 big) {
  char buf[32], *s;
  int i;
  sprintf(buf, "%d", n);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + ((pitch + i + 4) << big), pitch, *s - '0', 0x404040, big);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + (i << big), pitch, *s - '0', 0xffffff, big);
}

static bool IsOpeningIntroSkippable(void) {
  return game_state == kGameState_30_IntroCinematic &&
    cinematic_function != FUNC16(CinematicFunction_Intro_Func72) &&
    cinematic_function != FUNC16(CinematicFunction_Intro_Func73);
}

static void UpdateOpeningIntroSkipState(uint16 inputs) {
  if (!IsOpeningIntroSkippable()) {
    g_intro_skip_hold_frames = 0;
    return;
  }

  if ((inputs & kInputBit_Start) == 0) {
    g_intro_skip_hold_frames = 0;
    return;
  }

  if (g_intro_skip_hold_frames < kOpeningIntroSkipHoldFrames)
    ++g_intro_skip_hold_frames;

  if (g_intro_skip_hold_frames == kOpeningIntroSkipHoldFrames) {
    NewSaveFile();
    screen_fade_delay = 0;
    screen_fade_counter = 0;
    cinematic_function = FUNC16(CinematicFunction_Intro_Func72);
    g_intro_skip_hold_frames = 0;
  }
}

static void PutPixel(uint8 *pixel_buffer, size_t pitch, int x, int y, uint32 color) {
  ((uint32 *)(pixel_buffer + y * pitch))[x] = color;
}

static void FillRect(uint8 *pixel_buffer, size_t pitch, int width, int height, int x, int y, int w, int h, uint32 color) {
  int x0 = IntMax(x, 0), y0 = IntMax(y, 0);
  int x1 = IntMin(x + w, width), y1 = IntMin(y + h, height);
  for (int py = y0; py < y1; py++) {
    uint32 *dst = (uint32 *)(pixel_buffer + py * pitch);
    for (int px = x0; px < x1; px++)
      dst[px] = color;
  }
}

static uint32 BlendColorOverBgr(uint32 dst_bgr, uint32 src_bgr, uint8 alpha) {
  uint32 inv_a = 255 - alpha;
  uint32 rb = ((src_bgr & 0xff00ff) * alpha + (dst_bgr & 0xff00ff) * inv_a) >> 8;
  uint32 g = ((src_bgr & 0x00ff00) * alpha + (dst_bgr & 0x00ff00) * inv_a) >> 8;
  return (rb & 0xff00ff) | (g & 0x00ff00);
}

static void FillRectAlpha(uint8 *pixel_buffer, size_t pitch, int width, int height, int x, int y, int w, int h, uint32 color, uint8 alpha) {
  int x0 = IntMax(x, 0), y0 = IntMax(y, 0);
  int x1 = IntMin(x + w, width), y1 = IntMin(y + h, height);
  for (int py = y0; py < y1; py++) {
    uint32 *dst = (uint32 *)(pixel_buffer + py * pitch);
    for (int px = x0; px < x1; px++)
      dst[px] = BlendColorOverBgr(dst[px] & 0xffffff, color, alpha);
  }
}

static void DrawRectOutline(uint8 *pixel_buffer, size_t pitch, int width, int height, int x, int y, int w, int h, int thickness, uint32 color) {
  FillRect(pixel_buffer, pitch, width, height, x, y, w, thickness, color);
  FillRect(pixel_buffer, pitch, width, height, x, y + h - thickness, w, thickness, color);
  FillRect(pixel_buffer, pitch, width, height, x, y, thickness, h, color);
  FillRect(pixel_buffer, pitch, width, height, x + w - thickness, y, thickness, h, color);
}

static void DrawGlyph5x7(uint8 *pixel_buffer, size_t pitch, int width, int height, int x, int y, char ch, int scale, uint32 color) {
  static const struct {
    char ch;
    uint8 rows[7];
  } kGlyphs[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x06, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x10, 0x08, 0x04, 0x02, 0x1F } },
    { '3', { 0x1E, 0x10, 0x10, 0x0C, 0x10, 0x10, 0x1E } },
    { '4', { 0x08, 0x0C, 0x0A, 0x09, 0x1F, 0x08, 0x08 } },
    { '5', { 0x1F, 0x01, 0x01, 0x0F, 0x10, 0x10, 0x0F } },
    { '6', { 0x0E, 0x01, 0x01, 0x0F, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x10, 0x08, 0x04, 0x02, 0x02, 0x02 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x0E } },
    { ':', { 0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00 } },
    { '<', { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 } },
    { '>', { 0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01 } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x0F, 0x11, 0x11, 0x0F, 0x11, 0x11, 0x0F } },
    { 'C', { 0x0E, 0x11, 0x01, 0x01, 0x01, 0x11, 0x0E } },
    { 'D', { 0x0F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0F } },
    { 'E', { 0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F } },
    { 'F', { 0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x01 } },
    { 'G', { 0x0E, 0x11, 0x01, 0x1D, 0x11, 0x11, 0x1E } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'K', { 0x11, 0x09, 0x05, 0x03, 0x05, 0x09, 0x11 } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x13, 0x15, 0x19, 0x11, 0x11, 0x11 } },
    { 'L', { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1F } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01 } },
    { 'R', { 0x0F, 0x11, 0x11, 0x0F, 0x05, 0x09, 0x11 } },
    { 'S', { 0x1E, 0x01, 0x01, 0x0E, 0x10, 0x10, 0x0F } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
  };
  const uint8 *rows = NULL;
  for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); i++) {
    if (kGlyphs[i].ch == ch) {
      rows = kGlyphs[i].rows;
      break;
    }
  }
  if (!rows)
    return;

  for (int gy = 0; gy < 7; gy++) {
    for (int gx = 0; gx < 5; gx++) {
      if ((rows[gy] & (1 << gx)) == 0)
        continue;
      FillRect(pixel_buffer, pitch, width, height, x + gx * scale, y + gy * scale, scale, scale, color);
    }
  }
}

static void DrawText5x7(uint8 *pixel_buffer, size_t pitch, int width, int height, int x, int y, const char *text, int scale, uint32 color) {
  for (int i = 0; text[i]; i++)
    DrawGlyph5x7(pixel_buffer, pitch, width, height, x + i * scale * 6, y, text[i], scale, color);
}

static bool NativeMainMenuCanOpenOnTitleScene(void) {
  return game_state == kGameState_1_OpeningCinematic &&
         cinematic_function == FUNC16(CinematicFunc_Func1) &&
         cinematic_var18 == 0;
}

static void ResetTitleSequenceObjectState(void) {
  ClearCinematicSprites();
  ClearPaletteFXObjects();
  for (int i = 0; i < 2; i++) {
    mode7_obj_instr_ptr[i] = 0;
    mode7_obj_preinstr_func[i] = 0;
    mode7_obj_instr_timer[i] = 0;
    mode7_obj_goto_timer[i] = 0;
  }
  mode7_vram_write_queue_tail = 0;
}

static void StartNativeMainMenuTitleScene(void) {
  ResetTitleSequenceObjectState();
  LoadTitleSequenceGraphics();
  QueueMusic_Delayed8(0xFF03);
  cinematic_function = FUNC16(CinematicFunctionNone);
  SpawnCinematicSpriteObject(addr_kCinematicSpriteObjectDef_8BA0EF, FUNC16(CinematicFunctionNone));
  QueueMusic_Delayed8(5);
}

static void ClearTransientMenuInputs(void) {
  g_input1_state = 0;
  g_gamepad_button_inputs = 0;
  g_gamepad_analog_buttons = 0;
  g_gamepad_dpad_buttons = 0;
  g_gamepad_modifiers &= ~((1u << kGamepadBtn_A) | (1u << kGamepadBtn_B) |
                           (1u << kGamepadBtn_X) | (1u << kGamepadBtn_Y) |
                           (1u << kGamepadBtn_Back) | (1u << kGamepadBtn_Start) |
                           (1u << kGamepadBtn_DpadUp) | (1u << kGamepadBtn_DpadDown) |
                           (1u << kGamepadBtn_DpadLeft) | (1u << kGamepadBtn_DpadRight));
  for (int button = kGamepadBtn_A; button <= kGamepadBtn_DpadRight; button++)
    g_gamepad_last_cmd[button] = 0;
  g_native_main_menu_prev_inputs = 0;
  g_exit_to_main_menu_prompt_prev_inputs = 0;
}

static void OpenNativeMainMenuScene(void) {
  if (g_native_level_render_enabled) {
    g_native_level_render_enabled = false;
    SetModernLayerRendererEnabled(g_native_level_render_saved_modern_layer_renderer);
  }
  reg_BG1HOFS = 0;
  reg_BG1VOFS = 0;
  reg_BG2HOFS = 0;
  reg_BG2VOFS = 0;
  bg1_x_offset = 0;
  bg1_y_offset = 0;
  bg2_x_scroll = 0;
  bg2_y_scroll = 0;
  layer1_x_pos = 0;
  layer1_y_pos = 0;
  layer2_x_pos = 0;
  layer2_y_pos = 0;
  game_state = kGameState_3_Unused;
  screen_fade_delay = 0;
  screen_fade_counter = 0;
  demo_timer = 900;
  g_skip_menu = false;
  g_native_main_menu_active = true;
  g_native_main_menu_dismissed = false;
  g_native_main_menu_return_from_options = false;
  g_native_main_menu_scene_pending = false;
  g_native_options_active = false;
  g_native_file_select_active = false;
  g_native_file_select_selection = 0;
  g_native_file_select_prev_inputs = 0;
  g_native_file_select_status_slot = -1;
  g_native_file_select_status_timer = 0;
  g_native_level_editor_active = false;
  g_native_level_editor_page = kNativeLevelEditorPage_PackList;
  g_native_level_editor_pack_selection = 0;
  g_native_level_editor_pack_menu_selection = 0;
  g_native_level_editor_level_selection = 0;
  g_native_level_editor_level_scroll = 0;
  g_native_level_editor_selection = 0;
  g_native_level_editor_scroll = 0;
  g_native_level_editor_prev_inputs = 0;
  g_exit_to_main_menu_prompt_active = false;
  UnloadNativeLevelEditorPreview();
  ClearTransientMenuInputs();
  ScreenOff();
  if (TryStartTitleVideoPlayback()) {
    SeekTitleVideoPlayback(kTitleVideoLoopFrame);
    g_title_video.show_menu = true;
  }
}

static void MaybeActivateNativeMainMenu(void) {
  if (g_cinematic_capture_path)
    return;
  if (g_native_options_active)
    return;
  if (game_state == kGameState_1_OpeningCinematic && !g_title_video.active) {
    OpenNativeMainMenuScene();
    return;
  }

  if (g_native_main_menu_return_from_options) {
    if (game_state == kGameState_4_FileSelectMenus) {
      OpenNativeMainMenuScene();
      g_native_main_menu_return_from_options = false;
      return;
    }
    if (game_state != kGameState_2_GameOptionsMenu) {
      g_native_main_menu_return_from_options = false;
      g_native_main_menu_dismissed = true;
    }
  }

  if (!g_native_main_menu_dismissed && !g_native_main_menu_active && !g_native_file_select_active && !g_native_level_editor_active &&
      NativeMainMenuCanOpenOnTitleScene()) {
    g_native_main_menu_active = true;
    g_native_main_menu_return_from_options = false;
    g_main_menu_screenshot_requested = true;
    g_main_menu_screenshot_timer = 15;
    ClearTransientMenuInputs();
    demo_timer = 900;
  }
}

void RequestNativeMainMenuFromFileSelect(void) {
  OpenNativeMainMenuScene();
}

void StartNativePlayFromMainMenu(void) {
  OpenNativeFileSelect(0);
  WidescreenDebugLog("native-menu: play -> native file select");
}

static void OpenNativeOptionsFromMainMenu(uint16 held_inputs) {
  g_native_main_menu_active = false;
  g_native_options_active = true;
  g_native_main_menu_return_from_options = false;
  g_native_main_menu_prev_inputs = 0;
  g_native_options_selection = kNativeOptionsRow_Master;
  ClearTransientMenuInputs();
  (void)held_inputs;
}

static void OpenNativeFileSelect(uint16 held_inputs) {
  g_native_main_menu_active = false;
  g_native_options_active = false;
  g_native_file_select_active = true;
  g_native_level_editor_active = false;
  g_native_main_menu_dismissed = false;
  g_native_main_menu_return_from_options = false;
  g_native_main_menu_prev_inputs = 0;
  g_native_file_select_prev_inputs = held_inputs;
  g_native_file_select_selection = 0;
  g_native_file_select_status_slot = -1;
  g_native_file_select_status_timer = 0;
  game_state = kGameState_3_Unused;
  screen_fade_delay = 0;
  screen_fade_counter = 0;
  demo_timer = 900;
  if (g_title_video.active) {
    g_title_video.show_menu = true;
    if (g_title_video.frame_index < kTitleVideoLoopFrame)
      SeekTitleVideoPlayback(kTitleVideoLoopFrame);
  } else if (TryStartTitleVideoPlayback()) {
    SeekTitleVideoPlayback(kTitleVideoLoopFrame);
    g_title_video.show_menu = true;
  }
  ClearTransientMenuInputs();
}

static void CloseNativeFileSelect(void) {
  g_native_file_select_active = false;
  g_native_main_menu_active = true;
  g_native_main_menu_prev_inputs = 0;
  g_native_file_select_prev_inputs = 0;
  g_native_file_select_status_slot = -1;
  g_native_file_select_status_timer = 0;
  ClearTransientMenuInputs();
}

static void UpdateNativeFileSelect(uint16 inputs) {
  enum { kNativeFileSelectItemCount = 4 };
  uint16 new_inputs = inputs & ~g_native_file_select_prev_inputs;
  g_native_file_select_prev_inputs = inputs;

  if (g_native_file_select_status_timer > 0)
    g_native_file_select_status_timer--;

  if (MenuHasCancelInput(new_inputs) || (new_inputs & kInputBit_Select)) {
    CloseNativeFileSelect();
    return;
  }
  if (new_inputs & kInputBit_Up) {
    g_native_file_select_selection = (g_native_file_select_selection + kNativeFileSelectItemCount - 1) % kNativeFileSelectItemCount;
    return;
  }
  if (new_inputs & kInputBit_Down) {
    g_native_file_select_selection = (g_native_file_select_selection + 1) % kNativeFileSelectItemCount;
    return;
  }
  if (!MenuHasConfirmInput(new_inputs))
    return;

  if (g_native_file_select_selection == 3) {
    CloseNativeFileSelect();
    return;
  }

  g_native_file_select_status_slot = g_native_file_select_selection;
  g_native_file_select_status_timer = 120;
}

static void UnloadNativeLevelEditorPreview(void) {
  free(g_native_level_editor_preview.pixels);
  g_native_level_editor_preview.pixels = NULL;
  g_native_level_editor_preview.width = 0;
  g_native_level_editor_preview.height = 0;
  g_native_level_editor_preview.loaded_index = -2;
}

static bool LoadNativeLevelEditorPreviewFromPath(const char *preview_path, int loaded_key) {
  SDL_Surface *loaded_surface = NULL;
  SDL_Surface *surface = NULL;
  uint32 *pixels = NULL;

  if (g_native_level_editor_preview.loaded_index == loaded_key && g_native_level_editor_preview.pixels != NULL)
    return true;

  UnloadNativeLevelEditorPreview();
  g_native_level_editor_preview.loaded_index = loaded_key;
  WidescreenDebugLog("level-editor: preview request key=%d path=%s", loaded_key, preview_path ? preview_path : "(null)");
  if (!preview_path || !preview_path[0])
    return false;

  loaded_surface = SDL_LoadBMP(preview_path);
  if (!loaded_surface) {
    WidescreenDebugLog("level-editor: SDL_LoadBMP failed for %s", preview_path);
    return false;
  }
  surface = SDL_ConvertSurfaceFormat(loaded_surface, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(loaded_surface);
  loaded_surface = NULL;
  if (!surface)
    return false;

  pixels = (uint32 *)malloc(sizeof(uint32) * surface->w * surface->h);
  if (!pixels) {
    SDL_FreeSurface(surface);
    return false;
  }

  for (int y = 0; y < surface->h; y++) {
    const uint32 *src_row = (const uint32 *)((const uint8 *)surface->pixels + y * surface->pitch);
    uint32 *dst_row = pixels + y * surface->w;
    for (int x = 0; x < surface->w; x++) {
      Uint8 r, g, b, a;
      SDL_GetRGBA(src_row[x], surface->format, &r, &g, &b, &a);
      dst_row[x] = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | b;
    }
  }

  g_native_level_editor_preview.pixels = pixels;
  g_native_level_editor_preview.width = surface->w;
  g_native_level_editor_preview.height = surface->h;
  SDL_FreeSurface(surface);
  return true;
}

static bool LoadNativeLevelEditorPreview(int tileset_index) {
  return LoadNativeLevelEditorPreviewFromPath(GetNativeLevelEditorTilesetPreviewPath(tileset_index), tileset_index);
}

static bool LoadNativeLevelEditorLevelPreview(int level_index) {
  return LoadNativeLevelEditorPreviewFromPath(BlocksBoxRuntime_GetLevelPreviewPathByIndex(level_index), -3 - level_index);
}

static void OpenNativeLevelEditorTileSets(void) {
  EnsureNativeLevelEditorTilesetsLoaded();
  int tileset_count = GetNativeLevelEditorTilesetCount();
  g_native_level_editor_page = kNativeLevelEditorPage_TileSets;
  g_native_level_editor_selection = IntMin(IntMax(g_native_level_editor_selection, 0), IntMax(tileset_count - 1, 0));
  g_native_level_editor_scroll = IntMin(g_native_level_editor_scroll, g_native_level_editor_selection);
  UnloadNativeLevelEditorPreview();
  if (tileset_count > 0)
    LoadNativeLevelEditorPreview(g_native_level_editor_selection);
}

static void OpenNativeLevelEditor(uint16 held_inputs) {
  EnsureNativeLevelEditorTilesetsLoaded();
  int tileset_count = GetNativeLevelEditorTilesetCount();
  WidescreenDebugLog("level-editor: open count=%d runtime_count=%d fallback_count=%d",
                     tileset_count, BlocksBoxRuntime_GetTilesetCount(), g_native_level_editor_tilesets_count);
  g_native_main_menu_active = false;
  g_native_level_editor_active = true;
  g_native_level_editor_page = kNativeLevelEditorPage_PackList;
  g_native_level_editor_pack_selection = 0;
  g_native_level_editor_pack_menu_selection = 0;
  g_native_level_editor_level_selection = 0;
  g_native_level_editor_level_scroll = 0;
  g_native_level_editor_selection = IntMin(IntMax(g_native_level_editor_selection, 0), IntMax(tileset_count - 1, 0));
  g_native_level_editor_scroll = IntMin(g_native_level_editor_scroll, g_native_level_editor_selection);
  g_native_level_editor_prev_inputs = held_inputs;
  UnloadNativeLevelEditorPreview();
  demo_timer = 900;
}

static void CloseNativeLevelEditor(void) {
  g_native_level_editor_active = false;
  g_native_main_menu_active = true;
  g_native_main_menu_prev_inputs = 0;
  g_native_level_editor_prev_inputs = 0;
  UnloadNativeLevelEditorPreview();
  ClearTransientMenuInputs();
}

static void UpdateNativeMainMenu(uint16 inputs) {
  enum { kNativeMainMenuItemCount = 5 };
  uint16 new_inputs = inputs & ~g_native_main_menu_prev_inputs;
  g_native_main_menu_prev_inputs = inputs;

  if (new_inputs & kInputBit_Up) {
    g_native_main_menu_selection = (g_native_main_menu_selection + kNativeMainMenuItemCount - 1) % kNativeMainMenuItemCount;
    return;
  }
  if (new_inputs & kInputBit_Down) {
    g_native_main_menu_selection = (g_native_main_menu_selection + 1) % kNativeMainMenuItemCount;
    return;
  }
  if (!MenuHasConfirmInput(new_inputs))
    return;

  g_native_main_menu_active = false;
  g_native_main_menu_prev_inputs = 0;
  g_skip_menu = false;
  if (g_native_main_menu_selection == 0) {
    StartNativePlayFromMainMenu();
  } else if (g_native_main_menu_selection == 1) {
    OpenNativeOptionsFromMainMenu(inputs);
  } else if (g_native_main_menu_selection == 2) {
    OpenNativeLevelEditor(inputs);
  } else {
    if (g_native_main_menu_selection == 4) {
      g_native_main_menu_quit_requested = true;
      return;
    }
    g_native_main_menu_dismissed = true;
    StartDebugScenarioFromMainMenu();
  }
}

static uint16 MaskNativeMainMenuInputs(uint16 inputs) {
  return inputs & ~(uint16)(kInputBit_Up | kInputBit_Down | kInputBit_Left | kInputBit_Right |
                            kInputBit_A | kInputBit_B | kInputBit_Start |
                            kInputBit_PageUp | kInputBit_PageDown);
}

static void UpdateNativeLevelEditor(uint16 inputs) {
  enum { kVisibleItems = 10 };
  int tileset_count;
  int level_count;
  EnsureNativeLevelEditorTilesetsLoaded();
  tileset_count = GetNativeLevelEditorTilesetCount();
  level_count = BlocksBoxRuntime_GetLevelCount();
  uint16 new_inputs = inputs & ~g_native_level_editor_prev_inputs;
  g_native_level_editor_prev_inputs = inputs;

  if (MenuHasCancelInput(new_inputs) || (new_inputs & kInputBit_Select)) {
    if (g_native_level_editor_page == kNativeLevelEditorPage_TileSets ||
        g_native_level_editor_page == kNativeLevelEditorPage_LevelList) {
      g_native_level_editor_page = kNativeLevelEditorPage_PackHome;
      g_native_level_editor_prev_inputs = inputs;
      UnloadNativeLevelEditorPreview();
      return;
    }
    if (g_native_level_editor_page == kNativeLevelEditorPage_PackHome) {
      g_native_level_editor_page = kNativeLevelEditorPage_PackList;
      g_native_level_editor_prev_inputs = inputs;
      return;
    }
    CloseNativeLevelEditor();
    return;
  }

  if (g_native_level_editor_page == kNativeLevelEditorPage_PackList) {
    if (MenuHasConfirmInput(new_inputs)) {
      g_native_level_editor_page = kNativeLevelEditorPage_PackHome;
      g_native_level_editor_pack_menu_selection = 0;
      g_native_level_editor_prev_inputs = inputs;
    }
    return;
  }

  if (g_native_level_editor_page == kNativeLevelEditorPage_PackHome) {
    if (new_inputs & (kInputBit_Up | kInputBit_Down)) {
      g_native_level_editor_pack_menu_selection ^= 1;
      return;
    }
    if (MenuHasConfirmInput(new_inputs)) {
      if (g_native_level_editor_pack_menu_selection == 0) {
        g_native_level_editor_page = kNativeLevelEditorPage_LevelList;
        g_native_level_editor_level_selection = IntMin(g_native_level_editor_level_selection, IntMax(level_count - 1, 0));
        g_native_level_editor_level_scroll = IntMin(g_native_level_editor_level_scroll, g_native_level_editor_level_selection);
        UnloadNativeLevelEditorPreview();
        if (level_count > 0)
          LoadNativeLevelEditorLevelPreview(g_native_level_editor_level_selection);
      } else {
        OpenNativeLevelEditorTileSets();
      }
      g_native_level_editor_prev_inputs = inputs;
    }
    return;
  }

  if (g_native_level_editor_page == kNativeLevelEditorPage_LevelList) {
    if (level_count <= 0)
      return;
    if (new_inputs & (kInputBit_Up | kInputBit_PageUp)) {
      int delta = (new_inputs & kInputBit_PageUp) ? 10 : 1;
      g_native_level_editor_level_selection = IntMax(g_native_level_editor_level_selection - delta, 0);
    } else if (new_inputs & (kInputBit_Down | kInputBit_PageDown)) {
      int delta = (new_inputs & kInputBit_PageDown) ? 10 : 1;
      g_native_level_editor_level_selection = IntMin(g_native_level_editor_level_selection + delta, level_count - 1);
    } else {
      return;
    }
    if (g_native_level_editor_level_selection < g_native_level_editor_level_scroll)
      g_native_level_editor_level_scroll = g_native_level_editor_level_selection;
    if (g_native_level_editor_level_selection >= g_native_level_editor_level_scroll + kVisibleItems)
      g_native_level_editor_level_scroll = g_native_level_editor_level_selection - kVisibleItems + 1;
    LoadNativeLevelEditorLevelPreview(g_native_level_editor_level_selection);
    return;
  }

  if (tileset_count <= 0)
    return;

  if (new_inputs & (kInputBit_Up | kInputBit_PageUp)) {
    int delta = (new_inputs & kInputBit_PageUp) ? 10 : 1;
    g_native_level_editor_selection = IntMax(g_native_level_editor_selection - delta, 0);
  } else if (new_inputs & (kInputBit_Down | kInputBit_PageDown)) {
    int delta = (new_inputs & kInputBit_PageDown) ? 10 : 1;
    g_native_level_editor_selection = IntMin(g_native_level_editor_selection + delta, tileset_count - 1);
  } else {
    return;
  }

  if (g_native_level_editor_selection < g_native_level_editor_scroll)
    g_native_level_editor_scroll = g_native_level_editor_selection;
  if (g_native_level_editor_selection >= g_native_level_editor_scroll + kVisibleItems)
    g_native_level_editor_scroll = g_native_level_editor_selection - kVisibleItems + 1;
  LoadNativeLevelEditorPreview(g_native_level_editor_selection);
}

static uint16 MaskNativeLevelEditorInputs(uint16 inputs) {
  return inputs & ~(uint16)(kInputBit_Up | kInputBit_Down | kInputBit_Left | kInputBit_Right |
                            kInputBit_A | kInputBit_B | kInputBit_Start | kInputBit_Select |
                            kInputBit_PageUp | kInputBit_PageDown);
}

static void ShowExitToMainMenuPrompt(void) {
  if (g_native_main_menu_active || g_exit_to_main_menu_prompt_active)
    return;
  g_exit_to_main_menu_prompt_active = true;
  g_exit_to_main_menu_prompt_selection_yes = true;
  g_exit_to_main_menu_prompt_prev_inputs = 0;
  g_gamepad_analog_buttons = 0;
  g_gamepad_dpad_buttons = 0;
}

static void UpdateExitToMainMenuPrompt(uint16 inputs) {
  uint16 new_inputs = inputs & ~g_exit_to_main_menu_prompt_prev_inputs;
  g_exit_to_main_menu_prompt_prev_inputs = inputs;

  if (new_inputs & (kInputBit_Left | kInputBit_Right | kInputBit_Up | kInputBit_Down))
    g_exit_to_main_menu_prompt_selection_yes = !g_exit_to_main_menu_prompt_selection_yes;

  if (MenuHasCancelInput(new_inputs) || (new_inputs & kInputBit_Select)) {
    g_exit_to_main_menu_prompt_active = false;
    g_exit_to_main_menu_prompt_prev_inputs = 0;
    return;
  }

  if (MenuHasConfirmInput(new_inputs)) {
    if (g_exit_to_main_menu_prompt_selection_yes)
      g_native_main_menu_scene_pending = true;
    else {
      g_exit_to_main_menu_prompt_active = false;
      g_exit_to_main_menu_prompt_prev_inputs = 0;
    }
  }
}

static uint16 MaskExitToMainMenuPromptInputs(uint16 inputs) {
  return inputs & ~(uint16)(kInputBit_Up | kInputBit_Down | kInputBit_Left | kInputBit_Right |
                            kInputBit_A | kInputBit_B | kInputBit_Start | kInputBit_Select |
                            kInputBit_PageUp | kInputBit_PageDown);
}

static void RenderExitToMainMenuPrompt(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  if (!g_exit_to_main_menu_prompt_active)
    return;

  const int scale = IntMax(1, height / 240);
  const int panel_w = 150 * scale;
  const int panel_h = 58 * scale;
  const int panel_x = (width - panel_w) / 2;
  const int panel_y = (height - panel_h) / 2;
  uint32 yes_color = g_exit_to_main_menu_prompt_selection_yes ? 0x8FEA7D : 0xC5D0D8;
  uint32 no_color = !g_exit_to_main_menu_prompt_selection_yes ? 0x8FEA7D : 0xC5D0D8;

  if (g_modern_layer_renderer)
    FillRect(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, 0xb0101722);
  else
    FillRectAlpha(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, 0x101722, 176);
  DrawRectOutline(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, IntMax(1, scale), 0x8FEA7D);
  DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 17 * scale, panel_y + 12 * scale,
              "EXIT TO MENU", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 35 * scale, panel_y + 34 * scale,
              g_exit_to_main_menu_prompt_selection_yes ? "> YES" : "  YES", scale, yes_color);
  DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 91 * scale, panel_y + 34 * scale,
              !g_exit_to_main_menu_prompt_selection_yes ? "> NO" : "  NO", scale, no_color);
}

static void DrawScaledPreviewImage(uint8 *pixel_buffer, size_t pitch, int width, int height,
                                   int dst_x, int dst_y, int dst_w, int dst_h,
                                   const uint32 *src_pixels, int src_w, int src_h) {
  if (!src_pixels || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    return;
  for (int y = 0; y < dst_h; y++) {
    int py = dst_y + y;
    if ((unsigned)py >= (unsigned)height)
      continue;
    int src_y = (y * src_h) / dst_h;
    uint32 *dst_row = (uint32 *)(pixel_buffer + (size_t)py * pitch);
    const uint32 *src_row = src_pixels + src_y * src_w;
    for (int x = 0; x < dst_w; x++) {
      int px = dst_x + x;
      if ((unsigned)px >= (unsigned)width)
        continue;
      uint32 color = src_row[(x * src_w) / dst_w];
      uint32 alpha = color >> 24;
      if (alpha == 0)
        continue;
      dst_row[px] = color;
    }
  }
}

static void RenderNativeMainMenu(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  static const char *const kMenuItems[5] = { "PLAY", "OPTIONS", "LEVEL EDITOR", "TEST", "EXIT" };
  if (!g_native_main_menu_active)
    return;

  const int scale = IntMax(1, height / 240);
  const int panel_w = 132 * scale;
  const int panel_h = 126 * scale;
  const int panel_x = (width - panel_w) / 2;
  const int panel_y = (height - panel_h) / 2 + 28 * scale;
  const int title_x = panel_x + 18 * scale;
  const int title_y = panel_y + 11 * scale;
  const int item_x = panel_x + 31 * scale;
  const int first_item_y = panel_y + 32 * scale;
  const int item_gap = 16 * scale;
  uint8 border_flicker = (uint8)(190 + ((nmi_frame_counter_word * 17 + (nmi_frame_counter_word >> 2) * 53) & 63));
  uint32 border_color = BlendColorOverBgr(0x1C3A2B, 0x8FEA7D, border_flicker);

  FillRectAlpha(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, 0x101722, 156);
  for (int y = panel_y + 4 * scale + (nmi_frame_counter_word & 3) * scale; y < panel_y + panel_h - 4 * scale; y += 4 * scale) {
    FillRectAlpha(pixel_buffer, pitch, width, height, panel_x + 3 * scale, y, panel_w - 6 * scale, IntMax(1, scale), 0x2D6F58, 53);
  }
  DrawRectOutline(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, IntMax(1, scale), border_color);
  DrawText5x7(pixel_buffer, pitch, width, height, title_x, title_y, "MAIN MENU", scale, 0xFFFFFF);

  for (int i = 0; i < 5; i++) {
    int y = first_item_y + i * item_gap;
    uint32 text_color = i == g_native_main_menu_selection ? 0x8FEA7D : 0xC5D0D8;
    if (i == g_native_main_menu_selection) {
      DrawText5x7(pixel_buffer, pitch, width, height, item_x - 16 * scale, y, ">", scale, 0x8FEA7D);
      FillRectAlpha(pixel_buffer, pitch, width, height, item_x - 5 * scale, y + 9 * scale, 84 * scale, IntMax(1, scale), 0x2E6F58, 140);
    }
    DrawText5x7(pixel_buffer, pitch, width, height, item_x, y, kMenuItems[i], scale, text_color);
  }

}

static void RenderNativeFileSelect(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  static const char *const kFileItems[4] = { "FILE A", "FILE B", "FILE C", "BACK" };
  char label[64];
  if (!g_native_file_select_active)
    return;

  const int scale = IntMax(1, height / 240);
  const int panel_w = 154 * scale;
  const int panel_h = 104 * scale;
  const int panel_x = (width - panel_w) / 2;
  const int panel_y = (height - panel_h) / 2 + 30 * scale;
  const int title_x = panel_x + 17 * scale;
  const int title_y = panel_y + 10 * scale;
  const int item_x = panel_x + 35 * scale;
  const int first_item_y = panel_y + 33 * scale;
  const int item_gap = 15 * scale;
  uint8 border_flicker = (uint8)(190 + ((nmi_frame_counter_word * 19 + (nmi_frame_counter_word >> 1) * 41) & 63));
  uint32 border_color = BlendColorOverBgr(0x1C3A2B, 0x8FEA7D, border_flicker);

  FillRectAlpha(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, 0x101722, 168);
  for (int y = panel_y + 4 * scale + (nmi_frame_counter_word & 3) * scale; y < panel_y + panel_h - 4 * scale; y += 4 * scale)
    FillRectAlpha(pixel_buffer, pitch, width, height, panel_x + 3 * scale, y, panel_w - 6 * scale, IntMax(1, scale), 0x2D6F58, 46);
  DrawRectOutline(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, IntMax(1, scale), border_color);
  DrawText5x7(pixel_buffer, pitch, width, height, title_x, title_y, "FILE SELECT", scale, 0xFFFFFF);

  for (int i = 0; i < 4; i++) {
    int y = first_item_y + i * item_gap;
    uint32 text_color = i == g_native_file_select_selection ? 0x8FEA7D : 0xC5D0D8;
    if (i == g_native_file_select_selection) {
      DrawText5x7(pixel_buffer, pitch, width, height, item_x - 16 * scale, y, ">", scale, 0x8FEA7D);
      FillRectAlpha(pixel_buffer, pitch, width, height, item_x - 5 * scale, y + 9 * scale, 88 * scale, IntMax(1, scale), 0x2E6F58, 130);
    }
    DrawText5x7(pixel_buffer, pitch, width, height, item_x, y, kFileItems[i], scale, text_color);
  }

  if (g_native_file_select_status_timer > 0 && g_native_file_select_status_slot >= 0) {
    snprintf(label, sizeof(label), "FILE %c NOT WIRED", 'A' + g_native_file_select_status_slot);
    DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 19 * scale, panel_y + panel_h - 13 * scale,
                label, scale, 0xFFFFFF);
  }
}

static void RenderNativeLevelEditor(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  char label[64];
  const char *game_id;
  int tileset_count;
  int scale;
  int margin;
  int panel_y;
  int panel_h;
  int left_panel_x;
  int left_panel_w;
  int right_panel_x;
  int right_panel_w;
  int row_h;
  int list_start_y;
  int preview_box_x;
  int preview_box_y;
  int preview_box_w;
  int preview_box_h;
  int preview_draw_size;

  if (!g_native_level_editor_active)
    return;

  scale = IntMax(1, height / 240);
  margin = 16 * scale;
  panel_y = 28 * scale;
  panel_h = height - panel_y - 24 * scale;
  left_panel_x = margin;
  left_panel_w = 158 * scale;
  right_panel_x = left_panel_x + left_panel_w + 12 * scale;
  right_panel_w = width - right_panel_x - margin;
  row_h = 14 * scale;
  list_start_y = panel_y + 34 * scale;
  preview_box_x = right_panel_x + 10 * scale;
  preview_box_y = panel_y + 34 * scale;
  preview_box_w = right_panel_w - 20 * scale;
  preview_box_h = panel_h - 46 * scale;
  preview_draw_size = IntMin(preview_box_w - 10 * scale, preview_box_h - 10 * scale);
  tileset_count = BlocksBoxRuntime_GetTilesetCount();
  EnsureNativeLevelEditorTilesetsLoaded();
  tileset_count = GetNativeLevelEditorTilesetCount();
  game_id = BlocksBoxRuntime_GetGameId();

  FillRectAlpha(pixel_buffer, pitch, width, height, left_panel_x, panel_y, left_panel_w, panel_h, 0x101722, 168);
  FillRectAlpha(pixel_buffer, pitch, width, height, right_panel_x, panel_y, right_panel_w, panel_h, 0x101722, 168);
  DrawRectOutline(pixel_buffer, pitch, width, height, left_panel_x, panel_y, left_panel_w, panel_h, IntMax(1, scale), 0x8FEA7D);
  DrawRectOutline(pixel_buffer, pitch, width, height, right_panel_x, panel_y, right_panel_w, panel_h, IntMax(1, scale), 0x8FEA7D);

  if (g_native_level_editor_page == kNativeLevelEditorPage_PackList) {
    DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, panel_y + 10 * scale, "BLOCKSBOX PACKS", scale, 0xFFFFFF);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, panel_y + 10 * scale, "PACK PREVIEW", scale, 0xFFFFFF);
    FillRectAlpha(pixel_buffer, pitch, width, height,
                  left_panel_x + 7 * scale, list_start_y - 1 * scale,
                  left_panel_w - 14 * scale, 11 * scale, 0x2E6F58, 120);
    DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, list_start_y, ">", scale, 0x8FEA7D);
    DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 22 * scale, list_start_y, "SUPER METROID", scale, 0x8FEA7D);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, preview_box_y, "SUPER METROID", scale, 0xFFFFFF);
    snprintf(label, sizeof(label), "LEVELS %03d", BlocksBoxRuntime_GetLevelCount());
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, preview_box_y + 16 * scale, label, scale, 0xC5D0D8);
    snprintf(label, sizeof(label), "TILE SETS %02d", tileset_count);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, preview_box_y + 30 * scale, label, scale, 0xC5D0D8);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, panel_y + panel_h - 14 * scale,
                "B BACK", scale, 0xC5D0D8);
    return;
  }

  if (g_native_level_editor_page == kNativeLevelEditorPage_PackHome) {
    static const char *const kPackItems[2] = { "LEVEL LIST", "TILE SETS" };
    DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, panel_y + 10 * scale, "SUPER METROID", scale, 0xFFFFFF);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, panel_y + 10 * scale, "PACK CONTENTS", scale, 0xFFFFFF);
    for (int i = 0; i < 2; i++) {
      int row_y = list_start_y + i * row_h;
      uint32 text_color = i == g_native_level_editor_pack_menu_selection ? 0x8FEA7D : 0xC5D0D8;
      if (i == g_native_level_editor_pack_menu_selection) {
        FillRectAlpha(pixel_buffer, pitch, width, height,
                      left_panel_x + 7 * scale, row_y - 1 * scale,
                      left_panel_w - 14 * scale, 11 * scale, 0x2E6F58, 120);
        DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, row_y, ">", scale, 0x8FEA7D);
      }
      DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 22 * scale, row_y, kPackItems[i], scale, text_color);
    }
    snprintf(label, sizeof(label), "LEVELS %03d", BlocksBoxRuntime_GetLevelCount());
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, preview_box_y, label, scale, 0xC5D0D8);
    snprintf(label, sizeof(label), "TILE SETS %02d", tileset_count);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, preview_box_y + 14 * scale, label, scale, 0xC5D0D8);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, panel_y + panel_h - 14 * scale,
                "B BACK", scale, 0xC5D0D8);
    return;
  }

  if (g_native_level_editor_page == kNativeLevelEditorPage_LevelList) {
    int level_count = BlocksBoxRuntime_GetLevelCount();
    DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, panel_y + 10 * scale, "LEVEL LIST", scale, 0xFFFFFF);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, panel_y + 10 * scale, "LEVEL DETAILS", scale, 0xFFFFFF);
    if (level_count <= 0) {
      DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, list_start_y, "NO LEVELS FOUND", scale, 0xFFFFFF);
    } else {
      for (int visible_index = 0; visible_index < 10; visible_index++) {
        int level_index = g_native_level_editor_level_scroll + visible_index;
        int row_y = list_start_y + visible_index * row_h;
        uint32 text_color;
        if (level_index >= level_count)
          break;
        snprintf(label, sizeof(label), "LEVEL %03d", level_index + 1);
        text_color = level_index == g_native_level_editor_level_selection ? 0x8FEA7D : 0xC5D0D8;
        if (level_index == g_native_level_editor_level_selection) {
          FillRectAlpha(pixel_buffer, pitch, width, height,
                        left_panel_x + 7 * scale, row_y - 1 * scale,
                        left_panel_w - 14 * scale, 11 * scale, 0x2E6F58, 120);
          DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, row_y, ">", scale, 0x8FEA7D);
        }
        DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 22 * scale, row_y, label, scale, text_color);
      }
      snprintf(label, sizeof(label), "%d/%d", g_native_level_editor_level_selection + 1, level_count);
      DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, panel_y + panel_h - 14 * scale, label, scale, 0xC5D0D8);
      DrawRectOutline(pixel_buffer, pitch, width, height, preview_box_x, preview_box_y, preview_box_w, preview_box_h, IntMax(1, scale), 0x4F6B5C);
      if (g_native_level_editor_preview.pixels != NULL) {
        int max_w = preview_box_w - 10 * scale;
        int max_h = preview_box_h - 10 * scale;
        int draw_w = max_w;
        int draw_h = (g_native_level_editor_preview.height * draw_w) / IntMax(1, g_native_level_editor_preview.width);
        if (draw_h > max_h) {
          draw_h = max_h;
          draw_w = (g_native_level_editor_preview.width * draw_h) / IntMax(1, g_native_level_editor_preview.height);
        }
        DrawScaledPreviewImage(pixel_buffer, pitch, width, height,
                               preview_box_x + (preview_box_w - draw_w) / 2,
                               preview_box_y + (preview_box_h - draw_h) / 2,
                               draw_w, draw_h,
                               g_native_level_editor_preview.pixels,
                               g_native_level_editor_preview.width, g_native_level_editor_preview.height);
      } else {
        DrawText5x7(pixel_buffer, pitch, width, height, preview_box_x + 14 * scale, preview_box_y + 16 * scale, "PREVIEW UNAVAILABLE", scale, 0xFFFFFF);
      }
    }
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, panel_y + panel_h - 14 * scale,
                "B BACK", scale, 0xC5D0D8);
    return;
  }

  DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, panel_y + 10 * scale, "TILE SETS", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, panel_y + 10 * scale, "TILESET PREVIEW", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, panel_y + 20 * scale,
              (game_id && strcmp(game_id, "super_metroid") == 0) ? "SUPER METROID" : "BLOCKSBOX", scale, 0xC5D0D8);

  if (tileset_count <= 0) {
    DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, list_start_y, "NO TILESETS FOUND", scale, 0xFFFFFF);
    DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 18 * scale, preview_box_y + 18 * scale, "IMPORT REQUIRED", scale, 0xFFFFFF);
    return;
  }

  for (int visible_index = 0; visible_index < 10; visible_index++) {
    int tileset_index = g_native_level_editor_scroll + visible_index;
    int row_y = list_start_y + visible_index * row_h;
    uint32 text_color;
    if (tileset_index >= tileset_count)
      break;
    snprintf(label, sizeof(label), "AREA %02d SET %02d",
             GetNativeLevelEditorTilesetAreaIndex(tileset_index),
             GetNativeLevelEditorTilesetGraphicsSet(tileset_index));
    text_color = tileset_index == g_native_level_editor_selection ? 0x8FEA7D : 0xC5D0D8;
    if (tileset_index == g_native_level_editor_selection) {
      FillRectAlpha(pixel_buffer, pitch, width, height,
                    left_panel_x + 7 * scale, row_y - 1 * scale,
                    left_panel_w - 14 * scale, 11 * scale, 0x2E6F58, 120);
      DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, row_y, ">", scale, 0x8FEA7D);
    }
    DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 22 * scale, row_y, label, scale, text_color);
  }

  DrawRectOutline(pixel_buffer, pitch, width, height, preview_box_x, preview_box_y, preview_box_w, preview_box_h, IntMax(1, scale), 0x4F6B5C);
  if (preview_draw_size > 0 && g_native_level_editor_preview.pixels != NULL) {
    int preview_draw_x = preview_box_x + (preview_box_w - preview_draw_size) / 2;
    int preview_draw_y = preview_box_y + (preview_box_h - preview_draw_size) / 2;
    DrawScaledPreviewImage(pixel_buffer, pitch, width, height,
                           preview_draw_x, preview_draw_y, preview_draw_size, preview_draw_size,
                           g_native_level_editor_preview.pixels,
                           g_native_level_editor_preview.width, g_native_level_editor_preview.height);
  } else {
    DrawText5x7(pixel_buffer, pitch, width, height, preview_box_x + 14 * scale, preview_box_y + 16 * scale, "PREVIEW UNAVAILABLE", scale, 0xFFFFFF);
  }

  if (tileset_count > 10) {
    snprintf(label, sizeof(label), "%d/%d", g_native_level_editor_selection + 1, tileset_count);
    DrawText5x7(pixel_buffer, pitch, width, height, left_panel_x + 10 * scale, panel_y + panel_h - 14 * scale, label, scale, 0xC5D0D8);
  }
  DrawText5x7(pixel_buffer, pitch, width, height, right_panel_x + 10 * scale, panel_y + panel_h - 14 * scale,
              "B BACK", scale, 0xC5D0D8);
}

static int GetCurrentVolumePercent(void) {
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  int volume = GetApplicationVolume();
  return volume >= 0 ? volume : 100;
#else
  return (g_sdl_audio_mixer_volume * 100 + SDL_MIX_MAXVOLUME / 2) / SDL_MIX_MAXVOLUME;
#endif
}

static void SetCurrentVolumePercent(int new_volume) {
  new_volume = IntMin(IntMax(0, new_volume), 100);
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  SetApplicationVolume(new_volume);
#else
  g_sdl_audio_mixer_volume = (new_volume * SDL_MIX_MAXVOLUME + 50) / 100;
#endif
  g_config.msuvolume = (uint8)new_volume;
}

static void SaveNativeOptionsConfig(void) {
  FILE *f = fopen("sm.user.ini", "wb");
  if (!f) {
    WidescreenDebugLog("options-save: failed to open sm.user.ini");
    return;
  }

  fprintf(f,
          "!include sm.ini\n"
          "\n"
          "[Graphics]\n"
          "Widescreen16x9 = 0\n"
          "\n"
          "[Sound]\n"
          "MSUVolume = %u\n",
          (unsigned)g_config.msuvolume);
  fclose(f);
  WidescreenDebugLog("options-save: wrote sm.user.ini widescreen=0 msuvolume=%u",
                     (unsigned)g_config.msuvolume);
}

static void DrawNativeOptionsSlider(uint8 *pixel_buffer, size_t pitch, int width, int height,
                                    int x, int y, int w, int scale, int value, bool selected, bool enabled) {
  int slider_h = 6 * scale;
  int fill_w = (w * IntMin(IntMax(value, 0), 100)) / 100;
  int knob_x = x + IntMin(IntMax(fill_w - scale, 0), w - 2 * scale);
  uint32 fill_color = enabled ? 0x8FEA7D : 0x5F7370;
  uint32 outline_color = selected ? 0xFFFFFF : 0xC5D0D8;
  FillRect(pixel_buffer, pitch, width, height, x, y, w, slider_h, 0x2C3440);
  if (fill_w > 0)
    FillRect(pixel_buffer, pitch, width, height, x, y, fill_w, slider_h, fill_color);
  DrawRectOutline(pixel_buffer, pitch, width, height, x, y, w, slider_h, IntMax(1, scale), outline_color);
  FillRect(pixel_buffer, pitch, width, height, knob_x, y - scale, 2 * scale, slider_h + 2 * scale, outline_color);
}

static void RenderNativeOptionsOverlay(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  if (!IsNativeOptionsOverlayVisible())
    return;

  int scale = IntMax(1, height / 240);
  int panel_w = 212 * scale;
  int panel_h = 148 * scale;
  int panel_x = (width - panel_w) / 2;
  int panel_y = (height - panel_h) / 2;
  int label_x = panel_x + 18 * scale;
  int control_x = panel_x + 94 * scale;
  int slider_w = 84 * scale;
  int row_y;
  int volume = GetCurrentVolumePercent();
  char text[16];

  FillRectAlpha(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, 0x101722, 176);
  DrawRectOutline(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, IntMax(1, scale), 0x8FEA7D);

  DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 10 * scale, panel_y + 8 * scale, "OPTIONS", scale, 0xFFFFFF);

  DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 10 * scale, panel_y + 25 * scale, "CONTROLS", scale, 0x8FEA7D);
  row_y = panel_y + 39 * scale;
  if (g_native_options_selection == kNativeOptionsRow_Controls)
    DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 8 * scale, row_y, ">", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, label_x, row_y, "INPUT MAP", scale, 0xC5D0D8);

  DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 10 * scale, panel_y + 58 * scale, "VIDEO", scale, 0x8FEA7D);
  row_y = panel_y + 72 * scale;
  if (g_native_options_selection == kNativeOptionsRow_Aspect)
    DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 8 * scale, row_y, ">", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, label_x, row_y, "ASPECT", scale, 0xC5D0D8);
  snprintf(text, sizeof(text), "<4:3>");
  DrawText5x7(pixel_buffer, pitch, width, height, control_x, row_y, text, scale,
              g_native_options_selection == kNativeOptionsRow_Aspect ? 0xFFFFFF : 0xC5D0D8);

  DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 10 * scale, panel_y + 91 * scale, "AUDIO", scale, 0x8FEA7D);
  row_y = panel_y + 105 * scale;
  if (g_native_options_selection == kNativeOptionsRow_Master)
    DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 8 * scale, row_y, ">", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, label_x, row_y, "MASTER", scale, 0xC5D0D8);
  DrawNativeOptionsSlider(pixel_buffer, pitch, width, height, control_x, row_y + scale, slider_w, scale,
                          volume, g_native_options_selection == kNativeOptionsRow_Master, true);
  snprintf(text, sizeof(text), "%d", volume);
  DrawText5x7(pixel_buffer, pitch, width, height, control_x + 91 * scale, row_y, text, scale, 0xFFFFFF);

  row_y = panel_y + 119 * scale;
  if (g_native_options_selection == kNativeOptionsRow_Bgm)
    DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 8 * scale, row_y, ">", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, label_x, row_y, "BGM", scale, 0x8A949C);
  DrawNativeOptionsSlider(pixel_buffer, pitch, width, height, control_x, row_y + scale, slider_w, scale,
                          100, g_native_options_selection == kNativeOptionsRow_Bgm, false);

  row_y = panel_y + 133 * scale;
  if (g_native_options_selection == kNativeOptionsRow_Sfx)
    DrawText5x7(pixel_buffer, pitch, width, height, panel_x + 8 * scale, row_y, ">", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, label_x, row_y, "SFX", scale, 0x8A949C);
  DrawNativeOptionsSlider(pixel_buffer, pitch, width, height, control_x, row_y + scale, slider_w, scale,
                          100, g_native_options_selection == kNativeOptionsRow_Sfx, false);
}

static void RenderBuildTimestampOverlay(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  static const char kBuildTimestamp[] = __DATE__ " " __TIME__;
  int scale = IntMax(1, height / 240);
  int margin = 4 * scale;
  int text_width = ((int)strlen(kBuildTimestamp) * 6 - 1) * scale;
  int text_height = 7 * scale;
  int panel_x = margin;
  int panel_y = height - text_height - 2 * margin;
  int panel_w = text_width + 2 * margin;
  int panel_h = text_height + 2 * margin;

  FillRectAlpha(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, 0x101722, 144);
  DrawRectOutline(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, IntMax(1, scale), 0x4F6B5C);
  DrawText5x7(pixel_buffer, pitch, width, height, panel_x + margin, panel_y + margin, kBuildTimestamp, scale, 0xC5D0D8);
}

static bool IsGunshipEnemyData(const EnemyData *E) {
  if (!E->enemy_ptr)
    return false;
  EnemyDef *ED = get_EnemyDef_A2(E->enemy_ptr);
  return ED->ai_init == fnGunshipTop_Init || ED->ai_init == fnGunshipBottom_Init;
}

static uint32 NativeRgbFromCgram(Ppu *ppu, uint16 color) {
  uint32 r = color & 0x1f;
  uint32 g = (color >> 5) & 0x1f;
  uint32 b = (color >> 10) & 0x1f;
  return 0xff000000 |
         ppu->brightnessMult[b] |
         (uint32)ppu->brightnessMult[g] << 8 |
         (uint32)ppu->brightnessMult[r] << 16;
}

static int GetGunshipCustomSlotForOamPriority(uint16 oam1) {
  int sprite_priority = (oam1 & 0x3000) >> 12;
  int slot = sprite_priority * 4 + 3;
  return IntMin(slot, kModernFrontCustomLayer - 1);
}

static void RenderNativeSpritePieceLine(Ppu *ppu, int custom_slot, int row_y, uint32 *pixels,
                                        int sprite_x, int sprite_y, bool size_bit, uint16 oam1) {
  int sprite_size = kNativeSpriteSizes[ppu->objSize][size_bit ? 1 : 0];
  if (row_y < sprite_y || row_y >= sprite_y + sprite_size)
    return;
  if (GetGunshipCustomSlotForOamPriority(oam1) != custom_slot)
    return;

  int row = row_y - sprite_y;
  int obj_adr = (oam1 & 0x100) ? ppu->objTileAdr2 : ppu->objTileAdr1;
  bool h_flipped = (oam1 & 0x4000) != 0;
  bool v_flipped = (oam1 & 0x8000) != 0;
  int palette_base = 0x80 + 16 * ((oam1 & 0xe00) >> 9);
  int screen_left = -kPpuExtraLeftRight;
  int screen_right = 256 + kPpuExtraLeftRight;

  if (v_flipped)
    row = sprite_size - 1 - row;

  for (int col = 0; col < sprite_size; col += 8) {
    int tile_x = sprite_x + col;
    int px_left = IntMax(screen_left - tile_x, 0);
    int px_right = IntMin(screen_right - tile_x, 8);
    if (px_left >= px_right)
      continue;

    int used_col = h_flipped ? sprite_size - 1 - col : col;
    int used_tile = ((((oam1 & 0xff) >> 4) + (row >> 3)) << 4) |
                    (((oam1 & 0xf) + (used_col >> 3)) & 0xf);
    uint16 *addr = &ppu->vram[(obj_adr + used_tile * 16 + (row & 0x7)) & 0x7fff];
    uint32 plane = addr[0] | addr[8] << 16;

    for (int px = px_left; px < px_right; px++) {
      int shift = h_flipped ? px : 7 - px;
      uint32 bits = plane >> shift;
      int pixel = (bits >> 0) & 1 | (bits >> 7) & 2 | (bits >> 14) & 4 | (bits >> 21) & 8;
      if (pixel == 0)
        continue;
      int dst_x = tile_x + px + kPpuExtraLeftRight;
      if ((unsigned)dst_x >= kPpuXPixels)
        continue;
      pixels[dst_x] = NativeRgbFromCgram(ppu, ppu->cgram[palette_base + pixel]);
    }
  }
}

static void RenderNativeSpritemapLine(Ppu *ppu, int custom_slot, int row_y, uint32 *pixels,
                                      uint8 db, uint16 spritemap, int base_x, int base_y,
                                      uint16 palette_bits, uint16 tile_base) {
  if (!spritemap)
    return;

  const uint8 *pp = RomPtrWithBank(db, spritemap);
  int n = GET_WORD(pp);
  pp += 2;
  for (; n != 0; n--, pp += 5) {
    int sprite_x = base_x + (int16)GET_WORD(pp);
    int sprite_y = base_y + (int8)pp[2];
    uint16 oam1 = palette_bits | (tile_base + GET_WORD(pp + 3));
    RenderNativeSpritePieceLine(ppu, custom_slot, row_y, pixels, sprite_x, sprite_y,
                                (*(int16 *)pp) < 0, oam1);
  }
}

static void RenderNativeExtendedSpritemapLine(Ppu *ppu, int custom_slot, int row_y, uint32 *pixels,
                                              const EnemyData *E, int x2, int y2,
                                              uint16 palette_bits, uint16 tile_base) {
  int n = *RomPtrWithBank(E->bank, E->spritemap_pointer);
  uint16 ptr = E->spritemap_pointer + 2;
  while (n-- > 0) {
    ExtendedSpriteMap *ext = get_ExtendedSpriteMap(E->bank, ptr);
    if (*(uint16 *)RomPtrWithBank(E->bank, ext->spritemap) != 0xFFFE) {
      RenderNativeSpritemapLine(ppu, custom_slot, row_y, pixels, E->bank, ext->spritemap,
                                x2 + ext->xpos, y2 + ext->ypos, palette_bits, tile_base);
    }
    ptr += 8;
  }
}

static void RenderNativeGunshipEnemyLine(Ppu *ppu, int custom_slot, int y, uint32 *pixels,
                                         const EnemyData *E, const EnemySpawnData *ES) {
  if (!IsGunshipEnemyData(E))
    return;
  if (!E->spritemap_pointer || E->spritemap_pointer == addr_kSpritemap_Nothing_A0)
    return;

  int x2 = ES->xpos2 + E->x_pos - layer1_x_pos;
  int y2 = ES->ypos2 + E->y_pos - layer1_y_pos;
  if (E->shake_timer)
    x2 += ((E->frame_counter & 2) == 0) ? 1 : -1;

  uint16 palette_bits;
  if (E->flash_timer && (random_enemy_counter & 2) != 0)
    palette_bits = 0;
  else if (E->frozen_timer && (E->frozen_timer >= 0x5A || (E->frozen_timer & 2) != 0))
    palette_bits = 3072;
  else
    palette_bits = E->palette_index;

  if ((E->extra_properties & 4) != 0) {
    RenderNativeExtendedSpritemapLine(ppu, custom_slot, y - 1, pixels, E, x2, y2,
                                      palette_bits, E->vram_tiles_index);
  } else {
    RenderNativeSpritemapLine(ppu, custom_slot, y - 1, pixels, E->bank, E->spritemap_pointer,
                              x2, y2, palette_bits, E->vram_tiles_index);
  }
}

static void RenderModernGunshipCustomLayerLine(int custom_slot, int y, uint32 *pixels, int width, int height) {
  if (width != kPpuXPixels || height != kSnesNativeHeight || y <= 0 || y > kSnesNativeHeight)
    return;
  if (!g_modern_layer_renderer)
    return;
  if (custom_slot != 3 && custom_slot != 7 && custom_slot != 11 && custom_slot != 15)
    return;

  Ppu *ppu = g_snes->ppu;
  for (int i = 0; i < num_enemies_in_room; i++) {
    int enemy_index = i * 64;
    RenderNativeGunshipEnemyLine(ppu, custom_slot, y, pixels,
                                 gEnemyData(enemy_index), gEnemySpawnData(enemy_index));
  }
}

static void DrawPoint(uint8 *pixel_buffer, size_t pitch, int width, int height, int x, int y, int size, uint32 color) {
  FillRect(pixel_buffer, pitch, width, height, x - size / 2, y - size / 2, size, size, color);
}

static void DrawLine(uint8 *pixel_buffer, size_t pitch, int width, int height, int x0, int y0, int x1, int y1, int thickness, uint32 color) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (1) {
    DrawPoint(pixel_buffer, pitch, width, height, x0, y0, thickness, color);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void DrawCircleProgress(uint8 *pixel_buffer, size_t pitch, int width, int height, int cx, int cy, int radius, int thickness, float progress) {
  const int segments = 64;
  const int active_segments = (int)(progress * segments + 0.5f);
  for (int i = 0; i < segments; i++) {
    float t = (float)i / segments * 2.0f * (float)M_PI - (float)M_PI * 0.5f;
    int px = cx + (int)lroundf(cosf(t) * radius);
    int py = cy + (int)lroundf(sinf(t) * radius);
    uint32 color = (i < active_segments) ? 0x8FEA7D : 0x3A3A3A;
    FillRect(pixel_buffer, pitch, width, height, px - thickness / 2, py - thickness / 2, thickness, thickness, color);
  }
}

static void RenderOpeningIntroSkipPrompt(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  if (!IsOpeningIntroSkippable())
    return;

  const int scale = IntMax(1, height / 240);
  const int ring_radius = 18 * scale;
  const int ring_thickness = 3 * scale;
  const int margin = 12 * scale;
  const int cx = width - margin - ring_radius;
  const int cy = height - margin - ring_radius;
  const int icon_w = 34 * scale;
  const int icon_h = 12 * scale;
  const int icon_x = cx - icon_w / 2;
  const int icon_y = cy - icon_h / 2;
  const int hold_text_x = cx - 12 * scale;
  const int hold_text_y = icon_y - 10 * scale;
  float progress = (float)g_intro_skip_hold_frames / kOpeningIntroSkipHoldFrames;

  FillRect(pixel_buffer, pitch, width, height, icon_x, icon_y, icon_w, icon_h, 0x111111);
  DrawRectOutline(pixel_buffer, pitch, width, height, icon_x, icon_y, icon_w, icon_h, IntMax(1, scale), 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, hold_text_x, hold_text_y, "HOLD", scale, 0xFFFFFF);
  DrawText5x7(pixel_buffer, pitch, width, height, icon_x + 2 * scale, icon_y + 2 * scale, "START", scale, 0xFFFFFF);
  DrawCircleProgress(pixel_buffer, pitch, width, height, cx, cy, ring_radius, ring_thickness, progress);
}

static void RenderAnalogDebugOverlay(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  int scale = IntMax(1, height / 240);
  int arrow_box = 18 * scale;
  int arrow_cx = width - 14 * scale;
  int arrow_cy = height - 14 * scale;
  const char *mode_text = g_native_level_render_enabled ? "NATIVE" : "SNES";
  int mode_w = 44 * scale;
  int mode_h = 12 * scale;
  int mode_x = arrow_cx - arrow_box / 2 - mode_w - 4 * scale;
  int mode_y = arrow_cy - mode_h / 2;
  uint32 mode_color = g_native_level_render_enabled ? 0x8FEA7D : 0xC5D0D8;
  float aim_x, aim_y;

  FillRectAlpha(pixel_buffer, pitch, width, height, mode_x, mode_y, mode_w, mode_h, 0x101722, 176);
  DrawRectOutline(pixel_buffer, pitch, width, height,
      mode_x, mode_y, mode_w, mode_h, IntMax(1, scale), mode_color);
  DrawText5x7(pixel_buffer, pitch, width, height,
      mode_x + 4 * scale, mode_y + 3 * scale, mode_text, scale, mode_color);

  Samus_GetNormalizedAimDirection(&aim_x, &aim_y);
  DrawRectOutline(pixel_buffer, pitch, width, height,
      arrow_cx - arrow_box / 2, arrow_cy - arrow_box / 2, arrow_box, arrow_box, IntMax(1, scale), 0x7A7A7A);
  DrawLine(pixel_buffer, pitch, width, height,
      arrow_cx - 1, arrow_cy - 1, arrow_cx + 1, arrow_cy + 1, IntMax(1, scale), 0x7A7A7A);
  if (aim_x != 0.0f || aim_y != 0.0f) {
    int shaft = 7 * scale;
    int head = 3 * scale;
    int tip_x = arrow_cx + (int)lroundf(aim_x * shaft);
    int tip_y = arrow_cy + (int)lroundf(aim_y * shaft);
    int back_x = arrow_cx - (int)lroundf(aim_x * (2 * scale));
    int back_y = arrow_cy - (int)lroundf(aim_y * (2 * scale));
    float perp_x = -aim_y;
    float perp_y = aim_x;
    int left_x = tip_x - (int)lroundf(aim_x * head) + (int)lroundf(perp_x * head);
    int left_y = tip_y - (int)lroundf(aim_y * head) + (int)lroundf(perp_y * head);
    int right_x = tip_x - (int)lroundf(aim_x * head) - (int)lroundf(perp_x * head);
    int right_y = tip_y - (int)lroundf(aim_y * head) - (int)lroundf(perp_y * head);
    DrawLine(pixel_buffer, pitch, width, height, back_x, back_y, tip_x, tip_y, IntMax(1, scale), 0x8FEA7D);
    DrawLine(pixel_buffer, pitch, width, height, tip_x, tip_y, left_x, left_y, IntMax(1, scale), 0x8FEA7D);
    DrawLine(pixel_buffer, pitch, width, height, tip_x, tip_y, right_x, right_y, IntMax(1, scale), 0x8FEA7D);
  }
}

static void RenderModernFrontCustomLayer(uint32 *pixels, int width, int height) {
  static int render_front_log_budget = 12;
  if (render_front_log_budget-- > 0)
    WidescreenDebugLog("RenderModernFrontCustomLayer begin: width=%d height=%d active_menu=%d intro_hold=%u",
                       width, height, g_native_main_menu_active ? 1 : 0, g_intro_skip_hold_frames);
  memset(pixels, 0, sizeof(uint32) * width * height);
  RenderOpeningIntroSkipPrompt((uint8 *)pixels, width * sizeof(uint32), width, height);
  RenderAnalogDebugOverlay((uint8 *)pixels, width * sizeof(uint32), width, height);
  RenderNativeMainMenu((uint8 *)pixels, width * sizeof(uint32), width, height);
  RenderNativeFileSelect((uint8 *)pixels, width * sizeof(uint32), width, height);
  RenderNativeLevelEditor((uint8 *)pixels, width * sizeof(uint32), width, height);
  RenderNativeOptionsOverlay((uint8 *)pixels, width * sizeof(uint32), width, height);
  RenderExitToMainMenuPrompt((uint8 *)pixels, width * sizeof(uint32), width, height);
  RenderBuildTimestampOverlay((uint8 *)pixels, width * sizeof(uint32), width, height);
  for (int i = 0; i < width * height; i++) {
    if ((pixels[i] & 0xffffff) != 0 && (pixels[i] & 0xff000000) == 0)
      pixels[i] |= 0xff000000;
  }
  if (render_front_log_budget >= 0)
    WidescreenDebugLog("RenderModernFrontCustomLayer end: width=%d height=%d", width, height);
}

void RtlRenderModernCustomLayer(int custom_slot, int y, uint32 *pixels, int width, int height) {
  if (custom_slot == kModernFrontCustomLayer && width == g_snes_width && height == g_snes_height) {
    if (g_modern_front_custom_layer_size < (size_t)width * height) {
      g_modern_front_custom_layer_size = (size_t)width * height;
      g_modern_front_custom_layer = (uint32 *)realloc(g_modern_front_custom_layer,
                                                      g_modern_front_custom_layer_size * sizeof(uint32));
      if (!g_modern_front_custom_layer)
        Die("Unable to allocate modern front layer");
    }
    if (y <= 1)
      RenderModernFrontCustomLayer(g_modern_front_custom_layer, width, height);
    memcpy(pixels, &g_modern_front_custom_layer[(y - 1) * width], sizeof(uint32) * width);
    return;
  }
  memset(pixels, 0, sizeof(uint32) * width);
  RenderModernGunshipCustomLayerLine(custom_slot, y, pixels, width, height);
}

static uint32 BlendArgbOverBgr(uint32 dst_bgr, uint32 src_argb) {
  uint32 src_a = src_argb >> 24;
  if (src_a == 0)
    return dst_bgr;
  uint32 src_bgr = src_argb & 0xffffff;
  if (src_a == 255)
    return src_bgr;
  uint32 inv_a = 255 - src_a;
  uint32 rb = ((src_bgr & 0xff00ff) * src_a + (dst_bgr & 0xff00ff) * inv_a) >> 8;
  uint32 g = ((src_bgr & 0x00ff00) * src_a + (dst_bgr & 0x00ff00) * inv_a) >> 8;
  return (rb & 0xff00ff) | (g & 0x00ff00);
}

static void CompositeModernFrontCustomLayer(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  static int composite_front_log_budget = 12;
  bool should_log = composite_front_log_budget-- > 0;
  if (should_log)
    WidescreenDebugLog("CompositeModernFrontCustomLayer begin: width=%d height=%d pitch=%zu size=%zu ptr=%p",
                       width, height, pitch, g_modern_front_custom_layer_size, (void *)g_modern_front_custom_layer);
  if (g_modern_front_custom_layer_size < (size_t)width * height) {
    g_modern_front_custom_layer_size = (size_t)width * height;
    g_modern_front_custom_layer = (uint32 *)realloc(g_modern_front_custom_layer,
                                                    g_modern_front_custom_layer_size * sizeof(uint32));
    if (!g_modern_front_custom_layer)
      Die("Unable to allocate modern front layer");
    if (should_log)
      WidescreenDebugLog("CompositeModernFrontCustomLayer allocated: size=%zu ptr=%p",
                         g_modern_front_custom_layer_size, (void *)g_modern_front_custom_layer);
  }
  RenderModernFrontCustomLayer(g_modern_front_custom_layer, width, height);
  if (should_log)
    WidescreenDebugLog("CompositeModernFrontCustomLayer rendered overlay");
  for (int y = 0; y < height; y++) {
    uint32 *dst = (uint32 *)(pixel_buffer + y * pitch);
    uint32 *src = &g_modern_front_custom_layer[y * width];
    for (int x = 0; x < width; x++) {
      if (src[x] != 0)
        dst[x] = BlendArgbOverBgr(dst[x] & 0xffffff, src[x]);
    }
  }
  if (should_log)
    WidescreenDebugLog("CompositeModernFrontCustomLayer end");
}

static uint16 GetInputBitForControlCommand(uint32 j) {
  static const uint8 kKbdRemap[] = { 0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
  return (j >= kKeys_Controls && j <= kKeys_Controls_Last) ? (1 << kKbdRemap[j]) : 0;
}

static bool IsDirectionalControlCommand(uint16 cmd) {
  return cmd >= kKeys_Controls && cmd <= kKeys_Controls + 3;
}

static bool IsGameplayMovementState(void) {
  return game_state == kGameState_7_MainGameplayFadeIn || game_state == kGameState_8_MainGameplay;
}

static uint16 GetNativeMenuInputs(void) {
  uint16 inputs = (uint16)(g_input1_state | g_gamepad_dpad_buttons | GetMenuAnalogDirectionalInputs());

  // Native UI controls keep their own face-button semantics independent from
  // gameplay remaps so every native overlay sees the same actions.
  if (g_gamepad_modifiers & (1u << kGamepadBtn_A))
    inputs |= kInputBit_A;
  if (g_gamepad_modifiers & (1u << kGamepadBtn_B))
    inputs |= kInputBit_B;
  if (g_gamepad_modifiers & (1u << kGamepadBtn_Start))
    inputs |= kInputBit_Start;
  if (g_gamepad_modifiers & (1u << kGamepadBtn_Back))
    inputs |= kInputBit_Select;
  if (g_gamepad_modifiers & (1u << kGamepadBtn_L2))
    inputs |= kInputBit_PageUp;
  if (g_gamepad_modifiers & (1u << kGamepadBtn_R2))
    inputs |= kInputBit_PageDown;
  return inputs;
}

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last) {
    uint16 bit = GetInputBitForControlCommand(j);
    if (pressed)
      g_input1_state |= bit;
    else
      g_input1_state &= ~bit;
    return;
  }

  if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }

  if (!pressed)
    return;
  if (j <= kKeys_Load_Last) {
    RtlSaveLoad(kSaveLoad_Load, j - kKeys_Load);
  } else if (j <= kKeys_Save_Last) {
    RtlSaveLoad(kSaveLoad_Save, j - kKeys_Save);
  } else if (j <= kKeys_Replay_Last) {
    RtlSaveLoad(kSaveLoad_Replay, j - kKeys_Replay);
  } else if (j <= kKeys_LoadRef_Last) {
    RtlSaveLoad(kSaveLoad_Load, 256 + j - kKeys_LoadRef);
  } else if (j <= kKeys_ReplayRef_Last) {
    RtlSaveLoad(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef);
  } else {
    switch (j) {
    case kKeys_CheatLife: RtlCheat('w'); break;
    case kKeys_CheatJump: RtlCheat('q'); break;
    case kKeys_ToggleWhichFrame:
      g_other_image = !g_other_image;
      break;
    case kKeys_ClearKeyLog: RtlClearKeyLog(); break;
    case kKeys_StopReplay: RtlStopReplay(); break;
    case kKeys_Fullscreen:
      g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      g_cursor = !g_cursor;
      SDL_ShowCursor(g_cursor);
      break;
    case kKeys_Reset:
      RtlReset(1);
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed:
      g_paused = !g_paused;
      // SDL_RenderPresent may not be called more than once per frame.
      // Seems to work on Windows still. Temporary measure until it's fixed.
#ifdef _WIN32
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
#endif
      break;
    case kKeys_ReplayTurbo: g_replay_turbo = !g_replay_turbo; break;
    case kKeys_WindowBigger: ChangeWindowScale(1); break;
    case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
    case kKeys_DisplayPerf: g_display_perf ^= 1; break;
    case kKeys_ToggleRenderer:
      g_ppu_render_flags ^= kPpuRenderFlags_NewRenderer;
      g_new_ppu = (g_ppu_render_flags & kPpuRenderFlags_NewRenderer) != 0;
      break;
    case kKeys_ToggleModernLayerRenderer:
      SetModernLayerRendererEnabled(!g_modern_layer_renderer);
      if (g_native_level_render_enabled && !g_modern_layer_renderer)
        g_native_level_render_enabled = false;
      QueueToggleScreenshot(g_modern_layer_renderer ? "toggle_modern_layers_on" : "toggle_modern_layers_off");
      printf("[Modern layer renderer]=%s\n", g_modern_layer_renderer ? "on" : "off");
      break;
    case kKeys_ToggleModernLayerDebug:
      g_modern_layer_debug = !g_modern_layer_debug;
      if (g_modern_layer_debug && !g_modern_layer_renderer)
        SetModernLayerRendererEnabled(true);
      QueueToggleScreenshot(g_modern_layer_debug ? "toggle_false_color_on" : "toggle_false_color_off");
      printf("[Modern layer debug]=%s\n", g_modern_layer_debug ? "on" : "off");
      break;
    case kKeys_Screenshot:
      g_manual_screenshot_requested = true;
      printf("[Debug screenshot queued]\n");
      break;
    case kKeys_VolumeUp:
    case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
    default: assert(0);
    }
  }
}

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  if (pressed && g_title_video.active && !g_title_video.show_menu) {
    SeekTitleVideoPlayback(kTitleVideoLoopFrame);
    g_title_video.show_menu = true;
    g_native_main_menu_active = true;
    g_native_main_menu_dismissed = false;
    g_native_main_menu_prev_inputs = 0;
    ClearTransientMenuInputs();
    return;
  }
  if (pressed && keyCode == SDLK_ESCAPE) {
    if (g_native_options_active) {
      SaveNativeOptionsConfig();
      g_native_options_active = false;
      g_native_main_menu_active = true;
      g_native_options_prev_inputs = 0;
      ClearTransientMenuInputs();
      return;
    }
    if (g_native_file_select_active) {
      CloseNativeFileSelect();
      return;
    }
    if (g_native_level_editor_active) {
      CloseNativeLevelEditor();
      return;
    }
    if (g_native_main_menu_active) {
      g_native_main_menu_quit_requested = true;
      return;
    }
    if (game_state == kGameState_4_FileSelectMenus || game_state == kGameState_5_FileSelectMap) {
      OpenNativeMainMenuScene();
      return;
    }
    if (g_exit_to_main_menu_prompt_active) {
      g_exit_to_main_menu_prompt_active = false;
      g_exit_to_main_menu_prompt_prev_inputs = 0;
    } else {
      ShowExitToMainMenuPrompt();
    }
    return;
  }
  int j = FindCmdForSdlKey(keyCode, (SDL_Keymod)keyMod);
  if (j != 0)
    HandleCommand(j, pressed);
}

static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller)
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
  }
}

static int RemapSdlButton(int button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: return kGamepadBtn_A;
  case SDL_CONTROLLER_BUTTON_B: return kGamepadBtn_B;
  case SDL_CONTROLLER_BUTTON_X: return kGamepadBtn_X;
  case SDL_CONTROLLER_BUTTON_Y: return kGamepadBtn_Y;
  case SDL_CONTROLLER_BUTTON_BACK: return kGamepadBtn_Back;
  case SDL_CONTROLLER_BUTTON_GUIDE: return kGamepadBtn_Guide;
  case SDL_CONTROLLER_BUTTON_START: return kGamepadBtn_Start;
  case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kGamepadBtn_L3;
  case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kGamepadBtn_R3;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kGamepadBtn_L1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kGamepadBtn_R1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return kGamepadBtn_DpadUp;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kGamepadBtn_DpadDown;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kGamepadBtn_DpadLeft;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kGamepadBtn_DpadRight;
  default: return -1;
  }
}

static void HandleGamepadInput(int button, bool pressed) {
  if (!!(g_gamepad_modifiers & (1 << button)) == pressed)
    return;
  g_gamepad_modifiers ^= 1 << button;
  if (pressed)
    g_gamepad_last_cmd[button] = FindCmdForGamepadButton(button, g_gamepad_modifiers);
  if (g_gamepad_last_cmd[button] != 0) {
    if (button >= kGamepadBtn_DpadUp && button <= kGamepadBtn_DpadRight && IsDirectionalControlCommand(g_gamepad_last_cmd[button])) {
      uint16 bit = GetInputBitForControlCommand(g_gamepad_last_cmd[button]);
      if (pressed)
        g_gamepad_dpad_buttons |= bit;
      else
        g_gamepad_dpad_buttons &= ~bit;
      return;
    }
    if (g_gamepad_last_cmd[button] <= kKeys_Controls_Last) {
      uint16 bit = GetInputBitForControlCommand(g_gamepad_last_cmd[button]);
      if (pressed)
        g_gamepad_button_inputs |= bit;
      else
        g_gamepad_button_inputs &= ~bit;
      return;
    }
    HandleCommand(g_gamepad_last_cmd[button], pressed);
  }
}

static void HandleVolumeAdjustment(int volume_adjustment) {
  int current_volume = GetCurrentVolumePercent();
  int new_volume = IntMin(IntMax(0, current_volume + volume_adjustment * 5), 100);
  SetCurrentVolumePercent(new_volume);
  SaveNativeOptionsConfig();
  printf("[Volume]=%i\n", new_volume);
}

static void HandleAspectRatioSelection(int direction) {
  (void)direction;
  g_config.extended_aspect_ratio = 0;
  ApplyRuntimeAspectRatio();
  SaveNativeOptionsConfig();
  printf("[Aspect]=4:3\n");
}

// Approximates atan2(y, x) normalized to the [0,4) range
// with a maximum error of 0.1620 degrees
// normalized_atan(x) ~ (b x + x^2) / (1 + 2 b x + x^2)
static float ApproximateAtan2(float y, float x) {
  uint32 sign_mask = 0x80000000;
  float b = 0.596227f;
  // Extract the sign bits
  uint32 ux_s = sign_mask & *(uint32 *)&x;
  uint32 uy_s = sign_mask & *(uint32 *)&y;
  // Determine the quadrant offset
  float q = (float)((~ux_s & uy_s) >> 29 | ux_s >> 30);
  // Calculate the arctangent in the first quadrant
  float bxy_a = b * x * y;
  if (bxy_a < 0.0f) bxy_a = -bxy_a;  // avoid fabs
  float num = bxy_a + y * y;
  float atan_1q = num / (x * x + bxy_a + num + 0.000001f);
  // Translate it to the proper quadrant
  uint32_t uatan_2q = (ux_s ^ uy_s) | *(uint32 *)&atan_1q;
  return q + *(float *)&uatan_2q;
}

static float NormalizeGamepadAxis(int value) {
  const float raw = value >= 0 ? value / 32767.0f : value / 32768.0f;
  return raw < -1.0f ? -1.0f : (raw > 1.0f ? 1.0f : raw);
}

static void NormalizeStickPosition(int x, int y, float *out_x, float *out_y) {
  const float deadzone = 0.1f;
  const float raw_x = NormalizeGamepadAxis(x);
  const float raw_y = NormalizeGamepadAxis(y);
  const float magnitude = sqrtf(raw_x * raw_x + raw_y * raw_y);
  if (magnitude >= deadzone) {
    const float scaled = fminf((magnitude - deadzone) / (1.0f - deadzone), 1.0f);
    const float inv_magnitude = magnitude > 0.0f ? 1.0f / magnitude : 0.0f;
    *out_x = raw_x * inv_magnitude * scaled;
    *out_y = raw_y * inv_magnitude * scaled;
  } else {
    *out_x = 0.0f;
    *out_y = 0.0f;
  }
}

static void HandleGamepadAxisInput(int gamepad_id, int axis, int value) {
  static int last_gamepad_id, last_left_x, last_left_y, last_right_x, last_right_y;
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY ||
      axis == SDL_CONTROLLER_AXIS_RIGHTX || axis == SDL_CONTROLLER_AXIS_RIGHTY) {
    // ignore other gamepads unless they have a big input
    if (last_gamepad_id != gamepad_id) {
      if (value > -6000 && value < 6000)
        return;
      last_gamepad_id = gamepad_id;
      last_left_x = last_left_y = 0;
      last_right_x = last_right_y = 0;
    }
    if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
      *(axis == SDL_CONTROLLER_AXIS_LEFTX ? &last_left_x : &last_left_y) = value;
      NormalizeStickPosition(last_left_x, last_left_y, &g_left_stick_x, &g_left_stick_y);
    } else {
      *(axis == SDL_CONTROLLER_AXIS_RIGHTX ? &last_right_x : &last_right_y) = value;
      NormalizeStickPosition(last_right_x, last_right_y, &g_right_stick_x, &g_right_stick_y);
    }
    if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
      int buttons = 0;
      if (g_left_stick_x != 0.0f || g_left_stick_y != 0.0f) {
        // in the non deadzone part, divide the circle into eight 45 degree
        // segments rotated by 22.5 degrees that control which direction to move.
        // todo: do this without floats?
        static const uint8 kSegmentToButtons[8] = {
          1 << 4,           // 0 = up
          1 << 4 | 1 << 7,  // 1 = up, right
          1 << 7,           // 2 = right
          1 << 7 | 1 << 5,  // 3 = right, down
          1 << 5,           // 4 = down
          1 << 5 | 1 << 6,  // 5 = down, left
          1 << 6,           // 6 = left
          1 << 6 | 1 << 4,  // 7 = left, up
        };
        uint8 angle = (uint8)(int)(ApproximateAtan2(g_left_stick_y, g_left_stick_x) * 64.0f + 0.5f);
        buttons = kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
      }
      g_gamepad_analog_buttons = buttons;
    }
  } else if ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    if (value < 12000 || value >= 16000)  // hysteresis
      HandleGamepadInput(axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kGamepadBtn_L2 : kGamepadBtn_R2, value >= 12000);
  }
}

// Go some steps up and find sm.ini
static void SwitchDirectory(void) {
  char buf[4096];
  if (!getcwd(buf, sizeof(buf) - 32))
    return;
  size_t pos = strlen(buf);

  for (int step = 0; pos != 0 && step < 3; step++) {
    memcpy(buf + pos, "/sm.ini", 8);
    FILE *f = fopen(buf, "rb");
    if (f) {
      fclose(f);
      buf[pos] = 0;
      if (step != 0) {
        printf("Found sm.ini in %s\n", buf);
        int err = chdir(buf);
        (void)err;
      }
      return;
    }
    pos--;
    while (pos != 0 && buf[pos] != '/' && buf[pos] != '\\')
      pos--;
  }
}
