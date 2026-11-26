#############################################################################
# Copyright (c) 2026, Kim2091
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#############################################################################
"""Builds and installs the shaders, data and notices of NUMOS.

  build:   numos_shaders.py build <numos dir> <slangc> <spirv out dir> <stamp file>
  install: numos_shaders.py install <numos dir> <spirv dir> <destination dir>

build fails when the SPIR-V differs from the pass table of the library, because the runtime refuses such shaders.
install fills <destination>/numos, which the runtime reads next to d3d9.dll.
See documentation/NumosSky.md.
"""
import pathlib
import shutil
import subprocess
import sys


def build(numos, slangc, out, stamp):
    tools = numos / 'tools'
    subprocess.run([sys.executable, '-B', str(tools / 'build_shaders.py'), '--slangc', str(slangc), '--out', str(out),
                    '--jobs', '4'], check=True)
    subprocess.run([sys.executable, '-B', str(tools / 'gen_pass_table.py'), '--spirv', str(out),
                    '--out', str(numos / 'src' / 'generated' / 'passes.inc'), '--check'], check=True)
    pathlib.Path(stamp).write_text('\n'.join(sorted(p.name for p in out.glob('*.spv'))) + '\n')


def copy_files(source, destination, pattern='*'):
    files = [path for path in source.glob(pattern) if path.is_file()]
    if not files:
        sys.exit(f'No files match {pattern} in {source}. Update the numos submodule and build the shaders.')
    destination.mkdir(parents=True, exist_ok=True)
    for path in files:
        shutil.copy2(path, destination / path.name)


def install(numos, spirv, destination):
    output = destination / 'numos'
    copy_files(spirv, output / 'spirv', '*.spv')
    copy_files(numos / 'assets' / 'nightsky', output / 'nightsky')
    copy_files(numos / 'licenses', output / 'licenses')
    for name in ('LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copy2(numos / name, output / name)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mode, numos = sys.argv[1], pathlib.Path(sys.argv[2])
    if mode == 'build' and len(sys.argv) == 6:
        build(numos, pathlib.Path(sys.argv[3]), pathlib.Path(sys.argv[4]), sys.argv[5])
    elif mode == 'install' and len(sys.argv) == 5:
        install(numos, pathlib.Path(sys.argv[3]), pathlib.Path(sys.argv[4]))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
