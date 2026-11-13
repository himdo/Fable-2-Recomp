// Exports original and edited PNGs for every texture modified by GenerateSidecar.
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

#include "keycap_render.h"
#include "keyboard_prompts.h"
#include "xenos_tiling.h"

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

static std::string SafeName(const std::string& name) {
  std::string s;
  for (char c : name)
    s += (c == '\\' || c == '/' || c == ':') ? '_' : c;
  return s;
}

// Read u32 big-endian from header metadata.
static int HdrU32(const uint8_t* p, int off) {
  return (p[off] << 24) | (p[off+1] << 16) | (p[off+2] << 8) | p[off+3];
}

int main() {
  const std::string outDir = "tools/texture_export/out";
  std::filesystem::create_directories(outDir);
  fprintf(stderr, "output dir: %s/\n", outDir.c_str());

  std::vector<uint8_t> tx, hx;
  {
    std::ifstream f("data/art/gui/gui_textures.bnk", std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open texture bank\n"); return 1; }
    f.seekg(0, std::ios::end); tx.resize(size_t(f.tellg()));
    f.seekg(0, std::ios::beg); f.read((char*)tx.data(), (std::streamsize)tx.size());
  }
  {
    std::ifstream f("data/art/gui/gui_texture_headers.bnk", std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open header bank\n"); return 1; }
    f.seekg(0, std::ios::end); hx.resize(size_t(f.tellg()));
    f.seekg(0, std::ios::beg); f.read((char*)hx.data(), (std::streamsize)hx.size());
  }
  GuiTextureBank bank, hdrBank;
  if (!bank.Parse(tx.data(), tx.size())) { fprintf(stderr, "tex parse failed\n"); return 1; }
  if (!hdrBank.Parse(hx.data(), hx.size())) { fprintf(stderr, "hdr parse failed\n"); return 1; }
  fprintf(stderr, "bank: %zu entries, headers: %zu entries\n", bank.entries().size(), hdrBank.entries().size());

  std::unordered_map<std::string, std::string> labels;
  auto addLabel = [&labels](const char* key, const char* val) { labels[Ci(key)] = val; };
  addLabel("a", "E"); addLabel("b", "B"); addLabel("x", "LMB"); addLabel("y", "RMB");
  addLabel("lb", "Z"); addLabel("rb", "Tab"); addLabel("lt", "Q"); addLabel("rt", "R");
  addLabel("pause", "Esc"); addLabel("select", "M");
  addLabel("stickup", "W"); addLabel("stickdown", "S");
  addLabel("stickleft", "A"); addLabel("stickright", "D");
  addLabel("up", "Up"); addLabel("down", "Down");
  addLabel("left", "Left"); addLabel("right", "Right");

  // Helper: decode a texture entry to a 0x00A2 tile using header bank for format/size.
  auto DecodeToTile = [&](const auto& e, const std::string& name) -> std::vector<uint8_t> {
    auto hit = hdrBank.entries().find(Ci(name));
    if (hit == hdrBank.entries().end() || hit->second.length < 24) return {};
    const uint8_t* md = hdrBank.Payload(hit->second);
    int w = HdrU32(md, 16);
    int h = HdrU32(md, 20);
    int format = HdrU32(md, 12);
    if (w <= 0 || h <= 0) return {};
    int a2 = 4 + w * h * 4;
    std::vector<uint8_t> tile;
    if (format == 0xA2) {
      if (e.length != (unsigned)a2) return {};
      tile.assign(bank.Payload(e), bank.Payload(e) + e.length);
    } else {
      if (!DecodeCompressedIconToTile(bank.Payload(e), e.length, w, h, tile) ||
          (int)tile.size() != a2)
        return {};
    }
    return tile;
  };

  int count = 0;

  // === 1. Controller icons (32x32 and 64x64) ===
  fprintf(stderr, "controller icons:\n");
  for (auto& [key, entry] : bank.entries()) {
    auto labelOpt = IconLabel(entry.name, labels, /*mouseLook=*/true);
    if (!labelOpt) continue;
    auto hit = hdrBank.entries().find(Ci(entry.name));
    if (hit == hdrBank.entries().end() || hit->second.length < 24) continue;
    const uint8_t* md = hdrBank.Payload(hit->second);
    int w = HdrU32(md, 16);
    int h = HdrU32(md, 20);
    int format = HdrU32(md, 12);
    if ((w != 32 && w != 64) || h != w) continue;
    int a2 = 4 + w * h * 4;

    std::vector<uint8_t> original;
    if (format == 0xA2) {
      if (entry.length != (unsigned)a2) continue;
      original.assign(bank.Payload(entry), bank.Payload(entry) + entry.length);
    } else {
      if (!DecodeCompressedIconToTile(bank.Payload(entry), entry.length, w, h, original) ||
          (int)original.size() != a2)
        continue;
    }
    auto keycap = RenderKeycap(*labelOpt, w);
    if ((int)keycap.size() != a2) continue;

    // Use parent-dir + leaf to disambiguate 24x24 vs main tier.
    std::string subdir;
    {
      auto bs1 = entry.name.rfind('\\');
      if (bs1 != std::string::npos && bs1 > 0) {
        auto bs2 = entry.name.rfind('\\', bs1 - 1);
        if (bs2 != std::string::npos && bs1 - bs2 - 1 > 0)
          subdir = entry.name.substr(bs2 + 1, bs1 - bs2 - 1);
      }
    }
    std::string base = (subdir.empty() ? "" : subdir + "_") + LeafName(entry.name);

    WritePng(outDir + "/" + base + "_original.png", w, h, DeTile(original.data(), w, h).data());
    WritePng(outDir + "/" + base + "_edited.png", w, h, DeTile(keycap.data(), w, h).data());
    count += 2;
  }

  // === 2. abyx.tex (256x256) ===
  fprintf(stderr, "abyx.tex:\n");
  {
    std::string bs(1, '\\');
    const std::string abyxName = "art" + bs + "gui" + bs + "persistant" + bs + "abyx.tex";
    auto at = bank.entries().find(Ci(abyxName));
    if (at == bank.entries().end()) {
      fprintf(stderr, "  abyx not found\n");
    } else {
      auto tile = DecodeToTile(at->second, abyxName);
      if (tile.empty()) {
        fprintf(stderr, "  abyx decode failed\n");
      } else {
        const int w = 256, h = 256;
        WritePng(outDir + "/abyx_original.png", w, h, DeTile(tile.data(), w, h).data());
        std::vector<uint8_t> kb = tile;
        const char* btnKeys[] = {"a", "b", "y", "x"};
        for (int i = 0; i < 4; ++i) {
          auto it = labels.find(btnKeys[i]);
          if (it == labels.end()) continue;
          auto keycap = RenderKeycapInset(it->second, 10, 4, -3);
          ReplaceKeycapRegion(kb, keycap, w, h, i * 64, 0);
          ReplaceKeycapRegion(kb, keycap, w, h, i * 64, 64);
        }
        WritePng(outDir + "/abyx_edited.png", w, h, DeTile(kb.data(), w, h).data());
        count += 2;
      }
    }
  }

  // === 3. frames_elements.tex (512x512) ===
  fprintf(stderr, "frames_elements.tex:\n");
  {
    std::string bs(1, '\\');
    const std::string frameName = "art" + bs + "gui" + bs + "frames" + bs + "frames_elements.tex";
    auto at = bank.entries().find(Ci(frameName));
    if (at == bank.entries().end()) {
      fprintf(stderr, "  frames_elements not found\n");
    } else {
      auto tile = DecodeToTile(at->second, frameName);
      if (tile.empty()) {
        fprintf(stderr, "  frames_elements decode failed\n");
      } else {
        const int w = 512, h = 512;
        WritePng(outDir + "/frames_elements_original.png", w, h, DeTile(tile.data(), w, h).data());
        std::vector<uint8_t> kb = tile;
        struct KSpec { const char* key; int x, y; };
        const KSpec kspecs[] = { {"b", 46, 193}, {"a", 13, 372} };
        // 46 wide, 46+16 tall (8px taller on top and bottom), centered on the
        // original 46-tall region.
        const int kW = 46, kH = 62;
        for (auto& ks : kspecs) {
          auto it = labels.find(ks.key);
          if (it == labels.end()) continue;
          auto kcap = RenderKeycapLinear(it->second, kW, kH);
          const int top = ks.y - (kH - 46) / 2;  // shift up so it stays centered
          CompositeLinearRegion(kb, kcap.data(), kW, kH, w, h, ks.x, top);
        }
        WritePng(outDir + "/frames_elements_edited.png", w, h, DeTile(kb.data(), w, h).data());
        count += 2;
      }
    }
  }

  fprintf(stderr, "\ndone: %d PNGs written to %s/\n", count, outDir.c_str());
  return 0;
}
