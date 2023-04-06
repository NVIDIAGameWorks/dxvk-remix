lssUsd_src = files([
  'game_exporter.cpp'
])

if not dxvk_is_msvc
  lss_Usd_args = ['-fpermissive']
else
  lss_Usd_args = []
endif
thread_dep = dependency('threads')

lssUsd_lib = static_library('lssUsd', lssUsd_src,[generated_embedded_files],
  dependencies        : [ usd_dep, omni_client_dep, embedded_dep ],
  include_directories : [ usd_include_paths, omni_client_include_path ],
  override_options    : ['cpp_std='+dxvk_cpp_std],
  cpp_args            : lss_Usd_args,)

lssUsd_dep = declare_dependency(
  link_with           : [ lssUsd_lib ])
