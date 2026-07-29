# 'meson install -C <builddir>' will produce a distributable 'bin' folder

fs = import('fs')
hdremix_cpp_compiler = meson.get_compiler('cpp')
if hdremix_cpp_compiler.get_id() != 'msvc'
  error('HDREMIX requires the MSVC compiler.')
endif
hdremix_isdebug = get_option('buildtype') == 'debug'

hdremix_remixsdk_dir = get_option('remixsdk_local') == '' ? '../../../../public/' : get_option('remixsdk_local')

# check headers
if not fs.exists(join_paths(meson.current_source_dir(), hdremix_remixsdk_dir, 'include/remix'))
  error('HDREMIX: Remix SDK include folder not found, ' +
        'please copy Remix SDK headers into: ' + join_paths(meson.current_source_dir(), hdremix_remixsdk_dir, 'include/remix'))
endif
# build or check binaries
if get_option('remixsdk_local') == ''
  hdremix_dxvksdkbuilt = [d3d9_dep]
else
  hdremix_dxvksdkbuilt = []
  # use prebuilt Remix SDK
  if not fs.exists(join_paths(meson.current_source_dir(), hdremix_remixsdk_dir, 'bin/d3d9.dll'))
    error('HDREMIX: Remix SDK binaries not found, ' +
          'please compile Remix SDK and put the binaries into: ' + join_paths(meson.current_source_dir(), hdremix_remixsdk_dir, 'bin'))
  endif
endif


# download USD, 'run_command()' is invoked only during a configuration step, not each build
message('HDREMIX: Downloading USD and other dependencies...')
packman_result = run_command(
  join_paths(meson.current_source_dir(), '../../../../scripts-common/packman/packman.cmd'), 
  'pull', 
  join_paths(meson.current_source_dir(), 'packman-external.xml'), 
  '-p', 
  'windows-x86_64', 
  check: true,
)
if packman_result.returncode() != 0
  error('HDREMIX: packman failed: ' + packman_result.stderr().strip())
endif
# force meson to reconfigure when external dependency list has been updated
fs.read(join_paths(meson.current_source_dir(), 'packman-external.xml'))


hdremix_nvusd_dir = hdremix_isdebug ? '_external/usd_stock_x64/debug/' : '_external/usd_stock_x64/release/'
hdremix_nvusd_libdir = join_paths(meson.current_source_dir(), hdremix_nvusd_dir, 'lib')
hdremix_nvusd_bindir = join_paths(meson.current_source_dir(), hdremix_nvusd_dir, 'bin')

hdremix_python_dir = '_external/python311_x64/'
hdremix_python_libdir = join_paths(meson.current_source_dir(), hdremix_python_dir, 'libs')

hdremix_glfw_dir = hdremix_isdebug ? '_external/glfw/debug/' : '_external/glfw/release/'
hdremix_glfw_libdir = join_paths(meson.current_source_dir(), hdremix_glfw_dir, 'lib')


hdremix_includes = include_directories([
  '../../../../src/lssusd',       # because of 'usd_include_begin.h' / 'usd_include_end.h'
  '../../../../src/usd-plugins',  # support for custom usd plugins
  hdremix_remixsdk_dir + 'include',   # Remix SDK
  hdremix_nvusd_dir + 'include',
  hdremix_glfw_dir + 'include',
  hdremix_python_dir + 'include',
])


hdremix_defines = [
  '-DNOMINMAX',
  '-DGLEW_STATIC',
  '-DGLEW_NO_GLU',
  hdremix_isdebug ? '-DTBB_USE_DEBUG=1' : '-DTBB_USE_DEBUG=0',
]


hdremix_compilerflags = [
  '/Zc:inline-', # plugins dont register without this
]

hdremix_nvusd_deplibs = [
  'usd_cameraUtil',
  'usd_gf',
  'usd_glf',
  'usd_hd',
  'usd_hdx',
  'usd_hgi',
  'usd_hgiGL',
  'usd_sdf',
  'usd_tf',
  'usd_plug',
  'usd_usd',
  'usd_usdImaging',
  'usd_usdShade',
  'usd_usdGeom',
  'usd_vt',
  'usd_python',
]

hdremix_nvusd_depbins = [
  'MaterialXCore',
  'MaterialXFormat',
  'MaterialXGenGlsl',
  'MaterialXGenMdl',
  'MaterialXGenMsl',
  'MaterialXGenOsl',
  'MaterialXGenShader',
  'MaterialXRender',
  'MaterialXRenderGlsl',
  'MaterialXRenderHw',
  'MaterialXRenderOsl',
]

hdremix_depbins = []
foreach ulib : hdremix_nvusd_depbins
  hdremix_depbins += join_paths(hdremix_nvusd_bindir, ulib)
endforeach

hdremix_linklibs = [
  hdremix_cpp_compiler.find_library('opengl32'),
]
foreach ulib : hdremix_nvusd_deplibs
  hdremix_linklibs += hdremix_cpp_compiler.find_library(ulib, dirs: hdremix_nvusd_libdir)
endforeach

hdremix_linklibs += hdremix_cpp_compiler.find_library(hdremix_isdebug ? 'tbb12_debug' : 'tbb12', dirs: hdremix_nvusd_libdir)
hdremix_linklibs += hdremix_cpp_compiler.find_library('python311', dirs: hdremix_python_libdir)

hdremix_test_exe_src = files([
  'test_render.cpp',
])

lib_glfw3 = hdremix_cpp_compiler.find_library('glfw3', dirs: hdremix_glfw_libdir)

hdremix_test_exe = executable(
  'hdremix_test_renderer',
  hdremix_test_exe_src,
  include_directories: hdremix_includes,
  cpp_args: hdremix_defines + hdremix_compilerflags,
  dependencies: hdremix_linklibs + hdremix_dxvksdkbuilt + usd_plugin_deps + lib_glfw3,
  override_options: ['cpp_std=c++17'],
  install: true,
  install_dir: hdremix_outdir,
  install_tag: 'apics_HydraTestRender'
)

HydraTestRender_exepath = join_paths(meson.current_build_dir(), hdremix_test_exe.name() + '.exe')

dxvkrt_test_root = meson.global_source_root().replace('\\', '/') + '/tests/rtx/dxvk_rt_testing/'
hydra_test_render_path = join_paths(dxvkrt_test_root, 'apics', 'HydraTestRender')

# install USD dlls
meson.add_install_script(
  join_paths(global_src_root_norm, 'scripts-common/copy.bat'),
  hdremix_nvusd_libdir,
  hydra_test_render_path,
  'usd_*.dll',
  install_tag: 'apics_HydraTestRender' 
)

install_data(
  join_paths(hdremix_nvusd_bindir, hdremix_isdebug ? 'tbb12_debug.dll' : 'tbb12.dll'),
  install_dir: hydra_test_render_path,
  install_tag: 'apics_HydraTestRender'
)

install_data(
  join_paths(hdremix_nvusd_bindir, hdremix_isdebug ? 'tbbmalloc_debug.dll' : 'tbbmalloc.dll'),
  install_dir: hydra_test_render_path,
  install_tag: 'apics_HydraTestRender'
)

foreach ulib : hdremix_depbins
  install_data(
    ulib + '.dll',
    install_dir: hydra_test_render_path,
    install_tag: 'apics_HydraTestRender'
  )
endforeach
