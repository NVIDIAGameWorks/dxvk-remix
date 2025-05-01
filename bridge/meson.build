#############################################################################
# Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#############################################################################

project(
  'bridge', ['c', 'cpp'],
  version : 'remix-main',
  meson_version : '>= 0.58',
  default_options : ['werror=true', 'b_vscrt=static_from_buildtype', 'cpp_std=vc++17']
)

python_interpreter = find_program('python3', 'python')

global_src_root_norm = meson.global_source_root().replace('\\', '/')
output_dir = global_src_root_norm + '/_output/'

cpu_family = target_machine.cpu_family()
build_os = build_machine.system()
 
# Get script paths
if build_os == 'windows'
  script_extension = '.bat'
  recursive_arg = '/E'
elif build_os == 'linux'
  script_extension = '.sh'
  recursive_arg = '-r'
else
  script_extension = ''
endif
copy_script_path = global_src_root_norm + '/scripts-common/copy' + script_extension
recursive_copy_path = global_src_root_norm + '/scripts-common/recursive_copy' + script_extension

add_project_arguments('-DNOMINMAX', language : 'cpp')
vs_project_defines = 'NOMINMAX;'

build_type = get_option('buildtype')

if build_type != 'debug' and build_type != 'debugoptimized'
  add_project_arguments('-DNDEBUG', language: 'cpp')
endif

if build_type == 'debugoptimized'
  add_project_arguments('/DDEBUGOPT', language: 'cpp')
  vs_project_defines += 'DEBUGOPT;'
endif

bridge_compiler = meson.get_compiler('cpp')
bridge_is_msvc = bridge_compiler.get_id() == 'msvc'

# workarounds for our strange symbol name differences when embedding MDL
add_global_arguments('/DBUILD_NINJA=1', language : 'cpp')
vs_project_defines += 'BUILD_NINJA=1;'


# Set source paths base on the project root as a build-time define
# in case it is needed at build time for a local build.

add_global_arguments('/DBUILD_SOURCE_ROOT="' + global_src_root_norm + '/"', language : 'cpp')
vs_project_defines += 'BUILD_SOURCE_ROOT=' + global_src_root_norm + '/src/client/"' + ';'

if bridge_is_msvc
  add_global_arguments('/MP', language : 'cpp')
endif

if bridge_compiler.get_id() == 'clang'
  if bridge_compiler.has_argument('-Wno-unused-private-field')
    add_project_arguments('-Wno-unused-private-field', language: 'cpp')
  endif
  if bridge_compiler.has_argument('-Wno-microsoft-exception-spec')
    add_project_arguments('-Wno-microsoft-exception-spec', language: 'cpp')
  endif
endif

if get_option('enable_multithreaded_device')
  add_project_arguments('-DWITH_MULTITHREADED_DEVICE', language : 'cpp')
endif

if get_option('enable_tracy')
  add_project_arguments('-DTRACY_ENABLE', language : 'cpp')
  vs_project_defines += 'TRACY_ENABLE;'
  # only enable profiling when the profiler-application is connected
  add_project_arguments('-DTRACY_ON_DEMAND', language : 'cpp')
  vs_project_defines += 'TRACY_ON_DEMAND;'
endif

if not bridge_is_msvc
  if get_option('build_id') and bridge_compiler.has_link_argument('-Wl,--build-id')
    add_global_link_arguments('-Wl,--build-id', language: 'cpp')
  endif

  # We need to set the section alignment for debug symbols to
  # work properly as well as avoiding a memcpy from the Wine loader.
  if bridge_compiler.has_link_argument('-Wl,--file-alignment=4096')
    add_global_link_arguments('-Wl,--file-alignment=4096', language: 'cpp')
  endif
endif

bridge_include_path = '' # include_directories('./include')
util_include_path = include_directories('./src/util')
public_include_path = include_directories('../public/include/')

if (cpu_family == 'x86_64')
  bridge_library_path = meson.global_source_root() + '/lib'
else
  bridge_library_path = meson.global_source_root() + '/lib32'
endif

bridge_extradep = [  ]

if bridge_is_msvc
  wrc = find_program('rc')
else
  add_global_link_arguments('-static', '-static-libgcc', language: 'c')
  add_global_link_arguments('-static', '-static-libgcc', '-static-libstdc++', language: 'cpp')

  if cpu_family == 'x86_64'
    wrc = find_program('x86_64-w64-mingw32-windres')
  elif cpu_family == 'x86'
    wrc = find_program('i686-w64-mingw32-windres')
  else
    error('Unsupported target architecture')
  endif
endif

if cpu_family == 'x86_64'
  add_global_arguments('/DREMIX_BRIDGE_SERVER', language : 'cpp')

  if bridge_compiler.has_argument('-msse3')
    add_project_arguments('-msse3', language: ['c', 'cpp'])
  endif
elif cpu_family == 'x86'
  add_global_arguments('/DREMIX_BRIDGE_CLIENT', language : 'cpp')

  if bridge_compiler.has_link_argument('-Wl,--add-stdcall-alias')
    add_global_link_arguments('-Wl,--add-stdcall-alias', language: 'cpp')
  endif
  if bridge_compiler.has_link_argument('-Wl,--enable-stdcall-fixup')
    add_global_link_arguments('-Wl,--enable-stdcall-fixup', language: 'cpp')
  endif
  if bridge_compiler.has_argument('-msse') and bridge_compiler.has_argument('-msse2') and bridge_compiler.has_argument('-msse3')
    add_project_arguments('-msse', '-msse2', '-msse3', language: ['c', 'cpp'])
  endif
  if bridge_compiler.has_argument('-mfpmath=sse')
    add_project_arguments('-mfpmath=sse', language: ['c', 'cpp'])
  endif
endif

lib_version = bridge_compiler.find_library('Version')
lib_commCtrl = bridge_compiler.find_library('Comctl32')

exe_ext = ''
dll_ext = ''

if bridge_is_msvc
  res_ext = '.res'
else
  res_ext = '.o'
endif

def_spec_ext = '.def'

if bridge_is_msvc
  wrc_generator = generator(wrc,
  output    : [ '@BASENAME@' + res_ext ],
  arguments : [ '/fo', '@OUTPUT@', '@INPUT@' ])
else
  wrc_generator = generator(wrc,
  output    : [ '@BASENAME@' + res_ext ],
  arguments : [ '-i', '@INPUT@', '-o', '@OUTPUT@' ])
endif
  
message('##############')
message('# Versioning #')
message('##############')

full_version = meson.project_version()

git_rev_parse_out = run_command('git',['rev-parse','--verify','--short=8', 'HEAD'])
git_hash = ''
if git_rev_parse_out.returncode() != 0
  error('Failed to get current git hash')
else
  git_hash = git_rev_parse_out.stdout().replace('\n','')
  message('Current git hash: ' + git_hash)
endif

found_tag = ''
ci_commit_tag = ''
get_env_out = run_command(python_interpreter.full_path(),[global_src_root_norm + './scripts-common/get_env.py', 'CI_COMMIT_TAG'])
if get_env_out.returncode() != 0
  warning('get_env.py failed, ' + get_env_out.stderr().strip())
else
  ci_commit_tag = get_env_out.stdout().replace('\n','')
endif
b_found_ci_commit_tag = (ci_commit_tag != '')

if b_found_ci_commit_tag
  message('Found tag in CI_COMMIT_TAG envvar: ' + ci_commit_tag)
  found_tag = ci_commit_tag
else
  git_describe_tag = ''
  git_describe_out = run_command('git',['describe','--always','--exact-match', git_hash])
  if git_describe_out.returncode() == 0
    git_describe_tag = git_describe_out.stdout().replace('\n','')
    message('Found tag via git describe: ' + git_describe_tag)
  endif
  found_tag = git_describe_tag
endif

b_found_tag = found_tag != ''
b_found_tag_matches = false
if b_found_tag
  b_found_tag_matches = found_tag == meson.project_version()
endif

if b_found_tag
  if not b_found_tag_matches
    warning('Tag does not match current version: ' + meson.project_version() + '. Please reconcile tag with version in meson.build.')
  else
    message('Found tag matches current version: ' + meson.project_version())
    message('This is a release version.')
  endif
else
  message('No tag found at current commit.')
  message('This is NOT a release version')
  full_version += '+' + git_hash
endif

message('Full version: ' + full_version)

bridge_version = vcs_tag(
  command: [python_interpreter.full_path(), global_src_root_norm + './scripts-common/echo.py', full_version],
  input:  'version.h.in',
  output: 'version.h')

enable_tests = get_option('enable_tests') # Must be defined before subdir'ing src

subdir('src')

if enable_tests
  subdir('test/rtx/unit/')
  # subdir('test')
  subdir_done()
endif

vs_gen_cmdline = [ vs_project_defines ]

# add data from gametargets.conf to bridge_copy_targets
game_targets_run = run_command(python_interpreter,
                            meson.global_source_root() + '\\vsgen\\get_game_target_list.py')
game_targets = game_targets_run.stdout().strip()

copy_targets = {}

if game_targets != ''
    foreach t : game_targets.split('\n')
      s = t.split(',')
      key = s[0]
      target_suffix = key.underscorify()
      output_path = s[1]
      
      copy_targets += { key : { 'output_path': output_path, 'target_suffix': target_suffix } }
      
      # note: this doesn't go into the VS generator command line, gametargets.conf is read from python directly
    endforeach
endif

foreach key : copy_targets.keys()
    target_suffix = copy_targets[key]['target_suffix']
    output_path = copy_targets[key]['output_path']
    # the copy target is special
    # if we use custom_target for the d3d9 DLL, meson generates a target that does nothing in ninja
    # as a workaround, we do the copy as part of the test_* target itself  
    
    if cpu_family == 'x86'
        run_target('copy_' + target_suffix,
          depends: [ d3d9_dll ],
          command : [copy_script_path,
                    meson.current_build_dir().replace('\\', '/') + '/src/client',
                    output_path,
                    'd3d9*'])
        run_target('copy_' + target_suffix + '_launcher',
          depends: [ launcher_exe ],
          command : [copy_script_path,
                    meson.current_build_dir().replace('\\', '/') + '/src/launcher',
                    output_path,
                    'NvRemixLauncher32*'])
    else
        run_target('copy_' + target_suffix + '_server',
          depends: [ server_exe ],
          command : [copy_script_path,
                    meson.current_build_dir().replace('\\', '/') + '/src/server',
                    output_path + '/.trex',
                    'NvRemixBridge*'])
    endif
endforeach

# Building project files after builds for x86_64 and x86 platforms are created, here builds for x86 platform are created after x86_64
if cpu_family == 'x86'
    
  # generate VS project files for ninja
  # note: this writes to <source root>/_vs, which is outside the build directory
  vsproj = run_command(python_interpreter,
            meson.global_source_root() + '\\vsgen\\generate_vs_project_files.py',
            vs_gen_cmdline,
            check: false)

  if vsproj.stdout().strip() != ''
  message(vsproj.stdout().strip())
  endif
  if vsproj.stderr().strip() != ''
  message(vsproj.stderr().strip())
  endif
  if vsproj.returncode() != 0
  error('generate_vs_project_files failed')
  endif
endif
