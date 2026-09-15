"""Recreate development dependencies; runtime files never enter source Git.

The only private input is two original NVOF headers via a private-repository
file (user-authorized exception) or an Actions secret.
All remaining sources are pinned public checkouts or SHA-256 verified archives.
"""
import argparse
import base64
import binascii
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
LOCK_FILE = Path(__file__).with_name('dependencies.lock.json')
LOCK = json.loads(LOCK_FILE.read_text(encoding='utf-8'))
DEPS = ROOT / 'third_party_local'
WORK = ROOT / 'out' / 'ci-dependencies'
NVOF = DEPS / 'nvidia/Optical_Flow_SDK_5.0.7/NvOFInterface'
NVOF_FILE = ROOT / 'ci-private/nvof-headers.b64'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(*args, cwd=ROOT):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=dict(os.environ), check=True)


def git_output(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()


def write_same(path, data):
    """Reentry must never replace a user's different local SDK."""
    if path.exists() and path.read_bytes() != data:
        raise RuntimeError(f'Existing file differs: {path}')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def decode_headers(secret):
    if len(secret) > 48000:
        raise ValueError('NVOF secret exceeds the supported size')
    # Text editors may wrap the value. Only discard formatting whitespace;
    # never truncate at '=' or ignore arbitrary trailing/duplicated content.
    encoded_secret = ''.join(secret.lstrip('\ufeff').split())
    try:
        compressed = base64.b64decode(encoded_secret, validate=True)
    except (binascii.Error, ValueError):
        raise ValueError(
            'Invalid VEYRA_NVOF_HEADERS_B64 Base64. Replace the entire secret '
            'with the raw contents of out/ci-dependencies/nvof-secret.txt; '
            'do not append, include quotes, a filename, or Markdown. '
            'Line breaks and whitespace are accepted. Secret contents are not logged.'
        ) from None
    with gzip.GzipFile(fileobj=io.BytesIO(compressed)) as stream:
        raw = stream.read(100001)
    if len(raw) > 100000:
        raise ValueError('NVOF secret expands beyond the header limit')
    document = json.loads(raw)
    expected = LOCK['nvof']['headers']
    if set(document) != set(expected):
        raise ValueError('NVOF input must contain exactly the two approved headers')
    result = {}
    for name, encoded in document.items():
        data = base64.b64decode(encoded, validate=True)
        if hashlib.sha256(data).hexdigest() != expected[name]:
            raise ValueError(f'NVOF header SHA-256 mismatch: {name}')
        result[name] = data
    return result


def stage_nvof():
    if NVOF_FILE.is_file():
        # The user authorized this one SDK input in a private repository only.
        if os.environ.get('GITHUB_ACTIONS') == 'true':
            event_path = os.environ.get('GITHUB_EVENT_PATH')
            event = json.loads(Path(event_path).read_text(encoding='utf-8')) if event_path else {}
            if event.get('repository', {}).get('private') is not True:
                raise ValueError('ci-private/nvof-headers.b64 is only authorized for a private repository.')
        if NVOF_FILE.stat().st_size > 48000:
            raise ValueError('NVOF input file exceeds the supported size')
        secret = NVOF_FILE.read_text(encoding='utf-8-sig')
        print('Using NVOF repository file; any old Actions secret is ignored.', flush=True)
    else:
        secret = os.environ.get('VEYRA_NVOF_HEADERS_B64', '')
    if secret:
        for name, data in decode_headers(secret).items():
            write_same(NVOF / name, data)
    for name, expected in LOCK['nvof']['headers'].items():
        path = NVOF / name
        if not path.exists() or digest(path) != expected:
            raise RuntimeError('Supply ci-private/nvof-headers.b64 in a private repository, or configure VEYRA_NVOF_HEADERS_B64; see docs/GITHUB_ACTIONS_BUILD.md')
    print('NVOF original headers verified (contents not logged).', flush=True)


def export_secret(repository_file=False):
    if not all((NVOF / name).is_file() for name in LOCK['nvof']['headers']):
        sdk_zip = ROOT / 'Optical_Flow_SDK_5.0.7.zip'
        if not sdk_zip.is_file() or digest(sdk_zip) != LOCK['nvof']['provided_archive_sha256']:
            raise RuntimeError('Place the original verified Optical_Flow_SDK_5.0.7.zip in the project root')
        with zipfile.ZipFile(sdk_zip) as sdk:
            for name, expected in LOCK['nvof']['headers'].items():
                data = sdk.read('Optical_Flow_SDK_5.0.7/NvOFInterface/' + name)
                if hashlib.sha256(data).hexdigest() != expected:
                    raise RuntimeError('NVOF archive header identity mismatch')
                write_same(NVOF / name, data)
    stage_nvof()
    document = {name: base64.b64encode((NVOF / name).read_bytes()).decode('ascii')
                for name in LOCK['nvof']['headers']}
    secret = base64.b64encode(gzip.compress(json.dumps(document).encode(), mtime=0)).decode('ascii')
    decode_headers(secret)
    WORK.mkdir(parents=True, exist_ok=True)
    target = NVOF_FILE if repository_file else WORK / 'nvof-secret.txt'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(secret, encoding='ascii')
    if repository_file:
        print(f'Private-repository input saved locally: {target} ({len(secret)} characters). Never upload to a public repository or an artifact.')
    else:
        print(f'Secret saved locally: {target} ({len(secret)} characters). Do not commit or upload as an artifact.')


def checkout(spec):
    path = DEPS / spec['directory']
    if not (path / '.git').exists():
        if path.exists() and any(path.iterdir()):
            raise RuntimeError(f'Refusing to overwrite non-Git dependency: {path}')
        path.mkdir(parents=True, exist_ok=True)
        run('git', 'init', path)
        run('git', '-C', path, 'remote', 'add', 'origin', spec['url'])
        if spec.get('sparse'):
            run('git', '-C', path, 'sparse-checkout', 'init', '--cone')
            run('git', '-C', path, 'sparse-checkout', 'set', *spec['sparse'])
    if not (path / '.git/HEAD').exists() or not subprocess.run(
            ['git', '-C', str(path), 'rev-parse', '--verify', 'HEAD'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        run('git', '-C', path, 'fetch', '--depth', '1', '--filter=blob:none', 'origin', spec['commit'])
        run('git', '-C', path, 'checkout', '--detach', spec['commit'])
    if git_output(path, 'rev-parse', 'HEAD') != spec['commit']:
        raise RuntimeError(f'Dependency commit mismatch: {path}')
    if git_output(path, 'diff', '--name-only') or git_output(path, 'diff', '--cached', '--name-only'):
        raise RuntimeError(f'Dependency tracked source has local changes: {path}')
    if spec.get('recursive'):
        run('git', '-C', path, 'submodule', 'update', '--init', '--recursive', '--depth', '1')
    return path


def archive(name):
    spec = LOCK['archives'][name]
    target = WORK / (name + '-source.zip')
    WORK.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        partial = target.with_suffix('.partial')
        print('Downloading verified public dependency archive:', name, flush=True)
        with urllib.request.urlopen(spec['url'], timeout=60) as source, partial.open('wb') as dest:
            shutil.copyfileobj(source, dest)
        if digest(partial) != spec['sha256']:
            raise RuntimeError(f'Source archive checksum mismatch: {name}')
        partial.replace(target)
    if digest(target) != spec['sha256']:
        raise RuntimeError(f'Source archive checksum mismatch: {name}')
    return target


def copy_zip_subtree(archive_path, prefix, target):
    with zipfile.ZipFile(archive_path) as source:
        for info in source.infolist():
            if not info.filename.startswith(prefix) or info.is_dir():
                continue
            dest = (target / info.filename[len(prefix):]).resolve()
            if not dest.is_relative_to(target.resolve()):
                raise ValueError('Source archive path escapes destination')
            write_same(dest, source.read(info))


def prepare_ports():
    overlay = WORK / 'overlay'
    ff = archive('ffmpeg')
    # Recreate overlays in a versioned location; no edits to the upstream checkout.
    ffport = overlay / 'ffmpeg'
    original = WORK / 'original-ffmpeg-port'
    copy_zip_subtree(ff, 'vcpkg-port/', original)
    for source in original.iterdir():
        if source.is_file():
            ffport.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, ffport / source.name)
    patch = ROOT / 'scripts/ffmpeg/ps5-h264-slices.patch'
    shutil.copy2(patch, ffport / patch.name)
    port = (original / 'portfile.cmake').read_text()
    if port.count('    PATCHES\n') != 1:
        raise RuntimeError('Unexpected FFmpeg port patch list')
    port = port.replace('    PATCHES\n', '    PATCHES\n        ps5-h264-slices.patch\n', 1)
    # Verify applied source before compiling, and bind installed DLL to this patch.
    port += '''
file(READ "${SOURCE_PATH}/libavcodec/h264dec.h" VEYRA_H264_HEADER)
if(NOT VEYRA_H264_HEADER MATCHES "#define MAX_SLICES 256")
    message(FATAL_ERROR "Veyra PS5 slice patch is missing")
endif()
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/ps5-h264-slices.patch" VEYRA_PATCH_SHA)
file(SHA256 "${CURRENT_PACKAGES_DIR}/bin/avcodec-63.dll" VEYRA_DLL_SHA)
file(WRITE "${CURRENT_PACKAGES_DIR}/share/ffmpeg/veyra-local-build.json"
    "{\\"schema\\":1,\\"maxSlices\\":256,\\"patchSha256\\":\\"${VEYRA_PATCH_SHA}\\",\\"avcodecSha256\\":\\"${VEYRA_DLL_SHA}\\"}")
'''
    (ffport / 'portfile.cmake').write_text(port)
    rp = archive('remoteplay')
    for name in ('json-c', 'libevent', 'miniupnpc', 'openssl', 'opus', 'sdl3'):
        copy_zip_subtree(rp, f'dependencies/{name}/port/', overlay / name)
    return overlay


def prepare_dxc():
    target = DEPS / 'ci/dxc'
    copy_zip_subtree(archive('dxc'), 'bin/x64/', target)
    if not (target / 'dxc.exe').is_file():
        raise RuntimeError('DXC archive is missing the x64 compiler')
    return target / 'dxc.exe'


def build_dependencies():
    if ' ' in str(ROOT):
        raise RuntimeError('FFmpeg source builds require a workspace path without spaces')
    stage_nvof()
    os.environ.pop('VEYRA_NVOF_HEADERS_B64', None)
    paths = {name: checkout(spec) for name, spec in LOCK['repositories'].items()}
    dxc = prepare_dxc()
    overlay = prepare_ports()
    vcpkg = paths['vcpkg']
    if not (vcpkg / 'vcpkg.exe').exists():
        run(os.environ['COMSPEC'], '/d', '/c', str(vcpkg / 'bootstrap-vcpkg.bat'), '-disableMetrics')
    # No proprietary inputs enter the binary cache; build source dependencies fresh.
    os.environ['VCPKG_BINARY_SOURCES'] = 'clear'
    os.environ['VCPKG_DISABLE_METRICS'] = '1'
    triplets = WORK / 'triplets'
    triplets.mkdir(parents=True, exist_ok=True)
    for name, crt, linkage in [('x64-veyra-shared', 'dynamic', 'dynamic'), ('x64-veyra-static', 'static', 'static')]:
        (triplets / (name + '.cmake')).write_text(
            f'set(VCPKG_TARGET_ARCHITECTURE x64)\nset(VCPKG_CRT_LINKAGE {crt})\nset(VCPKG_LIBRARY_LINKAGE {linkage})\nset(VCPKG_BUILD_TYPE release)\n')
    install = DEPS / 'ci/installed'
    specs = ['ffmpeg[core,avcodec,avformat,swresample,swscale]:x64-veyra-shared']
    specs += [name + ':x64-veyra-static' for name in ('sdl3', 'opus', 'openssl', 'curl', 'json-c', 'libevent', 'miniupnpc', 'protobuf', 'pkgconf')]
    run(vcpkg / 'vcpkg.exe', 'install', '--classic', *specs, '--host-triplet=x64-windows',
        '--overlay-ports=' + str(overlay), '--overlay-triplets=' + str(triplets),
        '--x-install-root=' + str(install), cwd=vcpkg)
    ffmpeg = install / 'x64-veyra-shared'
    prefix = install / 'x64-veyra-static'
    verify_ffmpeg(ffmpeg)

    # Same fixed source, own stage and shared verification as the original build.
    stage = DEPS / 'remoteplay/chiaki-stage'
    if not stage.exists():
        run('git', '-C', paths['chiaki'], 'worktree', 'add', '--detach', stage, LOCK['repositories']['chiaki']['commit'])
        run('git', '-C', stage, 'submodule', 'update', '--init', '--recursive', '--depth', '1')
    for name in ('0001-chiaki-msvc-vla-compat.patch', '0002-chiaki-video-metadata.patch'):
        patch = ROOT / 'scripts/remoteplay/patches' / name
        applied = subprocess.run(['git', '-C', str(stage), 'apply', '--reverse', '--check', str(patch)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not applied:
            run('git', '-C', stage, 'apply', '--check', patch)
            run('git', '-C', stage, 'apply', patch)
    run(sys.executable, ROOT / 'scripts/remoteplay/verify-chiaki-stage.py', '--stage', stage, '--clean', paths['chiaki'])

    amd = paths['amd'] / 'sdk'
    run('cmake', '-S', amd, '-B', WORK / 'amd-build', '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DFFX_API_BACKEND=DX12_X64', '-DFFX_API_DX12=ON', '-DFFX_OF=ON', '-DFFX_ALL=OFF',
        '-DFFX_BUILD_AS_DLL=OFF', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
    run('cmake', '--build', WORK / 'amd-build', '--config', 'Release', '--target', 'ffx_opticalflow_x64', 'ffx_backend_dx12_x64', '--parallel', '4')
    protoc = prefix / 'tools/protobuf/protoc.exe'
    pkgconfig = prefix / 'tools/pkgconf/pkgconf.exe'
    if not protoc.is_file() or not pkgconfig.is_file():
        raise RuntimeError('vcpkg did not produce required protoc/pkgconf host tools')
    config = dict(schema=1, dlss=paths['dlss'], nvof=NVOF.parent, ffmpeg=ffmpeg,
                  xess=paths['xess'], fidelityfx=amd, chiakiStage=stage, chiakiClean=paths['chiaki'],
                  remotePlayPrefix=prefix, protoc=protoc, pkgConfig=pkgconfig, dxc=dxc)
    for key in config.keys() - {'schema'}:
        config[key] = config[key].relative_to(DEPS).as_posix()
    (DEPS / 'ci-build.json').write_text(json.dumps(config, indent=2))
    (DEPS / 'ci-source-lock.json').write_bytes(LOCK_FILE.read_bytes())
    print('Public development dependencies prepared; runtime/GPU tests not executed.', flush=True)


def verify_ffmpeg(prefix):
    record = json.loads((prefix / 'share/ffmpeg/veyra-local-build.json').read_text())
    if record.get('maxSlices') != 256 or record.get('patchSha256') != digest(ROOT / 'scripts/ffmpeg/ps5-h264-slices.patch'):
        raise RuntimeError('FFmpeg source patch provenance mismatch')
    if record.get('avcodecSha256') != digest(prefix / 'bin/avcodec-63.dll'):
        raise RuntimeError('FFmpeg binary does not match source-build record')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['export-nvof-secret', 'export-nvof-file', 'prepare', 'nvof', 'verify-ffmpeg'])
    parser.add_argument('--prefix', type=Path)
    args = parser.parse_args()
    if args.command == 'export-nvof-secret': export_secret()
    elif args.command == 'export-nvof-file': export_secret(repository_file=True)
    elif args.command == 'nvof': stage_nvof()
    elif args.command == 'prepare': build_dependencies()
    else: verify_ffmpeg(args.prefix)


if __name__ == '__main__':
    main()
