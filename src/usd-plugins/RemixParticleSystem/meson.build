# Minimal RemixParticleSystem.dll — registers TfType for HasAPI() support.
# Schema property defs come from the codeless resources/ folder, copied into
# this subdir's build folder so scripts-common/install_usd_plugins.bat picks
# them up alongside the DLL.

# shared_module: plugin DLL loaded at runtime by USD's plug registry.
# Nothing links against it, so no import-lib is needed (and MSVC won't emit
# a .lib because we have no dllexport'd symbols — only a TF_REGISTRY_FUNCTION).
# install: false because scripts-common/install_usd_plugins.bat copies the DLL
# from <BUILD>/src/usd-plugins/RemixParticleSystem/ into each apic's runtime
# layout at <output_path>/usd/plugins/RemixParticleSystem/.
remix_particle_system_plugin = shared_module(
  'RemixParticleSystem',
  'particleSystemAPI.cpp',
  include_directories: include_directories('../' + usd_schema_plugins_nvusd_dir + '/include'),
  dependencies: usd_schema_plugins_linklibs,
  cpp_args: ['-DNOMINMAX', usd_schema_plugins_isdebug ? '-DTBB_USE_DEBUG=1' : '-DTBB_USE_DEBUG=0', '/Zc:inline-', '/wd4251', '/wd4275', '/wd4305', '/wd4244', '/wd4273'],
  override_options: ['cpp_std=c++17'],
  install: false,
)

# Stage the codeless-schema resources next to the DLL in the build tree.
# install_usd_plugins.bat reads <BUILD>/src/usd-plugins/<plugin>/{plugInfo.json,
# generatedSchema.usda} and writes the schema to the plugin root used by USD
# and Toolkit.
configure_file(
  input: 'resources/plugInfo.json',
  output: 'plugInfo.json',
  copy: true,
)
configure_file(
  input: 'resources/generatedSchema.usda',
  output: 'generatedSchema.usda',
  copy: true,
)

# Also mirror the installed layout so USD can load the build-tree plugin.
subdir('resources')
