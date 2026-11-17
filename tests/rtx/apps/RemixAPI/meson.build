RemixAPI_exe = executable(
  'RemixAPI',
  files('./remixapi_example.cpp'),
  include_directories : [ remix_api_include_path ],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_exepath = join_paths(meson.current_build_dir(), RemixAPI_exe.name() + '.exe')

RemixAPI_MediumTest_exe = executable(
  'RemixAPI_MediumTest',
  files('./remixapi_medium_test.cpp'),
  include_directories : [ remix_api_include_path ],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_MediumTest_exepath = join_paths(meson.current_build_dir(), RemixAPI_MediumTest_exe.name() + '.exe')

RemixAPI_SkinningTest_exe = executable(
  'RemixAPI_SkinningTest',
  files('./remixapi_skinning_test.cpp'),
  include_directories : [ remix_api_include_path ],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_SkinningTest_exepath = join_paths(meson.current_build_dir(), RemixAPI_SkinningTest_exe.name() + '.exe')

RemixAPI_WaterTest_exe = executable(
  'RemixAPI_WaterTest',
  files('./remixapi_water_test.cpp'),
  include_directories : [ remix_api_include_path ],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_WaterTest_exepath = join_paths(meson.current_build_dir(), RemixAPI_WaterTest_exe.name() + '.exe')

RemixAPI_GpuResidentScene_exe = executable(
  'RemixAPI_GpuResidentScene',
  files('./remixapi_gpu_resident_scene.cpp'),
  include_directories : [ remix_api_include_path ],
  cpp_args            : ['-DGPU_RESIDENT_SCENE_SCENARIO=Basic'],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_GpuResidentScene_exepath = join_paths(meson.current_build_dir(), RemixAPI_GpuResidentScene_exe.name() + '.exe')

RemixAPI_GpuResidentSceneMedium_exe = executable(
  'RemixAPI_GpuResidentSceneMedium',
  files('./remixapi_gpu_resident_scene.cpp'),
  include_directories : [ remix_api_include_path ],
  cpp_args            : ['-DGPU_RESIDENT_SCENE_SCENARIO=Medium'],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_GpuResidentSceneMedium_exepath = join_paths(meson.current_build_dir(), RemixAPI_GpuResidentSceneMedium_exe.name() + '.exe')

RemixAPI_GpuResidentSceneTextures_exe = executable(
  'RemixAPI_GpuResidentSceneTextures',
  files('./remixapi_gpu_resident_scene.cpp'),
  include_directories : [ remix_api_include_path ],
  cpp_args            : ['-DGPU_RESIDENT_SCENE_SCENARIO=Textures'],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_GpuResidentSceneTextures_exepath = join_paths(meson.current_build_dir(), RemixAPI_GpuResidentSceneTextures_exe.name() + '.exe')

RemixAPI_GpuResidentSceneLifecycle_exe = executable(
  'RemixAPI_GpuResidentSceneLifecycle',
  files('./remixapi_gpu_resident_scene.cpp'),
  include_directories : [ remix_api_include_path ],
  cpp_args            : ['-DGPU_RESIDENT_SCENE_SCENARIO=Lifecycle'],
  override_options    : ['cpp_std=c++20']
)

RemixAPI_GpuResidentSceneLifecycle_exepath = join_paths(meson.current_build_dir(), RemixAPI_GpuResidentSceneLifecycle_exe.name() + '.exe')
