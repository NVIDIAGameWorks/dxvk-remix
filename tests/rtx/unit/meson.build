#############################################################################
# Copyright (c) 2023-2026, NVIDIA CORPORATION. All rights reserved.
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

test_unit_deps = [ dxvk_deps, dxvk_extradep, util_dep, tracy_dep ]

test_env = environment({'MALLOC_PERTURB_': '0'})

# include remix deps on the search path
foreach external_dll : external_dll_paths
  test_env.append('PATH', external_dll)
endforeach
test_env.append('PATH', nrc_dll_path, nrd_lib_path, join_paths(usd_lib_path, 'bin'))

tests = []

# Compile the production Slang projection helper to CPU code for numerical regression tests.
render_target_motion_cpu = custom_target('render_target_motion_cpu',
  input : 'test_render_target_motion.slang',
  output : 'render_target_motion_cpu.cpp',
  depend_files : files('../../../src/dxvk/shaders/rtx/utility/render_target_motion.slangh'),
  command : [slang_compiler, '@INPUT@', '-I', shader_directory,
    '-target', 'cpp', '-entry', 'testRenderTargetMotion', '-Wno-30081', '-o', '@OUTPUT@'])
exe = executable('test_render_target_motion', files('test_render_target_motion.cpp'), render_target_motion_cpu,
  win_subsystem : 'console', override_options : ['cpp_std='+dxvk_cpp_std])
test('test_render_target_motion', exe, env : test_env)
tests += exe

render_target_motion_integration_cpu = custom_target('render_target_motion_integration_cpu',
  input : 'test_render_target_motion_integration.slang',
  output : 'render_target_motion_integration_cpu.cpp',
  depend_files : files(
    '../../../src/dxvk/shaders/rtx/utility/render_target_motion.slangh',
    '../../../src/dxvk/shaders/rtx/utility/virtual_hit_projection.slangh',
    '../../../src/dxvk/shaders/rtx/utility/packing.slangh',
    '../../../src/dxvk/shaders/rtx/utility/packing_helpers.h',
    '../../../src/dxvk/shaders/rtx/utility/shader_types.h',
    '../../../src/dxvk/shaders/rtx/utility/math.slangh',
    '../../../src/dxvk/shaders/rtx/concept/camera/camera.slangh',
    '../../../src/dxvk/shaders/rtx/algorithm/render_target_motion_output.slangh',
    '../../../src/dxvk/shaders/rtx/algorithm/rtxdi/rtxdi_temporal_history.slangh'),
  command : [slang_compiler, '@INPUT@', '-I', shader_directory,
    '-target', 'cpp', '-entry', 'testRenderTargetMotionIntegration', '-Wno-30081', '-o', '@OUTPUT@'])
# The pinned Slang CPU prelude omits U16_min; unsigned promotion preserves min for 16-bit operands.
exe = executable('test_render_target_motion_integration', files('test_render_target_motion_integration.cpp'), render_target_motion_integration_cpu,
  cpp_args : ['-DU16_min=U32_min'],
  win_subsystem : 'console', override_options : ['cpp_std='+dxvk_cpp_std])
test('test_render_target_motion_integration', exe, env : test_env)
tests += exe

exe = executable('fastop_minmax',  files('test_fastop_minmax.cpp'),  dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('fastop_minmax', exe, env: test_env)
tests += exe

exe = executable('fastop_copysubtract',  files('test_fastop_copysubtract.cpp'),  dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('fastop_copysubtract', exe, env: test_env)
tests += exe

exe = executable('fastop_parallelmemcpy',  files('test_fastop_parallelmemcpy.cpp'),  dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('fastop_parallelmemcpy', exe, env: test_env)
tests += exe

exe = executable('util_threadpool',  files('test_util_threadpool.cpp'),  dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('util_threadpool', exe, env: test_env, timeout: 60)
tests += exe

exe = executable('test_intersection_helper_sat',  files('test_intersection_helper_sat.cpp'), include_directories : test_include_path,  dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_intersection_helper_sat', exe, env: test_env)
tests += exe

exe = executable('test_pnext',  files('test_pnext.cpp'), include_directories : remix_api_include_path,  dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_pnext', exe, env: test_env)
tests += exe

exe = executable('test_spatial_map',  files('test_spatial_map.cpp'), include_directories : remix_api_include_path,  dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_spatial_map', exe, env: test_env)
tests += exe

exe = executable('test_retained_buffer_table',  files('test_retained_buffer_table.cpp'), include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_retained_buffer_table', exe, env: test_env)
tests += exe

exe = executable('test_pending_slot_queues',  files('test_pending_slot_queues.cpp'), include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_pending_slot_queues', exe, env: test_env)
tests += exe

exe = executable('test_capture_termination_guard',  files('test_capture_termination_guard.cpp'), include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_capture_termination_guard', exe, env: test_env)
tests += exe

exe = executable('test_bindless_buffer_mirror',  files('test_bindless_buffer_mirror.cpp'), include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_bindless_buffer_mirror', exe, env: test_env)
tests += exe

exe = executable('test_documentation',  files('test_documentation.cpp'), include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_documentation', exe, env: test_env, priority : -50, args: d3d9_dll.full_path())
tests += exe

exe = executable('test_graph_documentation',  files('test_graph_documentation.cpp'), include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_graph_documentation', exe, env: test_env, priority : -50, args: d3d9_dll.full_path())
tests += exe

exe = executable('test_remix_categories', files('test_remix_categories.cpp'), dependencies : usd_dep, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_remix_categories', exe, env: test_env, priority : -50, args: [ d3d9_dll.full_path(), remix_categories_plugin.full_path() ], depends: [ d3d9_dll, remix_categories_plugin ])
tests += exe

exe = executable('test_particle_drag', files('test_particle_drag.cpp'), include_directories : test_include_path, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_particle_drag', exe, env: test_env)
tests += exe

exe = executable('test_particle_spawn_distribution', files('test_particle_spawn_distribution.cpp'), include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_particle_spawn_distribution', exe, env: test_env)
tests += exe

if get_option('particle_distribution_gpu_tests')
  particle_gpu_shaders = []
  foreach shader : [
    join_paths(shader_directory, 'rtx/pass/particles/particle_system_build_spawn_distribution.comp.slang'),
    join_paths(meson.current_source_dir(), 'particle_spawn_distribution_sample.comp.slang'),
  ]
    particle_gpu_shaders += custom_target(fs.name(shader),
      input: shader,
      output: fs.stem(shader) + '.spv',
      depfile: fs.stem(shader) + '.d',
      command: [slang_compiler, '-entry', 'main', '-target', 'spirv', '-zero-initialize',
        '-emit-spirv-directly', '-D__SLANG__', '-matrix-layout-column-major', '-Wno-30081', '-fvk-use-scalar-layout',
        '-I', shader_directory, '-I', rtxdi_include_path_string, '-I', rtxcr_include_path_string,
        '-I', submodules_include_path_string, '-I', volumetrics_include_path_string,
        '-depfile', '@DEPFILE@', '@INPUT@', '-o', '@OUTPUT@'])
  endforeach
  exe = executable('test_particle_spawn_distribution_gpu', files('test_particle_spawn_distribution_gpu.cpp'),
    include_directories: test_include_path, dependencies: lib_vulkan, win_subsystem: 'console',
    override_options: ['cpp_std=' + dxvk_cpp_std])
  test('test_particle_spawn_distribution_gpu', exe, args: particle_gpu_shaders, env: test_env, timeout: 120)
  tests += [exe] + particle_gpu_shaders
endif

exe = executable('test_particle_screen_size', files('test_particle_screen_size.cpp'), include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_particle_screen_size', exe, env: test_env)
tests += exe

exe = executable('test_particle_system_schema', files('test_particle_system_schema.cpp'), dependencies : usd_dep, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_particle_system_schema', exe, env: test_env, args: remix_particle_system_plugin.full_path(), depends: remix_particle_system_plugin)
tests += exe

exe = executable('test_graph_usd_parser',  files('graph/test_component.h', 'graph/test_flexible_component.h', 'graph/test_flexible_component.cpp', 'test_graph_usd_parser.cpp'),
  include_directories : test_include_path, dependencies : [ test_unit_deps, d3d9_dep ], link_with: [ dxvk_lib ] , win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_graph_usd_parser', exe, env: test_env)
tests += exe

exe = executable('test_hot_reload', files('test_hot_reload.cpp'),
  include_directories : test_include_path, dependencies : [ test_unit_deps, d3d9_dep ], link_with: [ dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_hot_reload', exe, env: test_env)
tests += exe

exe = executable('test_transform_components',  files('test_transform_components.cpp'), 
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ dxvk_lib ] , win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_transform_components', exe, env: test_env)
tests += exe

exe = executable('test_rtx_feature_options', files('test_rtx_feature_options.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_rtx_feature_options', exe, env: test_env)
tests += exe

exe = executable('test_rtx_option',  files('test_rtx_option.cpp'), 
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ dxvk_lib ] , win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_rtx_option', exe, env: test_env)
tests += exe

exe = executable('test_rtx_option_dynamic',  files('test_rtx_option_dynamic.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ d3d9_dll, dxvk_lib ] , win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_rtx_option_dynamic', exe, env: test_env)
tests += exe

exe = executable('test_option_layer_export',  files('test_option_layer_export.cpp'), 
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ dxvk_lib ] , win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_option_layer_export', exe, env: test_env)
tests += exe

exe = executable('test_preserved_object_picking',  files('test_preserved_object_picking.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ dxvk_lib ] , win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_preserved_object_picking', exe, env: test_env)
tests += exe

exe = executable('test_f16tof32',  files('test_f16tof32.cpp'),  include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_f16tof32', exe, env: test_env)
tests += exe

exe = executable('test_gpu_profile', files('test_gpu_profile.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ d3d9_dll, dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_gpu_profile', exe, env: test_env)
tests += exe

exe = executable('test_gpu_overrides', files('test_gpu_overrides.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ d3d9_dll, dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_gpu_overrides', exe, env: test_env)
tests += exe

exe = executable('test_texture_transform', files('test_texture_transform.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_texture_transform', exe, env: test_env)
tests += exe

exe = executable('test_remix_api_version', files('test_remix_api_version.cpp'),
  include_directories : [ test_include_path, remix_api_include_path ], dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ d3d9_dll, dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_remix_api_version', exe, env: test_env)
tests += exe

if dxvk_is_x64 and get_option('remix_with_shim')
  exe = executable('test_remix_shim', files('test_remix_shim.cpp'), include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
  test('test_remix_shim', exe, env: test_env, args: d3d9_shim_dll.full_path(), depends: [ d3d9_shim_dll, d3d9_impl_dll ])
  tests += exe
endif

# NV-DXVK start: Raytraced camera material packing coverage.
exe = executable('test_attenuation', files('test_attenuation.cpp'),
  include_directories : test_include_path, dependencies : test_unit_deps, win_subsystem : 'console', override_options : ['cpp_std='+dxvk_cpp_std])
test('test_attenuation', exe, env: test_env)

tests += exe

exe = executable('test_camera_material', files('test_camera_material.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with : [ d3d9_dll, dxvk_lib ], win_subsystem : 'console', override_options : ['cpp_std='+dxvk_cpp_std])
test('test_camera_material', exe, env: test_env)
tests += exe
# NV-DXVK end

# NV-DXVK start: Optional Vulkan execution of camera transport shader helpers.
if get_option('camera_gpu_tests')
  test('camera_resource_identity_gpu', exe, args : ['--gpu-resource-identity'],
    env : test_env, suite : 'gpu', timeout : 120)
  test('camera_transport_gpu', python_interpreter,
    args : files('test_camera_transport_gpu.py'),
    suite : 'gpu', timeout : 120)
endif
# NV-DXVK end

exe = executable('test_particle_system_lookup', files('test_particle_system_lookup.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ d3d9_dll, dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_particle_system_lookup', exe, env: test_env)
tests += exe

exe = executable('test_particle_object_picking', files('test_particle_object_picking.cpp'),
  include_directories : test_include_path, dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ d3d9_dll, dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_particle_object_picking', exe, env: test_env)
tests += exe

if fs.is_file('../../../nv-private/hdremix/particlePicking.h')
  exe = executable('test_material_particle_picking', files('test_material_particle_picking.cpp'),
    include_directories : [ test_include_path, include_directories('../../../src/lssusd') ], dependencies : [ d3d9_dep, test_unit_deps ], link_with: [ d3d9_dll, dxvk_lib ], win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
  test('test_material_particle_picking', exe, env: test_env)
  tests += exe
endif

alias_target('unit_tests', tests)
