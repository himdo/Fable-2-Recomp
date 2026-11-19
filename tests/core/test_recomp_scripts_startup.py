"""Check that the recomp Lua scripts are copied before the game folder is mounted.

With the game files in another folder (--game_data_root), recompscripts::Sync copies the
scripts there at start-up (src/core/recomp_scripts.h). The SDK lists the game folder once,
when the runtime mounts it, and the guest cannot open a file that appears after that. The
mount happens after the OnPostInitLogging hook and before OnLoadXexImage, so a copy made
from OnLoadXexImage or later leaves F5, the console and the direct mouse camera dead until
the next start, with the scripts already on disk.

These tests read the C++ source with the parser from test_world_thread_runscript.py.
"""
from .test_world_thread_runscript import functions_where

APP = "src/core/fable_2_app.h"


def describe_the_recomp_script_copy():
    def test_should_be_made_only_by_sync_recomp_scripts():
        assert functions_where(r"\brecompscripts::Sync\s*\(") == {(APP, "SyncRecompScripts")}

    def test_should_run_only_from_the_hook_before_the_mount():
        assert functions_where(r"\bSyncRecompScripts\s*\(") == {(APP, "OnPostInitLogging")}, (
            "SyncRecompScripts must run from OnPostInitLogging only. The runtime lists the "
            "game folder when it mounts it, right after that hook; a script copied later "
            "(OnLoadXexImage, OnPostSetup) can't be opened by the game until the next start.")

    def test_should_come_before_the_manifest_heal_that_lists_the_scripts():
        assert functions_where(r"\bmanifestheal::EnsureComplete\s*\(") == {(APP, "OnLoadXexImage")}
