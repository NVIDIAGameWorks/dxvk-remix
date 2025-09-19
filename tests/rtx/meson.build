subdir('unit')

dxvkrt_test_root = meson.global_source_root().replace('\\', '/') + '/tests/rtx/dxvk_rt_testing/'
if fs.is_dir('dxvk_rt_testing')
  subdir('dxvk_rt_testing')
endif

subdir('apps/RemixAPI')
subdir('apps/RemixAPI_C')
subdir('apps/HydraTestRender')
if dxvk_is_ninja
  # apps that are compiled as a part of dxvk-remix
  dxvkrt_output_targets += {
    'apics/RemixAPI'    : RemixAPI_exepath,
    'apics/RemixAPI_C'  : RemixAPI_C_exepath,
    'apics/HydraTestRender'  : HydraTestRender_exepath,
  }
endif