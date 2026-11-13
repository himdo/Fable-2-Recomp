// bank_rebuild.h
//
// Rebuilds a compact GUI texture bank (gui_textures.bnk / gui_texture_headers.bnk)
// so that a set of named entries carry new (usually larger) payloads. This is the
// C++ port of the launcher's GuiTextureBank.Write (src/gui/GuiTextureBank.cs,
// commit 65551160): the original payload bytes are left in place, the replacement
// payloads are appended to an end-of-file "extra" buffer (8-byte aligned), and the
// zlib-wrapped DEFLATE directory is recompressed with the updated relative
// offsets and lengths. Appending (rather than overwriting in place) is required
// because a bank can alias the same payload offset across multiple entries.
//
// The directory region is fixed in size ([19, base_offset)), so the recompressed
// table must fit there. Since only a handful of offsets/lengths change, the
// compressed size is essentially unchanged, so it fits.
#ifndef FABLE2_GUI_BANK_REBUILD_H
#define FABLE2_GUI_BANK_REBUILD_H

#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <zlib-ng.h>

namespace fable2::gui {

// One directory entry, in table order (used to rebuild the table).
struct BankTocEntry {
  int name_start = 0;  // offset of the name (with trailing null) in the table
  int name_length = 0;  // includes the trailing null
  int rel_offset = 0;  // relative payload offset (base_offset + rel = absolute)
  int length = 0;
};

// Parses the fixed bank header and the decompressed directory, returning the
// entries in table order plus the parsed header fields. `bytes` must outlive the
// caller's use of the returned toc (only integers/strings are copied).
inline bool ParseBankHeaderAndToc(const uint8_t* bytes, size_t size, int& base_offset,
                                  int& packed_length, int& table_length,
                                  std::vector<BankTocEntry>& toc,
                                  std::vector<uint8_t>& table) {
  if (size < 17 || int(uint32_t(bytes[4]) << 24 | uint32_t(bytes[5]) << 16 |
                          uint32_t(bytes[6]) << 8 | uint32_t(bytes[7])) != 3 ||
      bytes[8] != 0) {
    return false;
  }
  base_offset = int(uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 |
                    uint32_t(bytes[2]) << 8 | uint32_t(bytes[3]));
  packed_length = int(uint32_t(bytes[9]) << 24 | uint32_t(bytes[10]) << 16 |
                      uint32_t(bytes[11]) << 8 | uint32_t(bytes[12]));
  table_length = int(uint32_t(bytes[13]) << 24 | uint32_t(bytes[14]) << 16 |
                     uint32_t(bytes[15]) << 8 | uint32_t(bytes[16]));
  if (base_offset < 17 || base_offset > int(size) || packed_length < 2 ||
      packed_length > base_offset - 17 || table_length < 4) {
    return false;
  }
  table.assign(size_t(table_length), 0);
  const uint8_t* raw = bytes + 19;
  size_t raw_len = size_t(packed_length) - 2;
  zng_stream z{};
  if (zng_inflateInit2(&z, -15) != Z_OK) return false;
  z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(raw));
  z.avail_in = uInt(raw_len);
  z.next_out = reinterpret_cast<Bytef*>(table.data());
  z.avail_out = uInt(table.size());
  int ret = Z_OK;
  while (ret == Z_OK) ret = zng_inflate(&z, Z_NO_FLUSH);
  zng_inflateEnd(&z);
  if (size_t(z.total_out) != table.size() &&
      !(ret == Z_BUF_ERROR && size_t(z.total_out) == table.size()))
    return false;

  auto rd = [&](int off) {
    return int(uint32_t(table[off]) << 24 | uint32_t(table[off + 1]) << 16 |
               uint32_t(table[off + 2]) << 8 | uint32_t(table[off + 3]));
  };
  int count = rd(0);
  int cursor = 4;
  if (count < 0) return false;
  toc.clear();
  for (int i = 0; i < count; ++i) {
    if (cursor > int(table.size()) - 4) return false;
    BankTocEntry e;
    e.name_length = rd(cursor);
    cursor += 4;
    if (e.name_length < 2 || cursor + e.name_length > int(table.size()) ||
        table[cursor + e.name_length - 1] != 0)
      return false;
    e.name_start = cursor;
    cursor += e.name_length;
    e.rel_offset = rd(cursor);
    e.length = rd(cursor + 4);
    cursor += 8;
    toc.push_back(e);
  }
  if (cursor != int(table.size())) return false;
  return true;
}

inline std::string BankEntryName(const uint8_t* table, const BankTocEntry& e) {
  return std::string(reinterpret_cast<const char*>(table + e.name_start),
                     size_t(e.name_length) - 1);
}

// Raw DEFLATE (windowBits=-15) compression at a given level.
inline bool DeflateRaw(const uint8_t* src, size_t len, int level,
                       std::vector<uint8_t>& out) {
  zng_stream z{};
  if (zng_deflateInit2(&z, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    return false;
  z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
  z.avail_in = uInt(len);
  out.resize(len + len / 2 + 64);
  z.next_out = reinterpret_cast<Bytef*>(out.data());
  z.avail_out = uInt(out.size());
  int ret = zng_deflate(&z, Z_FINISH);
  zng_deflateEnd(&z);
  if (ret != Z_STREAM_END) return false;
  out.resize(z.total_out);
  return true;
}

// Rebuilds a bank, replacing the payloads of the entries in `replacements`
// (case-insensitive name -> new payload bytes). `out` receives the rebuilt bank
// bytes and `new_offsets` the absolute offset of each replaced entry in `out`.
// Returns false if the bank is malformed or the recompressed directory would not
// fit the fixed directory region.
inline bool RebuildBank(const std::vector<uint8_t>& bank,
                        const std::unordered_map<std::string, std::vector<uint8_t>>& replacements,
                        std::vector<uint8_t>& out, std::unordered_map<std::string, uint64_t>& new_offsets) {
  int base_offset = 0, packed_length = 0, table_length = 0;
  std::vector<BankTocEntry> toc;
  std::vector<uint8_t> table;
  if (!ParseBankHeaderAndToc(bank.data(), bank.size(), base_offset, packed_length,
                             table_length, toc, table)) {
    return false;
  }
  auto lower = [](std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
  };

  // Build the end-of-file extra buffer (8-byte aligned payloads) and the new
  // relative offsets for the replaced entries, in table order.
  std::vector<uint8_t> extra;
  auto align8 = [](std::vector<uint8_t>& v) {
    while (v.size() % 8 != 0) v.push_back(0);
  };
  align8(extra);  // start at an 8-byte boundary within the extra buffer
  const uint64_t extra_base = bank.size();  // absolute offset of extra[0] in `out`
  std::vector<int> new_rel(toc.size(), -1);
  for (size_t i = 0; i < toc.size(); ++i) {
    std::string name = lower(BankEntryName(table.data(), toc[i]));
    auto it = replacements.find(name);
    if (it == replacements.end()) continue;
    uint64_t extra_off = extra.size();
    extra.insert(extra.end(), it->second.begin(), it->second.end());
    align8(extra);
    new_rel[i] = int(extra_base - base_offset + extra_off);
    new_offsets[name] = base_offset + uint64_t(new_rel[i]);
  }

  // Rebuild the directory table in place: same names/order, updated offsets +
  // lengths for the replaced entries.
  std::vector<uint8_t> new_table = table;
  for (size_t i = 0; i < toc.size(); ++i) {
    const BankTocEntry& e = toc[i];
    const int rel = new_rel[i];
    int new_len = e.length;
    if (rel >= 0) {
      auto rit = replacements.find(lower(BankEntryName(table.data(), e)));
      if (rit != replacements.end()) new_len = int(rit->second.size());
    }
    // Write relative_offset (table[name_end..name_end+4]) and length (+4).
    auto wr = [&](int off, int v) {
      new_table[off] = uint8_t((v >> 24) & 0xFF);
      new_table[off + 1] = uint8_t((v >> 16) & 0xFF);
      new_table[off + 2] = uint8_t((v >> 8) & 0xFF);
      new_table[off + 3] = uint8_t(v & 0xFF);
    };
    const int fields_off = e.name_start + e.name_length;
    wr(fields_off, rel >= 0 ? rel : e.rel_offset);
    wr(fields_off + 4, new_len);
  }

  std::vector<uint8_t> compressed;
  if (!DeflateRaw(new_table.data(), new_table.size(), Z_DEFAULT_COMPRESSION, compressed))
    return false;
  // The fixed directory region holds [17, base_offset); keep the 0x78?? wrapper at
  // [17,19) and the raw DEFLATE at [19, base_offset).
  const int max_dir = base_offset - 19;
  if (int(compressed.size()) > max_dir) return false;

  // Assemble: original bytes (TOC region overwritten) + extra buffer.
  out = bank;
  out.resize(bank.size() + extra.size());
  // packed_length (at [9]) = 2 (wrapper) + compressed size.
  auto wr = [&](int off, int v) {
    out[off] = uint8_t((v >> 24) & 0xFF);
    out[off + 1] = uint8_t((v >> 16) & 0xFF);
    out[off + 2] = uint8_t((v >> 8) & 0xFF);
    out[off + 3] = uint8_t(v & 0xFF);
  };
  wr(9, int(compressed.size()) + 2);
  // Preserve the wrapper [17,19) and write the new DEFLATE at [19].
  std::memcpy(out.data() + 19, compressed.data(), compressed.size());
  // Append the extra buffer.
  std::memcpy(out.data() + bank.size(), extra.data(), extra.size());
  return true;
}

// Updates the 84-byte header entries for the given (case-insensitive) names to
// describe a full-RGBA (format 0x00A2) payload of `pixel_count` bytes. The header
// entry layout (21 big-endian dwords): [1]=pixel_byte_count, [3]=format, [6]=2,
// [20]=0x0D. The entry is modified in place (same size), so the bank's directory
// is unchanged. `names` are matched against the bank's entry names; `out`
// receives the (possibly) modified bank bytes.
inline bool RebuildHeaderBank(const std::vector<uint8_t>& bank,
                              const std::unordered_map<std::string, uint32_t>& names,
                              std::vector<uint8_t>& out) {
  int base_offset = 0, packed_length = 0, table_length = 0;
  std::vector<BankTocEntry> toc;
  std::vector<uint8_t> table;
  if (!ParseBankHeaderAndToc(bank.data(), bank.size(), base_offset, packed_length,
                             table_length, toc, table)) {
    return false;
  }
  auto lower = [](std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
  };
  auto rd = [&](const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 |
           uint32_t(p[3]);
  };
  auto wr = [&](uint8_t* p, uint32_t v) {
    p[0] = uint8_t((v >> 24) & 0xFF);
    p[1] = uint8_t((v >> 16) & 0xFF);
    p[2] = uint8_t((v >> 8) & 0xFF);
    p[3] = uint8_t(v & 0xFF);
  };
  out = bank;
  for (const BankTocEntry& e : toc) {
    std::string name = lower(BankEntryName(table.data(), e));
    auto it = names.find(name);
    if (it == names.end()) continue;
    const int off = base_offset + e.rel_offset;
    if (off < 0 || off + 84 > int(out.size())) continue;
    uint8_t* h = out.data() + off;
    const uint32_t count = it->second;
    wr(h + 4, count);    // [1] pixel_byte_count
    wr(h + 12, 0x00A2);  // [3] format
    wr(h + 24, 0x0002);  // [6]
    wr(h + 80, 0x000D);  // [20]
  }
  return true;
}

}  // namespace fable2::gui

#endif
