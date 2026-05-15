#include "sm_rtl.h"
#include "sm_cpu_infra.h"
#include "types.h"
#include "ida_types.h"
#include "variables.h"
#include "funcs.h"
#include "spc_player.h"
#include "util.h"
#include <math.h>

struct StateRecorder;

static void RtlSaveMusicStateToRam_Locked();
static void RtlRestoreMusicAfterLoad_Locked(bool is_reset);

uint8 g_ram[0x20000];
uint8 *g_sram;
const uint8 *g_rom;
bool g_skip_menu = true;
float g_left_stick_x;
float g_left_stick_y;
float g_right_stick_x;
float g_right_stick_y;

static uint8 *g_rtl_memory_ptr;
static RunFrameFunc *g_rtl_runframe;
static SyncAllFunc *g_rtl_syncall;
static int g_aim_buttons_frame = -1;
static uint16 g_aim_buttons_held;
static uint16 g_aim_buttons_prev;

float Samus_GetLeftStickMagnitude(void) {
  float magnitude = sqrtf(g_left_stick_x * g_left_stick_x + g_left_stick_y * g_left_stick_y);
  return magnitude > 1.0f ? 1.0f : magnitude;
}

float Samus_GetHorizontalInputMagnitude(void) {
  float magnitude = fabsf(g_left_stick_x);
  if (magnitude > 0.0001f)
    return magnitude > 1.0f ? 1.0f : magnitude;
  return (joypad1_lastkeys & (kButton_Left | kButton_Right)) != 0 ? 1.0f : 0.0f;
}

float Samus_GetShapedHorizontalInputMagnitude(void) {
  float magnitude = Samus_GetHorizontalInputMagnitude();
  return magnitude * magnitude;
}

static float Samus_GetRightStickMagnitude(void) {
  float magnitude = sqrtf(g_right_stick_x * g_right_stick_x + g_right_stick_y * g_right_stick_y);
  return magnitude > 1.0f ? 1.0f : magnitude;
}

float Samus_GetAimInputMagnitude(void) {
  return Samus_GetRightStickMagnitude();
}

bool Samus_IsAiming(void) {
  return Samus_GetAimInputMagnitude() > 0.25f;
}

int Samus_GetMovementDirectionSign(void) {
  if (g_left_stick_x >= 0.1f)
    return 1;
  if (g_left_stick_x <= -0.1f)
    return -1;
  if (joypad1_lastkeys & kButton_Right)
    return 1;
  if (joypad1_lastkeys & kButton_Left)
    return -1;
  return 0;
}

void Samus_GetNormalizedAimDirection(float *out_x, float *out_y) {
  float magnitude = Samus_GetRightStickMagnitude();
  if (magnitude > 0.25f) {
    *out_x = g_right_stick_x / magnitude;
    *out_y = g_right_stick_y / magnitude;
    return;
  }
  magnitude = Samus_GetLeftStickMagnitude();
  if (magnitude > 0.0f) {
    *out_x = g_left_stick_x / magnitude;
    *out_y = g_left_stick_y / magnitude;
    return;
  }
  *out_x = (samus_pose_x_dir == 4) ? -1.0f : 1.0f;
  *out_y = 0.0f;
}

uint8 Samus_GetDiscreteAimDirection(void) {
  static const uint8 kSegmentToProjectileDir[8] = {
    0, 1, 2, 3, 4, 6, 7, 8,
  };
  float aim_x = 0.0f, aim_y = 0.0f;
  Samus_GetNormalizedAimDirection(&aim_x, &aim_y);
  uint8 angle = (uint8)(int)(((atan2f(aim_y, aim_x) / (2.0f * (float)M_PI)) * 256.0f) + 0.5f);
  return kSegmentToProjectileDir[(uint8)(angle + 16 + 64) >> 5];
}

static int32 Samus_GetProjectileInheritanceComponent(uint16 positive_hi, uint16 positive_lo,
                                                     uint16 negative_hi, uint16 negative_lo) {
  if (positive_hi || positive_lo)
    return IPAIR32(positive_hi, positive_lo);
  if (negative_hi || negative_lo)
    return IPAIR32(negative_hi, negative_lo);
  return 0;
}

int16 Samus_GetProjectileInheritanceX(void) {
  return (int16)(Samus_GetProjectileInheritanceComponent(
      projectile_init_speed_samus_moved_right, projectile_init_speed_samus_moved_right_fract,
      projectile_init_speed_samus_moved_left, projectile_init_speed_samus_moved_left_fract) >> 8);
}

int16 Samus_GetProjectileInheritanceY(void) {
  return (int16)(Samus_GetProjectileInheritanceComponent(
      projectile_init_speed_samus_moved_down, projectile_init_speed_samus_moved_down_fract,
      projectile_init_speed_samus_moved_up, projectile_init_speed_samus_moved_up_fract) >> 8);
}

static uint16 ComputeAimButtons(void) {
  static const uint16 kSegmentToButtons[8] = {
    kButton_Up,
    kButton_Up | kButton_Right,
    kButton_Right,
    kButton_Down | kButton_Right,
    kButton_Down,
    kButton_Down | kButton_Left,
    kButton_Left,
    kButton_Up | kButton_Left,
  };
  float aim_x = 0.0f, aim_y = 0.0f;
  if (Samus_IsAiming()) {
    float magnitude = Samus_GetAimInputMagnitude();
    aim_x = g_right_stick_x / magnitude;
    aim_y = g_right_stick_y / magnitude;
  } else {
    float magnitude = Samus_GetLeftStickMagnitude();
    if (magnitude > 0.0f) {
      aim_x = g_left_stick_x / magnitude;
      aim_y = g_left_stick_y / magnitude;
    }
  }
  if (aim_x == 0.0f && aim_y == 0.0f)
    return 0;
  uint8 angle = (uint8)(int)(((atan2f(aim_y, aim_x) / (2.0f * (float)M_PI)) * 256.0f) + 0.5f);
  return kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
}

static void UpdateAimButtonsCache(void) {
  if (g_aim_buttons_frame == snes_frame_counter)
    return;
  g_aim_buttons_frame = snes_frame_counter;
  g_aim_buttons_prev = g_aim_buttons_held;
  g_aim_buttons_held = ComputeAimButtons();
}

uint16 Samus_GetAimButtonsHeld(void) {
  UpdateAimButtonsCache();
  return g_aim_buttons_held;
}

uint16 Samus_GetAimButtonsPressed(void) {
  UpdateAimButtonsCache();
  return g_aim_buttons_held & ~g_aim_buttons_prev;
}

bool Samus_HasHorizontalMovementInput(void) {
  return Samus_GetHorizontalInputMagnitude() >= 0.1f;
}

bool Samus_ShouldTreatRunButtonAsHeld(void) {
  if (Samus_GetHorizontalInputMagnitude() >= 0.1f)
    return true;
  return samus_x_extra_run_speed != 0 || samus_x_extra_run_subspeed != 0 ||
         samus_x_base_speed >= 1 || samus_total_x_speed >= 1;
}

void RtlSetupEmuCallbacks(uint8 *emu_ram, RunFrameFunc *func, SyncAllFunc *sync_all) {
  g_rtl_memory_ptr = emu_ram;
  g_rtl_runframe = func;
  g_rtl_syncall = sync_all;
}

static void RtlSynchronizeWholeState(void) {
  if (g_rtl_syncall)
    g_rtl_syncall();
}

// |ptr| must be a pointer into g_ram, will synchronize the RAM memory with the
// emulator.
static void RtlSyncMemoryRegion(void *ptr, size_t n) {
  uint8 *data = (uint8 *)ptr;
  assert(data >= g_ram && data < g_ram + 0x20000);
  if (g_rtl_memory_ptr)
    memcpy(g_rtl_memory_ptr + (data - g_ram), data, n);
}

void ByteArray_AppendVl(ByteArray *arr, uint32 v) {
  for (; v >= 255; v -= 255)
    ByteArray_AppendByte(arr, 255);
  ByteArray_AppendByte(arr, v);
}

void saveFunc(void *ctx_in, void *data, size_t data_size) {
  ByteArray_AppendData((ByteArray *)ctx_in, (uint8*)data, data_size);
}

typedef struct LoadFuncState {
  uint8 *p, *pend;
} LoadFuncState;

void loadFunc(void *ctx, void *data, size_t data_size) {
  LoadFuncState *st = (LoadFuncState *)ctx;
  assert((size_t)(st->pend - st->p) >= data_size);
  memcpy(data, st->p, data_size);
  st->p += data_size;
}
static void LoadSnesState(SaveLoadFunc *func, void *ctx) {
  // Do the actual loading
  snes_saveload(g_snes, func, ctx);

  g_snes->cpu->e = false;
  uint32 next = (g_snes->ram[g_snes->cpu->sp + 3] | g_snes->ram[g_snes->cpu->sp + 4] << 8 | g_snes->ram[g_snes->cpu->sp + 5] << 16) + 1;
  if (next == 0x82897e) {
    g_snes->ram[g_snes->cpu->sp + 3] = (0xF71B - 1) & 0xff;
    g_snes->ram[g_snes->cpu->sp + 4] = (0xF71B - 1) >> 8;
  }
  RtlSynchronizeWholeState();
}

static void SaveSnesState(SaveLoadFunc *func, void *ctx) {
  snes_saveload(g_snes, func, ctx);
}

typedef struct StateRecorder {
  uint16 last_inputs;
  uint32 frames_since_last;
  uint32 total_frames;

  // For replay
  uint32 replay_pos, replay_pos_last_complete;
  uint32 replay_frame_counter;
  uint32 replay_next_cmd_at;
  uint32 snapshot_flags;
  uint8 replay_cmd;
  bool replay_mode;

  ByteArray log;
  ByteArray base_snapshot;
} StateRecorder;

static StateRecorder state_recorder;

void StateRecorder_Init(StateRecorder *sr) {
  ByteArray_Destroy(&sr->log);
  ByteArray_Destroy(&sr->base_snapshot);
  memset(sr, 0, sizeof(*sr));
}

void StateRecorder_RecordCmd(StateRecorder *sr, uint8 cmd) {
  int frames = sr->frames_since_last;
  sr->frames_since_last = 0;
  int x = (cmd < 0xc0) ? 0xf : 0x1;
  ByteArray_AppendByte(&sr->log, cmd | (frames < x ? frames : x));
  if (frames >= x)
    ByteArray_AppendVl(&sr->log, frames - x);
}

void StateRecorder_Record(StateRecorder *sr, uint16 inputs) {
  uint16 diff = inputs ^ sr->last_inputs;
  if (diff != 0) {
    sr->last_inputs = inputs;
    //    printf("0x%.4x %d: ", diff, sr->frames_since_last);
    //    size_t lb = sr->log.size;
    for (int i = 0; i < 12; i++) {
      if ((diff >> i) & 1)
        StateRecorder_RecordCmd(sr, i << 4);
    }
    //    while (lb < sr->log.size)
    //      printf("%.2x ", sr->log.data[lb++]);
    //    printf("\n");
  }
  sr->frames_since_last++;
  sr->total_frames++;
}

void StateRecorder_RecordPatchByte(StateRecorder *sr, uint32 addr, const uint8 *value, int num) {
  assert(addr < 0x20000);

//  printf("%d: PatchByte(0x%x, 0x%x. %d): ", sr->frames_since_last, addr, *value, num);
  size_t lb = sr->log.size;
  int lq = (num - 1) <= 3 ? (num - 1) : 3;
  StateRecorder_RecordCmd(sr, 0xc0 | (addr & 0x10000 ? 2 : 0) | lq << 2);
  if (lq == 3)
    ByteArray_AppendVl(&sr->log, num - 1 - 3);
  ByteArray_AppendByte(&sr->log, addr >> 8);
  ByteArray_AppendByte(&sr->log, addr);
  for (int i = 0; i < num; i++)
    ByteArray_AppendByte(&sr->log, value[i]);
//    while (lb < sr->log.size)
//      printf("%.2x ", sr->log.data[lb++]);
//    printf("\n");
}

void ReadFromFile(FILE *f, void *data, size_t n) {
  if (fread(data, 1, n, f) != n)
    Die("fread failed\n");
}

void RtlReset(int mode) {
  snes_frame_counter = 0;
  snes_reset(g_snes, true);
  if (!(mode & 1))
    memset(g_sram, 0, 0x2000);
  
  coroutine_state_0 = 1;
    
  RtlApuLock();
  RtlRestoreMusicAfterLoad_Locked(true);
  RtlApuUnlock();

  RtlSynchronizeWholeState();

  if ((mode & 2) == 0)
    StateRecorder_Init(&state_recorder);
}

int GetFileSize(FILE *f) {
  fseek(f, 0, SEEK_END);
  int r = ftell(f);
  fseek(f, 0, SEEK_SET);
  return r;
}

void StateRecorder_Load(StateRecorder *sr, FILE *f, bool replay_mode) {
  uint32 hdr[16] = { 0 };

  bool is_old = false;
  bool is_reset = false;

  ReadFromFile(f, hdr, 8 * sizeof(uint32));
  if (hdr[0] != 2) {
    hdr[8] = hdr[7];
    hdr[7] = hdr[5] >> 1;
    hdr[5] = (hdr[5] & 1) ? hdr[6] : 0;
  } else if (hdr[0] == 2) {
    ReadFromFile(f, hdr + 8, 8 * sizeof(uint32));

  } else {
    assert(0);
  }

  sr->total_frames = hdr[1];
  ByteArray_Resize(&sr->log, hdr[2]);
  ReadFromFile(f, sr->log.data, sr->log.size);
  sr->last_inputs = hdr[3];
  sr->frames_since_last = hdr[4];

  ByteArray_Resize(&sr->base_snapshot, hdr[5]);
  ReadFromFile(f, sr->base_snapshot.data, sr->base_snapshot.size);

  sr->snapshot_flags = hdr[9];
  sr->replay_next_cmd_at = 0;
  sr->replay_mode = replay_mode;
  if (replay_mode) {
    sr->frames_since_last = 0;
    sr->last_inputs = 0;
    sr->replay_pos = sr->replay_pos_last_complete = 0;
    sr->replay_frame_counter = 0;
    // Load snapshot from |base_snapshot_|, or reset if empty.
    if (sr->base_snapshot.size > 8192 ) {
      LoadFuncState state = { sr->base_snapshot.data, sr->base_snapshot.data + sr->base_snapshot.size };
      LoadSnesState(&loadFunc, &state);
      assert(state.p == state.pend);
    } else {
      RtlReset(2);
      if (sr->base_snapshot.size == 8192)
        memcpy(g_sram, sr->base_snapshot.data, 8192);
      is_reset = true;
    }
  } else {
    // Resume replay from the saved position?
    sr->replay_pos = sr->replay_pos_last_complete = hdr[7];
    sr->replay_frame_counter = hdr[8];
    sr->replay_mode = (sr->replay_frame_counter != 0);

    assert(hdr[6] == 275493);

    ByteArray arr = { 0 };
    ByteArray_Resize(&arr, hdr[6]);
    ReadFromFile(f, arr.data, arr.size);
    LoadFuncState state = { arr.data, arr.data + arr.size };
    LoadSnesState(&loadFunc, &state);
    ByteArray_Destroy(&arr);
    assert(state.p == state.pend);

    if (is_old)
      RtlClearKeyLog();
  }

  if (!is_reset)
    RtlRestoreMusicAfterLoad_Locked(false);

  RtlUpdateSnesPatchForBugfix();

  // Temporarily fix reset state
//  if (g_snes->cpu->k == 0x82 && g_snes->cpu->pc == 0xf716)
//    g_snes->cpu->pc = 0xf71c;
}

void StateRecorder_Save(StateRecorder *sr, FILE *f, bool saving_with_bug) {
  uint32 hdr[16] = { 0 };
  ByteArray arr = { 0 };
  SaveSnesState(&saveFunc, &arr);
  assert(sr->base_snapshot.size == 0 || sr->base_snapshot.size == arr.size || sr->base_snapshot.size == 8192);

  hdr[0] = 2;
  hdr[1] = sr->total_frames;
  hdr[2] = (uint32)sr->log.size;
  hdr[3] = sr->last_inputs;
  hdr[4] = sr->frames_since_last;
  hdr[5] = (uint32)sr->base_snapshot.size;
  hdr[6] = (uint32)arr.size;
  // If saving while in replay mode, also need to persist
  // sr->replay_pos_last_complete and sr->replay_frame_counter
  // so the replaying can be resumed.
  if (sr->replay_mode) {
    hdr[7] = sr->replay_pos_last_complete;
    hdr[8] = sr->replay_frame_counter;
  }
  hdr[9] = saving_with_bug * 1;
  fwrite(hdr, 1, sizeof(hdr), f);
  fwrite(sr->log.data, 1, sr->log.size, f);
  fwrite(sr->base_snapshot.data, 1, sr->base_snapshot.size, f);
  fwrite(arr.data, 1, arr.size, f);

  ByteArray_Destroy(&arr);
}

void StateRecorder_ClearKeyLog(StateRecorder *sr) {
  printf("Clearing key log!\n");
  sr->base_snapshot.size = 0;
  SaveSnesState(&saveFunc, &sr->base_snapshot);
  ByteArray old_log = sr->log;
  int old_frames_since_last = sr->frames_since_last;
  memset(&sr->log, 0, sizeof(sr->log));
  // If there are currently any active inputs, record them initially at timestamp 0.
  sr->frames_since_last = 0;
  if (sr->last_inputs) {
    for (int i = 0; i < 12; i++) {
      if ((sr->last_inputs >> i) & 1)
        StateRecorder_RecordCmd(sr, i << 4);
    }
  }
  if (sr->replay_mode) {
    // When clearing the key log while in replay mode, we want to keep
    // replaying but discarding all key history up until this point.
    if (sr->replay_next_cmd_at != 0xffffffff) {
      sr->replay_next_cmd_at -= old_frames_since_last;
      sr->frames_since_last = sr->replay_next_cmd_at;
      sr->replay_pos_last_complete = (uint32)sr->log.size;
      StateRecorder_RecordCmd(sr, sr->replay_cmd);
      int old_replay_pos = sr->replay_pos;
      sr->replay_pos = (uint32)sr->log.size;
      ByteArray_AppendData(&sr->log, old_log.data + old_replay_pos, old_log.size - old_replay_pos);
    }
    sr->total_frames -= sr->replay_frame_counter;
    sr->replay_frame_counter = 0;
  } else {
    sr->total_frames = 0;
  }
  ByteArray_Destroy(&old_log);
  sr->frames_since_last = 0;
}

uint16 StateRecorder_ReadNextReplayState(StateRecorder *sr) {
  assert(sr->replay_mode);
  while (sr->frames_since_last >= sr->replay_next_cmd_at) {
    int replay_pos = sr->replay_pos;
    if (replay_pos != sr->replay_pos_last_complete) {
      // Apply next command
      sr->frames_since_last = 0;
      if (sr->replay_cmd < 0xc0) {
        sr->last_inputs ^= 1 << (sr->replay_cmd >> 4);
      } else if (sr->replay_cmd < 0xd0) {
        int nb = 1 + ((sr->replay_cmd >> 2) & 3);
        uint8 t;
        if (nb == 4) do {
          nb += t = sr->log.data[replay_pos++];
        } while (t == 255);
        uint32 addr = ((sr->replay_cmd >> 1) & 1) << 16;
        addr |= sr->log.data[replay_pos++] << 8;
        addr |= sr->log.data[replay_pos++];
        do {
          g_ram[addr & 0x1ffff] = sr->log.data[replay_pos++];
          RtlSyncMemoryRegion(&g_ram[addr & 0x1ffff], 1);
        } while (addr++, --nb);
      } else {
        assert(0);
      }
    }
    sr->replay_pos_last_complete = replay_pos;
    if (replay_pos >= sr->log.size) {
      sr->replay_pos = replay_pos;
      sr->replay_next_cmd_at = 0xffffffff;
      break;
    }
    // Read the next one
    uint8 cmd = sr->log.data[replay_pos++], t;
    int mask = (cmd < 0xc0) ? 0xf : 0x1;
    int frames = cmd & mask;
    if (frames == mask) do {
      frames += t = sr->log.data[replay_pos++];
    } while (t == 255);
    sr->replay_next_cmd_at = frames;
    sr->replay_cmd = cmd;
    sr->replay_pos = replay_pos;
  }
  sr->frames_since_last++;
  // Turn off replay mode after we reached the final frame position
  if (++sr->replay_frame_counter >= sr->total_frames) {
    sr->replay_mode = false;
  }
  return sr->last_inputs;
}

void StateRecorder_StopReplay(StateRecorder *sr) {
  if (!sr->replay_mode)
    return;
  sr->replay_mode = false;
  sr->total_frames = sr->replay_frame_counter;
  sr->log.size = sr->replay_pos_last_complete;
}


void RtlClearKeyLog(void) {
  StateRecorder_ClearKeyLog(&state_recorder);
}

void RtlStopReplay(void) {
  StateRecorder_StopReplay(&state_recorder);
}

enum {
  // Version was bumped to 1 after I fixed bug #1
  kCurrentBugFixCounter = 1,
};

bool RtlRunFrame(int inputs) {
  // Avoid up/down and left/right from being pressed at the same time
  if ((inputs & 0x30) == 0x30) inputs ^= 0x30;
  if ((inputs & 0xc0) == 0xc0) inputs ^= 0xc0;

  bool is_replay = state_recorder.replay_mode;

  // Either copy state or apply state
  if (is_replay) {
    inputs = StateRecorder_ReadNextReplayState(&state_recorder);
  } else {
    // Loading a bug snapshot?
    if (state_recorder.snapshot_flags & 1) {
      state_recorder.snapshot_flags &= ~1;
      inputs = state_recorder.last_inputs;
    } else {
      if (bug_fix_counter != kCurrentBugFixCounter) {
        if (g_debug_flag)
          printf("bug_fix_counter %d => %d\n", bug_fix_counter, kCurrentBugFixCounter);
        if (bug_fix_counter < kCurrentBugFixCounter) {
          bug_fix_counter = kCurrentBugFixCounter;
          StateRecorder_RecordPatchByte(&state_recorder, (uint8 *)&bug_fix_counter - g_ram, (uint8 *)&bug_fix_counter, 2);
        }
      }
    }

    StateRecorder_Record(&state_recorder, inputs);
  }

  if (bug_fix_counter != currently_installed_bug_fix_counter)
    RtlUpdateSnesPatchForBugfix();

  g_rtl_runframe(inputs, 0);

  snes_frame_counter++;

  RtlPushApuState();
  return is_replay;
}

void RtlSaveSnapshot(const char *filename, bool saving_with_bug) {
  FILE *f = fopen(filename, "wb");
  RtlApuLock();
  RtlSaveMusicStateToRam_Locked();
  StateRecorder_Save(&state_recorder, f, saving_with_bug);
  RtlApuUnlock();
  fclose(f);
}

static const char *const kBugSaves[] = {
  "Before Kraid",
  "Before Golden Torizo", "After Crocomire", "Baby Metroid", "Tourian Statue", "Before Ridley", "Enter Mother Brain",
};

void RtlSaveLoad(int cmd, int slot) {
  char name[128];
  if (slot >= 256) {
    int i = slot - 256;
    if (cmd == kSaveLoad_Save || i >= sizeof(kBugSaves) / sizeof(kBugSaves[0]))
      return;
    sprintf(name, "saves/%s.sav", kBugSaves[i]);
  } else {
    sprintf(name, "saves/save%d.sav", slot);
  }
  printf("*** %s slot %d\n",
    cmd == kSaveLoad_Save ? "Saving" : cmd == kSaveLoad_Load ? "Loading" : "Replaying", slot);
  if (cmd != kSaveLoad_Save) {

    FILE *f = fopen(name, "rb");
    if (f == NULL) {
      printf("Failed fopen: %s\n", name);
      return;
    }
    RtlApuLock();
    StateRecorder_Load(&state_recorder, f, cmd == kSaveLoad_Replay);
    ppu_copy(g_snes->my_ppu, g_snes->ppu);
    RtlApuUnlock();
    RtlSynchronizeWholeState();
    fclose(f);

    if (coroutine_state_0 | coroutine_state_1 | coroutine_state_2 | coroutine_state_3 | coroutine_state_4) {
      printf("Coroutine state: %d, %d, %d, %d, %d\n",
        coroutine_state_0, coroutine_state_1, coroutine_state_2, coroutine_state_3, coroutine_state_4);
    }

    // Earlier versions used coroutine_state_0 differently
    if (coroutine_state_0 == 4)
      coroutine_state_0 = 10 + game_state;

    // bug_fix_counter_BAD didn't actually belong to free ram...
    if (bug_fix_counter == 0)
      bug_fix_counter = bug_fix_counter_BAD;

  } else {
    RtlSaveSnapshot(name, false);
  }
}

void MemCpy(void *dst, const void *src, int size) {
  memcpy(dst, src, size);
}

PairU16 MakePairU16(uint16 k, uint16 j) {
  PairU16 r = { k, j };
  return r;
}

void mov24(struct LongPtr *a, uint32 d) {
  a->addr = d & 0xffff;
  a->bank = d >> 16;
}

uint32 Load24(const LongPtr *src) {
  return *(uint32 *)src & 0xffffff;
}

bool Unreachable(void) {
  printf("Unreachable!\n");
  assert(0);
  g_ram[0x1ffff] = 1;
  return false;
}

const uint8 *RomPtr(uint32_t addr) {
  if (!(addr & 0x8000)) {
    printf("RomPtr - Invalid access 0x%x!\n", addr);
    g_fail = true;
  }
  return &g_rom[(((addr >> 16) << 15) | (addr & 0x7fff)) & 0x3fffff];
}

void WriteReg(uint16 reg, uint8 value) {
  snes_write(g_snes, reg, value);
}

uint16 Mult8x8(uint8 a, uint8 b) {
  return a * b;
}

uint16 SnesDivide(uint16 a, uint8 b) {
  return (b == 0) ? 0xffff : a / b;
}

uint16 SnesModulus(uint16 a, uint8 b) {
  return (b == 0) ? a : a % b;
}


uint8 ReadReg(uint16 reg) {
  return snes_read(g_snes, reg);
}

uint16 ReadRegWord(uint16 reg) {
  uint16_t rv = ReadReg(reg);
  rv |= ReadReg(reg + 1) << 8;
  return rv;
}

void WriteRegWord(uint16 reg, uint16 value) {
  WriteReg(reg, (uint8)value);
  WriteReg(reg + 1, value >> 8);
}

// Maintain a queue cause the snes and audio callback are not in sync.
// If an entry is 255, it means unset.
typedef struct ApuWriteEnt {
  uint8 ports[4];
} ApuWriteEnt;

enum {
  kApuMaxQueueSize = 16,
};

static struct ApuWriteEnt g_apu_write_ents[kApuMaxQueueSize], g_apu_write;
static uint8 g_apu_write_ent_pos, g_apu_queue_size, g_apu_time_since_empty;

void RtlApuWrite(uint32 adr, uint8 val) {
  assert(adr >= APUI00 && adr <= APUI03);

  if (is_uploading_apu) {
    snes_catchupApu(g_snes); // catch up the apu before writing
    g_snes->apu->inPorts[adr & 0x3] = val;
    return;
  }

  if (g_snes->runningWhichVersion != 2) {
    g_apu_write.ports[adr & 0x3] = val;
  }
}

static bool IsFrameEmpty(ApuWriteEnt *w) {
  return (w->ports[0] == 255) && (w->ports[1] == 255) && (w->ports[2] == 255) && (w->ports[3] == 255);
}

void RtlPushApuState(void) {
  RtlApuLock();
  if (!is_uploading_apu) {
    // Strive for the queue to be empty.
    if (g_apu_queue_size == 0) {
        g_apu_time_since_empty = 0;
    } else {
      if (g_apu_time_since_empty >= 32 && IsFrameEmpty(&g_apu_write)) {
        g_apu_time_since_empty -= 4;
        RtlApuUnlock();
        return;
      }
      g_apu_time_since_empty++;
    }

    // Merge the two oldest to make space
    ApuWriteEnt *w0 = &g_apu_write_ents[g_apu_write_ent_pos++ & (kApuMaxQueueSize - 1)];
    if (g_apu_queue_size == kApuMaxQueueSize) {
      ApuWriteEnt *w1 = &g_apu_write_ents[g_apu_write_ent_pos & (kApuMaxQueueSize - 1)];
      for (int i = 0; i < 4; i++)
        if (w1->ports[i] == 255)
          w1->ports[i] = w0->ports[i];
    } else {
      g_apu_queue_size++;
    }
    *w0 = g_apu_write;
    memset(&g_apu_write, 0xff, sizeof(g_apu_write));
  } else {
    g_apu_queue_size = 0;
  }
  RtlApuUnlock();
}

static void RtlPopApuState_Locked(void) {
  if (is_uploading_apu)
    return;

  uint8 *input_ports = g_use_my_apu_code ? g_spc_player->input_ports : g_snes->apu->inPorts;
  if (g_apu_queue_size != 0) {
    ApuWriteEnt *w = &g_apu_write_ents[(g_apu_write_ent_pos - g_apu_queue_size--) & (kApuMaxQueueSize - 1)];
    for (int i = 0; i != 4; i++) {
      if (w->ports[i] != 255)
        input_ports[i] = w->ports[i];
    }
  }
}

static void RtlResetApuQueue(void) {
  g_apu_write_ent_pos = g_apu_time_since_empty = g_apu_queue_size = 0;
  memset(&g_apu_write, 0xff, sizeof(g_apu_write));
}

void RtlApuUpload(const uint8 *p) {
  RtlApuLock();
  RtlResetApuQueue();
  SpcPlayer_Upload(g_spc_player, p);
  RtlApuUnlock();
}

void RtlRestoreMusicAfterLoad_Locked(bool is_reset) {
  if (g_use_my_apu_code) {
    memcpy(g_spc_player->ram, g_snes->apu->ram, 65536);
    memcpy(g_spc_player->dsp->ram, g_snes->apu->dsp->ram, sizeof(Dsp) - offsetof(Dsp, ram));
    SpcPlayer_CopyVariablesFromRam(g_spc_player);
  }

  if (is_reset) {
    SpcPlayer_Initialize(g_spc_player);
  }

  RtlResetApuQueue();
}

void RtlSaveMusicStateToRam_Locked(void) {
  if (g_use_my_apu_code) {
    // Apply the whole contents of the queue to input ports
    SpcPlayer *spc_player = g_spc_player;
    for (int i = g_apu_queue_size; i; i--) {
      ApuWriteEnt *we = &g_apu_write_ents[(g_apu_write_ent_pos - i) & (kApuMaxQueueSize - 1)];
      for (int j = 0; j < 4; j++) {
        if (we->ports[j] != 255)
          spc_player->input_ports[j] = we->ports[j];
      }
    }
    SpcPlayer_CopyVariablesToRam(g_spc_player);
    memcpy(g_snes->apu->dsp->ram, g_spc_player->dsp->ram, sizeof(Dsp) - offsetof(Dsp, ram));
    memcpy(g_snes->apu->ram, g_spc_player->ram, 65536);
  }
}

void RtlRenderAudio(int16 *audio_buffer, int samples, int channels) {
  assert(channels == 2);
  RtlApuLock();

  RtlPopApuState_Locked();

  if (!g_use_my_apu_code) {
    if (!is_uploading_apu) {
      while (g_snes->apu->dsp->sampleOffset < 534)
        apu_cycle(g_snes->apu);
      dsp_getSamples(g_snes->apu->dsp, audio_buffer, samples);
    }
  } else {
    SpcPlayer_GenerateSamples(g_spc_player);
    dsp_getSamples(g_spc_player->dsp, audio_buffer, samples);
  }

  RtlApuUnlock();
}

void RtlCheat(char c) {
  if (c == 'w') {
    samus_health = samus_max_health;
    StateRecorder_RecordPatchByte(&state_recorder, 0x9C2, (uint8 *)&samus_health, 2);
  } else if (c == 'q') {
    samus_y_pos -= 4;
    samus_y_speed = -8;
    StateRecorder_RecordPatchByte(&state_recorder, 0xafa, (uint8 *)&samus_y_pos, 2);
    StateRecorder_RecordPatchByte(&state_recorder, 0xb2e, (uint8 *)&samus_y_speed, 2);
    menu_index = 0;
  } else if (c == 'q') {
    japanese_text_flag = !japanese_text_flag;
    StateRecorder_RecordPatchByte(&state_recorder, 0x9e2, (uint8 *)&japanese_text_flag, 2);
  }
}

void RtlReadSram(void) {
  FILE *f = fopen("saves/sm.srm", "rb");
  if (f) {
    if (fread(g_sram, 1, 8192, f) != 8192)
      fprintf(stderr, "Error reading saves/sm.srm\n");
    fclose(f);
    RtlSynchronizeWholeState();
    ByteArray_Resize(&state_recorder.base_snapshot, 8192);
    memcpy(state_recorder.base_snapshot.data, g_sram, 8192);
  }
}

void RtlWriteSram(void) {
  rename("saves/sm.srm", "saves/sm.srm.bak");
  FILE *f = fopen("saves/sm.srm", "wb");
  if (f) {
    fwrite(g_sram, 1, 8192, f);
    fclose(f);
  } else {
    fprintf(stderr, "Unable to write saves/sm.srm\n");
  }
}
