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

executable('hpgl-viewer',
  sources : ['src/main.cpp'],
  dependencies : [glfw_dep, gl_dep, imgui_dep],
  install : true)
