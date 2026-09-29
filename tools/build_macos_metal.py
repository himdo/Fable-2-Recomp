#!/usr/bin/env python3
"""Build the Metal runtime and macOS app from pinned dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'backends/metal'
WORK = ROOT / 'out/metal-build'
DEPS = WORK / 'dependencies'

def run(args, **kw):
    subprocess.run([str(x) for x in args], check=True, **kw)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def unpack(archive, dest):
    # Dependency archives have one top-level directory. Reject escaping members.
    with tarfile.open(archive) as tf:
        for m in tf.getmembers():
            parts = Path(m.name).parts
            if m.name.startswith('/') or '..' in parts:
                raise ValueError('Unsafe archive path: ' + m.name)
        with tempfile.TemporaryDirectory(dir=dest.parent) as tmp:
            tf.extractall(tmp, filter='data')
            entries = list(Path(tmp).iterdir())
            if len(entries) != 1 or not entries[0].is_dir():
                raise ValueError('Expected one archive root')
            shutil.move(entries[0], dest)

def prepare(cache):
    DEPS.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    for pin in json.loads((SOURCE / 'dependencies.json').read_text()):
        archive = cache / (pin['name'] + '-' + pin['commit'] + '.tar.gz')
        if not archive.exists():
            partial = archive.with_suffix('.part')
            run(['curl', '--fail', '--location', '--show-error', pin['url'], '-o', partial])
            if sha(partial) != pin['sha256']:
                partial.unlink()
                raise ValueError('Download hash mismatch: ' + pin['name'])
            partial.replace(archive)
        if sha(archive) != pin['sha256']:
            raise ValueError('Cache hash mismatch: ' + str(archive))
        dest = DEPS / pin['name']
        if not dest.exists():
            unpack(archive, dest)
    for tree, patch in [('sdk-source', 'sdk-metal.patch'), ('DirectXShaderCompiler', 'dxilconv-build.patch')]:
        marker = DEPS / tree / '.fable-patch-sha256'
        expected = sha(SOURCE / patch)
        if marker.exists():
            if marker.read_text() != expected:
                raise ValueError('Patch changed; remove out/metal-build/dependencies/' + tree + ' and rebuild')
        else:
            run(['patch', '--batch', '-p1', '-i', SOURCE / patch], cwd=DEPS / tree)
            marker.write_text(expected)
    third = DEPS / 'third_party'
    third.mkdir(exist_ok=True)
    for name in ['metal-cpp', 'metal-shader-converter']:
        path = third / name
        if not path.exists():
            path.symlink_to(Path('..') / name, target_is_directory=True)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cache', type=Path, default=ROOT / 'out/downloads/metal')
    ap.add_argument('--prepare-only', action='store_true')
    ap.add_argument('--backend-only', action='store_true')
    ap.add_argument('--jobs', type=int, default=int(os.getenv('FABLE2_BUILD_JOBS', '4')))
    args = ap.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        ap.error('Apple Silicon macOS is required')
    if args.jobs < 1:
        ap.error('--jobs must be positive')
    prepare(args.cache.resolve())
    if args.prepare_only:
        return
    sdk = Path(os.getenv('REXGLUE_SDK_ROOT', ROOT / 'out/sdk/mac-arm64')).resolve()
    if not args.backend_only:
        run(['bash', ROOT / 'tools/build_macos.sh'])
    if not (sdk / 'bin/rexglue').is_file():
        raise ValueError('Install ReXGlue 0.10.0 via tools/build_macos.sh or set REXGLUE_SDK_ROOT')
    cmd = json.loads((SOURCE / 'dxilconv-configure.json').read_text())
    run([x.replace('{deps}', str(DEPS)) for x in cmd])
    run(['cmake', '--build', DEPS / 'build-dxilconv', '--target', 'dxilconv', '--parallel', args.jobs])
    run([sys.executable, ROOT / 'tools/compile_metal_helpers.py', DEPS])
    run(['cmake', '-S', SOURCE, '-B', WORK / 'backend', '-G', 'Unix Makefiles', '-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_PREFIX_PATH=' + str(sdk), '-DFABLE2_METAL_DEPS=' + str(DEPS)])
    run(['cmake', '--build', WORK / 'backend', '--parallel', args.jobs])
    if args.backend_only:
        return
    runtime = ROOT / 'out/build/mac-arm64-metal-release'
    runtime.mkdir(parents=True, exist_ok=True)
    game = ROOT / 'out/build/mac-arm64-release'
    sources = [game / 'fable_2', WORK / 'backend/librexgpu-metal.dylib',
               DEPS / 'build-dxilconv/lib/libdxilconv.dylib', DEPS / 'metal-shader-converter/lib/libmetalirconverter.dylib']
    sources.extend(p for p in game.glob('*.dylib') if p.name != 'librexgpu-xenos.dylib')
    for config in ['fable2_config.toml', 'fable2_patches.toml']:
        if not (runtime / config).exists():
            shutil.copy2(ROOT / 'config' / config, runtime / config)
    shutil.copytree(ROOT / 'src/lua', runtime / 'data/scripts/recomp', dirs_exist_ok=True)
    for src in sources:
        dst = runtime / src.name
        temporary = dst.with_suffix(dst.suffix + '.next')
        shutil.copy2(src, temporary)
        os.replace(temporary, dst)
    (runtime / 'metal-build.json').write_text(json.dumps({
        'dependencies': json.loads((SOURCE / 'dependencies.json').read_text()),
        'runtime_sha256': {p.name: sha(p) for p in sources},
    }, indent=2) + '\n')
    print('Built:', runtime, '\nLaunch run_macos_metal.command. Saves are not copied or merged.')
    run([sys.executable, ROOT / 'tools/package_macos_app.py',
         '--runtime', runtime, '--output', runtime / 'Fable II.app',
         '--game-data-root', ROOT, '--backend', 'metal', '--replace'])

if __name__ == '__main__':
    main()
