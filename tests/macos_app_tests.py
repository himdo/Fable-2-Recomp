#!/usr/bin/env python3
"""Opt-in native app smoke test. Use a NEW isolated state folder and local game data.

Requires desktop access. Runs for 45 seconds, stops only its own process, verifies
Metal/input startup, external state, and an unchanged, correctly signed bundle.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import subprocess
import time


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--game-data-root', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    app, state = args.app.resolve(), args.state.resolve()
    if state.exists():
        raise RuntimeError('Test state must be a new directory; never use real saves')
    if subprocess.run(['pgrep', '-x', 'fable_2'], capture_output=True).returncode != 1:
        raise RuntimeError('A game is running or the process check failed')
    before = hashes(app)
    assert not any(p in before for p in ['Contents/MacOS/default.xex', 'Contents/MacOS/fable2_config.toml'])
    assert not any('/saves/' in p or '/cache/' in p for p in before)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    state.mkdir(parents=True)
    # Existing user settings must survive first launch and default-config staging.
    config = app / 'Contents/Resources/fable2_config.toml'
    original = config.read_bytes() + b'\n# Preserve my existing settings.\n'
    (state / config.name).write_bytes(original)
    env = dict(os.environ, FABLE2_APP_STATE_ROOT=str(state), FABLE2_GAME_DATA_ROOT=str(args.game_data_root.resolve()))
    start = time.monotonic()
    process = subprocess.Popen([str(app / 'Contents/MacOS/FableIILauncher')], env=env)
    try:
        while process.poll() is None and time.monotonic() - start < 45:
            time.sleep(0.5)
        alive = process.poll() is None
        if alive:
            process.terminate()
        try:
            code = process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            code = process.wait()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    logs = '\n'.join(p.read_text(errors='replace') for p in (state / 'logs').glob('*.log'))
    result = {
        'seconds': round(time.monotonic() - start, 2), 'exit_code': code,
        'stayed_running': alive,
        'metal_initialized': 'XeniOS Metal renderer' in logs,
        'input_attached': 'macOS keyboard/mouse input attached' in logs,
        'bundle_unchanged': hashes(app) == before,
        'existing_settings_preserved': (state / config.name).read_bytes() == original,
        'external_save_files': len(list((state / 'saves').rglob('*'))),
        'content_selection_remembered': plistlib.loads((state / 'launcher.plist').read_bytes())['GameDataRoot'] == str(args.game_data_root.resolve()),
        'scope': 'Bounded startup only; no manual gameplay validation',
    }
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    assert code == 0 and all(result[k] for k in ['stayed_running', 'metal_initialized', 'input_attached',
        'bundle_unchanged', 'existing_settings_preserved', 'content_selection_remembered'])

if __name__ == '__main__':
    main()
