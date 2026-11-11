usdg_src = files([
  'usdgFileFormat.cpp',
  'usdgResolver.cpp',
])

usdg_lib = shared_library(
  'usdg', usdg_src,
  cpp_args : [ '-DUSDG_IMPLEMENTATION'],
  include_directories : [ dxvk_include_path, usd_schema_plugins_nvusd_include, '.', './include' ],
  dependencies:         [ usd_schema_plugins_linklibs, util_dep, tracy_dep, dxvk_extradep ],
  install: true
)

usd_plugins += usdg_lib

usdg_plugin_dep = declare_dependency( link_with: [ usdg_lib ], include_directories : [ './include' ] )
usd_plugin_deps += usdg_plugin_dep
