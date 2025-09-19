
tests = []

# Command History Test
if (cpu_family == 'x86_64')
cmd_history_exe = executable(
    'command_history',  files('test_cmd_queue.cpp'),
    include_directories : util_include_path,
    dependencies : util_dep,
    win_subsystem : 'console')
test('command_history', cmd_history_exe)
tests += cmd_history_exe
endif

# Remix API Serializing Test
if (cpu_family == 'x86_64')
    name_suff = 'x64'
else
    name_suff = 'x86'
endif
write_name = 'test_remix_api_write'
write_exe = write_name + '_'  + name_suff
tests += executable(
    write_exe,  files(write_name + '.cpp'),
    include_directories : [util_include_path, public_include_path ],
    dependencies : util_dep,
    win_subsystem : 'console',)
read_name = 'test_remix_api_read'
read_exe = read_name + '_'  + name_suff
tests += executable(
    read_exe,  files(read_name + '.cpp'),
    include_directories : [util_include_path, public_include_path ],
    dependencies : util_dep,
    win_subsystem : 'console')
if (cpu_family == 'x86_64')
python = find_program('python')
remix_api_test_script = join_paths(meson.current_source_dir(), 'test_remix_api.py')
remix_api_test_artifacts_dir = join_paths(meson.global_build_root(), 'test_artifacts', 'remix_api')
variant = 'x64_2_x86'
test('remix_api__' + variant,
     python,
     args : [remix_api_test_script,
             '--' + variant,
             '--executablesDir', meson.current_build_dir(),
             '--artifactDir', remix_api_test_artifacts_dir ])
variant = 'x86_2_x64'
test('remix_api__' + variant,
     python,
     args : [remix_api_test_script,
             '--' + variant,
             '--executablesDir', meson.current_build_dir(),
             '--artifactDir', remix_api_test_artifacts_dir ])
endif

alias_target('unit_tests', tests)

