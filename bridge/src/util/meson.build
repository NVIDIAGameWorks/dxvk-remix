#############################################################################
# Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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

util_src = files([
	'util_bridgecommand.cpp',
	'util_filesys.cpp',
	'util_gdi.cpp',
	'util_messagechannel.cpp',
	'util_process.cpp',
	'util_remixapi.cpp',
	'util_seh.cpp',
	'util_semaphore.cpp',
	'util_sharedheap.cpp',
	'util_sharedmemory.cpp',
	'util_monitor.cpp',
	'log/log.cpp',
	'config/config.cpp',
	'config/global_options.cpp',
])

util_header = files([
	'util_atomiccircularqueue.h',
	'util_blockingcircularqueue.h',
	'util_bridge_assert.h',
	'util_bridge_state.h',
	'util_bridgecommand.h',
	'util_bytes.h',
	'util_circularbuffer.h',
	'util_circularqueue.h',
	'util_commands.h',
	'util_common.h',
	'util_detourtools.h',
    'util_devicecommand.h',
	'util_filesys.h',
	'util_gdi.h',
	'util_guid.h',
	'util_hack_d3d_debug.h',
	'util_ipcchannel.h',
	'util_messagechannel.h',
	'util_once.h',
	'util_process.h',
	'util_remixapi.h',
	'util_scopedlock.h',
	'util_seh.h',
	'util_semaphore.h',
	'util_serializable.h',
	'util_serializer.h',
	'util_sharedmemory.h',
	'util_singleton.h',
	'util_texture_and_volume.h',
	'util_version.h',
	'util_monitor.h',
	'log/log.h',
	'log/log_strings.h',
	'config/config.h',
	'config/global_options.h',
])

util_lib = static_library('util', util_src, util_header,
  include_directories : [ bridge_include_path, public_include_path  ],
)

util_dep = declare_dependency(
  link_with           : [ util_lib ])
