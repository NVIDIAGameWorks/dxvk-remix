fs = import('fs')
usd_schema_plugins_cpp_compiler = meson.get_compiler('cpp')
if usd_schema_plugins_cpp_compiler.get_id() != 'msvc'
  error('USD-SCHEMA-PLUGINS requires the MSVC compiler.')
endif
usd_schema_plugins_isdebug = get_option('buildtype') == 'debug'


# download USD, 'run_command()' is invoked only during a configuration step, not each build
message('USD-SCHEMA-PLUGINS: Downloading USD and other dependencies...')
packman_result = run_command(
  join_paths(meson.current_source_dir(), '../../scripts-common/packman/packman.cmd'), 
  'pull', 
  join_paths(meson.current_source_dir(), 'packman-external.xml'), 
  '-p', 
  'windows-x86_64', 
  check: true,
)
if packman_result.returncode() != 0
  error('USD-SCHEMA-PLUGINS: packman failed: ' + packman_result.stderr().strip())
endif
# force meson to reconfigure when external dependency list has been updated
fs.read(join_paths(meson.current_source_dir(), 'packman-external.xml'))

usd_schema_plugins_python = join_paths(meson.current_source_dir(), '_external', 'python', 'python.exe')

usd_schema_plugins_nvusd_dir = usd_schema_plugins_isdebug ? '_external/nv_usd/debug/' : '_external/nv_usd/release/'
usd_schema_plugins_nvusd_libdir = join_paths(meson.current_source_dir(), usd_schema_plugins_nvusd_dir, 'lib')
usd_schema_plugins_nvusd_bindir = join_paths(meson.current_source_dir(), usd_schema_plugins_nvusd_dir, 'bin')
usd_libpython_dir = join_paths(usd_schema_plugins_nvusd_libdir, 'python')
usd_schema_plugins_includes = include_directories([
  usd_schema_plugins_nvusd_dir + 'include',
])
usd_schema_plugins_nvusd_deplibs = [
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
usd_schema_plugins_linklibs = []
foreach ulib : usd_schema_plugins_nvusd_deplibs
  usd_schema_plugins_linklibs += usd_schema_plugins_cpp_compiler.find_library(ulib, dirs: usd_schema_plugins_nvusd_libdir)
endforeach

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