usdg_converter_src = files([
  'usdg.cpp',
  'rtx_asset_package_writer.cpp',
  'rtx_asset_package_extractor.cpp'
])

usdg_converter_tool = executable(
  'usdg-converter', usdg_converter_src,
  cpp_args:             ['/wd4251'],
  include_directories : [ dxvk_include_path, usd_schema_plugins_nvusd_include, '.', '../../src/util' ],
  dependencies:         [ usd_schema_plugins_linklibs, util_dep, tracy_dep, dxvk_extradep, rtxio_dep, lssUsd_dep, usdg_plugin_dep ],
  install: true
)

