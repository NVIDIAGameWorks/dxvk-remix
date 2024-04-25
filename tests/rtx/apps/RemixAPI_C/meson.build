RemixAPI_C_exe = executable(
  'RemixAPI_C',
  files('./remixapi_example_c.c'),
  include_directories : [ remix_api_include_path ],
  override_options    : ['-c_std=c99']
)

RemixAPI_C_exepath = join_paths(meson.current_build_dir(), RemixAPI_C_exe.name() + '.exe')
