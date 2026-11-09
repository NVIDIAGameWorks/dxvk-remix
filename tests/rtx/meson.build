subdir('unit')

# Note: on arm building only the unit tests
if target_machine.cpu() != 'armv8'

dxvkrt_test_root = meson.global_source_root().replace('\\', '/') + '/tests/rtx/dxvk_rt_testing/'

# Installing apics outputs creates dxvk_rt_testing, so public checkouts can have the directory without meson.build.
if fs.is_file('dxvk_rt_testing/meson.build')
  subdir('dxvk_rt_testing')
endif

subdir('apps/RemixAPI')
subdir('apps/RemixAPI_C')
subdir('apps/HydraTestRender')

if dxvk_is_ninja
  # apps that are compiled as a part of dxvk-remix
  dxvkrt_output_targets += {
    'apics/RemixAPI'              : RemixAPI_exepath,
    'apics/RemixAPI_C'            : RemixAPI_C_exepath,
    'apics/RemixAPI_MediumTest'   : RemixAPI_MediumTest_exepath,
    'apics/RemixAPI_SkinningTest' : RemixAPI_SkinningTest_exepath,
    'apics/RemixAPI_WaterTest'    : RemixAPI_WaterTest_exepath,
    'apics/HydraTestRender'       : HydraTestRender_exepath
  }
endif

endif
