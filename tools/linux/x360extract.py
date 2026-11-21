#!/usr/bin/env python3
"""Fable II game-file extractor for the Linux package (used by Fable2.sh).

Reads an Xbox 360 disc image (raw ISO) directly, without mounting it, and
copies default.xex and the data folder next to the game. A Python port of the
GDFX reader in tools/x360extract/lib (Gdfx.cs, X360Disc.cs), so the files it
writes are byte-for-byte what x360extract writes; it needs nothing beyond the
Python 3 that SteamOS and other desktop Linux systems already have.

  x360extract.py probe <iso> [--versions game_versions.json]
      Checks that <iso> is a Fable II disc and prints one line:
        ok <hash> <name>            a supported version
        unsupported <hash> <name>   a known version the build does not support
        unknown <hash>              an Xbox 360 disc with an unknown default.xex
      Exit status 0 = ok, 3 = unsupported/unknown, 2 = not an Xbox 360 image.

  x360extract.py extract <iso> <dest> [--versions FILE] [--force] [--all]
                 [--missing-only] [--zenity]
      Extracts default.xex and data/ (--all: every root entry) into <dest>.
      The default.xex hash is checked against the version catalogue first;
      an unknown or unsupported version stops with status 3 unless --force.
      Everything is written to <dest>/.fable2-extract first and moved into
      place at the end, so an interrupted run leaves no partial data folder.
      --missing-only skips root entries that already exist in <dest>.
      --zenity prints progress for `zenity --progress` on stdout.
      Exit status 0 = done, 2 = bad image, 3 = version check failed,
      4 = not enough free space, 1 = other error.
"""
import hashlib
import json
import os
import shutil
import struct
import sys
import time

MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR = 2048
CHUNK = 16 << 20
TMP_NAME = ".fable2-extract"
DEFAULT_ENTRIES = ("default.xex", "data")


class DiscError(Exception):
    pass


class Entry:
    __slots__ = ("name", "sector", "size", "is_dir")

    def __init__(self, name, sector, size, is_dir):
        self.name, self.sector, self.size, self.is_dir = name, sector, size, is_dir


class Disc:
    """An open GDFX disc image (see Gdfx.cs for the on-disc layout)."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.length = os.fstat(self.f.fileno()).st_size
        self.header = self._scan_header()
        self.base = self.header - 0x10000
        hdr = self._read(self.header, 0x24)
        self.root_sector, self.root_size = struct.unpack_from("<II", hdr, 0x14)
        self.root = self.read_dir(self.root_sector, self.root_size)

    def close(self):
        self.f.close()

    def _read(self, off, n):
        return os.pread(self.f.fileno(), n, off) if hasattr(os, "pread") else self._seek_read(off, n)

    def _seek_read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def _scan_header(self):
        # 64 MiB windows that overlap by the magic's length (X360Disc.ScanForGdfxHeader).
        # Retail discs have it at a fixed spot, so try those first.
        for guess in (0xFDA0000, 0x2090000, 0x18310000, 0x10000):  # XGD2, XGD3, XGD1, xiso
            if guess + len(MAGIC) <= self.length and self._read(guess, len(MAGIC)) == MAGIC:
                return guess
        chunk = 1 << 26
        off = 0
        while off < self.length:
            buf = self._read(off, min(chunk, self.length - off))
            i = buf.find(MAGIC)
            if i >= 0:
                return off + i
            if off + len(buf) >= self.length:
                break
            off += len(buf) - len(MAGIC)
        raise DiscError("this is not a complete Xbox 360 disc image (no GDFX header found)")

    def sector_offset(self, sector):
        return self.base + sector * SECTOR

    def read_dir(self, sector, size):
        """A GDFX directory listing (Gdfx.ReadDirectory), batches per 0x800 sector."""
        start = self.sector_offset(sector)
        end = start + size
        data = self._read(start, size) if size else b""
        entries = []
        pos = 0
        while pos + 14 <= len(data) and start + pos + 14 <= self.length:
            if struct.unpack_from("<I", data, pos)[0] == 0xFFFFFFFF:
                nxt = ((pos >> 11) + 1) << 11
                if start + nxt < end and data[pos:nxt] == b"\xff" * (nxt - pos):
                    pos = nxt
                    continue
                break
            _, esec, esize, attrs, nlen = struct.unpack_from("<IIIBB", data, pos)
            if nlen < 1 or nlen > 100:
                break
            raw = data[pos + 14:pos + 14 + nlen]
            if len(raw) < nlen or any(c <= 0x20 or c >= 0x7F for c in raw):
                break
            entries.append(Entry(raw.decode("ascii"), esec, esize, bool(attrs & 0x10)))
            pos += (14 + nlen + 3) & ~3
        return entries

    def find_root(self, name):
        for e in self.root:
            if e.name.lower() == name.lower():
                return e
        return None

    def walk(self, entries, rel=""):
        """Yields (relative path, entry) for every file under entries."""
        for e in entries:
            path = rel + safe_name(e.name)
            if e.is_dir:
                yield from self.walk(self.read_dir(e.sector, e.size), path + "/")
            else:
                yield path, e

    def read_file(self, e):
        off, left = self.sector_offset(e.sector), e.size
        while left > 0:
            n = min(CHUNK, left, self.length - off)
            if n <= 0:
                break
            buf = self._read(off, n)
            yield buf
            off += len(buf)
            left -= len(buf)


def safe_name(name):
    # X360Disc.SanitizeFileName with the Linux set of invalid characters.
    s = name.replace("/", "_").replace("\0", "_").strip().rstrip(".")
    return s or "_"


def load_versions(path):
    if not path:
        here = os.path.dirname(os.path.abspath(__file__))
        for cand in (os.path.join(here, "game_versions.json"),
                     os.path.join(here, "..", "..", "constants", "game_versions.json")):
            if os.path.isfile(cand):
                path = cand
                break
    if not path:
        return []
    with open(path, encoding="utf-8") as f:
        return json.load(f).get("versions", [])


def check_xex(disc, versions):
    """(status, hash, name) for the disc's default.xex."""
    xex = disc.find_root("default.xex")
    if xex is None or xex.is_dir:
        raise DiscError("this disc image has no default.xex (is it an Xbox 360 game disc?)")
    h = hashlib.sha256()
    for buf in disc.read_file(xex):
        h.update(buf)
    digest = h.hexdigest()
    for v in versions:
        if v.get("hash", "").lower() == digest:
            return ("ok" if v.get("compatible") else "unsupported"), digest, v.get("name", v.get("id", ""))
    return "unknown", digest, ""


class Progress:
    def __init__(self, total, zenity):
        self.total, self.zenity = max(total, 1), zenity
        self.done = 0
        self.last = 0.0
        self.start = time.monotonic()

    def say(self, text, force=False):
        now = time.monotonic()
        if not force and now - self.last < 0.25:
            return
        self.last = now
        pct = min(99, self.done * 100 // self.total)
        mib, tot = self.done >> 20, self.total >> 20
        if self.zenity:
            print(pct, flush=True)
            print("# %s  (%d of %d MB)" % (text, mib, tot), flush=True)
        else:
            sys.stderr.write("\r%3d%%  %d / %d MB  %-60s" % (pct, mib, tot, text[-60:]))
            sys.stderr.flush()


def copy_entry(disc, e, dst, prog, label, use_cfr):
    """Copies one file; returns whether copy_file_range still works."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    fd = os.open(dst, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, "O_BINARY", 0), 0o644)
    try:
        off, left = disc.sector_offset(e.sector), min(e.size, max(0, disc.length - disc.sector_offset(e.sector)))
        src = disc.f.fileno()
        while left > 0:
            n = min(CHUNK, left)
            done = 0
            if use_cfr:
                try:
                    done = os.copy_file_range(src, fd, n, off)
                except OSError:
                    use_cfr = False
                    done = 0
            if not use_cfr or done <= 0:
                buf = disc._read(off, n)
                if not buf:
                    break
                os.write(fd, buf)
                done = len(buf)
            off += done
            left -= done
            prog.done += done
            prog.say(label)
        if e.size and os.fstat(fd).st_size != e.size:
            os.ftruncate(fd, e.size)
    finally:
        os.close(fd)
    return use_cfr


def cmd_probe(args):
    iso = args[0]
    versions = load_versions(opt(args, "--versions"))
    disc = Disc(iso)
    try:
        status, digest, name = check_xex(disc, versions)
    finally:
        disc.close()
    print(("%s %s %s" % (status, digest, name)).rstrip())
    return 0 if status == "ok" else 3


def cmd_extract(args):
    iso, dest = args[0], args[1]
    zen = "--zenity" in args
    versions = load_versions(opt(args, "--versions"))
    prog_hint = Progress(1, zen)
    prog_hint.say("Reading the disc image...", force=True)
    disc = Disc(iso)
    try:
        status, digest, name = check_xex(disc, versions)
        if status != "ok" and "--force" not in args:
            what = ("%s, which this build does not support" % name) if status == "unsupported" \
                else "a version of Fable II this build does not recognise"
            err("The disc image holds %s (default.xex SHA-256 %s)." % (what, digest))
            return 3
        wanted = [e for e in disc.root if "--all" in args or e.name.lower() in DEFAULT_ENTRIES]
        if "--missing-only" in args:
            wanted = [e for e in wanted if not os.path.lexists(os.path.join(dest, safe_name(e.name)))]
        if not any(e.name.lower() == "data" for e in disc.root):
            raise DiscError("this disc image has no data folder (is it a Fable II disc?)")
        files = list(disc.walk(wanted))
        total = sum(e.size for _, e in files)

        os.makedirs(dest, exist_ok=True)
        tmp = os.path.join(dest, TMP_NAME)
        if os.path.isdir(tmp):
            shutil.rmtree(tmp)
        free = shutil.disk_usage(dest).free
        need = total + (64 << 20)
        if free < need:
            err("Not enough free space in %s: the game files need %.1f GB, %.1f GB is free."
                % (dest, need / 1e9, free / 1e9))
            return 4

        prog = Progress(total, zen)
        use_cfr = hasattr(os, "copy_file_range")
        try:
            for i, (rel, e) in enumerate(files, 1):
                label = "Extracting %s (%d of %d files)" % (rel, i, len(files))
                prog.say(label, force=True)
                use_cfr = copy_entry(disc, e, os.path.join(tmp, rel), prog, label, use_cfr)
            # Empty directories on the disc stay as directories.
            for e in wanted:
                if e.is_dir:
                    os.makedirs(os.path.join(tmp, safe_name(e.name)), exist_ok=True)
            # default.xex last: Fable2.sh treats default.xex + data as "installed".
            names = sorted((safe_name(e.name) for e in wanted), key=lambda n: n.lower() == "default.xex")
            for n in names:
                os.replace(os.path.join(tmp, n), os.path.join(dest, n))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        secs = time.monotonic() - prog.start
        if zen:
            print(100, flush=True)
            print("# Done: %d files, %d MB in %d s" % (len(files), total >> 20, secs), flush=True)
        else:
            sys.stderr.write("\nDone: %d files, %d MB in %.0f s -> %s\n" % (len(files), total >> 20, secs, dest))
        return 0
    finally:
        disc.close()


def opt(args, name):
    if name in args:
        i = args.index(name)
        if i + 1 < len(args):
            return args[i + 1]
    return None


def err(msg):
    sys.stderr.write("x360extract: %s\n" % msg)


def main(argv):
    if len(argv) < 2 or argv[0] not in ("probe", "extract") or argv[0] == "extract" and len(argv) < 3:
        sys.stderr.write(__doc__)
        return 1
    try:
        return cmd_probe(argv[1:]) if argv[0] == "probe" else cmd_extract(argv[1:])
    except DiscError as e:
        err(str(e))
        return 2
    except OSError as e:
        err("%s: %s" % (getattr(e, "filename", "") or "", e.strerror or e))
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
