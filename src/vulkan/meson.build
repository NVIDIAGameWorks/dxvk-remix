vkcommon_src = files([
  'vulkan_loader.cpp',
  'vulkan_names.cpp',
  'vulkan_presenter.cpp',
])

thread_dep = dependency('threads')

vulkan_deps = [ thread_dep, lib_vulkan ]

vkcommon_lib = static_library('vkcommon', vkcommon_src,
  dependencies        : [ vulkan_deps ],
  override_options    : ['cpp_std='+dxvk_cpp_std],
  include_directories : [ dxvk_include_path ])

vkcommon_dep = declare_dependency(
  link_with           : [ vkcommon_lib ],
  include_directories : [ dxvk_include_path ])
