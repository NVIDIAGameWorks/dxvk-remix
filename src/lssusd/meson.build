lssUsd_src = files([
  'game_exporter.cpp'
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
])

libusd_generated_embedded_files = embedding_compiler.process(libusd_embedded_data_files)

lssUsd_lib = static_library('lssUsd', lssUsd_src,[libusd_generated_embedded_files],
  dependencies        : [ usd_dep, embedded_dep ],
  include_directories : [ usd_include_paths ],
  override_options    : ['cpp_std='+dxvk_cpp_std],
  cpp_args            : lss_Usd_args,)

lssUsd_dep = declare_dependency(
  link_with           : [ lssUsd_lib ])
