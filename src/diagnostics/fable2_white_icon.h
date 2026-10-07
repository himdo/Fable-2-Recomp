// fable2_white_icon.h - turn the Xbox A-button icon into a plain white
// square by overriding its pixels in flight, with no art files modified.
//
// The icon texture Art\GUI\Controller\icon_button_a.tex lives inside
// data/art/gui/gui_textures.bnk (hashed-name bank, plain .tex payloads).
// Its entry (parsed from the bank TOC):
//
//   fileOffset  = 20993360 (0x1405550)  [relative to the bank data base 0x8000]
//   entry size  = 16388 (4-byte header + 16384 pixel bytes)
//
// The .tex payload is a raw 64x64 RGBA level: [u32 pixel_byte_count=0x4000]
// followed by 16384 bytes. So the icon's pixel bytes are at bank file offsets
// [0x140D554, 0x1411554).
//
// All guest file reads funnel through rex::system::XFile::ReadInternal, which
// invokes the RexSetXFilePostReadHook() callback with the VFS path, the host
// read buffer, the byte count, and the resolved file offset. This header
// self-registers that hook at process start and rewrites any read of
// gui_textures.bnk that overlaps the icon's pixel range to 0xFF (opaque
// white). The engine then decodes/uploads a solid white texture, so every
// icon_button_a instance (main-menu buttons, HUD, and the "Press A" prompt
// icon) renders as a filled white square.
//
//   FABLE2_WHITE_ICON=0   disable (default: enabled)
// Log: fable2_white_icon.log next to the exe.

#pragma once

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <rex/system/xfile.h>

#ifdef _WIN32
#include <windows.h>
#include <rex/filesystem.h>
#else
#define MAX_PATH 4096
#endif

namespace fable2::whiteicon {

// Icon pixel range inside gui_textures.bnk (see header comment).
inline constexpr uint64_t kPixelStart = 0x00140D554ull;  // 0x8000 + 0x1405550 + 4
inline constexpr uint64_t kPixelEnd = 0x001411554ull;    // kPixelStart + 0x4000

struct State {
  FILE* log = nullptr;
  int reads_seen = 0;      // reads of gui_textures.bnk (capped logging)
  int patches = 0;         // patch operations applied
  uint64_t bytes_patched = 0;
};

inline State& state() {
  static State s;
  return s;
}

inline void log_line(const char* fmt, ...) {
  FILE* f = state().log;
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  fprintf(f, "[whiteicon] ");
  vfprintf(f, fmt, ap);
  fprintf(f, "\n");
  va_end(ap);
  fflush(f);
}

inline bool is_target_file(const char* path) {
  // Case-insensitive substring match on the VFS path.
  if (!path) return false;
  const char* p = path;
  const char* needle = "gui_textures.bnk";
  size_t nlen = strlen(needle);
  while (*p) {
    const char* hit = p;
    for (size_t i = 0; i < nlen; ++i, ++hit, ++p) {
      if (std::tolower((unsigned char)*hit) != needle[i]) break;
      if (i + 1 == nlen) return true;
    }
  }
  return false;
}

inline void on_file_read(const char* path, uint8_t* buf, size_t bytes,
                         uint64_t offset) {
  // Cheap range prefilter (most reads miss the icon range by far).
  if (offset >= kPixelEnd || offset + bytes <= kPixelStart) return;
  if (!is_target_file(path)) return;

  State& s = state();
  if (s.reads_seen < 32) {
    s.reads_seen++;
    log_line("read %s off=0x%llX len=%zu", path, (unsigned long long)offset, bytes);
  }

  uint64_t start = offset < kPixelStart ? kPixelStart - offset : 0;
  uint64_t end = offset + bytes;
  uint64_t stop = end > kPixelEnd ? kPixelEnd - offset : end;
  if (stop <= start) return;
  memset(buf + start, 0xFF, (size_t)(stop - start));
  s.patches++;
  s.bytes_patched += (uint64_t)(stop - start);
  log_line("PATCHED icon pixels off=0x%llX +[0x%llX..0x%llX) (total %u bytes; "
           "read #%d)",
           (unsigned long long)offset, (unsigned long long)start,
           (unsigned long long)stop, (unsigned)s.bytes_patched, s.reads_seen);
}

struct Registrar {
  Registrar() {
    const char* env = getenv("FABLE2_WHITE_ICON");
    if (env && env[0] == '0' && !env[1]) return;  // explicitly disabled

    char dir[MAX_PATH] = {0};
#ifdef _WIN32
    if (const std::filesystem::path folder = rex::filesystem::GetExecutableFolder();
        folder.string().size() < sizeof(dir) - 32) {
      strncpy(dir, folder.string().c_str(), sizeof(dir) - 32);
      size_t len = strlen(dir);
      if (len && dir[len - 1] != '\\') {
        dir[len] = '\\';
        len++;
      }
      dir[len] = 0;
    }
#else
    (void)dir;
#endif
    char path[MAX_PATH + 32];
    snprintf(path, sizeof(path), "%sfable2_white_icon.log", dir);
    state().log = fopen(path, "w");

    rex::system::RexSetXFilePostReadHook(&on_file_read);
    log_line("installed (icon pixel range [0x%llX, 0x%llX))",
             (unsigned long long)kPixelStart, (unsigned long long)kPixelEnd);
  }
  ~Registrar() {
    if (state().log) {
      log_line("done: %d patch ops, %llu bytes rewritten", state().patches,
               (unsigned long long)state().bytes_patched);
      fclose(state().log);
    }
  }
};

// One instance per including TU (header is #pragma once and only included
// from main.cpp), constructed at process start before guest code runs.
static Registrar g_white_icon_registrar;

}  // namespace fable2::whiteicon
