"""Build the source-only Remote Play dependency asset, separate from the portable ZIP."""
import argparse, hashlib, json, subprocess, zipfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--root',type=Path,default=Path('.'))
p.add_argument('--output',type=Path,required=True)
p.add_argument('--version',required=True)
a=p.parse_args(); root=a.root.resolve()
clean=Path('C:/veyra-deps/chiaki-source'); vcpkg=Path('C:/veyra-deps/vcpkg')
prefix=Path('C:/veyra-deps/remoteplay-installed/x64-windows-static')
def git(path,*args): return subprocess.check_output(['git','-C',str(path),*args]).decode().strip()
pin='0e16950165f06e5c3291537c2eeba6e852be7120'
if git(clean,'rev-parse','HEAD')!=pin or git(clean,'status','--porcelain','--untracked-files=no'): raise RuntimeError('Chiaki pin/clean mismatch')
if a.output.exists(): raise RuntimeError('Output exists')
a.output.parent.mkdir(parents=True,exist_ok=True)
records=[]; excluded=[]
bad={'.dll','.exe','.lib','.pdb','.obj','.o','.a','.so','.dylib','.onnx','.pth','.pt','.zip','.7z','.mp4','.log','.addon64'}
def put(z,path,name):
 if path.suffix.lower() in bad or '.git' in path.parts:
  excluded.append(name);return
 if path.is_symlink(): raise RuntimeError('Source symlink needs explicit review: '+str(path))
 data=path.read_bytes()
 z.writestr(name,data)
 records.append(dict(path=name,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
def tree(z,folder,name):
 for path in sorted(folder.rglob('*')):
  if path.is_file(): put(z,path,name+'/'+path.relative_to(folder).as_posix())
with zipfile.ZipFile(a.output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 modules=git(clean,'submodule','status','--recursive')
 z.writestr('chiaki-submodules.txt',modules+'\n')
 repos=[clean]
 for line in modules.splitlines():
  fields=line.split(); repos.append(clean/fields[1])
 for repo in repos:
  rel=repo.relative_to(clean).as_posix()
  for name in git(repo,'ls-files').splitlines():
   path=repo/name
   if path.is_file(): put(z,path,'chiaki-clean/'+(('' if rel=='.' else rel+'/'))+name)
 tree(z,root/'scripts/remoteplay','veyra/scripts/remoteplay')
 for name in ['cmake/VeyraRemotePlay.cmake','scripts/build.ps1','CMakePresets.json',f'docs/REMOTEPLAY_BUILD_{a.version}.md','docs/BUILD.md','THIRD_PARTY_NOTICES.md']:
  put(z,root/name,'veyra/'+name)
 tree(z,root/'licenses/remoteplay','licenses/remoteplay')
 for dep in ['json-c','libevent','miniupnpc','openssl','opus','sdl3']:
  spdx=json.loads((prefix/'share'/dep/'vcpkg.spdx.json').read_text())
  for item in spdx['files']:
   if item['SPDXID'].startswith('SPDXRef-port-file-'):
    path=vcpkg/'ports'/dep/item['fileName']
    wanted=next(c['checksumValue'] for c in item['checksums'] if c['algorithm']=='SHA256')
    if hashlib.sha256(path.read_bytes()).hexdigest()!=wanted.lower(): raise RuntimeError('Port mismatch: '+str(path))
  sources=list((vcpkg/'buildtrees'/dep/'src').glob('*.clean'))
  if len(sources)!=1:raise RuntimeError('Ambiguous source: '+dep)
  tree(z,sources[0],'dependencies/'+dep+'/source')
  tree(z,vcpkg/'ports'/dep,'dependencies/'+dep+'/port')
  tree(z,prefix/'share'/dep,'dependencies/'+dep+'/installed-share')
 for dep in ['vcpkg-cmake','vcpkg-cmake-config','vcpkg-cmake-get-vars']:
  tree(z,vcpkg/'ports'/dep,'vcpkg-port-scripts/ports/'+dep)
 tree(z,vcpkg/'scripts/cmake','vcpkg-port-scripts/scripts/cmake')
 for name in ['scripts/buildsystems/vcpkg.cmake','triplets/x64-windows-static.cmake','LICENSE.txt']:
  put(z,vcpkg/name,'vcpkg-port-scripts/'+name)
 put(z,Path('C:/veyra-deps/remoteplay-installed/vcpkg/status'),'installed-status.txt')
 ff=root/'third_party_local/amd/FidelityFX-SDK'
 # Open-source FidelityFX source only; never NVIDIA/Intel SDK directories.
 for name in git(ff,'ls-files').splitlines():
  path=ff/name
  if path.is_file():put(z,path,'fidelityfx/'+name)
 z.writestr('README.txt',f'Veyra {a.version} corresponding dependency source. Application source is tag v{a.version} at Likely7/Veyra-NRVideo. See veyra/docs/REMOTEPLAY_BUILD_{a.version}.md. FFmpeg source is a separate asset. No proprietary SDK/runtime, credentials or user data.\n')
 z.writestr('source-manifest.json',json.dumps(records,indent=2))
 z.writestr('excluded-development-binaries.json',json.dumps(excluded,indent=2))
digest=hashlib.sha256(a.output.read_bytes()).hexdigest().upper()
a.output.with_suffix('.zip.sha256').write_text(digest+'  '+a.output.name+'\n',encoding='ascii')
print(json.dumps(dict(archive=str(a.output),files=len(records),excluded=len(excluded),sha256=digest)))
