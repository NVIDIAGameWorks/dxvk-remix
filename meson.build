#############################################################################
# Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
  'dxvk', ['c', 'cpp'],
  version : 'remix-main',
  meson_version : '>= 0.58',
  default_options : ['werror=true', 'b_vscrt=from_buildtype']
)

global_src_root_norm = meson.global_source_root().replace('\\', '/')

# Fetch dependencies
#   `get_dependencies` is intentionally called twice. Why?:
#     (1) On a clean clone, the `include_directories` calls below will fail without
#           having fetched the dependencies, since all meson scripts complete entirely
#           before the first build target (fetch_packman_target) is called.
#     (2) On already built trees with no meson changes, the script is not rerun, so
#           run_command will not be executed again, therefore a default build target
#           version is required to pull updated packman info.
#   Implications: in very rare circumstances (clean clone, editing meson), get_dependencies
#     script will be called twice on the same build. This will not be noticeable as packman
#     early outs if everything's already fetched, so the waste is negligible + forgiveable.
get_dependencies_script_path = global_src_root_norm + '/scripts-common/update-deps.cmd'
message('Downloading dependencies...')
packman_out = run_command(get_dependencies_script_path)
message(packman_out.stdout().strip())
if packman_out.returncode() != 0
  message(packman_out.stderr().strip())
  error('Failed to get dependencies.')
endif
fetch_packman_target = custom_target('fetch_packman',
  output           : ['fetch_packman'],
  build_by_default : true,
  depends          : [],
  command          : [get_dependencies_script_path] )

fs = import('fs')

cpu_family = target_machine.cpu_family()
build_os = build_machine.system()

add_project_arguments('-DNOMINMAX', language : 'cpp')
vs_project_defines = 'NOMINMAX;'

dxvk_compiler = meson.get_compiler('cpp')
dxvk_is_msvc = dxvk_compiler.get_id() == 'msvc'
dxvk_is_ninja = meson.backend() == 'ninja'
enable_rtxio = get_option('enable_rtxio')

if dxvk_is_msvc
  if get_option('buildtype') == 'release'
    add_global_arguments('/Zi', language : 'cpp')
    add_global_arguments('/FS', language : 'cpp')
    add_global_link_arguments('/DEBUG', language : 'cpp')
    add_global_link_arguments('/OPT:REF', language : 'cpp')
    add_global_link_arguments('/OPT:ICF', language : 'cpp')
  endif
endif

if enable_rtxio == true
  add_global_arguments('/DWITH_RTXIO', language : 'cpp')
  vs_project_defines += 'WITH_RTXIO;'
endif

if get_option('buildtype') == 'release'
  add_project_arguments('/DNDEBUG', language : 'cpp')
  # note: this define depends on the build type, so it's not included in vs_project_defines
endif

if dxvk_is_ninja
  # workarounds for our strange symbol name differences when embedding MDL
  add_global_arguments('/DBUILD_NINJA=1', language : 'cpp')
  vs_project_defines += 'BUILD_NINJA=1;'
else
  add_global_arguments('/DBUILD_NINJA=0', language : 'cpp')
  vs_project_defines += 'BUILD_NINJA=0;'
endif

# Note: Runtime shader recompilation requires source paths based on the project root, so making a build-time define is the easiest way
# to give it this information reliably for a local build (when distributing a build to others a manual source root must be specified in the runtime
# configuration).
add_global_arguments('/DBUILD_SOURCE_ROOT="' + global_src_root_norm + '/"', language : 'cpp')
vs_project_defines += 'BUILD_SOURCE_ROOT=' + global_src_root_norm + '/"' + ';'

# c++17 was added in 15.3, older version needs c++latest
if dxvk_is_msvc and dxvk_compiler.version().version_compare('<15.3')
  if dxvk_is_ninja
    # for whatever reason, ninja gets /fpermissive- when using c++latest/c++17 but vs does not
    dxvk_cpp_std='vc++latest'
  else
    dxvk_cpp_std='c++latest'
  endif
else
  if dxvk_is_ninja
    dxvk_cpp_std='vc++17'
  else
    dxvk_cpp_std='c++17'
  endif
endif

if dxvk_is_msvc
  if not dxvk_is_ninja
    add_project_arguments('/std:' + dxvk_cpp_std, language : 'cpp')
  endif
  add_global_arguments('/MP', language : 'cpp')
endif

if dxvk_compiler.get_id() == 'clang'
  if dxvk_compiler.has_argument('-Wno-unused-private-field')
    add_project_arguments('-Wno-unused-private-field', language: 'cpp')
  endif
  if dxvk_compiler.has_argument('-Wno-microsoft-exception-spec')
    add_project_arguments('-Wno-microsoft-exception-spec', language: 'cpp')
  endif
endif

if not dxvk_is_msvc
  add_project_arguments('-D_WIN32_WINNT=0xa00', language : 'cpp')

  if get_option('build_id') and dxvk_compiler.has_link_argument('-Wl,--build-id')
    add_global_link_arguments('-Wl,--build-id', language: 'cpp')
  endif

  # We need to set the section alignment for debug symbols to
  # work properly as well as avoiding a memcpy from the Wine loader.
  if dxvk_compiler.has_link_argument('-Wl,--file-alignment=4096')
    add_global_link_arguments('-Wl,--file-alignment=4096', language: 'cpp')
  endif

  # Wine's built-in back traces only work with dwarf2 symbols
  if get_option('debug') and target_machine.system() == 'windows'
    if dxvk_compiler.has_argument('-gstrict-dwarf') and dxvk_compiler.has_argument('-gdwarf-2')
      add_project_arguments('-gstrict-dwarf', '-gdwarf-2', language: ['c', 'cpp'])
    endif
  endif
endif

remix_api_include_path = include_directories('./public/include')
add_global_arguments('/DREMIX_LIBRARY_EXPORTS=1', language : 'cpp')

dxvk_include_dirs = [
  './include',
  './include/vulkan/include'
]

dxvk_include_path = include_directories(dxvk_include_dirs)
dxvk_shader_include_path = include_directories('./src/dxvk/shaders')

test_include_dirs = [
  './include',
  './include/vulkan/include',
  './src/dxvk',
  './src/dxvk/shaders'
]

test_include_path = include_directories(test_include_dirs)

if (cpu_family == 'x86_64')
  dxvk_library_path = meson.global_source_root() + '/lib'
else
  dxvk_library_path = meson.global_source_root() + '/lib32'
endif

dxvk_extradep = [ ]

if dxvk_is_msvc
  wrc = find_program('rc')
else
  add_global_link_arguments('-static', '-static-libgcc', language: 'c')
  add_global_link_arguments('-static', '-static-libgcc', '-static-libstdc++', language: 'cpp')

  wrc = find_program('windres')
endif

if cpu_family == 'x86_64'
  if dxvk_compiler.has_argument('-msse3')
    add_project_arguments('-msse3', language: ['c', 'cpp'])
  endif
elif cpu_family == 'x86'
  if dxvk_compiler.has_link_argument('-Wl,--add-stdcall-alias')
    add_global_link_arguments('-Wl,--add-stdcall-alias', language: 'cpp')
  endif
  if dxvk_compiler.has_link_argument('-Wl,--enable-stdcall-fixup')
    add_global_link_arguments('-Wl,--enable-stdcall-fixup', language: 'cpp')
  endif
  if dxvk_compiler.has_argument('-msse') and dxvk_compiler.has_argument('-msse2') and dxvk_compiler.has_argument('-msse3')
    add_project_arguments('-msse', '-msse2', '-msse3', language: ['c', 'cpp'])
  endif
  if dxvk_compiler.has_argument('-mfpmath=sse')
    add_project_arguments('-mfpmath=sse', language: ['c', 'cpp'])
  endif
endif

lib_vulkan  = dxvk_compiler.find_library('vulkan-1', dirs : dxvk_library_path)
lib_d3d9    = dxvk_compiler.find_library('d3d9')
lib_d3d11   = dxvk_compiler.find_library('d3d11')
lib_dxgi    = dxvk_compiler.find_library('dxgi')
lib_d3dcompiler_43 = dxvk_compiler.find_library('d3dcompiler_43', dirs : dxvk_library_path)
if not dxvk_is_msvc
  lib_ws2_32 = dxvk_compiler.find_library('ws2_32', dirs : dxvk_library_path)
endif

if dxvk_is_msvc
  lib_d3dcompiler_47 = dxvk_compiler.find_library('d3dcompiler')
else
  lib_d3dcompiler_47 = dxvk_compiler.find_library('d3dcompiler_47')
endif

lib_shlwapi  = dxvk_compiler.find_library('shlwapi')
dxvk_extradep += lib_shlwapi

if enable_rtxio == true
  rtxio_bin_path = global_src_root_norm + '/external/rtxio/bin'
  rtxio_lib = dxvk_compiler.find_library('rtxio', dirs : join_paths(meson.global_source_root(), 'external/rtxio/lib'))
  rtxio_include_path = include_directories(join_paths(meson.global_source_root(), 'external/rtxio/include'))
  dxvk_extradep += rtxio_lib
endif

exe_ext = ''
dll_ext = ''

if dxvk_is_msvc
  res_ext = '.res'
else
  res_ext = '.o'
endif

def_spec_ext = '.def'

# Shader compile tools
glsl_compiler = find_program('external/glslangvalidator/glslangValidator')
slang_compiler = find_program('external/slang/slangc')
python_interpreter = find_program('python3', 'python')
shader_compile_script = meson.global_source_root() + '/scripts-common/compile_shaders.py'

# GLSL compiler...

# The ShaderManager::compileShaders() function also uses glslangValidator to compile shader source for debug purpose.
# If the command here is changed, the command in that function should also be updated.
glsl_args = [ '-V', '--vn', '@BASENAME@', '@INPUT@', '-o', '@OUTPUT@', '--target-env', 'vulkan1.2', '@EXTRA_ARGS@' ]

if run_command(glsl_compiler, [ '--quiet', '--version' ]).returncode() == 0
  glsl_args += [ '--quiet' ]
endif

if get_option('buildtype').startswith('debug')
  glsl_args += [ '-g' ]
endif

glsl_generator = generator(
  glsl_compiler,
  output    : [ '@BASENAME@.h' ],
  arguments : glsl_args
)

if dxvk_is_msvc
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

dxvk_version = vcs_tag(
  command: [python_interpreter.full_path(), global_src_root_norm + './scripts-common/echo.py', full_version],
  input:  'version.h.in',
  output: 'version.h')

#################################
# Handle external dependencies ##
#################################

output_dir = global_src_root_norm + '/_output/'

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

if get_option('enable_tracy')
  add_project_arguments('-DTRACY_ENABLE', language : 'cpp')
  vs_project_defines += 'TRACY_ENABLE;'
  # only enable profiling when the profiler-application is connected
  add_project_arguments('-DTRACY_ON_DEMAND', language : 'cpp')
  vs_project_defines += 'TRACY_ON_DEMAND;'
endif

if get_option('buildtype') == 'debug'
  debug_or_release = 'debug'
else
  debug_or_release = 'release'
endif

# embedding compiler
xxd_exe = find_program('external/nv_xxd/nv_xxd')
embedding_compiler = generator(xxd_exe,
    output : '@PLAINNAME@.h',
    arguments : [ '@INPUT@', '@OUTPUT@', '@BASENAME@' ]
)

mdl_include_dir = include_directories(join_paths(meson.global_source_root(), 'external/omni_core_materials'))

nv_usd_include_path = include_directories('external/nv_usd/include/')
boost_include_path = include_directories('external/nv_usd/include/boost-1_78/')

usd_include_paths = [nv_usd_include_path, boost_include_path] 

vk_include_path = include_directories('./include/vulkan/include')

lssusd_include_paths = [nv_usd_include_path, boost_include_path, vk_include_path]

nvapi_include_paths = include_directories(join_paths(meson.global_source_root(), 'external/nvapi'))
nvapi_lib_path = join_paths(meson.global_source_root(), 'external/nvapi/amd64')
nvapi_lib = dxvk_compiler.find_library('nvapi64', dirs : nvapi_lib_path)

# Cache lib locations
usd_lib_path = meson.global_source_root() + '/external/nv_usd/'+ debug_or_release + '/'
usd_lib = dxvk_compiler.find_library('usd_ms', dirs : usd_lib_path)
if get_option('buildtype') == 'debug'
  tbb_lib = dxvk_compiler.find_library('tbb_debug', dirs : usd_lib_path)
else
  tbb_lib = dxvk_compiler.find_library('tbb', dirs : usd_lib_path)
endif

reflex_include_path = include_directories(join_paths(meson.global_source_root(), 'external/reflex/inc'))
reflex_lib_path = join_paths(meson.global_source_root(), 'external/reflex/lib')
reflex_lib = dxvk_compiler.find_library('NvLowLatencyVk', dirs : reflex_lib_path)

aftermath_include_path = include_directories(join_paths(meson.global_source_root(), 'external/aftermath/include'))
aftermath_lib_path = join_paths(meson.global_source_root(), 'external/aftermath/lib/x64/')
aftermath_lib = dxvk_compiler.find_library('GFSDK_Aftermath_Lib.x64', dirs : aftermath_lib_path)

# this is repeated below, since we overwrite dlss_lib_path in src/dxvk/meson.build for some reason
dlss_root_path = 'external/ngx_sdk_dlss'
dlss_lib_path = meson.global_source_root() + '/' + dlss_root_path + '/Lib/Windows_x86_64/rel/'
dlfg_root_path = 'external/ngx_sdk_dlfg'
dlfg_lib_path = meson.global_source_root() + '/' + dlfg_root_path + '/Lib/Windows_x86_64/rel/'

nrd_lib_path = meson.global_source_root() + '/external/nrd/Lib/Release/'
if get_option('buildtype') == 'debug'  
  nrd_lib_path = meson.global_source_root() + '/external/nrd/Lib/Debug/'
endif
lss_usd_plugins_path = meson.global_source_root() + '/usd_plugins/'

rtxdi_include_path = include_directories(join_paths(meson.global_source_root(), 'submodules/rtxdi/rtxdi-sdk/include'))
# Need to use the include path as an argument to the shader build string,
# and I couldn't find how to convert the IncludeDirs thing to a string... Meson.
rtxdi_include_path_string = meson.global_source_root() + '/submodules/rtxdi/rtxdi-sdk/include'

if not dxvk_is_ninja
  # Copy dependencies that must be in same dir as executable to _output dir
  recursive_copy_usd_target = custom_target('recursive_copy_usd',
    output           : ['recursive_copy_usd'],
    build_by_default : true,
    command          : [recursive_copy_path,  usd_lib_path + '/usd/', output_dir + 'usd/'] )

  copy_usd_dll_target = custom_target('copy_usd_dll',
    output           : ['copy_usd_dll'],
    build_by_default : true,
    command          : [copy_script_path, usd_lib_path, output_dir, 'usd_ms.dll'] )

  if get_option('buildtype') == 'debug'
    tbb_dll_name = 'tbb_debug.dll'
    tbbmalloc_dll_name = 'tbbmalloc_debug.dll'
  else
    tbb_dll_name = 'tbb.dll'
    tbbmalloc_dll_name = 'tbbmalloc.dll'
  endif

  copy_tbb_dll_target = custom_target('copy_tbb_dll',
    output           : ['copy_tbb_dll'],
    build_by_default : true,
    command          : [copy_script_path, usd_lib_path, output_dir, tbb_dll_name] )

  copy_tbbmalloc_dll_target = custom_target('copy_tbbmalloc_dll',
    output           : ['copy_tbbmalloc_dll'],
    build_by_default : true,
    command          : [copy_script_path, usd_lib_path, output_dir, tbbmalloc_dll_name] )

  copy_nrd_dll_target = custom_target('copy_nrd_dll',
    output           : ['copy_nrd_dll'],
    build_by_default : true,
    command          : [copy_script_path, nrd_lib_path, output_dir, 'NRD.dll'] )

  copy_dlss_dll_target = custom_target('copy_dlss_dll',
    output           : ['copy_dlss_dll'],
    build_by_default : true,
    command          : [copy_script_path, dlss_lib_path, output_dir, 'nvngx_dlss.dll'] )

  copy_dlfg_dll_target = custom_target('copy_dlfg_dll',
    output           : ['copy_dlfg_dll'],
    build_by_default : true,
    command          : [copy_script_path, dlfg_lib_path, output_dir, 'nvngx_dlssg.dll'] )
  
  copy_reflex_dll_target = custom_target('copy_reflex_dll',
  output           : ['copy_reflex_dll'],
  build_by_default : true,
  depends          : [],
  command          : [copy_script_path, reflex_lib_path, output_dir, 'NvLowLatencyVk.dll'] )

   copy_aftermath_dll_target = custom_target('copy_aftermath_dll',
   output           : ['copy_aftermath_dll'],
   build_by_default : true,
   depends          : [],
   command          : [copy_script_path, aftermath_lib_path, output_dir, 'GFSDK_Aftermath_Lib.x64.dll'] )

  recursive_copy_lss_usd_plugins_target = custom_target('recursive_copy_lss_usd_plugins',
    output           : ['recursive_copy_lss_usd_plugins'],
    build_by_default : true,
    command          : [recursive_copy_path, lss_usd_plugins_path, output_dir + 'lss/usd_plugins/'] )

  if enable_rtxio == true
    copy_rtxio_target = custom_target('copy_rtxio',
      output           : ['copy_rtxio'],
      build_by_default : true,
      depends          : [],
      command          : [copy_script_path, rtxio_bin_path, output_dir, 'rtxio.*'] )
  endif
endif

# Build meson dependency types
usd_dep = declare_dependency(
  dependencies : [ usd_lib, tbb_lib ],
  # link_args : ['-L'+usd_lib_path],
  include_directories : usd_include_paths
)

reflex_dep = declare_dependency(
  dependencies : [ reflex_lib ],
  include_directories : reflex_include_path
)

aftermath_dep = declare_dependency(
    dependencies : [ aftermath_lib ],
    include_directories : aftermath_include_path
  )
  
nvapi_dep = declare_dependency(
    dependencies : [ nvapi_lib ],
    include_directories : nvapi_include_paths
  )

embedded_includes = [ mdl_include_dir ]

if enable_rtxio == true
  rtxio_dep = declare_dependency(
    dependencies : [ rtxio_lib ],
    include_directories : rtxio_include_path
  )
endif

embedded_dep = declare_dependency(
  dependencies : [ ],
  include_directories : embedded_includes
)
subdir('src')

enable_tests = get_option('enable_tests')

if enable_tests
  subdir('tests')
endif

# dict of "output" targets --- key is an output path, value is an optional executable inside that path,
# which is used to run that "output" from the IDE
dxvkrt_output_targets = { '_output': '' }

subdir('tests/rtx')

if dxvk_is_ninja
  # this is how we force meson to reconfigure when external files change
  # (meson does not do globbing)
  if fs.exists('gametargets.conf')
    fs.read('gametargets.conf')
  endif
  fs.read('vsgen/dxvk_project.py')
  fs.read('vsgen/dxvk-remix.vcxproj.template')
  fs.read('vsgen/get_game_target_list.py')
  fs.read('vsgen/sln.py')
  fs.read('vsgen/testcase_project.py')
  fs.read('vsgen/testcase_project.vcxproj.template')
  fs.read('vsgen/testcase_project.vcxproj.user.template')
  fs.read('vsgen/testcase_project.args.json.template')
  fs.read('vsgen/vsutil.py')

  # ... and now we do this again, because src/dxvk/meson.build overwrites this for some reason
  dlss_root_path = 'external/ngx_sdk_dlss'
  dlss_lib_path = meson.global_source_root().replace('\\', '/') + '/' + dlss_root_path + '/Lib/Windows_x86_64/rel/'
  dlfg_root_path = 'external/ngx_sdk_dlfg'
  dlfg_lib_path = meson.global_source_root().replace('\\', '/') + '/' + dlfg_root_path + '/Lib/Windows_x86_64/rel/'

  # meson seems to force / as the path separator in custom_target
  # ... except in the case where the path separators are inconsistent, in which case it does nothing
  # force / as the path separator here to make sure it stays consistent
  nrd_lib_path = global_src_root_norm + '/external/nrd/Lib/Release/'
  if get_option('buildtype') == 'debug'  
    nrd_lib_path = global_src_root_norm + '/external/nrd/Lib/Debug/'
  endif
  
  apic_copy_targets = []
  sln_project_targets = []

  vs_gen_cmdline = [ vs_project_defines ]
  # dict of: { <projectname>: { 'output_path': <copy_output_path>, 'target_suffix': <copy_target_suffix> } }
  dxvkrt_copy_targets = {}

  # fill in all of the above
  foreach t, exe : dxvkrt_output_targets
    if t == '_output'
      # _output target is special
      target_suffix = 'output'
      output_path = output_dir
    else
      target_suffix = t.underscorify()
      output_path = dxvkrt_test_root + t

      # capture data for VS generator
      sln_project_targets += t
      vs_gen_cmdline += t + ',' + exe
    endif
    
    dxvkrt_copy_targets += { t : { 'output_path': output_path, 'target_suffix': target_suffix } }
  endforeach

  # add data from gametargets.conf to dxvkrt_copy_targets
  game_targets_run = run_command(python_interpreter,
                                 meson.global_source_root() + '\\vsgen\\get_game_target_list.py')
  game_targets = game_targets_run.stdout().strip()

  if game_targets != ''
    foreach t : game_targets.split('\n')
      s = t.split(',')
      key = s[0]
      target_suffix = key.underscorify()
      output_path = s[1]
      
      dxvkrt_copy_targets += { key : { 'output_path': output_path, 'target_suffix': target_suffix } }
      
      # note: this doesn't go into the VS generator command line, gametargets.conf is read from python directly
    endforeach
  endif

  foreach key : dxvkrt_copy_targets.keys()
    target_suffix = dxvkrt_copy_targets[key]['target_suffix']
    output_path = dxvkrt_copy_targets[key]['output_path']

    # generate targets to copy DLLs to individual test dirs, with names of the form 'copy_Portal1_SingleFrame_A'
    # these targets are not run by default, since we have a lot of tests: the intent is that we run this as a build task from the IDE before launching the test
    # also, meson feature: we *can not* pass down paths using \ as arguments to the command in custom_target, they are forced to / somewhere,
    # except if the path separators are inconsistent, where meson skips all munging

    copy_usd = custom_target('recursive_copy_usd_' + target_suffix,
      output           : ['recursive_copy_usd_' + target_suffix],
      command          : [recursive_copy_path,
                          usd_lib_path + 'usd/',
                          output_path + '/usd/'] )

    copy_usd_dll = custom_target('copy_usd_dll_' + target_suffix,
      output           : ['copy_usd_dll_' + target_suffix],
      command          : [copy_script_path,
                          usd_lib_path,
                          output_path,
                          'usd_ms.dll'] )

    if get_option('buildtype') == 'debug'
      tbb_dll_name = 'tbb_debug.dll'
      tbbmalloc_dll_name = 'tbbmalloc_debug.dll'
    else
      tbb_dll_name = 'tbb.dll'
      tbbmalloc_dll_name = 'tbbmalloc.dll'
    endif

    copy_tbb_dll = custom_target('copy_tbb_dll_' + target_suffix,
      output           : ['copy_tbb_dll_' + target_suffix],
      command          : [copy_script_path,
                          usd_lib_path,
                          output_path,
                          tbb_dll_name] )

    copy_tbbmalloc_dll = custom_target('copy_tbbmalloc_dll_' + target_suffix,
      output           : ['copy_tbbmalloc_dll_' + target_suffix],
      command          : [copy_script_path,
                          usd_lib_path,
                          output_path,
                          tbbmalloc_dll_name] )

    copy_nrd_dll = custom_target('copy_nrd_dll_' + target_suffix,
      output           : ['copy_nrd_dll_' + target_suffix],
      command          : [copy_script_path,
                          nrd_lib_path,
                          output_path,
                          'NRD.dll'] )

    copy_dlss_dll = custom_target('copy_dlss_dll_' + target_suffix,
      output           : ['copy_dlss_dll_' + target_suffix],
      command          : [copy_script_path,
                          dlss_lib_path, 
                          output_path,
                          'nvngx_dlss.dll'] )

    copy_dlfg_dll = custom_target('copy_dlfg_dll_' + target_suffix,
      output           : ['copy_dlfg_dll_' + target_suffix],
      command          : [copy_script_path,
                          dlfg_lib_path, 
                          output_path,
                          'nvngx_dlssg.dll'] )

    copy_reflex_dll = custom_target('copy_reflex_dll_' + target_suffix,
      output           : ['copy_reflex_dll_' + target_suffix],
      depends          : [],
      command          : [copy_script_path,
                          reflex_lib_path,
                          output_path,
                          'NvLowLatencyVk.dll'] )

    copy_aftermath_dll = custom_target('copy_aftermath_dll_' + target_suffix,
       output           : ['copy_aftermath_dll_' + target_suffix],
       depends          : [],
       command          : [copy_script_path, aftermath_lib_path, output_path, 'GFSDK_Aftermath_Lib.x64.dll'] )

    recursive_copy_lss_usd_plugins = custom_target('recursive_copy_lss_usd_plugins_' + target_suffix,
      output           : ['recursive_copy_lss_usd_plugins_' + target_suffix],
      command          : [recursive_copy_path,
                          lss_usd_plugins_path,
                          output_path + 'lss/usd_plugins/'] )

    if enable_rtxio == true
      copy_rtxio = custom_target('copy_rtxio_' + target_suffix,
        output           : ['copy_rtxio_' + target_suffix],
        depends          : [],
        command          : [copy_script_path, rtxio_bin_path, output_path, 'rtxio.*'] )
    endif

    # the copy target is special
    # if we use custom_target for the d3d9 DLL, meson generates a target that does nothing in ninja
    # as a workaround, we do the copy as part of the test_* target itself
    copy_target_depends = [ copy_usd,
                            copy_usd_dll,
                            copy_tbb_dll,
                            copy_tbbmalloc_dll,
                            copy_nrd_dll,
                            copy_dlss_dll,
                            copy_dlfg_dll,
                            copy_aftermath_dll,
                            d3d9_dll ]
                            
    copy_target_depends += copy_reflex_dll

    if enable_rtxio == true
      copy_target_depends += copy_rtxio
    endif

    copy_target = run_target('copy_' + target_suffix.replace('apics_', ''),
      depends: copy_target_depends,
      command : [copy_script_path,
                meson.current_build_dir().replace('\\', '/') + '/src/d3d9',
                output_path,
                'd3d9*'])

    if t != '_output'
      apic_copy_targets += copy_target
    endif
  endforeach

  # xxxnsubtil: this one line replaces a bunch of python code used for the vscode build, but requires meson 0.61.3
  # (specifically, https://github.com/mesonbuild/meson/pull/9202)
  # alias_target('copy_to_all_apics', apic_copy_targets)

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
endif # dxvk_is_ninja
