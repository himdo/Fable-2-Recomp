"""Compile XeniOS's built-in Metal helpers using its shader_cc MSL arguments."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import subprocess
import tempfile

root=Path(__import__("sys").argv[1]).resolve()
# Xcode 27's default wrapper may ignore the installed asset toolchain.
component=__import__('json').loads(subprocess.check_output(['xcodebuild','-showComponent','MetalToolchain','-json'],text=True))
print(component, flush=True)
metal_bin=Path(component['toolchainSearchPath'])/'Metal.xctoolchain/usr/bin'
xenia=root/'xenios-source/src/xenia'
jobs=[]
for area in ['gpu','ui']:
 for p in (xenia/area/'shaders').glob('*.xesl'):
  if ('ffx_' in p.name or 'fxaa' in p.name) or not re.search(r'\.(vs|ps|cs)\.xesl$',p.name):continue
  jobs.append((area,p,p.stem.replace('.','_'),[]))
for uint in [0,1]:
 for full,bpps in [(False,[32,64]),(True,[8,16,32,64,128])]:
  for bpp in bpps:
   for msaa in [1,2,4]:
    for scaled in [False,True]:
     stem='resolve_host_color'+('_full' if full else '')
     prefix='XE_RESOLVE_HOST_COLOR_FULL_DEST' if full else 'XE_RESOLVE_HOST_COLOR'
     name=stem+('_uint' if uint else '')+f'_{bpp}bpp_{msaa}xmsaa'+('_scaled' if scaled else '')+'_cs'
     defines=[f'{prefix}_BPP={bpp}',f'XE_RESOLVE_HOST_COLOR_MSAA_SAMPLES={msaa}',f'XE_RESOLVE_HOST_COLOR_SOURCE_UINT={uint}']
     if scaled:defines+=['XE_RESOLVE_RESOLUTION_SCALED=1']
     jobs.append(('gpu',xenia/'gpu/shaders'/(stem+'_entry.xesli'),name,defines))
for msaa in [1,2,4]:
 for scaled in [False,True]:
  name=f'resolve_host_depth_32bpp_{msaa}xmsaa'+('_scaled' if scaled else '')+'_cs'
  defines=[f'XE_RESOLVE_HOST_DEPTH_MSAA_SAMPLES={msaa}']
  if scaled:defines+=['XE_RESOLVE_RESOLUTION_SCALED=1']
  jobs.append(('gpu',xenia/'gpu/shaders/resolve_host_depth_entry.xesli',name,defines))
jobs.append(('gpu',xenia/'gpu/shaders/texture_upload_repack.metal','texture_upload_repack',[]))
def compile(job):
 area,source,name,defines=job
 dest=root/'generated/rex'/area.replace('gpu','graphics')/'shaders/bytecode/metal'/(name+'.h')
 if dest.exists():return None
 dest.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='fable-metal-helper-') as tmp:
  air=Path(tmp)/'shader.air';lib=Path(tmp)/'shader.metallib'
  cmd=[str(metal_bin/'metal'),'-x','metal','-std=macos-metal2.3','-mmacosx-version-min=15.0','-D','SHADING_LANGUAGE_MSL_XE=1','-w','-I',str(source.parent)]
  for d in defines:cmd+=['-D',d]
  cmd+=['-c',str(source),'-o',str(air)]
  result=subprocess.run(cmd,capture_output=True,text=True)
  if result.returncode:return name+': '+result.stderr
  result=subprocess.run([str(metal_bin/'metallib'),str(air),'-o',str(lib)],capture_output=True,text=True)
  if result.returncode:return name+': '+result.stderr
  data=lib.read_bytes()
  dest.write_text('#pragma once\n#include <cstdint>\nconst uint8_t '+name+'_metallib[] = {\n'+','.join(str(v) for v in data)+'\n};\n')
 return None
with ThreadPoolExecutor(max_workers=4) as pool:errors=[e for e in pool.map(compile,jobs) if e]
(root/'helper-errors.log').write_text('\n'.join(errors))
print('Helper jobs:',len(jobs),'failed:',len(errors))
if errors:print('\n'.join(errors[:3]))
raise SystemExit(bool(errors))
