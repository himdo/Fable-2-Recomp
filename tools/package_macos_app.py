#!/usr/bin/env python3
"""Package an existing macOS runtime as a movable, locally signed app.

No game content, saves, caches or local config are included. --game-data-root
optionally embeds a local first-run hint; the launcher remembers the user's choice
outside the bundle. This tool never writes to the user's Application Support.
"""
import argparse
from pathlib import Path
import platform
import plistlib
import re
import shutil
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BACKENDS = {
    'metal': ('librexruntime.dylib', 'libTracyClient.dylib', 'librexgpu-metal.dylib',
              'libdxilconv.dylib', 'libmetalirconverter.dylib'),
    'vulkan': ('librexruntime.dylib', 'libTracyClient.dylib', 'librexgpu-xenos.dylib'),
}

def run(*args):
    subprocess.run([str(a) for a in args], check=True)

def localize(binary, libraries):
    # Resolve dependencies beside the loading library rather than in the build
    # machine's SDK. Vulkan's separately loaded MoltenVK lives in Contents/lib.
    output = subprocess.check_output(['otool', '-l', str(binary)], text=True)
    paths = set(re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', output))
    for path in sorted(paths):
        run('install_name_tool', '-delete_rpath', path, binary)
    run('install_name_tool', '-add_rpath', '@loader_path', binary)
    output = subprocess.check_output(['otool', '-L', str(binary)], text=True)
    for line in output.splitlines():
        if not line.startswith('\t'):
            continue
        dependency = line.strip().split(' (compatibility')[0]
        if dependency.startswith(('/usr/lib/', '/System/Library/')):
            continue
        name = Path(dependency).name
        if name not in libraries:
            raise ValueError(f'Unbundled dependency: {dependency}')
        if dependency != '@rpath/' + name:
            run('install_name_tool', '-change', dependency, '@rpath/' + name, binary)
    if binary.suffix == '.dylib':
        run('install_name_tool', '-id', '@rpath/' + binary.name, binary)
    run('codesign', '--force', '--sign', '-', binary)

def publish(app, output, replace=False):
    """Keep a previous build intact until the new bundle is fully verified."""
    if output.is_symlink():
        raise ValueError('Refusing to replace an app symlink')
    if not output.exists():
        app.rename(output)
        return
    if not replace:
        raise FileExistsError(f'{output} already exists; use --replace to rebuild it')
    info = plistlib.loads((output / 'Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier') not in {'org.fable2recomp.metal', 'org.fable2recomp.vulkan'}:
        raise ValueError('Refusing to replace an unrelated app')
    previous = app.parent / 'previous.app'
    output.rename(previous)
    try:
        app.rename(output)
    except BaseException:
        previous.rename(output)
        raise
    shutil.rmtree(previous)


def package(runtime, output, game_data_root=None, backend='metal', replace=False, sdk_lib_dir=None, configuration='Release'):
    runtime, output = runtime.resolve(), output.absolute()
    if output.suffix != '.app':
        raise ValueError('Output must end in .app')
    if (output.exists() or output.is_symlink()) and not replace:
        raise FileExistsError(f'{output} already exists; use --replace to rebuild it')
    libraries = BACKENDS[backend]
    if backend == 'vulkan':
        postfix = {'Debug': 'd', 'RelWithDebInfo': 'rd'}.get(configuration, '')
        libraries = tuple(name.replace('.dylib', postfix + '.dylib') for name in libraries)
        if sdk_lib_dir is None:
            raise ValueError('Vulkan packaging requires --sdk-lib-dir for its bundled MoltenVK runtime')
    archs = subprocess.check_output(['lipo', '-archs', str(runtime / 'fable_2')], text=True).split()
    if len(archs) != 1 or archs[0] not in {'arm64', 'x86_64'}:
        raise ValueError('Build one Mac architecture at a time')
    arch = archs[0]
    if backend == 'metal' and arch != 'arm64':
        raise ValueError('The Metal backend requires Apple Silicon')
    for name in ('fable_2', *libraries):
        if not (runtime / name).is_file():
            raise FileNotFoundError(runtime / name)
    if game_data_root and not ((game_data_root / 'default.xex').is_file() and (game_data_root / 'data').is_dir()):
        raise ValueError('Game folder must contain default.xex and data/')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fable-app-', dir=output.parent) as temporary:
        work = Path(temporary)
        app = work / output.name
        macos = app / 'Contents/MacOS'
        resources = app / 'Contents/Resources'
        macos.mkdir(parents=True)
        resources.mkdir()
        minimum_versions = ["15.0"]
        for name in ('fable_2', *libraries):
            dst = macos / name
            shutil.copy2(runtime / name, dst)
            # Match the executable architecture even if a dependency is universal.
            archs = subprocess.check_output(['lipo', '-archs', str(dst)], text=True).split()
            if arch not in archs:
                raise ValueError(f'{name} has no {arch} slice')
            if len(archs) > 1:
                thin = work / name
                run('lipo', dst, '-thin', arch, '-output', thin)
                shutil.copy2(thin, dst)
            if backend == 'vulkan' and arch == 'arm64' and name == 'librexgpu-xenos.dylib':
                run(sys.executable, ROOT / 'tools/macos_terrain_fix.py', dst)
            localize(dst, libraries)
            commands = subprocess.check_output(['otool', '-l', str(dst)], text=True)
            minimum_versions.extend(re.findall(r'\bminos ([0-9.]+)', commands))
        if backend == 'vulkan':
            # SDK detection checks Contents/lib before any system Vulkan SDK.
            lib = app / 'Contents/lib'
            lib.mkdir()
            molten = lib / 'libMoltenVK.dylib'
            shutil.copy2(sdk_lib_dir / molten.name, molten)
            localize(molten, ('libMoltenVK.dylib', 'libMoltenVK.1.dylib'))
            commands = subprocess.check_output(['otool', '-l', str(molten)], text=True)
            minimum_versions.extend(re.findall(r'\bminos ([0-9.]+)', commands))
        run('clang', '-fobjc-arc', '-arch', arch, '-mmacosx-version-min=15.0',
            '-framework', 'AppKit', ROOT / 'tools/macos/AppLauncher.m', '-o', macos / 'FableIILauncher')
        run('clang', '-fobjc-arc', '-framework', 'AppKit', ROOT / 'tools/macos/MakeIcon.m', '-o', work / 'make-icon')
        icons = work / 'FableII.iconset'
        icons.mkdir()
        run(work / 'make-icon', icons)
        run('iconutil', '-c', 'icns', icons, '-o', resources / 'FableII.icns')
        for name in ('fable2_config.toml', 'fable2_patches.toml'):
            shutil.copy2(ROOT / 'config' / name, resources / name)
        if game_data_root:
            (resources / 'LocalDefaults.plist').write_bytes(plistlib.dumps({'GameDataRoot': str(game_data_root.resolve())}))
        info = {
            'CFBundleExecutable': 'FableIILauncher', 'CFBundleIdentifier': 'org.fable2recomp.' + backend, 'Fable2Backend': backend,
            'CFBundleName': 'Fable II', 'CFBundleDisplayName': 'Fable II', 'CFBundlePackageType': 'APPL',
            'CFBundleShortVersionString': '0.1.0', 'CFBundleVersion': '1',
            'CFBundleIconFile': 'FableII', 'LSMinimumSystemVersion': max(minimum_versions, key=lambda v: tuple(map(int, v.split('.')))),
            'LSArchitecturePriority': [arch], 'LSMultipleInstancesProhibited': True,
            'NSHighResolutionCapable': True,
        }
        (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
        (app / 'Contents/PkgInfo').write_bytes(b'APPL????')
        run('codesign', '--force', '--sign', '-', macos / 'FableIILauncher')
        run('codesign', '--force', '--sign', '-', app)
        run('codesign', '--verify', '--deep', '--strict', app)
        publish(app, output, replace)
    print(f'Created {output}')
    return output

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=ROOT / 'out/build/mac-arm64-metal-release')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/app/Fable II.app')
    parser.add_argument('--game-data-root', type=Path)
    parser.add_argument('--sdk-lib-dir', type=Path)
    parser.add_argument('--configuration', default='Release')
    parser.add_argument('--backend', choices=BACKENDS, default='metal')
    parser.add_argument('--replace', action='store_true', help='Replace a previous generated app after successful packaging')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('macOS is required')
    package(args.runtime, args.output, args.game_data_root, args.backend, args.replace, args.sdk_lib_dir, args.configuration)

if __name__ == '__main__':
    main()
