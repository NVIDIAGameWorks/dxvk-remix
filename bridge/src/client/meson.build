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

d3d9_version = vcs_tag(
  command: [python_interpreter.full_path(), global_src_root_norm + './scripts-common/echo.py', full_version],
  input:   'version.rc.in',
  output:  'version.rc')

d3d9_res = wrc_generator.process(d3d9_version)

d3d9_src = files([
  'd3d9_bootstrap.cpp',
  'd3d9_cubetexture.cpp',
  'd3d9_device_base.cpp',
  'd3d9_device.cpp',
  'd3d9_indexbuffer.cpp',
  'd3d9_lss.cpp',
  'd3d9_module.cpp',
  'd3d9_pixelshader.cpp',
  'd3d9_query.cpp',
  'd3d9_resource.cpp',
  'd3d9_stateblock.cpp',
  'd3d9_surface.cpp',
  'd3d9_swapchain.cpp',
  'd3d9_texture.cpp',
  'd3d9_vertexbuffer.cpp',
  'd3d9_vertexdeclaration.cpp',
  'd3d9_vertexshader.cpp',
  'd3d9_volume.cpp',
  'd3d9_volumetexture.cpp',
  'di_hook.cpp',
  'message_channels.cpp',
  'pch.cpp',
  'remix_api.cpp',
  'remix_state.cpp',
  'window.cpp',
])

d3d9_header = files([
  'base.h',
  'client_options.h',
  'd3d9_base_texture.h',
  'd3d9_commonshader.h',
  'd3d9_cubetexture.h',
  'd3d9_device_base.h',
  'd3d9_device.h',
  'd3d9_indexbuffer.h',
  'd3d9_lss.h',
  'd3d9_pixelshader.h',
  'd3d9_privatedata.h',
  'd3d9_query.h',
  'd3d9_resource.h',
  'd3d9_surface.h',
  'd3d9_surfacebuffer_helper.h',
  'd3d9_swapchain.h',
  'd3d9_texture.h',
  'd3d9_util.h',
  'd3d9_vertexbuffer.h',
  'd3d9_vertexdeclaration.h',
  'd3d9_vertexshader.h',
  'd3d9_volume.h',
  'd3d9_volumetexture.h',
  'swapchain_map.h',
  'detours_common.h',
  'di_hook.h',
  'framework.h',
  'lockable_buffer.h',
  'message_channels.h',
  'pch.h',
  'remix_state.h',
  'resource.h',
  'shadow_map.h',
  'window.h',
])

d3d9_def = files([
  'd3d9_lss.def',
])

detours_include_path = include_directories('../../../submodules/Detours/src')

thread_dep = dependency('threads')

d3d9_dll = shared_library('d3d9', d3d9_header, d3d9_src, d3d9_res, 
  sources             : [ bridge_version ],
  dependencies        : [ thread_dep, util_dep, lib_version, lib_version, lib_commCtrl, tracy_dep ],
  include_directories : [ bridge_include_path, util_include_path, detours_include_path, public_include_path ],
  vs_module_defs      : 'd3d9_lss.def')


ext_x86_path = meson.global_source_root().replace('\\', '/') + '/ext/x86/'
ext_x64_path = meson.global_source_root().replace('\\', '/') + '/ext/x64/'

if build_os == 'windows'
custom_target('copy_d3d9_to_output',
    output           : ['copy_d3d9_to_output'],
    build_by_default : true,
    depends          : [ d3d9_dll ],
    command          : [copy_script_path, meson.current_build_dir(), output_dir, 'd3d9*'] )
endif