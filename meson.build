project('hpgl-viewer', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++17', 'warning_level=2'])

# ── Dependencies ──────────────────────────────────────────────────────────────

glfw_dep  = dependency('glfw3')
gl_dep    = dependency('gl')
opengl_dep = dependency('opengl', required: false)

# ── ImGui (vendored or system) ────────────────────────────────────────────────
# We expect imgui sources in subprojects/imgui/ (wrap or manual clone).
# The wrap below pulls the official repo.

imgui_proj = subproject('imgui',
  default_options: ['default_library=static'])
imgui_dep = imgui_proj.get_variable('imgui_dep')

# ── Executable ───────────────────────────────────────────────────────────────

parser_lib = static_library('hpgl_parser',
  sources : ['src/hpgl_parser.cpp'],
  include_directories : include_directories('src'))

fix_lib = static_library('hpgl_fix',
  sources : ['src/hpgl_fix.cpp'],
  link_with : parser_lib,
  include_directories : include_directories('src'))

config_lib = static_library('hpgl_config',
  sources : ['src/config.cpp'],
  include_directories : include_directories('src'))

view_lib = static_library('hpgl_view',
  sources : ['src/view_state.cpp'],
  link_with : parser_lib,
  include_directories : include_directories('src'))

renderer_lib = static_library('hpgl_renderer',
  sources : ['src/renderer.cpp'],
  link_with : [parser_lib, fix_lib],
  include_directories : include_directories('src'),
  dependencies : [glfw_dep, gl_dep, imgui_dep])

executable('hpgl-viewer',
  sources : ['src/main.cpp'],
  link_with : [parser_lib, fix_lib, config_lib, view_lib, renderer_lib],
  include_directories : include_directories('src'),
  dependencies : [glfw_dep, gl_dep, imgui_dep],
  install : true)

# ── Tests ─────────────────────────────────────────────────────────────────────

test_exe = executable('test_hpgl_parser',
  sources : ['tests/test_hpgl_parser.cpp'],
  link_with : parser_lib,
  include_directories : include_directories('src'))

test('hpgl parser', test_exe)

fix_test_exe = executable('test_hpgl_fix',
  sources : ['tests/test_hpgl_fix.cpp'],
  link_with : [parser_lib, fix_lib],
  include_directories : include_directories('src'))

test('hpgl fix', fix_test_exe,
  args : [meson.project_source_root() / 'tests/data'])

config_test_exe = executable('test_config',
  sources : ['tests/test_config.cpp'],
  link_with : config_lib,
  include_directories : include_directories('src'))

test('config', config_test_exe)

view_test_exe = executable('test_view_state',
  sources : ['tests/test_view_state.cpp'],
  link_with : [parser_lib, view_lib],
  include_directories : include_directories('src'))

test('view state', view_test_exe)

renderer_test_exe = executable('test_renderer',
  sources : ['tests/test_renderer.cpp'],
  link_with : [parser_lib, fix_lib, renderer_lib],
  include_directories : include_directories('src'),
  dependencies : [glfw_dep, gl_dep, imgui_dep])

test('renderer', renderer_test_exe)

# ── Desktop integration ───────────────────────────────────────────────────────

install_data('data/hpgl-viewer.desktop',
  install_dir : get_option('datadir') / 'applications')

install_data('data/hpgl.xml',
  install_dir : get_option('datadir') / 'mime/packages')

install_data('data/mimeapps.list',
  install_dir : get_option('datadir') / 'applications')

meson.add_install_script('sh', '-c',
  'update-mime-database "$DESTDIR"' + get_option('prefix') / get_option('datadir') / 'mime')

meson.add_install_script('sh', '-c',
  'update-desktop-database "$DESTDIR"' + get_option('prefix') / get_option('datadir') / 'applications')
