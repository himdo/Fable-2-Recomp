# German GOTY support

**Status:** merged · **Commits:** bfcc5d6, 724495a, 77059af · **Last checked against:** 7863041

The default `goty-compatible` profile accepts these two original XEXs:

| Edition | SHA-256 |
|---|---|
| GOTY USA/Europe | `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662` |
| German GOTY | `3f36e7870a06e04b3702760e93c61b1c7fded321b94021da6bfa120b424e6eb4` |

Both are 21,217,280 bytes. Only 318 bytes differ, all in the XEX header before
`0x4000`. The remaining payload has SHA-256
`84651650d00ccd62021847a39fad6e63da0f7c49ef587a733e215e5ec5a23a4a` in both.
USA/Europe has media ID `716F0A0D`, version `0.0.0.26`; German GOTY has media ID
`1D1F480C`, version `0.0.0.28`. Separate ReXGlue 0.10.0 codegen runs produced
595 byte-identical C++/header files, including function mappings and hooks.
This is why one recompiled target can accept both hashes; the checksum guard
is not disabled and unknown images are not accepted.

The selected original dump must contain `data/gold_version.txt` and
`data/startup.vfsconfig`; German GOTY also needs
`data/language/de-de/text/book.babel`. Known German retail/TU1 content markers
are rejected. These are sanity checks, not a complete content hash manifest.
The observed German retail/TU1 XEX (`cec9238ef5d7b391345a8897ef00a4673ae4f106f89385c81723ba5f9d0807b5`,
21,053,440 bytes) still needs its own codegen/address validation. Replacing its
XEX with the USA/EU GOTY image froze during the intro and is not a supported fix.
No claim is made about Russian editions or other disc/title-update revisions.

`FABLE2_BUILD_PROFILE` may also be set to `goty-us-eu` or `goty-german` for an
explicit single-edition build. Unknown values fail CMake configuration. CMake
stages `fable2_build.json` next to the EXE so the launcher can check compiled
profile support. Without a descriptor, it assumes a legacy USA/Europe EXE and
does not offer German startup. Native validation also runs on direct EXE launch,
fails closed if the XEX is unreadable, and binds verification-cache entries to
the canonical source path, size and high-resolution modification timestamp.

English (1) or German (3) is selected as the `user_language` default per image;
explicit config/environment/command-line language choices still win. The
launcher never replaces the original XEX or copies localization/game assets.
Configs, saves, logs and caches stay beside the native executable.

Local Windows tests: a native Release build started with both unchanged GOTY
dumps using the same EXE. German startup was confirmed by the tester; a copy
of the tester's existing USA/EU save loaded normally on the matched renderer
build. Launcher config/profile tests and native SHA-256/cache tests passed.
The tester subsequently loaded a Version 1 adult-hero save on German GOTY and
confirmed normal hero/dog rendering with the matched upstream renderer.
Extended play, region transitions, complete localization and exhaustive
cross-edition/save-version compatibility remain unverified.
Existing AllocFixed messages and a missing `de-de/lang.ini` lookup are not fixed
by this change. See [runtime fixes and validation](../../fix/runtime_fixes/technical.md) for the
additional SDK patch and frame-pacing/audio tests; no allocator fix is claimed.

Tests require no distributed game files:

```cmd
dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release
clang++ -std=c++23 -DFABLE2_GOTY_COMPATIBLE tests/native/test_xex_verify.cpp -o out/test_xex_verify.exe
out\test_xex_verify.exe
```

Optional positional paths to users' original XEXs exercise fresh/cached and
cross-folder verification. Optional launcher test arguments accept a local game
root, with `--expect-german-goty` for the German case. Do not commit game dumps,
generated guest sources, save files, runtime binaries or local paths/reports.
