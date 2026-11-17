// Changing values in fable2_config.toml (src/core/fable2_config_edit.h), as
// the Advanced Options page saves them. The rules match the launcher's
// Fable2ConfigFile.Write, so both can write the same file.
#include "../../src/core/fable2_config_edit.h"

#include <string>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace fable2::config;

static std::string Edit(const std::string& text, std::vector<FileEdit> edits) {
  return ApplyFileEdits(text, edits);
}

TEST_CASE("A key in its section keeps its line, only the value changes") {
  const std::string file =
      "# top comment\n"
      "[patches]\n"
      "# Motion blur\n"
      "  disable_motion_blur  =   false   # keep me\n"
      "unlock_ce = true\n"
      "\n"
      "[input]\n"
      "mouse_look_scale = 256\n";
  SECTION("indentation, spacing and the inline comment are kept") {
    CHECK(Edit(file, {{"patches", "disable_motion_blur", "true"}}) ==
          "# top comment\n"
          "[patches]\n"
          "# Motion blur\n"
          "  disable_motion_blur  =   true   # keep me\n"
          "unlock_ce = true\n"
          "\n"
          "[input]\n"
          "mouse_look_scale = 256\n");
  }
  SECTION("only the key in the named section is changed") {
    const std::string two = "[a]\nx = 1\n[b]\nx = 1\n";
    CHECK(Edit(two, {{"b", "x", "2"}}) == "[a]\nx = 1\n[b]\nx = 2\n");
  }
  SECTION("a key that only starts with the name is not the key") {
    const std::string f = "[input]\nmouse_look_scale = 256\nmouse_look = true\n";
    CHECK(Edit(f, {{"input", "mouse_look", "false"}}) == "[input]\nmouse_look_scale = 256\nmouse_look = false\n");
  }
  SECTION("section names match without regard to case") {
    CHECK(Edit("[Input]\nmouse_look = true\n", {{"input", "mouse_look", "false"}}) ==
          "[Input]\nmouse_look = false\n");
  }
}

TEST_CASE("String values") {
  SECTION("a '#' inside the string is part of the value, the comment after it stays") {
    const std::string f = "[photo_mode]\nscreenshot_folder = \"shots #1\"  # where\n";
    CHECK(Edit(f, {{"photo_mode", "screenshot_folder", TomlString("pics")}}) ==
          "[photo_mode]\nscreenshot_folder = \"pics\"  # where\n");
  }
  SECTION("escaped quotes do not end the string") {
    const std::string f = "[s]\nk = \"a \\\" b\" # c\n";
    CHECK(Edit(f, {{"s", "k", "1"}}) == "[s]\nk = 1 # c\n");
  }
  SECTION("TomlString escapes quotes and backslashes") {
    CHECK(TomlString("a\"b\\c") == "\"a\\\"b\\\\c\"");
  }
}

TEST_CASE("Missing keys and sections are added") {
  SECTION("a missing key goes after the last non-blank line of its section") {
    const std::string f = "[patches]\nunlock_ce = true\n\n\n[input]\nmouse_look = true\n";
    CHECK(Edit(f, {{"patches", "skip_intro_videos", "true"}}) ==
          "[patches]\nunlock_ce = true\nskip_intro_videos = true\n\n\n[input]\nmouse_look = true\n");
  }
  SECTION("a missing section is added at the end, after a blank line") {
    CHECK(Edit("[input]\nmouse_look = true\n", {{"perf", "hotfunc_yield_every", "4"}}) ==
          "[input]\nmouse_look = true\n\n[perf]\nhotfunc_yield_every = 4\n");
  }
  SECTION("an empty file gets the section") {
    CHECK(Edit("", {{"perf", "hotfunc_yield_every", "4"}}) == "[perf]\nhotfunc_yield_every = 4\n");
  }
  SECTION("a key only in a comment is not taken for the key") {
    CHECK(Edit("[patches]\n# unlock_ce = false\n", {{"patches", "unlock_ce", "true"}}) ==
          "[patches]\n# unlock_ce = false\nunlock_ce = true\n");
  }
}

TEST_CASE("Line endings and other lines are kept") {
  SECTION("CRLF files stay CRLF") {
    CHECK(Edit("[a]\r\nx = 1\r\n# end\r\n", {{"a", "x", "2"}}) == "[a]\r\nx = 2\r\n# end\r\n");
  }
  SECTION("a file without a final newline keeps that") {
    CHECK(Edit("[a]\nx = 1", {{"a", "x", "2"}}) == "[a]\nx = 2");
  }
  SECTION("an edit that changes nothing gives the same text") {
    const std::string f = "# c\n[a]  # header comment\nx = 1\n\n[b]\ny = \"z\"\n";
    CHECK(Edit(f, {{"a", "x", "1"}, {"b", "y", "\"z\""}}) == f);
  }
}

TEST_CASE("Number literals read back as the same kind") {
  CHECK(TomlDouble(4.0) == "4.0");
  CHECK(TomlDouble(0.06) == "0.06");
  CHECK(TomlDouble(0.065) == "0.065");
  CHECK(TomlDouble(-0.0022) == "-0.0022");
  CHECK(TomlInt(256) == "256");
  CHECK(TomlBool(true) == "true");
}
