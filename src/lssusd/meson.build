lssUsd_src = files([
  'game_exporter.cpp',
  'usd_mesh_importer.cpp',
  'usd_mesh_importer.h',
  'usd_mesh_samplers.h',
  'hd/usd_mesh_util.cpp',
  'hd/usd_mesh_util.h',
  'mdl_helpers.h'
])

if not dxvk_is_msvc
  lss_Usd_args = ['-fpermissive']
else
  lss_Usd_args = []
endif
thread_dep = dependency('threads')

libusd_embedded_data_files = files([
  '../../external/omni_core_materials/Base/AperturePBR_Opacity.mdl',
  '../../external/omni_core_materials/Base/AperturePBR_Translucent.mdl',
  '../../external/omni_core_materials/Base/AperturePBR_Model.mdl',
  '../../external/omni_core_materials/Base/AperturePBR_Normal.mdl',
  '../../external/omni_core_materials/Base/AperturePBR_SpriteSheet.mdl',
])

libusd_generated_embedded_files = embedding_compiler.process(libusd_embedded_data_files)

lssUsd_lib = static_library('lssUsd', lssUsd_src,[libusd_generated_embedded_files],
  dependencies        : [ usd_dep, embedded_dep ],
  include_directories : [ usd_include_paths, lssusd_include_paths ],
  override_options    : ['cpp_std='+dxvk_cpp_std],
  cpp_args            : lss_Usd_args)

lssUsd_dep = declare_dependency(
  link_with           : [ lssUsd_lib ])
