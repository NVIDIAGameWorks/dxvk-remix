# NOTE: This needs to be updated per plugin, based on the class names defined in the schema
plugin_classes = [
	'ParticleSystemAPI',
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

schema_script = join_paths(global_src_root_norm, 'scripts-common', 'generate_and_patch_schema.py')

gen_and_patch = custom_target(
  'gen_and_patch_schema_' + plugin_name,
  input: [ schema_usda, usd_schema_plugins_python ],
  output: [
    'generatedSchema.usda',
    'plugInfo.json',
    'api.h',
    'tokens.h',
    'tokens.cpp',
    'wrapTokens.cpp'
  ] + generated_files,
  command: [
    usd_schema_plugins_python,
    schema_script,
    '--schema-usda', schema_usda,
    '--outdir', meson.current_build_dir(),
    '--plugin-name', plugin_name
  ],
  build_by_default: true,
  console : true,    
  env: {
    # mirror what the script expects
    'USD_SCHEMA_PYTHON':    usd_schema_plugins_python,
    'USD_NVUSD_BINDIR':     usd_schema_plugins_nvusd_bindir,
    'USD_LIBPYTHON_DIR':    usd_libpython_dir,
    'ORIG_PATH':            orig_path,
    'USD_NVUSD_LIBDIR':     usd_schema_plugins_nvusd_libdir,
  }
)

dep = declare_dependency(sources: gen_and_patch)

# Compile the plugin shared library
plugin_lib = shared_library(
  plugin_name,
  [ gen_and_patch ],
  include_directories : [
    usd_schema_plugins_includes,
    '.'
  ],
  cpp_args:             [ '-DREMIX_EXPORTS', usd_schema_plugins_defines, usd_schema_plugins_compilerflags ],
  c_args:               [ '-DREMIX_EXPORTS', usd_schema_plugins_defines, usd_schema_plugins_compilerflags ],
  dependencies:         [ dep, usd_schema_plugins_linklibs],
  override_options:     ['cpp_std=c++17'],   # USD 22.11 requires c++17,
  install: true
)

usd_plugins += plugin_lib
usd_plugin_deps += declare_dependency( link_with: [ plugin_lib ] )
usd_plugin_delay_load_args += '/DELAYLOAD:' + plugin_name + '.dll'