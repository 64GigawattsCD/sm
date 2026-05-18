#include "blocksbox_bootstrap.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#endif

enum {
  kPathBufferSize = 1024,
};

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

static void CopyPath(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0)
    return;
  snprintf(dst, dst_size, "%s", src ? src : "");
}

static void BuildPath2(char *dst, size_t dst_size, const char *a, const char *b) {
  if (!dst || dst_size == 0)
    return;
  if (!a || !a[0]) {
    CopyPath(dst, dst_size, b);
    return;
  }
#ifdef _WIN32
  snprintf(dst, dst_size, "%s\\%s", a, b ? b : "");
#else
  snprintf(dst, dst_size, "%s/%s", a, b ? b : "");
#endif
}

#ifdef _WIN32
static void GetExecutableDirectory(char *dst, size_t dst_size) {
  DWORD n = GetModuleFileNameA(NULL, dst, (DWORD)dst_size);
  if (!n || n >= dst_size) {
    CopyPath(dst, dst_size, ".");
    return;
  }
  for (int i = (int)n - 1; i >= 0; i--) {
    if (dst[i] == '\\' || dst[i] == '/') {
      dst[i] = 0;
      return;
    }
  }
  CopyPath(dst, dst_size, ".");
}

static bool FindRomByPatternInDirectory(const char *directory, const char *pattern, char *dst, size_t dst_size) {
  char search_path[kPathBufferSize];
  WIN32_FIND_DATAA find_data;
  HANDLE h;
  bool found = false;
  BuildPath2(search_path, sizeof(search_path), directory, pattern);
  h = FindFirstFileA(search_path, &find_data);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  do {
    if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      char candidate[kPathBufferSize];
      BuildPath2(candidate, sizeof(candidate), directory, find_data.cFileName);
      if (BlocksBoxBootstrap_GetAbsolutePath(candidate, dst, dst_size))
        found = true;
      else
        CopyPath(dst, dst_size, candidate);
      break;
    }
  } while (FindNextFileA(h, &find_data));
  FindClose(h);
  return found;
}
#endif

bool BlocksBoxBootstrap_GetAbsolutePath(const char *src, char *dst, size_t dst_size) {
  if (!src || !src[0] || !dst || dst_size == 0)
    return false;
#ifdef _WIN32
  DWORD n = GetFullPathNameA(src, (DWORD)dst_size, dst, NULL);
  return n > 0 && n < dst_size;
#else
  return realpath(src, dst) != NULL;
#endif
}

bool BlocksBoxBootstrap_GetExecutableRelativePath(const char *relative_path, char *dst, size_t dst_size) {
  if (!relative_path || !relative_path[0] || !dst || dst_size == 0)
    return false;
#ifdef _WIN32
  char exe_dir[kPathBufferSize];
  char candidate[kPathBufferSize];
  GetExecutableDirectory(exe_dir, sizeof(exe_dir));
  BuildPath2(candidate, sizeof(candidate), exe_dir, relative_path);
  if (!BlocksBoxBootstrap_GetAbsolutePath(candidate, dst, dst_size))
    CopyPath(dst, dst_size, candidate);
  return true;
#else
  return false;
#endif
}

bool BlocksBoxBootstrap_ResolveRomPath(const char *preferred_rom_path, char *dst, size_t dst_size) {
  const char *local_candidates[] = {
    "sm.smc",
    "sm.sfc",
    "Super Metroid.smc",
    "Super Metroid.sfc",
  };
  if (preferred_rom_path && preferred_rom_path[0] && FileExists(preferred_rom_path)) {
    if (!BlocksBoxBootstrap_GetAbsolutePath(preferred_rom_path, dst, dst_size))
      CopyPath(dst, dst_size, preferred_rom_path);
    return true;
  }

  for (size_t i = 0; i < sizeof(local_candidates) / sizeof(local_candidates[0]); i++) {
    if (FileExists(local_candidates[i])) {
      if (!BlocksBoxBootstrap_GetAbsolutePath(local_candidates[i], dst, dst_size))
        CopyPath(dst, dst_size, local_candidates[i]);
      return true;
    }
  }

#ifdef _WIN32
  {
    char exe_dir[kPathBufferSize];
    char candidate[kPathBufferSize];
    GetExecutableDirectory(exe_dir, sizeof(exe_dir));
    for (size_t i = 0; i < sizeof(local_candidates) / sizeof(local_candidates[0]); i++) {
      BuildPath2(candidate, sizeof(candidate), exe_dir, local_candidates[i]);
      if (FileExists(candidate)) {
        if (!BlocksBoxBootstrap_GetAbsolutePath(candidate, dst, dst_size))
          CopyPath(dst, dst_size, candidate);
        return true;
      }
    }

    if (FindRomByPatternInDirectory(".", "*Metroid*.smc", dst, dst_size) ||
        FindRomByPatternInDirectory(".", "*Metroid*.sfc", dst, dst_size) ||
        FindRomByPatternInDirectory(exe_dir, "*Metroid*.smc", dst, dst_size) ||
        FindRomByPatternInDirectory(exe_dir, "*Metroid*.sfc", dst, dst_size)) {
      return true;
    }
  }
#endif

  return false;
}

bool BlocksBoxBootstrap_RunImporter(const char *rom_path, const char *output_dir) {
#ifdef _WIN32
  char command[4096];
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  DWORD exit_code = 1;
  BOOL ok;
  snprintf(command, sizeof(command),
           "dotnet run --project \"BlockBox\\src\\BlocksBox.Tool\\BlocksBox.Tool.csproj\" -- extract-super-metroid --input \"%s\" --output \"%s\" --names-file \"assets\\names.txt\"",
           rom_path, output_dir);
  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));
  si.cb = sizeof(si);
  ok = CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  if (!ok)
    return false;
  WaitForSingleObject(pi.hProcess, INFINITE);
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return exit_code == 0;
#else
  (void)rom_path;
  (void)output_dir;
  return false;
#endif
}
