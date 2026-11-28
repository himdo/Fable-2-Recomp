# Brazilian Portuguese language pack implementation

**Status:** open PR #242 · **Commits:** bf3eb48, b53cbb8, c5d3c15 (#242) · **Last checked against:** 7863041

## Goal

Expose an optional PT-BR text pack through the native language selector while preserving the extracted disc's other locales and English voice bank. The player supplies the disc and the translation ZIP.

## Design

- **Launcher.** [BrazilianPortuguesePack.cs](../../../launcher/Fable2.Launcher/BrazilianPortuguesePack.cs) imports the PT-BR text and dialogue resources and four Pub Games XML files. It merges the PT-BR rows into the existing UTF-16 `data/language/cultures.txt`, using `pt-br` for text and `en-uk` for fallback. The shared **Merge Locales** menu reflects the selected game folder's installation state.
- **Recovery.** Before replacing a file, the installer records the original contents and the installed hash in `.fable2-pt-br.json` beside `default.xex`. Removal checks for conflicting edits, restores the original files and removes only manifest rows introduced for new pack files. A conflict leaves the backup available for retry.
- **Verified in the generated PowerPC code.** `FrontMenu_HandleEvent` passes the requested index in `r3` to `Locale_SetActiveByIndex_82373D60` (formerly `sub_82373D60`, address `0x82373D60`). The locale table at `0x833197BC` holds the record pointer at offset 0, the count at +4 and the selected index at +8. Each record is 0x60 bytes and starts with the locale code. The setter checks the index against the count. When the selection changes, it writes the index and sets the byte at +16 to 1.
- **Recomp menu.** [CurrentLanguage](../../../src/menu/main_menu.cpp) reads that table when the menu is rebuilt. The Recomp's own quit item uses its Portuguese label for the exact `pt-br` code and its English fallback otherwise. The [manifest](../../../fable_2_manifest.toml) names the existing guest setter. The Recomp reads the selection without installing a hook in that function.

## Verification

The before-and-after checks for the manifest change were the game-version catalogue check, `python -m pytest -q`, the Release launcher configuration tests and `tests\run_native_tests.cmd`. The [pack tests](../../../tests/Fable2.Launcher.ConfigTests/PortuguesePackTests.cs) cover UTF-16 byte order, line endings, incomplete archives, interrupted installs, conflicting edits and restoration. The manifest was also checked against the Release runtime DLL's exports.

On GOTY USA/Europe with Beta 0.9.1, the real ZIP installed through the Windows launcher. Switching between English and PT-BR updated the quit label, and the selection persisted across a restart. After removal, the culture file, the four Pub Games XML files and the manifest matched the originals byte for byte. Earlier in-game checks covered child and adult saves, both DLC maps and all three Pub Games. [Test evidence and known findings](https://github.com/himdo/Fable-2-Recomp/pull/242#issuecomment-7069670877)

## Coverage limits

Other regional discs, a complete playthrough and full DLC quests remain unverified. The childhood-to-adulthood check used debug progression. A diagnostic lookup returned a raw DLC quest key in both English and PT-BR. Its appearance during normal quest play remains unconfirmed.
