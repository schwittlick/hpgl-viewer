# HPGL Viewer

A lightweight HPGL file viewer built with Dear ImGui + OpenGL3 + GLFW.

## Supported HPGL commands

| Command | Description |
|---------|-------------|
| `SP n`  | Select pen n (1–8) |
| `PU`    | Pen up (optionally move to x,y) |
| `PD`    | Pen down (optionally draw to x,y pairs) |
| `PA x,y[,x,y,…]` | Plot absolute — moves or draws depending on pen state |

## Features

- Open `.hpgl` / `.plt` files via path input
- Fit-to-window button
- Pan (left-drag or middle-drag)
- Zoom to cursor (scroll wheel)
- Per-pen color (color picker) and line thickness (slider)
- Plotter coordinate tooltip on hover

## Build

### Dependencies

- `glfw3` (system package) (sudo pacman -S glfw-wayland)
- `gl` / `opengl` (system)
- `meson` ≥ 1.0, `ninja`
- kdialog
- Internet access for the first build (fetches ImGui via wrap)

```bash
# Arch
sudo pacman -S glfw-x11 mesa meson ninja

# or Wayland
sudo pacman -S glfw-wayland mesa meson ninja
```

### Compile

```bash
cd hpgl-viewer
meson setup build
ninja -C build
sudo ninja install
```

### Run

```bash
./build/hpgl-viewer
```

Then type your file path in the **File** panel and click **Open**.

## Notes

- HPGL Y-axis: the viewer treats Y as-is (no flip). If your plotter files
  appear upside-down, tick the "Flip Y" checkbox (easy to add).
- Pen numbers beyond 8 are clamped to pen 8's style.
