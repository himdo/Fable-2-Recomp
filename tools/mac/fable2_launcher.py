#!/usr/bin/env python3
"""Fable 2 Recomp macOS Launcher Core (CLI & API).

Provides ISO extraction with GDFX, XEX hash verification against
constants/game_versions.json, non-destructive config management for
fable_2.toml and fable2_config.toml, quality presets, and launch orchestration.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
from typing import Callable, Dict, Iterator, List, Optional, Tuple

SECTOR_SIZE = 2048
GDFX_MAGIC = b"MICROSOFT*XBOX*MEDIA"
PARTITION_OFFSETS = (0x0, 0xFD90000, 0x2080000, 0x18300000)

DEFAULT_KEYBOARD_MAP = (
    "LMB:X,RMB:Y,MMB:B,Shift:A,E:A,Q:LT,Tab:RB,R:RT,Z:LB,B:B,"
    "W:StickUp,S:StickDown,A:StickLeft,D:StickRight,Escape:Pause,M:Select,"
    "Up:Up,Down:Down,Left:Left,Right:Right,X:X"
)


def get_repo_root() -> Path:
    """Returns the workspace root directory."""
    env_root = os.environ.get("FABLE2_ROOT") or os.environ.get("FABLE2_REPO_ROOT")
    if env_root and Path(env_root).is_dir():
        return Path(env_root).resolve()

    # Try relative to this file
    p = Path(__file__).resolve()
    for parent in [p.parent] + list(p.parents):
        if (parent / "constants" / "game_versions.json").is_file():
            return parent
        if (parent / "CMakeLists.txt").is_file() and (parent / "tools").is_dir():
            return parent

    # Fallback to standard known path
    standard = Path("/Users/antoniorodriguez/GITHUB-RASTY/Fable-2-Recomp")
    if standard.is_dir():
        return standard

    return p.parents[2] if len(p.parents) > 2 else Path.cwd()


def get_default_build_dir(repo_root: Optional[Path] = None) -> Path:
    """Returns the path to out/build/mac-arm64-release."""
    root = repo_root or get_repo_root()
    return root / "out" / "build" / "mac-arm64-release"


# =============================================================================
# 1. GDFX Extractor & ISO Verification
# =============================================================================

class GdfxExtractor:
    """Extracts Xbox 360 GDFX ISOs with pre-hashing validation of default.xex."""

    def __init__(self, iso_path: Path):
        self.iso_path = Path(iso_path)
        if not self.iso_path.is_file():
            raise FileNotFoundError(f"ISO file not found: {self.iso_path}")

    def inspect_header(self, f) -> Tuple[int, int, int]:
        """Finds GDFX partition base, root_sector, root_size."""
        iso_len = os.fstat(f.fileno()).st_size
        for base in PARTITION_OFFSETS:
            f.seek(base + 0x10000)
            hdr = f.read(0x24)
            if hdr[:20] == GDFX_MAGIC:
                root_sec, root_sz = struct.unpack_from("<II", hdr, 20)
                return base, root_sec, root_sz
        raise ValueError("No GDFX volume header found in ISO")

    def _read_dir_entries(self, f, base: int, sector: int, size: int) -> Iterator[Tuple[str, int, int, bool]]:
        start = base + sector * SECTOR_SIZE
        end = start + size
        pos = start
        iso_len = os.fstat(f.fileno()).st_size

        while pos + 14 <= end and pos + 14 <= iso_len:
            f.seek(pos)
            rec = f.read(14)
            if struct.unpack_from("<I", rec)[0] == 0xFFFFFFFF:
                nxt = start + ((((pos - start) >> 11) + 1) << 11)
                if nxt < end:
                    f.seek(pos)
                    if f.read(nxt - pos) == b"\xff" * (nxt - pos):
                        pos = nxt
                        continue
                return
            esector, esize, attrs, nlen = struct.unpack_from("<IIBB", rec, 4)
            if not 1 <= nlen <= 100:
                return
            name_bytes = f.read(nlen)
            if any(c <= 0x20 or c >= 0x7F for c in name_bytes):
                return
            name = name_bytes.decode("ascii", errors="replace")
            if name in (".", "..") or "/" in name or "\\" in name:
                raise ValueError(f"Unsafe entry name: {name}")
            is_dir = bool(attrs & 0x10)
            yield name, esector, esize, is_dir
            pos += (14 + nlen + 3) & ~3

    def list_files(self) -> List[Tuple[str, int, int]]:
        """Returns flat list of (relative_path, sector, size)."""
        results: List[Tuple[str, int, int]] = []
        with open(self.iso_path, "rb") as f:
            base, root_sec, root_sz = self.inspect_header(f)

            def recurse(sec: int, sz: int, prefix: str):
                for name, esec, esz, is_dir in self._read_dir_entries(f, base, sec, sz):
                    rel = f"{prefix}/{name}" if prefix else name
                    if is_dir:
                        recurse(esec, esz, rel)
                    else:
                        results.append((rel, esec, esz))

            recurse(root_sec, root_sz, "")
        return results

    def extract_and_hash_xex(self) -> Tuple[bytes, str]:
        """Extracts default.xex into memory and calculates its SHA-256."""
        with open(self.iso_path, "rb") as f:
            base, root_sec, root_sz = self.inspect_header(f)
            for name, esec, esz, is_dir in self._read_dir_entries(f, base, root_sec, root_sz):
                if not is_dir and name.lower() == "default.xex":
                    f.seek(base + esec * SECTOR_SIZE)
                    data = f.read(esz)
                    sha = hashlib.sha256(data).hexdigest()
                    return data, sha
        raise FileNotFoundError("default.xex not found in GDFX root directory")

    def extract_all(
        self,
        dest_dir: Path,
        progress_cb: Optional[Callable[[int, int, str], None]] = None,
        cancel_check: Optional[Callable[[], bool]] = None
    ) -> Dict[str, any]:
        """Extracts the full ISO to dest_dir with progress."""
        dest_dir = Path(dest_dir)
        dest_dir.mkdir(parents=True, exist_ok=True)

        files = self.list_files()
        total_bytes = sum(sz for _, _, sz in files)
        copied_bytes = 0

        with open(self.iso_path, "rb") as f:
            base, _, _ = self.inspect_header(f)
            for rel_path, esec, esz in files:
                if cancel_check and cancel_check():
                    return {"status": "cancelled", "copied_bytes": copied_bytes, "total_bytes": total_bytes}

                target_file = dest_dir / rel_path
                target_file.parent.mkdir(parents=True, exist_ok=True)
                if target_file.exists() and not os.access(target_file, os.W_OK):
                    try:
                        os.chmod(target_file, 0o666)
                    except OSError:
                        pass

                f.seek(base + esec * SECTOR_SIZE)
                remaining = esz
                with open(target_file, "wb") as out_f:
                    while remaining > 0:
                        if cancel_check and cancel_check():
                            return {"status": "cancelled", "copied_bytes": copied_bytes, "total_bytes": total_bytes}
                        chunk_sz = min(remaining, 4 * 1024 * 1024)
                        chunk = f.read(chunk_sz)
                        if not chunk:
                            break
                        out_f.write(chunk)
                        remaining -= len(chunk)
                        copied_bytes += len(chunk)
                        if progress_cb:
                            progress_cb(copied_bytes, total_bytes, rel_path)

        return {"status": "completed", "copied_bytes": copied_bytes, "total_bytes": total_bytes, "file_count": len(files)}


# =============================================================================
# 2. Version Catalog & Verification
# =============================================================================

class GameCatalog:
    """Loads and inspects versions from constants/game_versions.json."""

    def __init__(self, catalog_path: Optional[Path] = None):
        if catalog_path:
            path = Path(catalog_path)
        else:
            bundled = Path(__file__).resolve().parent / "constants" / "game_versions.json"
            if bundled.is_file():
                path = bundled
            else:
                path = get_repo_root() / "constants" / "game_versions.json"
        with open(path, "r", encoding="utf-8") as f:
            self.data = json.load(f)
        self.versions = self.data.get("versions", [])

    def find_by_hash(self, sha256_hash: str) -> Optional[Dict[str, any]]:
        h = sha256_hash.lower()
        for v in self.versions:
            if v.get("hash", "").lower() == h:
                return v
        return None

    def verify_directory(self, game_dir: Path) -> Dict[str, any]:
        """Checks default.xex hash and game directory completeness."""
        game_dir = Path(game_dir)
        xex_path = game_dir / "default.xex"
        if not xex_path.is_file():
            return {"installed": False, "error": f"default.xex not found in {game_dir}"}

        hasher = hashlib.sha256()
        with open(xex_path, "rb") as f:
            while chunk := f.read(1024 * 1024):
                hasher.update(chunk)
        xex_hash = hasher.hexdigest()
        info = self.find_by_hash(xex_hash)

        req_missing = []
        if info:
            for req in info.get("requiredFiles", []):
                if not (game_dir / req).exists():
                    req_missing.append(req)

        return {
            "installed": True,
            "xex_path": str(xex_path),
            "xex_hash": xex_hash,
            "version_info": info,
            "compatible": info.get("compatible", False) if info else False,
            "profile_id": info.get("id", "unknown") if info else "unknown",
            "profile_name": info.get("name", "Unknown XEX") if info else "Unknown XEX",
            "missing_required_files": req_missing
        }


# =============================================================================
# 3. Non-Destructive In-Place TOML Configuration Manager
# =============================================================================

class TomlConfigManager:
    """Non-destructive in-place TOML updater preserving comments and layout."""

    ASSIGNMENT_RE = re.compile(r"^(\s*)([A-Za-z0-9_]+)(\s*=\s*)(.*?)(\s+#.*)?$")
    SECTION_RE = re.compile(r"^\s*\[([A-Za-z0-9_.-]+)\]\s*$")

    @classmethod
    def read_root_values(cls, file_path: Path) -> Dict[str, str]:
        """Reads root-level key = value pairs before any [section]."""
        vals: Dict[str, str] = {}
        if not file_path.is_file():
            return vals
        for line in file_path.read_text(encoding="utf-8").splitlines():
            line_str = line.strip()
            if line_str.startswith("["):
                break
            m = cls.ASSIGNMENT_RE.match(line)
            if m:
                vals[m.group(2)] = m.group(4).strip()
        return vals

    @classmethod
    def update_root_values(cls, file_path: Path, updates: Dict[str, str]) -> None:
        """Updates or adds root-level key = value pairs preserving comments."""
        lines: List[str] = []
        if file_path.is_file():
            lines = file_path.read_text(encoding="utf-8").splitlines()
        else:
            lines = ["# ReXGlue SDK configuration"]

        updated = set()
        root_end = len(lines)
        for i, l in enumerate(lines):
            if l.strip().startswith("["):
                root_end = i
                break

        for i in range(root_end):
            m = cls.ASSIGNMENT_RE.match(lines[i])
            if m:
                key = m.group(2)
                if key in updates:
                    val = updates[key]
                    comment = m.group(5) or ""
                    lines[i] = f"{m.group(1)}{key}{m.group(3)}{val}{comment}"
                    updated.add(key)

        # Append missing keys before root_end
        new_lines = []
        for k, v in updates.items():
            if k not in updated:
                new_lines.append(f"{k} = {v}")

        if new_lines:
            lines = lines[:root_end] + new_lines + lines[root_end:]

        file_path.parent.mkdir(parents=True, exist_ok=True)
        file_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    @classmethod
    def read_section_values(cls, file_path: Path) -> Dict[str, Dict[str, str]]:
        """Reads section-level key = value pairs."""
        sections: Dict[str, Dict[str, str]] = {}
        curr_sec: Optional[str] = None
        if not file_path.is_file():
            return sections

        for line in file_path.read_text(encoding="utf-8").splitlines():
            sec_m = cls.SECTION_RE.match(line)
            if sec_m:
                curr_sec = sec_m.group(1)
                sections.setdefault(curr_sec, {})
                continue
            if curr_sec:
                m = cls.ASSIGNMENT_RE.match(line)
                if m:
                    sections[curr_sec][m.group(2)] = m.group(4).strip()
        return sections

    @classmethod
    def update_section_value(cls, file_path: Path, section: str, key: str, value: str) -> None:
        """Updates a single key in a specific section preserving comments."""
        if not file_path.is_file():
            raise FileNotFoundError(f"Config file not found: {file_path}")

        lines = file_path.read_text(encoding="utf-8").splitlines()
        curr_sec: Optional[str] = None
        key_found = False

        for i, l in enumerate(lines):
            sec_m = cls.SECTION_RE.match(l)
            if sec_m:
                curr_sec = sec_m.group(1)
                continue
            if curr_sec == section:
                m = cls.ASSIGNMENT_RE.match(l)
                if m and m.group(2) == key:
                    comment = m.group(5) or ""
                    lines[i] = f"{m.group(1)}{key}{m.group(3)}{value}{comment}"
                    key_found = True
                    break

        if not key_found:
            # Append to section
            sec_idx = -1
            next_sec_idx = len(lines)
            for i, l in enumerate(lines):
                sec_m = cls.SECTION_RE.match(l)
                if sec_m:
                    if sec_m.group(1) == section:
                        sec_idx = i
                    elif sec_idx != -1:
                        next_sec_idx = i
                        break
            if sec_idx != -1:
                lines.insert(next_sec_idx, f"{key} = {value}")
            else:
                lines.extend([f"[{section}]", f"{key} = {value}"])

        file_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# =============================================================================
# 4. Preset Manager
# =============================================================================

PRESETS = {
    "original": {
        "description": "Original Xbox 360 experience: 720p windowed, 30 FPS cap",
        "fable_2_toml": {
            "window_width": "1280",
            "window_height": "720",
            "resolution_scale": "1",
            "fullscreen": "false",
            "fullscreen_exclusive": "false",
            "vsync": "true",
            "frame_limit": "30",
            "guest_vblank_pacing": "true",
            "swap_post_effect": "\"none\""
        },
        "fable2_config_toml": {
            ("patches", "high_tick_rate"): "false",
            ("patches", "higher_hf_tick_rate"): "false",
            ("patches", "skip_intro_videos"): "true",
            ("patches", "unlock_website"): "true",
            ("patches", "unlock_ce"): "true"
        }
    },
    "balanced": {
        "description": "Balanced: 1080p, 30 FPS cap, crisp presentation",
        "fable_2_toml": {
            "window_width": "1920",
            "window_height": "1080",
            "resolution_scale": "1",
            "fullscreen": "false",
            "fullscreen_exclusive": "false",
            "vsync": "true",
            "frame_limit": "30",
            "guest_vblank_pacing": "true",
            "swap_post_effect": "\"fxaa\""
        },
        "fable2_config_toml": {
            ("patches", "high_tick_rate"): "false",
            ("patches", "higher_hf_tick_rate"): "false",
            ("patches", "skip_intro_videos"): "true",
            ("patches", "unlock_website"): "true",
            ("patches", "unlock_ce"): "true"
        }
    },
    "enhanced": {
        "description": "Enhanced: Fullscreen Retina display, native 30 FPS cap, FXAA",
        "fable_2_toml": {
            "window_width": "3024",
            "window_height": "1964",
            "resolution_scale": "1",
            "fullscreen": "true",
            "fullscreen_exclusive": "false",
            "vsync": "true",
            "frame_limit": "30",
            "guest_vblank_pacing": "true",
            "swap_post_effect": "\"fxaa\""
        },
        "fable2_config_toml": {
            ("patches", "high_tick_rate"): "false",
            ("patches", "higher_hf_tick_rate"): "false",
            ("patches", "skip_intro_videos"): "true",
            ("patches", "unlock_website"): "true",
            ("patches", "unlock_ce"): "true"
        }
    },
    "high-fps": {
        "description": "High Tick Rate: 30/60 Hz patches for smooth in-game UI/camera",
        "fable_2_toml": {
            "window_width": "1920",
            "window_height": "1080",
            "resolution_scale": "1",
            "fullscreen": "false",
            "fullscreen_exclusive": "false",
            "vsync": "true",
            "frame_limit": "60",
            "guest_vblank_pacing": "false",
            "swap_post_effect": "\"fxaa\""
        },
        "fable2_config_toml": {
            ("patches", "high_tick_rate"): "true",
            ("patches", "higher_hf_tick_rate"): "true",
            ("patches", "skip_intro_videos"): "true",
            ("patches", "unlock_website"): "true",
            ("patches", "unlock_ce"): "true"
        }
    }
}


def apply_preset(name: str, build_dir: Optional[Path] = None) -> bool:
    preset = PRESETS.get(name.lower())
    if not preset:
        return False
    b_dir = build_dir or get_default_build_dir()
    f2_toml = b_dir / "fable_2.toml"
    f2_cfg = b_dir / "fable2_config.toml"

    # Apply fable_2.toml cvars
    TomlConfigManager.update_root_values(f2_toml, preset["fable_2_toml"])

    # Apply fable2_config.toml patches
    if f2_cfg.is_file():
        for (sec, key), val in preset["fable2_config_toml"].items():
            TomlConfigManager.update_section_value(f2_cfg, sec, key, val)
    return True


# =============================================================================
# 5. Game Runner & Diagnostics
# =============================================================================

def get_last_measured_fps(logs_dir: Optional[Path] = None) -> Optional[float]:
    """Reads latest FPS from guest vblank pacer in the newest log."""
    d = logs_dir or (get_default_build_dir() / "logs")
    if not d.is_dir():
        return None
    logs = sorted(d.glob("fable_2_*.log"), key=os.path.getmtime, reverse=True)
    if not logs:
        return None
    latest_log = logs[0]
    pacer_re = re.compile(r"guest frame \(gate\) ~([0-9.]+) Hz")
    fps_values = []
    try:
        with open(latest_log, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if "guest vblank pacer" in line:
                    m = pacer_re.search(line)
                    if m:
                        fps_values.append(float(m.group(1)))
        if fps_values:
            # Average of last 10 samples
            recent = fps_values[-10:]
            return round(sum(recent) / len(recent), 1)
    except Exception:
        pass
    return None


def run_game(
    build_dir: Optional[Path] = None,
    game_data_root: Optional[Path] = None,
    extra_args: Optional[List[str]] = None,
    detach: bool = False
) -> subprocess.Popen:
    """Launches fable_2 with proper macOS environment."""
    b_dir = build_dir or get_default_build_dir()
    binary = b_dir / "fable_2"
    if not binary.is_file():
        raise FileNotFoundError(f"fable_2 binary not found in {b_dir}")

    env = os.environ.copy()
    # Ensure SDL uses non-Spaces fullscreen to avoid black screen
    env["SDL_VIDEO_MAC_FULLSCREEN_SPACES"] = "0"

    root = str(game_data_root or get_repo_root())
    cmd = [str(binary), f"--game_data_root={root}"]
    if extra_args:
        cmd.extend(extra_args)

    if detach:
        return subprocess.Popen(cmd, cwd=str(b_dir), env=env, start_new_session=True)
    else:
        return subprocess.Popen(cmd, cwd=str(b_dir), env=env)


# =============================================================================
# 6. CLI Dispatcher
# =============================================================================

def cli_main() -> None:
    parser = argparse.ArgumentParser(
        description="Fable 2 Recomp macOS Launcher CLI",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # verify
    p_verify = sub.add_parser("verify", help="Verify game files and default.xex SHA-256")
    p_verify.add_argument("--dir", default=None, help="Game data directory (default: workspace root)")

    # extract
    p_extract = sub.add_parser("extract", help="Extract Fable 2 Xbox 360 GDFX ISO")
    p_extract.add_argument("iso", help="Path to the .iso file")
    p_extract.add_argument("--dest", default=None, help="Destination directory (default: workspace root)")
    p_extract.add_argument("--force", action="store_true", help="Extract even if default.xex hash is unknown")
    p_extract.add_argument("--json", action="store_true", help="Output progress as JSON stream")

    # preset
    p_preset = sub.add_parser("preset", help="View or apply quality presets")
    p_preset.add_argument("name", nargs="?", choices=["original", "balanced", "enhanced", "high-fps", "list"], default="list")

    # config
    p_config = sub.add_parser("config", help="Read or update configuration")
    p_config.add_argument("action", choices=["get", "set", "show"])
    p_config.add_argument("target", nargs="?", help="Target key (e.g. patches.skip_intro_videos or window_width)")
    p_config.add_argument("value", nargs="?", help="New value for 'set'")

    # status
    sub.add_parser("status", help="Show system, game and save diagnostics")

    # run
    p_run = sub.add_parser("run", help="Launch Fable 2 Recomp")
    p_run.add_argument("--fullscreen", choices=["true", "false"], default=None)
    p_run.add_argument("--resolution", choices=["720p", "1080p", "1440p", "retina"], default=None)
    p_run.add_argument("--detach", action="store_true", help="Launch in background detached")

    args, unknown = parser.parse_known_args()
    root = get_repo_root()
    build_dir = get_default_build_dir(root)

    if args.command == "verify":
        target_dir = Path(args.dir) if args.dir else root
        cat = GameCatalog()
        res = cat.verify_directory(target_dir)
        print(json.dumps(res, indent=2))
        sys.exit(0 if res.get("compatible") else 1)

    elif args.command == "extract":
        iso_p = Path(args.iso)
        dest = Path(args.dest) if args.dest else root
        extractor = GdfxExtractor(iso_p)

        # 1. Gate: Pre-hash XEX
        data, sha = extractor.extract_and_hash_xex()
        cat = GameCatalog()
        ver = cat.find_by_hash(sha)
        if not ver and not args.force:
            print(f"[ERROR] Unknown default.xex hash ({sha}). Use --force to extract anyway.", file=sys.stderr)
            sys.exit(1)

        edition = ver["name"] if ver else "Unknown"
        if not args.json:
            print(f"[GDFX] Verified {edition} (SHA-256: {sha[:16]}...). Extracting to {dest}...")
        else:
            print(json.dumps({"status": "starting", "edition": edition, "dest": str(dest)}), flush=True)

        last_print = 0.0

        def prog_cb(copied, total, curr):
            nonlocal last_print
            now = time.time()
            if args.json:
                print(json.dumps({"copied": copied, "total": total, "percent": round(copied / total * 100, 1), "file": curr}), flush=True)
            elif now - last_print >= 0.5 or copied == total:
                pct = (copied / total) * 100
                mb_c = copied / (1024 * 1024)
                mb_t = total / (1024 * 1024)
                print(f"\rExtracting: {pct:5.1f}% [{mb_c:6.1f} / {mb_t:6.1f} MB] -> {curr[:40]:<40}", end="", flush=True)
                last_print = now

        res = extractor.extract_all(dest, progress_cb=prog_cb)
        if not args.json:
            print("\n[GDFX] Extraction complete!")
        else:
            print(json.dumps({"status": "completed", "percent": 100.0, "file": "Complete"}), flush=True)

    elif args.command == "preset":
        if args.name == "list":
            print("Available Presets:")
            for k, v in PRESETS.items():
                print(f"  * {k:<10}: {v['description']}")
        else:
            if apply_preset(args.name, build_dir):
                print(f"Applied preset '{args.name}'.")
            else:
                print(f"Error applying preset '{args.name}'.", file=sys.stderr)
                sys.exit(1)

    elif args.command == "config":
        f2_toml = build_dir / "fable_2.toml"
        f2_cfg = build_dir / "fable2_config.toml"

        if args.action == "show":
            print(f"--- {f2_toml.name} (ReXGlue SDK) ---")
            for k, v in TomlConfigManager.read_root_values(f2_toml).items():
                print(f"  {k} = {v}")
            print(f"\n--- {f2_cfg.name} (Patches & Input) ---")
            for sec, kv in TomlConfigManager.read_section_values(f2_cfg).items():
                print(f"  [{sec}]")
                for k, v in kv.items():
                    print(f"    {k} = {v}")

        elif args.action == "set":
            if not args.target or not args.value:
                print("Usage: config set <key> <value>", file=sys.stderr)
                sys.exit(1)
            target = args.target
            val = args.value
            if "." in target:
                sec, k = target.split(".", 1)
                TomlConfigManager.update_section_value(f2_cfg, sec, k, val)
                print(f"Updated {f2_cfg.name} [{sec}] {k} = {val}")
            else:
                TomlConfigManager.update_root_values(f2_toml, {target: val})
                print(f"Updated {f2_toml.name} {target} = {val}")

    elif args.command == "status":
        cat = GameCatalog()
        v_res = cat.verify_directory(root)
        fps = get_last_measured_fps(build_dir / "logs")
        saves = list((build_dir / "saves").glob("**/mainsave.bin")) if (build_dir / "saves").exists() else []

        print("=== Fable 2 Recomp macOS Status ===")
        print(f"Game Installed:  {v_res.get('installed')} ({v_res.get('profile_name')})")
        print(f"Profile ID:      {v_res.get('profile_id')} (Compatible: {v_res.get('compatible')})")
        print(f"Executable:      {build_dir / 'fable_2'} ({'Present' if (build_dir / 'fable_2').exists() else 'Missing'})")
        print(f"Saves found:     {len(saves)} save profiles")
        print(f"Recent Game FPS: {fps or 'N/A'} FPS")

    elif args.command == "run":
        extra = list(unknown)
        if args.fullscreen:
            extra.append(f"--fullscreen={args.fullscreen}")
        if args.resolution:
            res_map = {
                "720p": ("1280", "720"),
                "1080p": ("1920", "1080"),
                "1440p": ("2560", "1440"),
                "retina": ("3024", "1964")
            }
            w, h = res_map[args.resolution]
            extra.extend([f"--window_width={w}", f"--window_height={h}"])

        print(f"Launching Fable 2 with extra args: {extra}")
        proc = run_game(build_dir, root, extra_args=extra, detach=args.detach)
        if not args.detach:
            proc.wait()


if __name__ == "__main__":
    cli_main()
