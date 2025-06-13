# NOTE: This needs to be updated per plugin, based on the class names defined in the schema
plugin_classes = [
	'prim',
]

# Below here, things should remain the same plugin to plugin -

fs = import('fs')
plugin_name = fs.name(meson.current_source_dir())

# Source layout:
# /<plugin_name>/schema.usda
plugin_src = meson.current_source_dir()
schema_usda = join_paths(plugin_src, 'schema.usda')

generated_files = []
foreach class : plugin_classes
  generated_files += 'wrap' + class + '.cpp'
  generated_files += class + '.h'
  generated_files += class + '.cpp'
endforeach

# Code-gen step: usdGenSchema
gen = custom_target(
  'gen_schema_' + plugin_name,
  input: [ schema_usda ],
  output: [ 
    #'generatedSchema.usda',
    #'plugInfo.json',
    'api.h',
    'tokens.h',
    'tokens.cpp',
    'wrapTokens.cpp',
	generated_files
  ],
  command: [
    usd_schema_plugins_python,
    join_paths(usd_schema_plugins_nvusd_bindir, 'usdGenSchema'),
    schema_usda, '@OUTDIR@',
  ],
  build_by_default: true,
  env: {
    'PYTHONPATH': usd_libpython_dir,
    'PATH': orig_path + ';' + usd_schema_plugins_nvusd_libdir
  }
)

library_filename = plugin_name + '.dll'
replace_script = join_paths(global_src_root_norm, 'scripts-common', 'replace_keywords.py')

patched_pluginfo = custom_target(
  'patch_plugInfo_' + plugin_name,
  output: 'plugInfo.json',
  depends: [ gen ],
  command: [
    usd_schema_plugins_python,
    replace_script,
    join_paths(meson.current_build_dir(), 'plugInfo.json'),
    '@OUTPUT@',
    'PLUG_INFO_LIBRARY_PATH=' + library_filename,
    'PLUG_INFO_NAME=' + plugin_name,
    'PLUG_INFO_RESOURCE_PATH=.',
    'PLUG_INFO_ROOT=./../',
  ],
  build_by_default: true
)

dep = declare_dependency(sources: [ gen, patched_pluginfo ])

# Compile the plugin shared library
plugin_lib = shared_library(
  plugin_name,
  [ gen ],
  include_directories : [
    usd_schema_plugins_includes,
    '.'
  ],
  cpp_args:             [ '-D' + plugin_name.to_upper() + '_EXPORTS', usd_schema_plugins_defines, usd_schema_plugins_compilerflags ],
  c_args:               [ '-D' + plugin_name.to_upper() + '_EXPORTS', usd_schema_plugins_defines, usd_schema_plugins_compilerflags ],
  dependencies:         [ dep, usd_schema_plugins_linklibs],
  override_options:     ['cpp_std=c++17'],   # USD 22.11 requires c++17,
)

usd_plugins += plugin_lib
usd_plugin_deps += declare_dependency( link_with: [ plugin_lib ] )
usd_plugin_delay_load_args += '/DELAYLOAD:' + plugin_name + '.dll'