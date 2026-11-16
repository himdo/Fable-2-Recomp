// fable_2 - Frame capture: save the game's rendered frame as a PNG file.
//
// Used by the remote control server's `screenshot` command (see
// src/input/remote_control_server.h) so an external AI harness can "see" the
// game.
//
// HOW IT WORKS
// We ask the renderer's presenter to read back the guest output texture - the
// exact frame the game just rendered, still in GPU memory - via
// Presenter::CaptureGuestOutput(). The SDK performs a device-side
// CopyTextureRegion into a readback buffer, awaits the fence, and hands us the
// pixels as an 8-bit R8G8B8X8 image (see rex::ui::RawImage). We drop the
// unused alpha lane and encode the result as a PNG with a small self-contained
// writer (stored / uncompressed zlib deflate blocks, so there is no dependency
// on GDI+, libpng, or any codec DLL).
//
// WHY THIS (instead of a screen/window capture)
//  - It reads the game's OWN rendered frame, so it is exactly "the on-screen
//    frame" and is isolated from every other window: occluding windows, the
//    taskbar, and other monitors never appear in the picture. This is the
//    robust behavior we want from a screenshot, and it holds even while other
//    windows cover the game.
//  - It is backend-agnostic: both the D3D12 and Vulkan presenters implement
//    CaptureGuestOutput, so it works for --gpu_plugin=xenos and
//    --gpu_plugin=xenos-vulkan alike.
//  - It is reliable in this (virtualized / headless) environment: it never
//    touches the display or DWM, whose redirection-surface readback is
//    flaky there (foreground windows come back black). Reading the guest
//    output straight from GPU memory is not affected by that.
//  - The captured frame is the guest output at the guest's native resolution
//    (e.g. 1920x1080), before any host-side scaling / letterboxing, so it is
//    the true game frame.
//
// COST
// A capture is one GPU-to-CPU readback plus a ~15 MB write; it blocks the
// caller for well under a second. That is fine for the remote-control debug
// channel, which is otherwise idle.

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/ui/presenter.h>

namespace fable2::framecapture {

struct Result {
  bool ok = false;
  std::string error;
  int width = 0;
  int height = 0;
  int64_t bytes = 0;  // PNG file size
  int avg_luma = 0;   // 0..255 mean brightness of the captured frame (diag)
  bool looked_black = false;
};

namespace detail {

inline void PutBE32(std::vector<uint8_t>& o, uint32_t v) {
  o.push_back(static_cast<uint8_t>(v >> 24));
  o.push_back(static_cast<uint8_t>(v >> 16));
  o.push_back(static_cast<uint8_t>(v >> 8));
  o.push_back(static_cast<uint8_t>(v));
}
inline void PutBE16(std::vector<uint8_t>& o, uint16_t v) {
  o.push_back(static_cast<uint8_t>(v >> 8));
  o.push_back(static_cast<uint8_t>(v));
}

inline uint32_t Crc32(const uint8_t* d, size_t n) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (int i = 0; i < 256; ++i) {
      uint32_t c = static_cast<uint32_t>(i);
      for (int k = 0; k < 8; ++k)
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i)
    c = table[(c ^ d[i]) & 0xFFu] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

inline uint32_t Adler32(const uint8_t* d, size_t n) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < n; ++i) {
    a = (a + d[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

// Append a full PNG chunk (length + type + data + CRC) to `out`.
inline void PngChunk(std::vector<uint8_t>& out, const char type[4],
                     const uint8_t* data, size_t n) {
  PutBE32(out, static_cast<uint32_t>(n));
  out.insert(out.end(), type, type + 4);
  if (n > 0) out.insert(out.end(), data, data + n);
  // CRC covers type + data.
  uint32_t crc = 0xFFFFFFFFu;
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (int i = 0; i < 256; ++i) {
      uint32_t c = static_cast<uint32_t>(i);
      for (int k = 0; k < 8; ++k)
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  auto crcb = [&](uint8_t x) {
    crc = table[(crc ^ x) & 0xFFu] ^ (crc >> 8);
  };
  for (int i = 0; i < 4; ++i) crcb(static_cast<uint8_t>(type[i]));
  for (size_t i = 0; i < n; ++i) crcb(data[i]);
  PutBE32(out, crc ^ 0xFFFFFFFFu);
}

// Encode `w` x `h` 24-bit RGB pixels as a PNG (stored / uncompressed deflate).
// Parent directories are created. Returns false on write failure.
inline bool WritePngRgb(const std::string& utf8_path, int w, int h,
                        const uint8_t* rgb) {
  // 1) Filtered scanlines: each row is a filter byte (0 = None) + w*3 bytes.
  std::vector<uint8_t> filt;
  filt.reserve(static_cast<size_t>(h) * (1 + static_cast<size_t>(w) * 3));
  for (int y = 0; y < h; ++y) {
    filt.push_back(0);
    const uint8_t* row = rgb + static_cast<size_t>(y) * w * 3;
    filt.insert(filt.end(), row, row + w * 3);
  }

  // 2) zlib stream: header (0x78 0x01) + stored deflate blocks + adler32.
  std::vector<uint8_t> idat;
  idat.reserve(filt.size() + 8);
  idat.push_back(0x78);
  idat.push_back(0x01);
  size_t off = 0;
  while (off < filt.size()) {
    size_t len = filt.size() - off;
    if (len > 65535u) len = 65535u;
    const bool last = (off + len == filt.size());
    idat.push_back(last ? 1 : 0);  // BFINAL | (BTYPE=00 << 1)
    // Stored-block LEN / NLEN are little-endian (RFC 1951 3.2.4); only the
    // zlib header and the Adler-32 are big-endian.
    const uint16_t len16 = static_cast<uint16_t>(len);
    idat.push_back(static_cast<uint8_t>(len16));
    idat.push_back(static_cast<uint8_t>(len16 >> 8));
    idat.push_back(static_cast<uint8_t>(~len16));
    idat.push_back(static_cast<uint8_t>(static_cast<uint16_t>(~len16) >> 8));
    idat.insert(idat.end(), filt.data() + off, filt.data() + off + len);
    off += len;
  }
  PutBE32(idat, Adler32(filt.data(), filt.size()));

  // 3) Assemble PNG.
  std::vector<uint8_t> png;
  png.insert(png.end(), {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A});
  std::vector<uint8_t> ihdr;
  PutBE32(ihdr, static_cast<uint32_t>(w));
  PutBE32(ihdr, static_cast<uint32_t>(h));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // color type: truecolor (RGB)
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace
  PngChunk(png, "IHDR", ihdr.data(), ihdr.size());
  PngChunk(png, "IDAT", idat.data(), idat.size());
  PngChunk(png, "IEND", nullptr, 0);

  // 4) Write to disk (creating parent directories).
  std::error_code ec;
  std::filesystem::path out(utf8_path);
  if (out.has_parent_path())
    std::filesystem::create_directories(out.parent_path(), ec);
  std::FILE* f = std::fopen(utf8_path.c_str(), "wb");
  if (!f) return false;
  const bool wrote =
      std::fwrite(png.data(), 1, png.size(), f) == png.size();
  std::fclose(f);
  return wrote;
}

}  // namespace detail

// Captures the game's currently rendered frame (the guest output, at the
// guest's native resolution) from `presenter` and writes it as a PNG to
// `utf8_path` (UTF-8; parent directories are created).
//
// `presenter` is the renderer presenter attached to the game window (either
// the D3D12 or Vulkan presenter). Pass nullptr to report "no presenter". The
// call blocks until the GPU readback completes (well under a second).
inline Result CaptureGuestOutputToPng(rex::ui::Presenter* presenter,
                                      const std::string& utf8_path) {
  Result r;
  auto fail = [&](std::string err) -> Result {
    r.ok = false;
    r.error = std::move(err);
    return r;
  };

  if (!presenter)
    return fail("no graphics presenter available (game not running?)");

  rex::ui::RawImage image;
  if (!presenter->CaptureGuestOutput(image) || image.data.empty())
    return fail("could not read the game's rendered frame (no frame captured yet)");
  const int w = static_cast<int>(image.width);
  const int h = static_cast<int>(image.height);
  if (w < 4 || h < 4)
    return fail("captured frame is empty or too small");

  // 1) Convert R8G8B8X8 (stride = width*4) to 24-bit RGB.
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
  for (int y = 0; y < h; ++y) {
    const uint8_t* s = image.data.data() + static_cast<size_t>(y) * image.stride;
    uint8_t* d = rgb.data() + static_cast<size_t>(y) * w * 3;
    for (int x = 0; x < w; ++x) {
      d[x * 3 + 0] = s[x * 4 + 0];  // R
      d[x * 3 + 1] = s[x * 4 + 1];  // G
      d[x * 3 + 2] = s[x * 4 + 2];  // B
    }
  }

  // 2) Encode + save the PNG.
  if (!detail::WritePngRgb(utf8_path, w, h, rgb.data()))
    return fail("could not write the PNG file");

  std::error_code ec;
  const std::filesystem::path out(utf8_path);
  r.bytes = std::filesystem::file_size(out, ec);

  // 3) Brightness diagnostic (catch a black/empty capture).
  uint64_t luma_sum = 0;
  const int step = (w * h) / 200000 + 1;  // ~200k samples max
  for (int i = 0; i < w * h; i += step) {
    const uint8_t* p = rgb.data() + static_cast<size_t>(i) * 3;
    luma_sum += (p[0] * 77u + p[1] * 150u + p[2] * 29u) / 256u;
  }
  const int samples = (w * h + step - 1) / step;
  r.avg_luma = static_cast<int>(luma_sum / static_cast<uint64_t>(samples));
  r.looked_black = (r.avg_luma < 4);

  r.ok = true;
  r.width = w;
  r.height = h;
  return r;
}

}  // namespace fable2::framecapture
