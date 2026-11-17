// Exports original and edited PNGs for every texture the keycap prompts change.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <malloc.h>
#include <zlib-ng.h>

#include "prompt_textures.h"

namespace rex::filesystem {
std::filesystem::path GetExecutableFolder() { return std::filesystem::current_path(); }
}

extern "C" void* aligned_malloc(size_t size, size_t alignment) {
  if (alignment < sizeof(void*)) alignment = sizeof(void*);
  return _aligned_malloc(size, alignment);
}
extern "C" void aligned_free(void* p) { _aligned_free(p); }

static void WritePng(const std::string& path, int w, int h, const uint8_t* argb) {
  auto put32 = [](std::vector<uint8_t>& v, uint32_t u) {
    v.push_back((u >> 24) & 0xFF); v.push_back((u >> 16) & 0xFF);
    v.push_back((u >> 8) & 0xFF);  v.push_back(u & 0xFF);
  };
  std::vector<uint8_t> png;
  png = {0x89,'P','N','G','\r','\n','\x1a','\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)w); put32(ihdr, (uint32_t)h);
  ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  put32(png, (uint32_t)ihdr.size());
  png.insert(png.end(), (uint8_t*)"IHDR", (uint8_t*)"IHDR" + 4);
  png.insert(png.end(), ihdr.begin(), ihdr.end());
  uint32_t cIhdr = zng_crc32(0, (const uint8_t*)"IHDR", 4);
  cIhdr = zng_crc32(cIhdr, ihdr.data(), ihdr.size());
  put32(png, cIhdr);
  std::vector<uint8_t> raw;
  const size_t stride = (size_t)w * 4;
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    const uint8_t* row = argb + y * stride;
    for (int x = 0; x < w; ++x) {
      raw.push_back(row[x * 4 + 1]);
      raw.push_back(row[x * 4 + 2]);
      raw.push_back(row[x * 4 + 3]);
      raw.push_back(row[x * 4]);
    }
  }
  std::vector<uint8_t> compressed(raw.size() + 256);
  zng_stream zs = {};
  zng_deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);
  zs.next_in = raw.data(); zs.avail_in = raw.size();
  zs.next_out = compressed.data(); zs.avail_out = compressed.size();
  zng_deflate(&zs, Z_FINISH);
  compressed.resize(zs.total_out);
  zng_deflateEnd(&zs);
  put32(png, (uint32_t)compressed.size());
  png.insert(png.end(), (uint8_t*)"IDAT", (uint8_t*)"IDAT" + 4);
  png.insert(png.end(), compressed.begin(), compressed.end());
  uint32_t cIdat = zng_crc32(0, (const uint8_t*)"IDAT", 4);
  cIdat = zng_crc32(cIdat, compressed.data(), compressed.size());
  put32(png, cIdat);
  put32(png, 0);
  png.insert(png.end(), (uint8_t*)"IEND", (uint8_t*)"IEND" + 4);
  put32(png, (uint32_t)zng_crc32(0, (const uint8_t*)"IEND", 4));
  std::ofstream f(path, std::ios::binary);
  f.write((const char*)png.data(), png.size());
  fprintf(stderr, "  wrote %s (%dx%d)\n", path.c_str(), w, h);
}

using namespace fable2::gui;

static std::vector<uint8_t> DeTile(const uint8_t* tile, int w, int h) {
  std::vector<uint8_t> linear((size_t)w * h * 4, 0);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint8_t* s = tile + 4 + XenosTiledOffset(x, y, w);
      uint8_t* d = linear.data() + ((size_t)y * w + x) * 4;
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
    }
  return linear;
}

static std::string LeafName(const std::string& name) {
  auto bs = name.find_last_of("\\/");
  std::string leaf = (bs == std::string::npos) ? name : name.substr(bs + 1);
  auto dot = leaf.rfind('.');
  if (dot != std::string::npos) leaf = leaf.substr(0, dot);
  return leaf;
}

// "<parent folder>_<leaf>", so the 64x64 and 32x32 icon tiers don't collide.
static std::string OutputName(const std::string& name) {
  std::string parent = name.substr(0, name.find_last_of("\\/"));
  parent = parent.substr(parent.find_last_of("\\/") + 1);
  return parent + "_" + LeafName(name);
}

static std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main() {
  const std::string outDir = "tools/texture_export/out";
  std::filesystem::create_directories(outDir);

  const auto tx = ReadFile("data/art/gui/gui_textures.bnk");
  const auto hx = ReadFile("data/art/gui/gui_texture_headers.bnk");
  GuiTextureBank bank, headers;
  if (!bank.Parse(tx.data(), tx.size()) || !headers.Parse(hx.data(), hx.size())) {
    fprintf(stderr, "cannot read the GUI banks under data/art/gui\n");
    return 1;
  }

  // The default key map, as in config/fable2_config.toml.
  const auto labels = ResolveLabels(
      "LMB:X,RMB:Y,MMB:B,Shift:A,E:A,Q:LT,Tab:RB,R:RT,Z:LB,B:B,W:StickUp,S:StickDown,"
      "A:StickLeft,D:StickRight,Escape:Pause,M:Select,Up:Up,Down:Down,Left:Left,Right:Right",
      "");
  int count = 0;
  for (const auto& [name, keycaps] : PlanKeycaps(bank, headers, labels, /*mouse_look=*/true)) {
    const auto& entry = bank.entries().at(name);
    const auto h = ReadHeader(headers, name);
    const auto images = DrawPromptImages(bank.Payload(entry), size_t(entry.length), *h, keycaps);
    if (!images) {
      fprintf(stderr, "  %s: decode failed\n", entry.name.c_str());
      continue;
    }
    const std::string base = outDir + "/" + OutputName(entry.name);
    WritePng(base + "_original.png", h->width, h->height,
             DeTile(images->rgba[0].data(), h->width, h->height).data());
    WritePng(base + "_edited.png", h->width, h->height,
             DeTile(images->rgba[1].data(), h->width, h->height).data());
    count += 2;
  }
  fprintf(stderr, "done: %d PNGs written to %s/\n", count, outDir.c_str());
  return 0;
}
