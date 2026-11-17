// fable_2 - ReXGlue Recompiled Project
//
// Changing values in fable2_config.toml without touching anything else in
// it. The in-game PC Settings page (src/core/port_settings_dialog.h) and
// photo mode's controller shortcut save through this; the launcher
// (launcher/Fable2.Launcher/Fable2ConfigFile.cs) edits the file the same way,
// so the game and the launcher can both write it:
//   - a key already in its section keeps its line's indentation, spacing and
//     inline comment; only the value changes;
//   - a missing key goes at the end of its section, after the last non-blank
//     line; a missing section is added at the end of the file;
//   - every other line (comments, unknown keys and sections) is kept, and the
//     file's line endings (CRLF or LF) are kept.
// No dependencies, so the native tests can include it.

#pragma once

#include <charconv>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fable2::config {

struct FileEdit {
  std::string section;
  std::string key;
  std::string literal;  // TOML text: true, 256, 0.06, "direct"
};

// TOML literals for FileEdit.
inline std::string TomlBool(bool v) { return v ? "true" : "false"; }
inline std::string TomlInt(int64_t v) { return std::to_string(v); }
// Shortest text that reads back as the same double, always with a '.' or an
// exponent: 4 would read back as an integer, so it is written 4.0.
inline std::string TomlDouble(double v) {
  char buf[64];
  const auto r = std::to_chars(buf, buf + sizeof(buf), v);
  std::string s(buf, r.ptr);
  if (s.find_first_of(".eEn") == std::string::npos) s += ".0";  // n: nan / inf
  return s;
}
inline std::string TomlString(std::string_view v) {
  std::string s = "\"";
  for (char c : v) {
    if (c == '"' || c == '\\') s += '\\';
    s += c;
  }
  return s + "\"";
}

namespace edit_detail {

inline std::string_view TrimLeft(std::string_view s) {
  const size_t k = s.find_first_not_of(" \t");
  return k == std::string_view::npos ? std::string_view{} : s.substr(k);
}

// "[name]" -> name; empty if the line is not a section header. Like the
// launcher, "[#..." is not a header; nor is an array of tables "[[x]]".
inline std::string SectionName(std::string_view line) {
  line = TrimLeft(line);
  if (line.size() < 2 || line[0] != '[' || line[1] == '#' || line[1] == '[') return {};
  const size_t close = line.find(']');
  if (close == std::string_view::npos) return {};
  std::string_view name = line.substr(1, close - 1);
  while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
  while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
  return std::string(name);
}

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}

// [start, end) line range of `section`, start being its header; start = -1
// when the file has no such section.
inline std::pair<int, int> SectionRange(const std::vector<std::string>& lines, std::string_view section) {
  int start = -1;
  for (int i = 0; i < int(lines.size()); ++i) {
    const std::string name = SectionName(lines[i]);
    if (name.empty()) continue;
    if (start >= 0) return {start, i};
    if (EqualsIgnoreCase(name, section)) start = i;
  }
  return {start, int(lines.size())};
}

// Where the value of a `key = value  # comment` line starts and ends. A
// string value ends at its closing quote (so a '#' inside it is kept), any
// other value before the inline comment and the spaces in front of it.
inline bool FindValue(std::string_view line, std::string_view key, size_t& begin, size_t& end) {
  const size_t indent = line.find_first_not_of(" \t");
  if (indent == std::string_view::npos || line.compare(indent, key.size(), key) != 0) return false;
  size_t p = indent + key.size();
  while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
  if (p >= line.size() || line[p] != '=') return false;
  ++p;
  while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
  begin = p;
  if (p < line.size() && (line[p] == '"' || line[p] == '\'')) {
    const char quote = line[p];
    size_t i = p + 1;
    while (i < line.size() && line[i] != quote) i += (quote == '"' && line[i] == '\\') ? 2 : 1;
    end = i < line.size() ? i + 1 : line.size();
    return true;
  }
  size_t hash = line.find('#', p);
  if (hash == std::string_view::npos) hash = line.size();
  end = hash;
  while (end > begin && (line[end - 1] == ' ' || line[end - 1] == '\t')) --end;
  return true;
}

}  // namespace edit_detail

// `text` with every edit applied (see the top of this file).
inline std::string ApplyFileEdits(const std::string& text, const std::vector<FileEdit>& edits) {
  using namespace edit_detail;
  const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  const bool final_eol = text.empty() || text.back() == '\n';
  std::vector<std::string> lines;
  for (size_t pos = 0; pos < text.size();) {
    size_t le = text.find('\n', pos);
    if (le == std::string::npos) le = text.size();
    size_t cut = le;
    if (cut > pos && text[cut - 1] == '\r') --cut;
    lines.push_back(text.substr(pos, cut - pos));
    pos = le + 1;
  }

  for (const FileEdit& e : edits) {
    const auto [start, end] = SectionRange(lines, e.section);
    bool done = false;
    for (int i = start + 1; start >= 0 && i < end && !done; ++i) {
      size_t b = 0, v = 0;
      if (!FindValue(lines[i], e.key, b, v)) continue;
      lines[i].replace(b, v - b, e.literal);
      done = true;
    }
    if (done) continue;
    std::string line = e.key + " = " + e.literal;
    if (start < 0) {
      if (!lines.empty() && !TrimLeft(lines.back()).empty()) lines.emplace_back();
      lines.push_back("[" + e.section + "]");
      lines.push_back(std::move(line));
      continue;
    }
    int at = end;
    while (at > start + 1 && TrimLeft(lines[at - 1]).empty()) --at;
    lines.insert(lines.begin() + at, std::move(line));
  }

  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    out += lines[i];
    if (i + 1 < lines.size() || final_eol) out += eol;
  }
  return out;
}

}  // namespace fable2::config
