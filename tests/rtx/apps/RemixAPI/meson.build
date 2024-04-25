RemixAPI_exe = executable(
  'RemixAPI',
  files('./remixapi_example.cpp'),
  include_directories : [ remix_api_include_path ],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_exepath = join_paths(meson.current_build_dir(), RemixAPI_exe.name() + '.exe')
