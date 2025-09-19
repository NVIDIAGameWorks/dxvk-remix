#############################################################################
# Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

detours_include_path = include_directories('../../../submodules/Detours/src')

launcher_src = files([
  'launcher.cpp'
])

lib_shlwapi = bridge_compiler.find_library('shlwapi')

launcher_name = 'NvRemixLauncher32'

if cpu_family == 'x86_64'
  launcher_name = 'NvRemixLauncher64'
endif

launcher_exe = executable(launcher_name, launcher_src,
build_by_default    : (cpu_family == 'x86') ? true : false,
dependencies        : [ lib_shlwapi, util_dep ],
include_directories : [ detours_include_path, util_include_path ])


if cpu_family == 'x86'
if build_os == 'windows'
  custom_target('copy_launcher_to_output',
    output           : ['copy_launcher_to_output'],
    build_by_default : true,
    depends          : [ launcher_exe ],
    command          : [copy_script_path, meson.current_build_dir(), output_dir, 'NvRemixLauncher*'] )
endif
endif