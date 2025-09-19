#############################################################################
# Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
  meson_version : '>= 0.60',
  default_options : ['werror=true', 'b_vscrt=from_buildtype']
)

global_src_root_norm = meson.global_source_root().replace('\\', '/')
current_build_dir_norm = meson.current_build_dir().replace('\\', '/')

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
get_dependencies_script_path = join_paths(global_src_root_norm, 'scripts-common/update-deps.cmd')
message('Downloading dependencies...')
packman_out = run_command(get_dependencies_script_path, 'packman-external.xml', 'update-deps.log', check: true)
message(packman_out.stdout().strip())
if packman_out.returncode() != 0
  message(packman_out.stderr().strip())
  error('Failed to get dependencies.')
endif
fetch_packman_target = custom_target('fetch_packman',
  input            : ['packman-external.xml', get_dependencies_script_path],
  output           : 'update-deps.log',
  command          : [get_dependencies_script_path, '@INPUT0@', '@OUTPUT0@' ] )

fs = import('fs')

cpu_family = target_machine.cpu_family()
build_os = build_machine.system()

assert(cpu_family != 'x86', 'dxvk-remix-nv can\'t be built in x86. Make sure you\'re running this from a regular Powershell window.')

add_project_arguments('-DNOMINMAX', language : 'cpp')
vs_project_defines = 'NOMINMAX;'

add_project_arguments('-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR', language : 'cpp')
vs_project_defines += '_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR;'


dxvk_compiler = meson.get_compiler('cpp')
dxvk_is_msvc = dxvk_compiler.get_id() == 'msvc'
dxvk_is_ninja = meson.backend() == 'ninja'
enable_rtxio = get_option('enable_rtxio')

external_dll_paths = []

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
elif get_option('buildtype') == 'debugoptimized'
  add_project_arguments('/DDEBUG_OPTIMIZED', language : 'cpp')
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

add_global_arguments('/DBUILD_SOURCE_ROOT="' + global_src_root_norm + '/"', language : 'cpp')
vs_project_defines += 'BUILD_SOURCE_ROOT="' + global_src_root_norm + '/";'

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
  './include/vulkan/include',
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
  dxvk_library_path = join_paths(global_src_root_norm, 'lib')
else
  dxvk_library_path = join_paths(global_src_root_norm, 'lib32')
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
lib_delayimp = dxvk_compiler.find_library('delayimp', dirs: [])

if dxvk_is_msvc
  lib_d3dcompiler_47 = dxvk_compiler.find_library('d3dcompiler')
else
  lib_d3dcompiler_47 = dxvk_compiler.find_library('d3dcompiler_47')
endif

lib_shlwapi  = dxvk_compiler.find_library('shlwapi')
dxvk_extradep += lib_shlwapi

if enable_rtxio == true
  rtxio_bin_path = join_paths(global_src_root_norm, 'external/rtxio/bin')
  external_dll_paths += rtxio_bin_path
  rtxio_lib = dxvk_compiler.find_library('rtxio', dirs : join_paths(global_src_root_norm, 'external/rtxio/lib'))
  rtxio_include_path = include_directories('external/rtxio/include')
  dxvk_extradep += rtxio_lib
endif

# Shader Compile Tools
glsl_compiler = find_program('external/glslangvalidator/glslangValidator')
slang_compiler = find_program('external/slang/slangc')
spirv_val = find_program('external/spirv_tools/spirv-val')
python_interpreter = find_program('python3', 'python')
shader_compile_script = join_paths(global_src_root_norm, 'scripts-common/compile_shaders.py')

# GLSL Compiler

glsl_args = [ '-V', '--vn', '@BASENAME@', '@INPUT@', '-o', '@OUTPUT@', '--target-env', 'vulkan1.2', '@EXTRA_ARGS@' ]

if run_command(glsl_compiler, [ '--quiet', '--version' ], check: false).returncode() == 0
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

# RTX Shader Compilation Information

# Note: These paths were previously '@CURRENT_SOURCE_DIR@/shaders', '@CURRENT_SOURCE_DIR@/shaders/rtx', and '@OUTDIR@/rtx_shaders'
# when used in src/dxvk/meson.build. This logic has been moved here now to be able to pass these paths into defines. Additionally because
# the CURRENT_SOURCE_DIR/OUTDIR variables wouldn't work in a raw string the path is computed manually based on the assumption that the
# project's name is "dxvk". If this name ever changes these directories will have to change too to match it as this solution isn't
# quite as automatic as those per-project variables were.
shader_directory = join_paths(global_src_root_norm, 'src/dxvk/shaders')
rtx_shader_source_directory = join_paths(global_src_root_norm, 'src/dxvk/shaders/rtx')
# Note: The directory these temporary shader outputs are written to is fairly arbitrary, it could be a temporary directory or any other
# directory, this is just the best way currently for organizing it into the respective project it is outputting data for.
rtx_shader_output_directory = join_paths(current_build_dir_norm, 'src/dxvk/rtx_shaders')

rtxdi_include_path = include_directories('submodules/rtxdi/rtxdi-sdk/include')
# Need to use the include path as an argument to the shader build string,
# and I couldn't find how to convert the IncludeDirs thing to a string... Meson.
rtxdi_include_path_string = join_paths(global_src_root_norm, 'submodules/rtxdi/rtxdi-sdk/include')
volumetrics_include_path_string = join_paths(global_src_root_norm, 'submodules/rtxdi/rtxdi-sdk/include/volumetrics')

rtxcr_include_path = include_directories('submodules/rtxcr/shaders/include')
rtxcr_include_path_string = join_paths(global_src_root_norm, 'submodules/rtxcr/shaders/include')

submodules_include_path = include_directories('submodules')
submodules_include_path_string = join_paths(global_src_root_norm, 'submodules')

rtx_shaders_command_arguments = [
  shader_compile_script,
  # Note: Meson annoyingly represents these "find_program" results as a special object which works fine when used in Meson functions, but
  # not so fine when trying to convert to an argument string like we need for Remix's runtime shader compilation system. As such, we convert it
  # to a path in advance (and ensure it's a POSIX path as Windows-style path slashes are not escaped currently) just so it works for both
  # a Meson custom_target command, as well as an argument string.
  # This can in theory cause problems since the special object Meson uses is actually used to reference the program as a dependency, but for
  # this use case currently it works fine, and worst case this array can just be duplicated to have 2 sets of behavior if needed.
  # See this for more info: https://mesonbuild.com/Reference-manual_returned_external_program.html#external_programfull_path
  '-glslang', fs.as_posix(glsl_compiler.full_path()),
  '-slangc', fs.as_posix(slang_compiler.full_path()),
  '-spirvval', fs.as_posix(spirv_val.full_path()),
  '-input', rtx_shader_source_directory,
  '-include', shader_directory,
  '-include', rtxdi_include_path_string,
  '-include', rtxcr_include_path_string,
  '-include', submodules_include_path_string,
  '-include', volumetrics_include_path_string,
  '-output', rtx_shader_output_directory,
  '-parallel',
]

if get_option('buildtype').startswith('debug')
  # Note: Append -debug to the argument list for debug-esque builds
  rtx_shaders_command_arguments += [ '-debug' ]
endif

# Note: Make a copy of the command arguments up to this point for the runtime shader recompilation system as it requires a slightly
# different set of arguments.
# Note: Append -binary to the runtime shader command arguments to output SPIR-V in binary form rather than as C headers.
rtx_shaders_runtime_command_arguments = rtx_shaders_command_arguments + [ '-binary' ]

# Runtime Shader Recompilation Definitions
# Note: Runtime shader recompilation requires various paths and script arguments as constant strings, so this information is generated
# here and passed in via definitions.

rtx_shaders_runtime_command_argument_string = ' '.join(rtx_shaders_runtime_command_arguments)

add_global_arguments('/DRUNTIME_SHADER_RECOMPILATION_PYTHON_ARGUMENTS="' + rtx_shaders_runtime_command_argument_string + '"', language : 'cpp')
vs_project_defines += 'RUNTIME_SHADER_RECOMPILATION_PYTHON_ARGUMENTS="' + rtx_shaders_runtime_command_argument_string + '";'
add_global_arguments('/DRUNTIME_SHADER_RECOMPILATION_SHADER_SOURCE_PATH="' + rtx_shader_source_directory + '"', language : 'cpp')
vs_project_defines += 'RUNTIME_SHADER_RECOMPILATION_SHADER_SOURCE_PATH="' + rtx_shader_source_directory + '";'
add_global_arguments('/DRUNTIME_SHADER_RECOMPILATION_SPIRV_BINARY_OUTPUT_PATH="' + rtx_shader_output_directory + '"', language : 'cpp')
vs_project_defines += 'RUNTIME_SHADER_RECOMPILATION_SPIRV_BINARY_OUTPUT_PATH="' + rtx_shader_output_directory + '";'

exe_ext = ''
dll_ext = ''

if dxvk_is_msvc
  res_ext = '.res'
else
  res_ext = '.o'
endif

def_spec_ext = '.def'

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

git_rev_parse_out = run_command('git', ['rev-parse','--verify','--short=8', 'HEAD'], check: false)
git_hash = ''
if git_rev_parse_out.returncode() != 0
  error('Failed to get current git hash')
else
  git_hash = git_rev_parse_out.stdout().replace('\n','')
  message('Current git hash: ' + git_hash)
endif

found_tag = ''
ci_commit_tag = ''
get_env_out = run_command(python_interpreter.full_path(), [join_paths(global_src_root_norm, 'scripts-common/get_env.py'), 'CI_COMMIT_TAG'], check: false)
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
  git_describe_out = run_command('git', ['describe','--always','--exact-match', git_hash], check: false)
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
  command: [python_interpreter.full_path(), join_paths(global_src_root_norm, 'scripts-common/echo.py'), full_version],
  input:  'version.h.in',
  output: 'version.h')

#################################
# Handle external dependencies ##
#################################

output_dir = join_paths(global_src_root_norm, '_output/')

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
copy_script_path = join_paths(global_src_root_norm, 'scripts-common/copy' + script_extension)
recursive_copy_path = join_paths(global_src_root_norm, 'scripts-common/recursive_copy' + script_extension)

if get_option('enable_tracy')
  add_project_arguments('-DTRACY_ENABLE', language : 'cpp')
  vs_project_defines += 'TRACY_ENABLE;'
  # only enable profiling when the profiler-application is connected
  add_project_arguments('-DTRACY_ON_DEMAND', language : 'cpp')
  vs_project_defines += 'TRACY_ON_DEMAND;'
endif

if get_option('buildtype') == 'debug'
  usd_debug_or_release = 'Debug'
else
  usd_debug_or_release = 'Release'
endif

# embedding compiler
xxd_exe = find_program('external/nv_xxd/nv_xxd')
embedding_compiler = generator(xxd_exe,
    output : '@PLAINNAME@.h',
    arguments : [ '@INPUT@', '@OUTPUT@', '@BASENAME@' ]
)

mdl_include_dir = include_directories('external/omni_core_materials')

usd_debug_release_dir = 'external/nv_usd_' + usd_debug_or_release + '/'
usd_lib_path = join_paths(global_src_root_norm, usd_debug_release_dir)

# NOTE newer versions of meson prefer relative paths for include directories
nv_usd_include_path = include_directories(join_paths(usd_debug_release_dir, 'include'))
boost_include_path = include_directories(join_paths(usd_debug_release_dir, 'include', 'boost'), is_system: true)

usd_include_paths = [nv_usd_include_path, boost_include_path]

vk_include_path = include_directories('./include/vulkan/include')
# required for NV_present_metering
add_project_arguments('-DVK_ENABLE_BETA_EXTENSIONS', language : 'cpp')

lssusd_include_paths = [nv_usd_include_path, boost_include_path, vk_include_path]

# This will be populated by the usd plugin builder
usd_plugins = []
usd_plugin_deps = []
usd_plugin_delay_load_args = []

nvapi_include_paths = include_directories('submodules/nvapi')
nvapi_lib_path = join_paths(global_src_root_norm, 'submodules/nvapi/amd64')
nvapi_lib = dxvk_compiler.find_library('nvapi64', dirs : nvapi_lib_path)

# Cache lib locations
external_dll_paths += usd_lib_path

nvusd_deplibs = [
  'ar',
  'arch',
  'js',
  'gf',
  'kind',
  'sdf',
  'sdr',
  'ndr',
  'pcp',
  'tf',
  'trace',
  'usd',
  'usdLux',
  'usdGeom',
  'usdRender',
  'usdShade',
  'usdSkel',
  'work',
  'vt',
  'python310',
  'plug',
  'tbbmalloc',
  get_option('buildtype') == 'debug' ? 'boost_python310-vc142-mt-gd-x64-1_76' :
                                       'boost_python310-vc142-mt-x64-1_76',
  get_option('buildtype') == 'debug' ? 'tbb_debug' :
                                       'tbb',    # NOTE: remixsdk also depends on tbb.dll, assuming both USD / remixsdk use the same tbb version
]

usd_linklibs = []
foreach ulib : nvusd_deplibs
  usd_linklibs += dxvk_compiler.find_library(ulib, dirs: join_paths(usd_lib_path, 'lib'))
endforeach

reflex_include_path = include_directories( 'external/reflex/inc')
reflex_lib_path = join_paths(global_src_root_norm, 'external/reflex/lib')
external_dll_paths += reflex_lib_path
reflex_lib = dxvk_compiler.find_library('NvLowLatencyVk', dirs : reflex_lib_path)

aftermath_include_path = include_directories( 'external/aftermath/include')
aftermath_lib_path = join_paths(global_src_root_norm, 'external/aftermath/lib/x64/')
external_dll_paths += aftermath_lib_path
aftermath_lib = dxvk_compiler.find_library('GFSDK_Aftermath_Lib.x64', dirs : aftermath_lib_path)

# this is repeated below, since we overwrite dlss_lib_path in src/dxvk/meson.build for some reason
dlss_root_path = 'external/ngx_sdk_dldn'
dlss_lib_path = join_paths(global_src_root_norm, dlss_root_path, 'lib/Windows_x86_64/rel/')
external_dll_paths += dlss_lib_path
dlfg_root_path = 'external/ngx_sdk_dlfg'
dlfg_lib_path = join_paths(global_src_root_norm, dlfg_root_path, 'lib/Windows_x86_64/rel/')
external_dll_paths += dlfg_lib_path

nrc_dll_name = 'NRC_Vulkan.dll'
nrc_dll_path = join_paths(global_src_root_norm, 'submodules/nrc/bin/')

nrd_lib_path = join_paths(global_src_root_norm, 'external/nrd/Lib/Release/')
if get_option('buildtype') == 'debug'  
  nrd_lib_path = join_paths(global_src_root_norm, 'external/nrd/Lib/Debug/')
endif

# Build meson dependency types
usd_dep = declare_dependency(
  dependencies : [ usd_linklibs ],
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

hdremix_outdir = join_paths(meson.current_source_dir(), 'nv-private', 'hdremix', 'bin', get_option('buildtype'))

# dict of "output" targets --- key is an output path, value is an optional executable inside that path,
# which is used to run that "output" from the IDE
dxvkrt_output_targets = {
  '_output': '',
  '_sdk': '',
  '_hdremix': '',
  'unit_test': '',
}

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
  dlss_root_path = 'external/ngx_sdk_dldn'
  dlss_lib_path = join_paths(global_src_root_norm, dlss_root_path, 'lib/Windows_x86_64/rel/')
  dlfg_root_path = 'external/ngx_sdk_dlfg'
  dlfg_lib_path = join_paths(global_src_root_norm, dlfg_root_path, 'lib/Windows_x86_64/rel/')

  # meson seems to force / as the path separator in custom_target
  # ... except in the case where the path separators are inconsistent, in which case it does nothing
  # force / as the path separator here to make sure it stays consistent
  nrc_lib_path = join_paths(global_src_root_norm, 'submodules/nrc/Lib/')
 
  nrd_lib_path = join_paths(global_src_root_norm, 'external/nrd/Lib/Release/')
  if get_option('buildtype') == 'debug'
    nrd_lib_path = join_paths(global_src_root_norm, 'external/nrd/Lib/Debug/')
  endif
  
  apic_copy_targets = []

  vs_gen_cmdline = [ vs_project_defines ]
  # dict of: { <projectname>: { 'output_path': <copy_output_path>, 'target_suffix': <copy_target_suffix>, 'src_exe': <if non-empty, copy exe from this absolute path> } }
  dxvkrt_copy_targets = {}

  # fill in all of the above
  foreach t, exe : dxvkrt_output_targets
    if t == '_output'
      # _output target is special
      target_suffix = 'output'
      output_path = output_dir
      src_exe = ''
    elif t == '_sdk'
      # _sdk target is special
      target_suffix = 'sdk'
      output_path = join_paths(global_src_root_norm, 'public/bin/')
      src_exe = ''
    elif t == '_hdremix'
      # _hdremix target is special
      target_suffix = 'hdremix'
      output_path = hdremix_outdir
      src_exe = ''
    elif t == 'unit_test'
      # unit_test target is special
      target_suffix = 'unit'
      output_path = join_paths(current_build_dir_norm, 'tests/rtx/unit/')
      src_exe = ''
    else
      target_suffix = t.underscorify()
      output_path = dxvkrt_test_root + t

      if fs.is_absolute(exe)
        src_exe = exe
        vs_gen_cmdline += t + ',' + fs.name(exe)
      else
        src_exe = ''
        # capture data for VS generator
        vs_gen_cmdline += t + ',' + exe
      endif
    endif

    dxvkrt_copy_targets += { t : { 'output_path': output_path, 'target_suffix': target_suffix, 'src_exe': src_exe } }
  endforeach
  
  # add data from gametargets.conf to dxvkrt_copy_targets
  game_targets_run = run_command(python_interpreter,
                                 join_paths(global_src_root_norm, 'vsgen/get_game_target_list.py'),
                                 check: true)
  game_targets = game_targets_run.stdout().strip()

  if game_targets != ''
    foreach t : game_targets.split('\n')
      s = t.split(',')
      key = s[0]
      target_suffix = key.underscorify()
      output_path = s[1]

      dxvkrt_copy_targets += { key : { 'output_path': output_path, 'target_suffix': target_suffix, 'src_exe': '' } }
      
      # note: this doesn't go into the VS generator command line, gametargets.conf is read from python directly
    endforeach
  endif

  foreach key : dxvkrt_copy_targets.keys()
    target_suffix = dxvkrt_copy_targets[key]['target_suffix']
    output_path = dxvkrt_copy_targets[key]['output_path']
    src_exe = dxvkrt_copy_targets[key]['src_exe']
	requires_pdb = target_suffix == 'sdk' or target_suffix == 'output'

    # using tagged install targets to get the files in the right place at install time (each project in VS knows which tag to use)

    install_subdir(join_paths(usd_lib_path, 'lib/usd/'), install_dir : output_path, install_tag: target_suffix)  
    
    foreach ulib : nvusd_deplibs
      install_data(join_paths(usd_lib_path, 'lib', ulib + '.dll'), install_dir : output_path, install_tag: target_suffix) 
    endforeach
  
    install_data(join_paths(global_src_root_norm, 'src', 'usd-plugins', 'plugInfo.json'), install_dir : join_paths(output_path, 'usd', 'plugins'), install_tag: target_suffix) 

    # install USD plugins
    meson.add_install_script(
      join_paths(global_src_root_norm, 'scripts-common/install_usd_plugins.bat'),
      output_path,
      install_tag: target_suffix 
    )

    install_data(join_paths(nrc_dll_path, nrc_dll_name), install_dir : output_path, install_tag: target_suffix) 
    install_data(join_paths(nrc_dll_path, 'cudart64_12.dll'), install_dir : output_path, install_tag: target_suffix) 
    install_data(join_paths(nrc_dll_path, 'nvrtc64_120_0.dll'), install_dir : output_path, install_tag: target_suffix) 
    install_data(join_paths(nrc_dll_path, 'nvrtc-builtins64_125.dll'), install_dir : output_path, install_tag: target_suffix) 

    install_data(join_paths(nrd_lib_path, 'NRD.dll'), install_dir : output_path, install_tag: target_suffix) 

    install_data(join_paths(dlss_lib_path, 'nvngx_dlss.dll'), install_dir : output_path, install_tag: target_suffix) 
    install_data(join_paths(dlss_lib_path, 'nvngx_dlssd.dll'), install_dir : output_path, install_tag: target_suffix) 

    install_data(join_paths(dlfg_lib_path, 'nvngx_dlssg.dll'), install_dir : output_path, install_tag: target_suffix) 

    install_data(join_paths(reflex_lib_path, 'NvLowLatencyVk.dll'), install_dir : output_path, install_tag: target_suffix) 

    install_data(join_paths(aftermath_lib_path, 'GFSDK_Aftermath_Lib.x64.dll'), install_dir : output_path, install_tag: target_suffix) 

    if enable_rtxio == true
      install_data(join_paths(rtxio_bin_path, 'rtxio.dll'), install_dir : output_path, install_tag: target_suffix) 
    endif

    if src_exe != ''
      # intentionally using two install jobs here so we can have unique tags
      # the exe only allows test applications to be compiled and installed, without installing the runtime.  Such copy-less runtime testing scenarios exist in CI.
      meson.add_install_script(
        join_paths(global_src_root_norm, 'scripts-common/install_file_in_dir.bat'),
        src_exe, output_path,
        install_tag: target_suffix + '_exeonly'
      )
      # the regular path allows the exe to be built and deployed along with a runtime.  Such scenarios exist during local development, when launching from VS for example.
      meson.add_install_script(
        join_paths(global_src_root_norm, 'scripts-common/install_file_in_dir.bat'),
        src_exe, output_path,
        install_tag: target_suffix
      )
    endif

    if t != '_output'
      meson.add_install_script(
        join_paths(global_src_root_norm, 'scripts-common/install_file_in_dir.bat'),
        join_paths(current_build_dir_norm, 'src/d3d9', 'd3d9.dll'), output_path,
        install_tag: target_suffix
      )
    endif

    if requires_pdb
	  # ensure we copy everything to these special targets, the PDB is important!
      meson.add_install_script(
        join_paths(global_src_root_norm, 'scripts-common/install_file_in_dir.bat'),
        join_paths(current_build_dir_norm, 'src/d3d9', 'd3d9.*'), output_path,
        install_tag: target_suffix
      )
    endif

  endforeach

  # generate VS project files for ninja
  # note: this writes to <source root>/_vs, which is outside the build directory
  vsproj = run_command(python_interpreter,
                      join_paths(global_src_root_norm, 'vsgen/generate_vs_project_files.py'),
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

if fs.is_dir('nv-private')
  subdir('nv-private')
endif
