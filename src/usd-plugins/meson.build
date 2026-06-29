fs = import('fs')
usd_schema_plugins_cpp_compiler = meson.get_compiler('cpp')
if usd_schema_plugins_cpp_compiler.get_id() != 'msvc'
  error('USD-SCHEMA-PLUGINS requires the MSVC compiler.')
endif
usd_schema_plugins_isdebug = get_option('buildtype') == 'debug'
usd_schema_plugins_is_woa = target_machine.cpu() == 'armv8'

# download USD, 'run_command()' is invoked only during a configuration step, not each build
# Note: arm64 deps placed in a separate xml to keep x64 builds functional
dependencies_file = not usd_schema_plugins_is_woa ? 'packman-external.xml' : 'packman-external-arm64.xml'
message('USD-SCHEMA-PLUGINS: Downloading USD and other dependencies...')
packman_result = run_command(
  join_paths(meson.current_source_dir(), '../../scripts-common/packman/packman.cmd'), 
  'pull', 
  join_paths(meson.current_source_dir(), dependencies_file), 
  '-p', 
  'windows-x86_64', 
  check: true,
)
if packman_result.returncode() != 0
  error('USD-SCHEMA-PLUGINS: packman failed: ' + packman_result.stderr().strip())
endif
# force meson to reconfigure when external dependency list has been updated
fs.read(join_paths(meson.current_source_dir(), 'packman-external.xml'))

usd_schema_plugins_python_dir = '_external/python/'
if usd_schema_plugins_is_woa
  usd_schema_plugins_python_dir = '_external/python311_x64/'
endif

usd_schema_plugins_python = join_paths(meson.current_source_dir(), usd_schema_plugins_python_dir, 'python.exe')

usd_schema_plugins_nvusd_dir = usd_schema_plugins_isdebug ? '_external/nv_usd/debug/' : '_external/nv_usd/release/'
if usd_schema_plugins_is_woa
  usd_schema_plugins_nvusd_dir = usd_schema_plugins_isdebug ? '_external/usd_stock_arm64/debug/' : '_external/usd_stock_arm64/release/'
endif
usd_schema_plugins_nvusd_libdir = join_paths(meson.current_source_dir(), usd_schema_plugins_nvusd_dir, 'lib')
usd_schema_plugins_nvusd_bindir = join_paths(meson.current_source_dir(), usd_schema_plugins_nvusd_dir, 'bin')
usd_libpython_dir = usd_schema_plugins_nvusd_libdir
usd_schema_plugins_includes = include_directories([
  usd_schema_plugins_nvusd_dir + 'include',
  usd_schema_plugins_python_dir + 'include',
])

if usd_schema_plugins_is_woa
  # atm must use different USD version on host when building for arm64
  usd_schema_plugins_nvusd_dir_host = usd_schema_plugins_isdebug ? '_external/usd_stock_x64/debug/' : '_external/usd_stock_x64/release/'
else
  usd_schema_plugins_nvusd_dir_host = usd_schema_plugins_isdebug ? '_external/nv_usd/debug/' : '_external/nv_usd/release/'
endif
usd_schema_plugins_nvusd_bindir_host = join_paths(meson.current_source_dir(), usd_schema_plugins_nvusd_dir_host, 'bin')
usd_schema_plugins_nvusd_libdir_host = join_paths(meson.current_source_dir(), usd_schema_plugins_nvusd_dir_host, 'lib')
usd_libpython_dir_host = join_paths(usd_schema_plugins_nvusd_libdir_host, 'python')

usd_schema_plugins_nvusd_deplibs_22 = [
  'arch',
  'cameraUtil',
  'gf',
  'hd',
  'hdst',
  'hf',
  'hgi',
  'hgiGL',
  'ndr',
  'sdf',
  'sdr',
  'tf',
  'usd',
  'usdLux',
  'usdGeom',
  'usdImaging',
  'usdVolImaging',
  'vt',
  'python310',
  'plug',
  usd_schema_plugins_isdebug ? 'boost_python310-vc142-mt-gd-x64-1_76' :
                               'boost_python310-vc142-mt-x64-1_76',
  usd_schema_plugins_isdebug ? 'tbb_debug' :
                               'tbb',    # NOTE: remixsdk also depends on tbb.dll, assuming both USD / remixsdk use the same tbb version
]

usd_schema_plugins_nvusd_deplibs = [
  'usd_ar',
  'usd_arch',
  'usd_js',
  'usd_gf',
  'usd_kind',
  'usd_sdf',
  'usd_sdr',
  'usd_pcp',
  'usd_tf',
  'usd_trace',
  'usd_usd',
  'usd_usdLux',
  'usd_usdGeom',
  'usd_usdRender',
  'usd_usdShade',
  'usd_usdSkel',
  'usd_work',
  'usd_vt',
  'usd_python',
  'usd_plug',
  'usd_ts',
  usd_schema_plugins_isdebug ? 'tbb12_debug' :
                               'tbb12',    # NOTE: remixsdk also depends on tbb.dll, assuming both USD / remixsdk use the same tbb version
]
usd_schema_plugins_linklibs = []
if usd_schema_plugins_is_woa
  foreach ulib : usd_schema_plugins_nvusd_deplibs
    usd_schema_plugins_linklibs += usd_schema_plugins_cpp_compiler.find_library(ulib, dirs: usd_schema_plugins_nvusd_libdir)
  endforeach

  usd_schema_plugins_linklibs += usd_schema_plugins_cpp_compiler.find_library('python311', dirs: usd_schema_plugins_nvusd_libdir)
else
  foreach ulib : usd_schema_plugins_nvusd_deplibs_22
    usd_schema_plugins_linklibs += usd_schema_plugins_cpp_compiler.find_library(ulib, dirs: usd_schema_plugins_nvusd_libdir)
  endforeach
endif

usd_schema_plugins_defines = [
  '-DNOMINMAX',
  '-DGLEW_STATIC',
  '-DGLEW_NO_GLU',
  usd_schema_plugins_isdebug ? '-DTBB_USE_DEBUG=1' : '-DTBB_USE_DEBUG=0',
]
usd_schema_plugins_compilerflags = [
  '/Zc:inline-', # plugins dont register without this
  '/wd4305',  # truncation from 'double' to 'float'
  '/wd4244',  # conversion from 'double' to 'int'
]

# On Windows, echo %PATH% gives you the system PATH
orig_path = run_command('cmd', ['/C', 'echo', '%PATH%'])
orig_path = orig_path.stdout().strip()

# Do we have jinja
jinja_ok = run_command(
  usd_schema_plugins_python, ['-c', 'import jinja2'],
  check:      false,
  env:        {'PYTHONPATH': usd_libpython_dir}
)

if jinja_ok.returncode() != 0
  pip_install = run_command(
    usd_schema_plugins_python,
    ['-m', 'pip', 'install', '--user', 'jinja2'],
    check:      false
  )
endif

usd_plugins_output_dir = join_paths(meson.project_build_root(), 'remix-usd-plugins', 'usd')

# List all schema-plugin names:
schema_plugins = [
  'RemixParticleSystem',
]

foreach plugin : schema_plugins
  subdir(plugin)
endforeach