#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <SDL.h>
#ifdef _WIN32
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

#ifdef __SWITCH__
#include "switch_impl.h"
#endif

static void playAudio(Snes *snes, SDL_AudioDeviceID device, int16_t *audioBuffer);
static void renderScreen(Snes *snes, SDL_Renderer *renderer, SDL_Texture *texture);
static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len);
static void SwitchDirectory();
static void RenderNumber(uint8 *dst, size_t pitch, int n, uint8 big);
static void OpenOneGamepad(int i);
static void HandleVolumeAdjustment(int volume_adjustment);
static void HandleGamepadAxisInput(int gamepad_id, int axis, int value);
static int RemapSdlButton(int button);
static void HandleGamepadInput(int button, bool pressed);
static void HandleInput(int keyCode, int keyMod, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
static uint16 GetInputBitForControlCommand(uint32 j);
static bool IsDirectionalControlCommand(uint16 cmd);
static bool IsGameplayMovementState(void);
static int NormalizeGamepadButtonsForMenus(int inputs);
static void UpdateOpeningIntroSkipState(uint16 inputs);
static void MaybeActivateNativeMainMenu(void);
static void UpdateNativeMainMenu(uint16 inputs);
static uint16 MaskNativeMainMenuInputs(uint16 inputs);
static void RenderNativeMainMenu(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void ShowExitToMainMenuPrompt(void);
static void UpdateExitToMainMenuPrompt(uint16 inputs);
static uint16 MaskExitToMainMenuPromptInputs(uint16 inputs);
static void RenderExitToMainMenuPrompt(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void RenderOpeningIntroSkipPrompt(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void RenderAnalogDebugOverlay(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void RenderModernFrontCustomLayer(uint32 *pixels, int width, int height);
static void CompositeModernFrontCustomLayer(uint8 *pixel_buffer, size_t pitch, int width, int height);
static void SaveDebugScreenshot(uint8 *pixel_buffer, size_t pitch, int width, int height, const char *prefix, int *counter);
static void WidescreenDebugLog(const char *fmt, ...);
void OpenGLRenderer_Create(struct RendererFuncs *funcs);

bool g_debug_flag;
bool g_is_turbo;
bool g_is_turbo;
bool g_want_dump_memmap_flags;
bool g_new_ppu;
bool g_new_ppu = true;
bool g_other_image;
struct SpcPlayer *g_spc_player;
static uint32_t button_state;

static uint8_t g_pixels[kPpuXPixels * 4 * 240];
static uint8_t g_my_pixels[kPpuXPixels * 4 * 240];
static uint32 *g_modern_front_custom_layer;
static size_t g_modern_front_custom_layer_size;
static bool g_shinespark_screenshot_requested;
static bool g_manual_screenshot_requested;
static int g_shinespark_screenshot_counter;
static int g_manual_screenshot_counter;
static int g_startup_screenshot_counter;
static int g_startup_screenshot_remaining = 24;
static int g_startup_screenshot_timer = 1;
static bool g_native_main_menu_active;
static bool g_native_main_menu_dismissed;
static bool g_native_main_menu_return_from_options;
static int g_native_main_menu_selection;
static uint16 g_native_main_menu_prev_inputs;
static bool g_native_main_menu_quit_requested;
static bool g_exit_to_main_menu_prompt_active;
static bool g_exit_to_main_menu_prompt_selection_yes;
static uint16 g_exit_to_main_menu_prompt_prev_inputs;

int g_got_mismatch_count;


enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
  kSnesNativeWidth = 256,
  kSnesNativeHeight = 240,
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
};

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
  if (g_display_perf)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);

  if (!g_modern_layer_renderer) {
    RenderAnalogDebugOverlay(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderOpeningIntroSkipPrompt(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderNativeMainMenu(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderExitToMainMenuPrompt(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
  } else {
    CompositeModernFrontCustomLayer(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
  }
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
  if (g_startup_screenshot_remaining > 0 && --g_startup_screenshot_timer <= 0) {
    g_startup_screenshot_timer = 15;
    g_startup_screenshot_remaining--;
    SaveDebugScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale,
                        "startup", &g_startup_screenshot_counter);
  }

  g_renderer_funcs.EndDraw();
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
  SDL_DestroyRenderer(g_renderer);
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
  ParseConfigFile(config_file);

  g_snes_width = (g_config.extended_aspect_ratio * 2 + kSnesNativeWidth);
  g_snes_height = kSnesNativeHeight;// (g_config.extend_y ? 240 : 224);
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
  const char* filename = argv[0] ? argv[0] : "sm.smc";
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

  bool enable_audio = true;
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

    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    int inputs = g_input1_state | NormalizeGamepadButtonsForMenus(g_gamepad_button_inputs);
    uint8 analog_buttons = g_gamepad_analog_buttons;
    if (g_input1_state & 0xf0)
      analog_buttons = 0;
    inputs |= analog_buttons;
    if (!IsGameplayMovementState())
      inputs |= g_gamepad_dpad_buttons;

    MaybeActivateNativeMainMenu();
    uint8 is_replay = 0;
    if (g_exit_to_main_menu_prompt_active) {
      uint16 game_inputs = MaskExitToMainMenuPromptInputs((uint16)inputs);
      UpdateExitToMainMenuPrompt((uint16)inputs);
      is_replay = RtlRunFrame(game_inputs);
      g_snes->disableRender = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0;
    } else if (g_native_main_menu_active) {
      uint16 game_inputs = MaskNativeMainMenuInputs((uint16)inputs);
      UpdateNativeMainMenu((uint16)inputs);
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
      UpdateOpeningIntroSkipState(inputs);
      if (log_loop)
        WidescreenDebugLog("main-loop RtlRunFrame gameplay: inputs=0x%x", inputs);
      is_replay = RtlRunFrame(inputs);
      if (log_loop)
        WidescreenDebugLog("main-loop RtlRunFrame gameplay done: replay=%u game_state=%u cinematic=%04x",
                           is_replay, game_state, cinematic_function);
      g_snes->disableRender = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0;
    }

    frameCtr++;

    if (!g_snes->disableRender) {
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
  }

  if (g_config.autosave)
    HandleCommand(kKeys_Save + 0, true);

  // clean sdl
  SDL_PauseAudioDevice(g_audio_device, 1);
  SDL_CloseAudioDevice(g_audio_device);
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
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x10, 0x08, 0x04, 0x02, 0x1F } },
    { '3', { 0x1E, 0x10, 0x10, 0x0C, 0x10, 0x10, 0x1E } },
    { '4', { 0x08, 0x0C, 0x0A, 0x09, 0x1F, 0x08, 0x08 } },
    { '5', { 0x1F, 0x01, 0x01, 0x0F, 0x10, 0x10, 0x0F } },
    { '6', { 0x0E, 0x01, 0x01, 0x0F, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x10, 0x08, 0x04, 0x02, 0x02, 0x02 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x0E } },
    { ':', { 0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00 } },
    { '>', { 0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01 } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x0F, 0x11, 0x11, 0x0F, 0x11, 0x11, 0x0F } },
    { 'C', { 0x0E, 0x11, 0x01, 0x01, 0x01, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F } },
    { 'F', { 0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x01 } },
    { 'G', { 0x0E, 0x11, 0x01, 0x1D, 0x11, 0x11, 0x1E } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x13, 0x15, 0x19, 0x11, 0x11, 0x11 } },
    { 'L', { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1F } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01 } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x09, 0x11, 0x11 } },
    { 'S', { 0x1E, 0x01, 0x01, 0x0E, 0x10, 0x10, 0x0F } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
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

static void ClearTransientMenuInputs(void) {
  g_gamepad_analog_buttons = 0;
  g_gamepad_dpad_buttons = 0;
  g_gamepad_modifiers &= ~((1u << kGamepadBtn_DpadUp) | (1u << kGamepadBtn_DpadDown) |
                           (1u << kGamepadBtn_DpadLeft) | (1u << kGamepadBtn_DpadRight));
  for (int button = kGamepadBtn_DpadUp; button <= kGamepadBtn_DpadRight; button++)
    g_gamepad_last_cmd[button] = 0;
  g_native_main_menu_prev_inputs = 0;
  g_exit_to_main_menu_prompt_prev_inputs = 0;
}

static void OpenNativeMainMenuScene(void) {
  game_state = kGameState_1_OpeningCinematic;
  cinematic_function = FUNC16(CinematicFunctionOpening);
  screen_fade_delay = 0;
  screen_fade_counter = 0;
  demo_timer = 900;
  g_skip_menu = false;
  g_native_main_menu_active = false;
  g_native_main_menu_dismissed = false;
  g_native_main_menu_return_from_options = false;
  g_exit_to_main_menu_prompt_active = false;
  ClearTransientMenuInputs();
}

static void MaybeActivateNativeMainMenu(void) {
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

  if (!g_native_main_menu_dismissed && !g_native_main_menu_active &&
      NativeMainMenuCanOpenOnTitleScene()) {
    g_native_main_menu_active = true;
    g_native_main_menu_return_from_options = false;
    ClearTransientMenuInputs();
    demo_timer = 900;
  }
}

void RequestNativeMainMenuFromFileSelect(void) {
  OpenNativeMainMenuScene();
}

static void UpdateNativeMainMenu(uint16 inputs) {
  enum { kNativeMainMenuItemCount = 4 };
  const uint16 confirm_inputs = kInputBit_A | kInputBit_Start;
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
  if ((new_inputs & confirm_inputs) == 0)
    return;

  g_native_main_menu_active = false;
  g_native_main_menu_prev_inputs = 0;
  g_skip_menu = false;
  if (g_native_main_menu_selection == 0) {
    g_native_main_menu_dismissed = true;
    cinematic_function = FUNC16(CinematicFunc_Func10);
    screen_fade_delay = 2;
    screen_fade_counter = 2;
  } else if (g_native_main_menu_selection == 1) {
    g_native_main_menu_return_from_options = true;
    game_state = kGameState_2_GameOptionsMenu;
    game_options_screen_index = 0;
    menu_index = 0;
    screen_fade_delay = 0;
    screen_fade_counter = 0;
  } else {
    if (g_native_main_menu_selection == 3) {
      g_native_main_menu_quit_requested = true;
      return;
    }
    g_native_main_menu_dismissed = true;
    StartDebugScenarioFromMainMenu();
  }
}

static uint16 MaskNativeMainMenuInputs(uint16 inputs) {
  return inputs & ~(uint16)(kInputBit_Up | kInputBit_Down | kInputBit_Left | kInputBit_Right |
                            kInputBit_A | kInputBit_B | kInputBit_Start);
}

static void ShowExitToMainMenuPrompt(void) {
  if (g_native_main_menu_active || g_exit_to_main_menu_prompt_active)
    return;
  g_exit_to_main_menu_prompt_active = true;
  g_exit_to_main_menu_prompt_selection_yes = false;
  g_exit_to_main_menu_prompt_prev_inputs = 0;
  g_gamepad_analog_buttons = 0;
  g_gamepad_dpad_buttons = 0;
}

static void UpdateExitToMainMenuPrompt(uint16 inputs) {
  uint16 new_inputs = inputs & ~g_exit_to_main_menu_prompt_prev_inputs;
  g_exit_to_main_menu_prompt_prev_inputs = inputs;

  if (new_inputs & (kInputBit_Left | kInputBit_Right | kInputBit_Up | kInputBit_Down))
    g_exit_to_main_menu_prompt_selection_yes = !g_exit_to_main_menu_prompt_selection_yes;

  if (new_inputs & (kInputBit_B | kInputBit_Select)) {
    g_exit_to_main_menu_prompt_active = false;
    g_exit_to_main_menu_prompt_prev_inputs = 0;
    return;
  }

  if (new_inputs & (kInputBit_A | kInputBit_Start)) {
    if (g_exit_to_main_menu_prompt_selection_yes)
      OpenNativeMainMenuScene();
    else {
      g_exit_to_main_menu_prompt_active = false;
      g_exit_to_main_menu_prompt_prev_inputs = 0;
    }
  }
}

static uint16 MaskExitToMainMenuPromptInputs(uint16 inputs) {
  return inputs & ~(uint16)(kInputBit_Up | kInputBit_Down | kInputBit_Left | kInputBit_Right |
                            kInputBit_A | kInputBit_B | kInputBit_Start | kInputBit_Select);
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

static void RenderNativeMainMenu(uint8 *pixel_buffer, size_t pitch, int width, int height) {
  static const char *const kMenuItems[4] = { "PLAY", "OPTIONS", "TEST", "EXIT" };
  if (!g_native_main_menu_active)
    return;

  const int scale = IntMax(1, height / 240);
  const int panel_w = 108 * scale;
  const int panel_h = 108 * scale;
  const int panel_x = (width - panel_w) / 2;
  const int panel_y = (height - panel_h) / 2 + 28 * scale;
  const int title_x = panel_x + 18 * scale;
  const int title_y = panel_y + 11 * scale;
  const int item_x = panel_x + 31 * scale;
  const int first_item_y = panel_y + 32 * scale;
  const int item_gap = 16 * scale;
  uint8 border_flicker = (uint8)(190 + ((nmi_frame_counter_word * 17 + (nmi_frame_counter_word >> 2) * 53) & 63));
  uint32 border_color = BlendColorOverBgr(0x1C3A2B, 0x8FEA7D, border_flicker);

  if (g_modern_layer_renderer)
    FillRect(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, 0x9c101722);
  else
    FillRectAlpha(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, 0x101722, 156);
  for (int y = panel_y + 4 * scale + (nmi_frame_counter_word & 3) * scale; y < panel_y + panel_h - 4 * scale; y += 4 * scale) {
    if (g_modern_layer_renderer)
      FillRect(pixel_buffer, pitch, width, height, panel_x + 3 * scale, y, panel_w - 6 * scale, IntMax(1, scale), 0x352D6F58);
    else
      FillRectAlpha(pixel_buffer, pitch, width, height, panel_x + 3 * scale, y, panel_w - 6 * scale, IntMax(1, scale), 0x2D6F58, 53);
  }
  DrawRectOutline(pixel_buffer, pitch, width, height, panel_x, panel_y, panel_w, panel_h, IntMax(1, scale), border_color);
  DrawText5x7(pixel_buffer, pitch, width, height, title_x, title_y, "MAIN MENU", scale, 0xFFFFFF);

  for (int i = 0; i < 4; i++) {
    int y = first_item_y + i * item_gap;
    uint32 text_color = i == g_native_main_menu_selection ? 0x8FEA7D : 0xC5D0D8;
    if (i == g_native_main_menu_selection) {
      DrawText5x7(pixel_buffer, pitch, width, height, item_x - 16 * scale, y, ">", scale, 0x8FEA7D);
      if (g_modern_layer_renderer)
        FillRect(pixel_buffer, pitch, width, height, item_x - 5 * scale, y + 9 * scale, 55 * scale, IntMax(1, scale), 0x8c2E6F58);
      else
        FillRectAlpha(pixel_buffer, pitch, width, height, item_x - 5 * scale, y + 9 * scale, 55 * scale, IntMax(1, scale), 0x2E6F58, 140);
    }
    DrawText5x7(pixel_buffer, pitch, width, height, item_x, y, kMenuItems[i], scale, text_color);
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
  float aim_x, aim_y;

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
  RenderExitToMainMenuPrompt((uint8 *)pixels, width * sizeof(uint32), width, height);
  for (int i = 0; i < width * height; i++) {
    if ((pixels[i] & 0xffffff) != 0 && (pixels[i] & 0xff000000) == 0)
      pixels[i] |= 0xff000000;
  }
  if (render_front_log_budget >= 0)
    WidescreenDebugLog("RenderModernFrontCustomLayer end: width=%d height=%d", width, height);
}

void RtlRenderModernCustomLayer(int custom_slot, int y, uint32 *pixels, int width, int height) {
  if (custom_slot != kModernFrontCustomLayer || width != kSnesNativeWidth || height != kSnesNativeHeight)
    return;
  if (g_snes_width != kSnesNativeWidth) {
    memset(pixels, 0, sizeof(uint32) * width);
    return;
  }
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

static int NormalizeGamepadButtonsForMenus(int inputs) {
  if (IsGameplayMovementState())
    return inputs;

  // Gameplay remaps move jump/fire off face buttons. Menus should still use
  // physical A/B as yes/no, and physical X/Y should not inherit gameplay actions.
  inputs &= ~(kInputBit_A | kInputBit_B | kInputBit_X | kInputBit_Y);
  if (g_gamepad_modifiers & (1 << kGamepadBtn_A))
    inputs |= kInputBit_A;
  if (g_gamepad_modifiers & (1 << kGamepadBtn_B))
    inputs |= kInputBit_B;
  if (g_gamepad_modifiers & (1 << kGamepadBtn_Start))
    inputs |= kInputBit_Start;
  if (g_gamepad_modifiers & (1 << kGamepadBtn_Back))
    inputs |= kInputBit_Select;
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
      g_ppu_render_flags ^= kPpuRenderFlags_ModernLayerRenderer;
      g_modern_layer_renderer = (g_ppu_render_flags & kPpuRenderFlags_ModernLayerRenderer) != 0;
      printf("[Modern layer renderer]=%s\n", g_modern_layer_renderer ? "on" : "off");
      break;
    case kKeys_ToggleModernLayerDebug:
      g_modern_layer_debug = !g_modern_layer_debug;
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
  if (pressed && keyCode == SDLK_ESCAPE) {
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
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  int current_volume = GetApplicationVolume();
  int new_volume = IntMin(IntMax(0, current_volume + volume_adjustment * 5), 100);
  SetApplicationVolume(new_volume);
  printf("[System Volume]=%i\n", new_volume);
#else
  g_sdl_audio_mixer_volume = IntMin(IntMax(0, g_sdl_audio_mixer_volume + volume_adjustment * (SDL_MIX_MAXVOLUME >> 4)), SDL_MIX_MAXVOLUME);
  printf("[SDL mixer volume]=%i\n", g_sdl_audio_mixer_volume);
#endif
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
  const float deadzone = 0.025f;
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
