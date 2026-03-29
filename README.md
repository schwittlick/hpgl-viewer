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

- Open `.hpgl` / `.plt` files via path input, drag-and-drop, or `O` key
- Add multiple files as layers (`A` key or drag-and-drop additional files)
- Pan (left-drag or middle-drag), zoom to cursor (scroll wheel), rotate 90° (`R`)
- Fit-to-window (`C`), fullscreen (`F`)
- Per-pen color (color picker) and line thickness (slider)
- Plotter coordinate tooltip on hover
- Pen-up move visualisation: green = short, orange = long but outside zone, red = will be fixed
- Pen-up smear fix: inserts pen-8 waypoint dots along long pen-up moves, exported as a separate `_fixed.hpgl` file
  - **Threshold** slider: minimum move length to flag/fix (cm)
  - **Waypoint spacing** slider: distance between inserted dots (cm)
  - **Left zone** slider: only fix moves that start within the leftmost X% of the document
- PNG export at physical DPI

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `O` | Open file (replaces current) |
| `A` | Add file as additional layer |
| `F` | Toggle fullscreen |
| `Q` | Quit |
| `C` | Fit to window |
| `R` | Rotate 90° |
| `E` | Apply pen-up fix (preview in viewport, use Export to save) |

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

### Test

```bash
cd hpgl-viewer
meson setup build
ninja -C build test
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
