// Read-only parser for the compact GUI texture banks (gui_textures.bnk and
// gui_texture_headers.bnk): named, uncompressed payloads with a zlib-wrapped
// DEFLATE directory. Ported from the launcher's src/gui/GuiTextureBank.cs.
//
// Header layout (big-endian), 17 bytes:
//   [0]  base_offset   (4)  offset of the payload region
//   [4]  magic = 3      (4)
//   [8]  0              (1)
//   [9]  packed_length (4)  zlib-wrapped directory length (incl. 2-byte header)
//   [13] table_length  (4)  decompressed directory length
//   [17] zlib wrapper  (2)  0x78?? (the disc omits the Adler-32 trailer)
//   [19] raw DEFLATE stream (packed_length - 2)
// Directory: entry_count (4), then per entry: name_length (4), name (UTF8,
// null-terminated, name_length includes the null), relative_offset (4),
// length (4).
#ifndef FABLE2_GUI_GUI_TEXTURE_BANK_H
#define FABLE2_GUI_GUI_TEXTURE_BANK_H

#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <zlib-ng.h>

namespace fable2::gui {

class GuiTextureBank {
 public:
  struct Entry {
    int offset = 0;  // absolute byte offset within the bank file
    int length = 0;  // payload length in bytes
    std::string name;  // original-cased entry name (for label resolution)
  };

  // Parse the header and directory. Returns false on any malformed structure.
  // The bank bytes must outlive this object (they are not copied).
  bool Parse(const uint8_t* bytes, size_t length) {
    data_ = bytes;
    size_ = length;
    if (size_ < 17 || ReadIntBE(4) != 3 || bytes[8] != 0) return false;
    base_offset_ = ReadIntBE(0);
    int packed_length = ReadIntBE(9);
    int table_length = ReadIntBE(13);
    if (base_offset_ < 17 || base_offset_ > (int)size_ || packed_length < 2 ||
        packed_length > base_offset_ - 17 || table_length < 4 ||
        table_length > 2 * 1024 * 1024)
      return false;
    // The zlib wrapper: 0x78?? where (value % 31) == 0 and the deflate flag is
    // clear. The disc omits the Adler-32 trailer, so this is raw DEFLATE.
    uint32_t wrapper = (uint32_t(bytes[17]) << 8) | uint32_t(bytes[18]);
    if ((wrapper >> 8) != 0x78 || wrapper % 31 != 0 || (wrapper & 0x20) != 0)
      return false;

    table_.resize(size_t(table_length));
    if (!InflateRaw(bytes + 19, size_t(packed_length) - 2, table_)) return false;

    int count = ReadIntFromTable(0);
    size_t cursor = 4;
    if (count < 0 || count > (int)((table_.size() - cursor) / 14)) return false;
    for (int i = 0; i < count; ++i) {
      if (cursor > table_.size() - 4) return false;
      int name_length = ReadIntFromTable((int)cursor);
      cursor += 4;
      if (name_length < 2 ||
          name_length > (int)table_.size() - (int)cursor - 8 ||
          table_[cursor + name_length - 1] != 0)
        return false;
      std::string name(reinterpret_cast<const char*>(table_.data() + cursor),
                       size_t(name_length) - 1);
      cursor += name_length;
      int relative_offset = ReadIntFromTable((int)cursor);
      int length = ReadIntFromTable((int)cursor + 4);
      if (relative_offset < 0 || length < 0 ||
          relative_offset > (int)size_ - base_offset_ - length)
        return false;
      // Case-insensitive lookup (the texture and header banks use the same
      // names); the original-cased name is preserved for label resolution.
      entries_[ToLower(name)] = Entry{base_offset_ + relative_offset, length, name};
      cursor += 8;
    }
    if (cursor != table_.size()) return false;
    ok_ = true;
    return true;
  }

  bool ok() const { return ok_; }
  const std::unordered_map<std::string, Entry>& entries() const { return entries_; }
  const uint8_t* Payload(const Entry& e) const { return data_ + e.offset; }
  int PayloadLength(const Entry& e) const { return e.length; }

  // Big-endian 32-bit read at an absolute bank offset.
  static int ReadIntBE(const uint8_t* p) {
    return (int(uint32_t(p[0]) << 24) | int(uint32_t(p[1]) << 16) |
            int(uint32_t(p[2]) << 8) | int(uint32_t(p[3])));
  }

 private:
  int ReadIntBE(int offset) const { return ReadIntBE(data_ + offset); }
  int ReadIntFromTable(int offset) const { return ReadIntBE(table_.data() + offset); }
  static std::string ToLower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
  }
  static bool InflateRaw(const uint8_t* raw, size_t raw_len, std::vector<uint8_t>& out) {
    // `out` is pre-sized to the declared decompressed length. Inflate with a
    // little slack so the final block's end marker can be consumed even when the
    // output lands exactly on the buffer boundary (zlib would otherwise report
    // Z_BUF_ERROR once the buffer is full, before reading the end-of-stream).
    const size_t expected = out.size();
    out.resize(expected + 64);
    zng_stream z{};
    z.zalloc = nullptr;
    z.zfree = nullptr;
    z.opaque = nullptr;
    if (zng_inflateInit2(&z, -15) != Z_OK) {  // raw DEFLATE, 32K window
      out.resize(expected);
      return false;
    }
    z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(raw));
    z.avail_in = uInt(raw_len);
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(out.size());
    int ret = Z_OK;
    while (ret == Z_OK) ret = zng_inflate(&z, Z_NO_FLUSH);
    zng_inflateEnd(&z);
    // The directory's DEFLATE stream decodes to exactly the declared length but
    // does not always carry a clean end-of-stream marker, so zlib reports
    // Z_BUF_ERROR (input exhausted) rather than Z_STREAM_END once the buffer is
    // full. Accept either, as long as we produced exactly the expected bytes.
    const bool ok = size_t(z.total_out) == expected &&
                    (ret == Z_STREAM_END || ret == Z_BUF_ERROR);
    if (ok) out.resize(expected);
    return ok;
  }

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  int base_offset_ = 0;
  std::vector<uint8_t> table_;
  std::unordered_map<std::string, Entry> entries_;  // lowercase names
  bool ok_ = false;
};

}  // namespace fable2::gui

#endif
