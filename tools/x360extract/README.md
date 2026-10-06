# x360extract

Command-line tool that extracts game content from Xbox 360 disc images (raw
ISO/rumb). Written for the Fable 2 GOTY recompile project, but it works on any
retail Xbox 360 disc that uses the GDFX filesystem.

## What it does

The tool reads the disc image directly (no mounting, no virtual drive):

1. **Scans for the GDFX header** (`MICROSOFT*XBOX*MEDIA` magic) — 64 MiB chunks,
   ~1 s on a 7.8 GB disc.
2. **Walks the GDFX directory tree** (variable-length entries,
   `0xFFFFFFFF`-terminated) starting at the root sector in the header.
3. **Reads each file's content** from the GDFX data region
   (`header_offset - 0x10000 + sector * 2048`) and writes it to disk.
4. **Copies every file byte-for-byte**, including STFS containers
   (PIRS/LIVE/CON packages) such as `nxeart` and
   `$SystemUpdate/su20076000_00000000`, which stay raw PIRS blobs — exactly
   how retail extraction tools present the GDFX tree. With `--unpack-stfs`,
   STFS file entries are instead expanded into a directory named after the
   entry (e.g. `nxeart/` containing `DashStyle`, `nxebg.jpg`, `nxeslot.jpg`).

Game data on these discs is stored as raw, unencrypted files — there is no
CAB/LZX layer to decode.

GDFX directories can span multiple 0x800-byte sectors (e.g. `data/audio` is
5 sectors / 213 entries on the Fable 2 GOTY disc). Entries are grouped per
sector with 0xFF padding at the end of each batch; the walker follows the
batch chain across sector boundaries up to the directory's declared size.

## Build

```
build.cmd                    framework-dependent single-file exe (default)
build.cmd self-contained     bundles the .NET 8 runtime (~70 MB exe)
```

The cmd cleans the staged output and intermediate build dirs, then publishes
to `out\tooling\x360extract\` at the repo root:

```
out\tooling\x360extract\x360extract.exe
```

The default (framework-dependent) build needs the .NET 8 runtime installed.
The project already requires .NET 8 for the launcher, so this adds no new
user dependency.

## Usage

```
x360extract <iso> [options]          Extract game content from an Xbox 360 ISO
x360extract --list <iso>             List the GDFX file tree without extracting

Options:
  -o, --out <dir>       Output directory (default: current directory)
  --list                List the GDFX directory tree
  --files <a,b,c>       Only extract the named root entries (case-insensitive)
  --unpack-stfs         Expand STFS (PIRS/LIVE/CON) file entries into
                        directories (default: copy them as raw files)
  --quiet               Suppress progress output
  -h, --help            Show this help
```

Exit codes: `0` success, `1` usage error, `2` disc/format error (e.g. no
GDFX header found), `3` unexpected error.

### Examples

```bat
:: Inspect a disc without extracting anything
x360extract.exe "D:\discs\Fable II (GOTY).iso" --list

:: Extract everything into the project root
x360extract.exe "D:\discs\Fable II (GOTY).iso" -o .

:: Extract only the executable and the data tree
x360extract.exe "D:\discs\Fable II (GOTY).iso" -o . --files default.xex,data

:: Extract quietly (scriptable; progress goes nowhere, errors to stderr)
x360extract.exe "D:\discs\Fable II (GOTY).iso" -o C:\content --quiet
```

`--list` prints the full tree with sizes, e.g.:

```
GDFX header at 0FDA0000  (base 0FD90000)
Root sector: 001B387F  size: 2048

  dir  data
      dir  interactivecutscenes
          file interactivecutscenes.gdb  (795,682)
          ...
  dir  $SystemUpdate
      file su20076000_00000000  (7,938,048)
      file system.manifest  (2,100)
  file default.xex  (21,217,280)
  file nxeart  (1,433,600)
```

## Output layout

For the Fable 2 GOTY (USA/EU) disc, a full extraction produces exactly the
content root that `tools\stage_content.cmd` expects:

```
<out>\
├── default.xex              game executable (21 MB)
├── data\                    447 game asset files (worlds, audio, shaders,
│                            scripts, .bnk/.bik streams, dir.manifest, ...)
├── nxeart                   dashboard art package (raw 1.4 MB PIRS file)
└── $SystemUpdate\           system update package
    ├── su20076000_00000000  raw PIRS blob
    └── system.manifest      XMNP manifest
```

With `--unpack-stfs`, `nxeart` becomes a directory:

```
<out>\
├── default.xex
├── data\
├── nxeart\
│   ├── DashStyle
│   ├── nxebg.jpg
│   └── nxeslot.jpg
└── $SystemUpdate\
```

Verified: a default extraction is **byte-for-byte identical** (SHA-1 of all
451 files) to the output of the XexTool GUI on the same ISO.

Files are written with `FileMode.Create` — re-running over an existing output
tree overwrites each file in place but does **not** delete files that are no
longer in the disc tree. Remove the output directory first if you need a
pristine copy.

## Notes

- The input must be a full raw disc image (sector dump). A truncated or
  compressed image will not contain the GDFX data region.
- Extraction throughput is limited by disk reads: a full Fable 2 GOTY
  extraction (~371 files, 6.3 GB) takes a few seconds plus the one-time
  header scan.
- The tool is intentionally format-generic: it extracts whatever the GDFX
  tree contains, so it also works on other retail Xbox 360 titles
  (including discs whose data is in a `data.cab` — that is copied raw as a
  regular file).

## Source layout

| File | Role |
|---|---|
| `Program.cs` | CLI, GDFX header scan, tree walk/extraction driver |
| `Gdfx.cs` | GDFX filesystem reader (header, variable-length directory entries, file reads) |
| `Stfs.cs` | STFS (PIRS/LIVE/CON) container reader (used by `--unpack-stfs`) — hash-tree block addressing, file table, block streaming |
| `Iso9660.cs` | Raw image byte/sector reader (`IsoImage`) |
