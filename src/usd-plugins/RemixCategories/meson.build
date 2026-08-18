# Minimal RemixCategories.dll — registers TfType for HasAPI() support.
# Schema property definitions come from checked-in codeless resources.

remix_categories_plugin = shared_module(
  'RemixCategories',
  'remixInstanceCategoryAPI.cpp',
  include_directories: include_directories('../' + usd_schema_plugins_nvusd_dir + '/include'),
  dependencies: usd_schema_plugins_linklibs,
  cpp_args: ['-DNOMINMAX', usd_schema_plugins_isdebug ? '-DTBB_USE_DEBUG=1' : '-DTBB_USE_DEBUG=0', '/Zc:inline-', '/wd4251', '/wd4275', '/wd4305', '/wd4244', '/wd4273'],
  override_options: ['cpp_std=c++17'],
  install: false,
)

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
