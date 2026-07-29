# External Dependencies Configuration
# This file contains build configuration for external dependencies
# that are not managed by the version control system (external/)
# and third-party submodules (submodules/).
#
# NOTE: NVAPI, Reflex, Aftermath, DLSS, DLFG, NRD, NRC and XeSS are configured
# directly in the root meson.build, as they carry Windows-on-Arm (dxvk_is_woa)
# specific handling that selects between x64 and arm64 binaries.

# ============================================================================
# Sentry Crash Reporting
# ============================================================================
# Sentry Native SDK is located in external/sentry-native/ (downloaded and linked via packman).
# Disabled for `debug` builds, as the prebuilt Release Sentry/Crashpad will fail to link in debug builds;

enable_sentry = get_option('buildtype') != 'debug'

if enable_sentry
  if dxvk_is_woa
    sentry_root_dir = 'external/sentry-native_arm64'
  else
    sentry_root_dir = 'external/sentry-native'
  endif
  sentry_lib_path = join_paths(global_src_root_norm, sentry_root_dir, 'lib')
  sentry_bin_path = join_paths(global_src_root_norm, sentry_root_dir, 'bin')
  external_dll_paths += sentry_bin_path

  # Find all Sentry and Crashpad static libraries
  sentry_include_path = include_directories(join_paths('..', sentry_root_dir, 'include'))
  sentry_lib = dxvk_compiler.find_library('sentry', dirs : sentry_lib_path)
  crashpad_client_lib = dxvk_compiler.find_library('crashpad_client', dirs : sentry_lib_path)
  crashpad_compat_lib = dxvk_compiler.find_library('crashpad_compat', dirs : sentry_lib_path)
  crashpad_getopt_lib = dxvk_compiler.find_library('crashpad_getopt', dirs : sentry_lib_path)
  crashpad_minidump_lib = dxvk_compiler.find_library('crashpad_minidump', dirs : sentry_lib_path)
  crashpad_mpack_lib = dxvk_compiler.find_library('crashpad_mpack', dirs : sentry_lib_path)
  crashpad_snapshot_lib = dxvk_compiler.find_library('crashpad_snapshot', dirs : sentry_lib_path)
  crashpad_tools_lib = dxvk_compiler.find_library('crashpad_tools', dirs : sentry_lib_path)
  crashpad_util_lib = dxvk_compiler.find_library('crashpad_util', dirs : sentry_lib_path)
  crashpad_zlib_lib = dxvk_compiler.find_library('crashpad_zlib', dirs : sentry_lib_path)
  mini_chromium_lib = dxvk_compiler.find_library('mini_chromium', dirs : sentry_lib_path)

  # Find Windows system libraries that Sentry/Crashpad depend on
  dbghelp_lib = dxvk_compiler.find_library('dbghelp')
  shlwapi_lib = dxvk_compiler.find_library('shlwapi')
  version_lib = dxvk_compiler.find_library('version')
  winhttp_lib = dxvk_compiler.find_library('winhttp')
  advapi32_lib = dxvk_compiler.find_library('advapi32')
  kernel32_lib = dxvk_compiler.find_library('kernel32')
  powrprof_lib = dxvk_compiler.find_library('powrprof')
  rpcrt4_lib = dxvk_compiler.find_library('rpcrt4')
  user32_lib = dxvk_compiler.find_library('user32')
  synchronization_lib = dxvk_compiler.find_library('Synchronization')

  # Declare the complete Sentry dependency with all required libraries
  sentry_dep = declare_dependency(
      dependencies : [
        sentry_lib,
        crashpad_client_lib,
        crashpad_compat_lib,
        crashpad_getopt_lib,
        crashpad_minidump_lib,
        crashpad_mpack_lib,
        crashpad_snapshot_lib,
        crashpad_tools_lib,
        crashpad_util_lib,
        crashpad_zlib_lib,
        mini_chromium_lib,
        dbghelp_lib,
        shlwapi_lib,
        version_lib,
        winhttp_lib,
        advapi32_lib,
        kernel32_lib,
        powrprof_lib,
        rpcrt4_lib,
        user32_lib,
        synchronization_lib
      ],
      include_directories : sentry_include_path
    )
  add_project_arguments('/DREMIX_SENTRY_ENABLED=1', language : 'cpp')
else
  sentry_dep = declare_dependency()
  add_project_arguments('/DREMIX_SENTRY_ENABLED=0', language : 'cpp')
endif

# Sentry environment: when -Dremix_sentry_environment= is set (e.g. by CI), bake it in; otherwise C++ uses "development".
remix_sentry_environment = get_option('remix_sentry_environment')
if remix_sentry_environment != ''
  add_global_arguments('/DREMIX_SENTRY_ENVIRONMENT="' + remix_sentry_environment + '"', language : 'cpp')
endif

# ============================================================================
# USD (Universal Scene Description)
# ============================================================================
if get_option('buildtype') == 'debug'
  usd_debug_or_release = 'Debug'
else
  usd_debug_or_release = 'Release'
endif

if dxvk_is_woa
  usd_debug_release_dir = 'external/usd_stock_arm64/' + usd_debug_or_release + '/'
else
  usd_debug_release_dir = 'external/usd_stock_x64/' + usd_debug_or_release + '/'
endif

usd_lib_path = join_paths(global_src_root_norm, usd_debug_release_dir)

if dxvk_is_woa
  python_lib_path = join_paths(usd_lib_path, 'lib')
  python_dll_path = python_lib_path
  # The stock Arm64 USD package already contains the Python headers.
  python_include_paths = []
else
  python_path = join_paths(global_src_root_norm, 'external/python311_x64')
  python_lib_path = join_paths(python_path, 'libs')
  python_dll_path = python_path
  python_include_paths = [include_directories('../external/python311_x64/include')]
endif

# NOTE newer versions of meson prefer relative paths for include directories.
# Prefix with ../ since these paths are resolved relative to this subdir.
nv_usd_include_path = include_directories(join_paths('..', usd_debug_release_dir, 'include'))

usd_include_paths = [nv_usd_include_path] + python_include_paths

# ============================================================================
# RTX IO
# ============================================================================
# rtxio_bin_path / rtxio_lib / rtxio_include_path are configured in the root meson.build
# (they carry Windows-on-Arm specific handling); we only expose the dependency object here.
if enable_rtxio == true
  rtxio_dep = declare_dependency(
    dependencies : [ rtxio_lib ],
    include_directories : rtxio_include_path
  )
endif

# ============================================================================
# Materials and Shaders
# ============================================================================
mdl_include_dir = include_directories('../external/omni_core_materials')
