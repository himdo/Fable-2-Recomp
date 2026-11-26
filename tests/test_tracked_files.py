"""Check that git tracks no game files.

The game's files come from the player's own disc and must never be committed
(CLAUDE.md, "Never commit"). .gitignore keeps the usual names out, but
`git add -f` or a rip folder with an unusual name gets past it, and nothing
else would notice. These tests fail if git tracks one.
"""
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]

# The folders and files a disc rip puts in the repository root (see .gitignore).
GAME_FOLDERS = {"data", "nxeart", "$systemupdate", "gamefiles"}
# File types that only the game's own content uses: the executable (.xex),
# data banks such as gamescripts.bnk (.bnk) and Bink videos (.bik), plus the
# other game-only types an unpacked GOTY USA/Europe disc holds (counted on
# 26 Nov 2026; see docs/feature/legal_and_distribution/technical.md): audio
# databases and sound banks (.adb, .sbk, .amp), fonts (.bft), text (.babel),
# game databases such as globals.gdb (.gdb), animation data
# (.animation_data, .animation_toc), locomotion data (.loco) and the virtual
# file system config (.vfsconfig). Common types the disc also holds (.xml,
# .lua, .txt, .dds, .swf, .list, .csv, .manifest) are left out: the repo has
# files of those types of its own.
GAME_FILE_TYPES = (".xex", ".bnk", ".bik",
                   ".adb", ".bft", ".babel", ".gdb", ".sbk",
                   ".animation_data", ".animation_toc", ".loco", ".amp", ".vfsconfig")
# One example path per added type, for the tests below.
DISC_EXAMPLES = ["audio/animals.adb", "fonts/body.bft", "language/en-uk/text/book.babel",
                 "Globals/globals.gdb", "audio/music.sbk", "fable2_anims.animation_data",
                 "fable2_anims.animation_toc", "entity/locomotion.loco", "audio/profiles.amp",
                 "startup.vfsconfig"]


def game_files(paths):
    """The paths that look like game content: inside a rip folder at the root, or of a game file type."""
    return [path for path in paths
            if path.split("/", 1)[0].lower() in GAME_FOLDERS or path.lower().endswith(GAME_FILE_TYPES)]


def tracked_files():
    """Every path git tracks in this checkout, with forward slashes."""
    result = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"],
                            capture_output=True, text=True, encoding="utf-8", check=True)
    return [path for path in result.stdout.split("\0") if path]


def describe_game_files():
    # --- Happy flow -------------------------------------------------------

    def test_should_pass_source_files():
        paths = ["src/main.cpp", "tools/xex_image.py", "docs/README.md"]

        assert game_files(paths) == []

    # --- Bad flow ---------------------------------------------------------

    def test_should_find_the_game_executable():
        assert game_files(["default.xex"]) == ["default.xex"]

    def test_should_find_a_renamed_executable_in_any_folder():
        assert game_files(["scratch/default_new.xex"]) == ["scratch/default_new.xex"]

    def test_should_find_a_data_bank():
        assert game_files(["tools/gamescripts.bnk"]) == ["tools/gamescripts.bnk"]

    def test_should_find_a_video():
        assert game_files(["assets/lionhead_logo.bik"]) == ["assets/lionhead_logo.bik"]

    @pytest.mark.parametrize("path", DISC_EXAMPLES)
    def test_should_find_the_other_game_only_types_outside_the_data_folder(path):
        moved = "scratch/" + path.rsplit("/", 1)[-1]

        assert game_files([moved]) == [moved]

    def test_should_find_files_in_the_data_folder():
        assert game_files(["data/levels/level.txt"]) == ["data/levels/level.txt"]

    def test_should_find_the_nxeart_file():
        assert game_files(["nxeart"]) == ["nxeart"]

    def test_should_find_the_system_update_folder():
        assert game_files(["$SystemUpdate/su20076000_00000000"]) == ["$SystemUpdate/su20076000_00000000"]

    def test_should_find_the_gamefiles_folder():
        assert game_files(["Gamefiles/readme.txt"]) == ["Gamefiles/readme.txt"]

    # --- Edge cases -------------------------------------------------------

    def test_should_ignore_case():
        # Windows doesn't care about case, so a rip can arrive as DATA or Default.XEX.
        assert game_files(["DATA/x.txt", "Default.XEX"]) == ["DATA/x.txt", "Default.XEX"]

    def test_should_pass_a_data_folder_below_the_root():
        # .gitignore only ignores /data/ at the root; other data folders are the repo's own.
        assert game_files(["tests/data/sample.json"]) == []

    def test_should_pass_common_types_the_disc_also_uses():
        # The disc holds these too, but so does the repo (src/lua/*.lua, docs, configs).
        paths = ["src/lua/F5.lua", "launcher/app.xml", "notes.txt", "assets/icon.dds",
                 "docs/ui.swf", "art/gui.list", "constants/table.csv", "dir.manifest"]

        assert game_files(paths) == []

    def test_should_match_the_whole_extension():
        # ".gdb" must not catch ".gdbinit", nor ".loco" a ".locomotion" note.
        assert game_files(["tools/.gdbinit", "docs/walk.locomotion.md"]) == []

    def test_should_pass_names_that_only_start_like_a_rip_folder():
        assert game_files(["database.md", "nxeart_notes.md"]) == []


def describe_the_repository():
    @pytest.fixture
    def tracked():
        if not (ROOT / ".git").exists():
            pytest.skip("not a git checkout")
        return tracked_files()

    def test_should_list_the_tracked_files(tracked):
        # Guard: an empty list would let the next test pass without checking anything.
        assert "CLAUDE.md" in tracked
        assert "tests/README.md" in tracked

    def test_should_track_no_game_files(tracked):
        assert game_files(tracked) == []
