"""Generate a matching FFmpeg rebuild script; does not install/replace DLLs.

Run in a VS x64 developer environment, then invoke the emitted script with
the supplied MSYS bash. The source must already have ps5-h264-slices.patch.
"""
import argparse
import ctypes
import os
from pathlib import Path
import shlex

p = argparse.ArgumentParser()
for name in ('reference-bin', 'source', 'build', 'prefix', 'msys', 'msvc-bin', 'nasm', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
if '#define MAX_SLICES 256' not in (a.source / 'libavcodec/h264dec.h').read_text():
    raise SystemExit('Apply ps5-h264-slices.patch to a separate n9.0.1 source tree first')
with os.add_dll_directory(str(a.reference_bin.resolve())):
    lib = ctypes.CDLL(str((a.reference_bin / 'avcodec-63.dll').resolve()))
    lib.avcodec_configuration.restype = ctypes.c_char_p
    lib.avcodec_license.restype = ctypes.c_char_p
    if lib.avcodec_license().decode() != 'LGPL version 2.1 or later':
        raise SystemExit('Unexpected reference license')
    config = lib.avcodec_configuration().decode()
def posix(path):
    value = path.resolve().as_posix()
    return '/' + value[0].lower() + value[2:] if len(value) > 2 and value[1] == ':' else value
args = shlex.split(config)
ar = a.msys / 'usr/share/automake-1.18/ar-lib'
args = [('--prefix=' + a.prefix.resolve().as_posix() if x.startswith('--prefix=') else
         '--ar=' + shlex.quote(posix(ar)) + ' lib.exe' if x.startswith('--ar=') else x) for x in args]
a.build.mkdir(parents=True, exist_ok=True)
a.output.parent.mkdir(parents=True, exist_ok=True)
search = ':'.join(posix(x) for x in (a.msvc_bin, a.msys / 'usr/bin', a.nasm))
script = ('#!/bin/bash\nset -eu\nexport PATH=' + shlex.quote(search) + ':"$PATH"\n'
          + 'cd ' + shlex.quote(posix(a.build)) + '\n'
          + shlex.quote(posix(a.source / 'configure')) + ' ' + shlex.join(args)
          + '\nmake -j8\nmake install\n')
a.output.write_text(script, encoding='utf-8', newline='\n')
a.output.with_suffix('.reference-config.txt').write_text(config, encoding='utf-8')
print(a.output)
