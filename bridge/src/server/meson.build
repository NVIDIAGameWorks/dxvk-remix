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

server_src = files([
	'main.cpp',
	'module_processing.cpp',
	'remix_api.cpp'
])

server_header = files([
	'module_processing.h',
	'server_options.h',
	'remix_api.h'
])

thread_dep = dependency('threads')

server_tag = vcs_tag(
  command: [python_interpreter.full_path(), global_src_root_norm + './scripts-common/echo.py', full_version],
  input:   'version.rc.in',
  output:  'version.rc')

server_version = wrc_generator.process(server_tag)

windows = import('windows')
server_resource = windows.compile_resources('NvRemixBridge.rc',
	depend_files : ['NvRemixBridge.ico']
)

server_exe = executable('NvRemixBridge', server_src, server_header, server_version, server_resource,
sources             : [ bridge_version ],
build_by_default    : (cpu_family == 'x86_64') ? true : false,
dependencies        : [ thread_dep, util_dep, lib_version, tracy_dep ],
include_directories : [ bridge_include_path, util_include_path, public_include_path ],
win_subsystem       : 'windows')


server_output_dir = output_dir + '.trex'

if cpu_family == 'x86_64'
	if build_os == 'windows'
	custom_target('copy_server_to_output',
		output           : ['copy_server_to_output'],
		build_by_default : true,
		depends          : [ server_exe ],
		command          : [copy_script_path, meson.current_build_dir(), server_output_dir, 'NvRemixBridge*'] )
	endif
endif