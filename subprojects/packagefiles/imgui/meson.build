project('imgui', 'cpp',
  version : '1.91.6',
  default_options : ['cpp_std=c++17'])

inc = include_directories('.')

imgui_sources = files(
  'imgui.cpp',
  'imgui_demo.cpp',
  'imgui_draw.cpp',
  'imgui_tables.cpp',
  'imgui_widgets.cpp',
  'backends/imgui_impl_glfw.cpp',
  'backends/imgui_impl_opengl3.cpp',
)

glfw_dep = dependency('glfw3')
gl_dep   = dependency('gl')

imgui_lib = static_library('imgui',
  imgui_sources,
  include_directories : [inc, include_directories('backends')],
  dependencies : [glfw_dep, gl_dep],
)

imgui_dep = declare_dependency(
  include_directories : [inc, include_directories('backends')],
  link_with : imgui_lib,
)
