# HPGL Viewer

A lightweight HPGL file viewer built with Dear ImGui + OpenGL3 + GLFW.

## Supported HPGL commands

| Command | Description |
|---------|-------------|
| `SP n`  | Select pen n (1–8) |
| `PU`    | Pen up (optionally move to x,y) |
| `PD`    | Pen down (optionally draw to x,y pairs) |
| `PA x,y[,x,y,…]` | Plot absolute — moves or draws depending on pen state |
| `LB text␃` | Label — draws text with a single-stroke font, up to the terminator |
| `DT c`  | Define the label terminator (default `ETX`, `0x03`) |
| `SI w,h` | Absolute character size in cm (no parameters → 0.19 × 0.27 cm) |
| `LO n`  | Label origin 1–9 (left/centre/right × bottom/centre/top), 11–19 offset by half a cell |
| `DI run,rise` | Label direction (no parameters → horizontal) |
| `SL t`  | Character slant, as a tangent (no parameters → upright) |
| `CP s,l` | Move the pen s character cells and l lines without drawing (no parameters → CR + LF) |
| `IN` / `DF` | Reset the labelling state (size, origin, direction, slant, terminator) |

Inside a label, `LF` starts a new line (spaced two character heights apart)
and `CR` returns to the start of the current line; other control characters
are skipped. Unsupported labelling commands (`SR`, `DR`, `ES`, `CS`/`SS`/`SA`)
are ignored, so text using them still renders at the last absolute size and
direction.

## Features

- Open `.hpgl` / `.plt` files via path input, drag-and-drop, or `O` key — files are parsed on a worker thread with a progress bar overlay; multiple drops queue and load sequentially
- Add multiple files as layers (`A` key or drag-and-drop additional files); the **Flatten all layers into one** button concatenates every loaded layer's strokes (in order) into a single `_merged` layer for combined fixing/exporting
- Pan (left-drag or middle-drag), zoom to cursor (scroll wheel), rotate 90° (`R`)
- Fit-to-window (`C`), fullscreen (`F`)
- Per-pen color (color picker), line thickness (dropdown), and opacity (transparency slider)
- Per-layer solid color: each layer row has a solid-colour toggle, an editable colour swatch, and a line-thickness dropdown, so a layer can override the pen palette and render every stroke in one colour + width — useful for compositing multiple single-color files (e.g. a dots layer and a lines layer at different widths). Pen-mode layers keep using the shared pen palette. The override is applied to PNG export too
- Text rendering: `LB` labels are drawn with the Hershey Sans 1-stroke font, sized/positioned/rotated per `SI`, `LO`, `DI` and `SL`, so plotter text (titles, calendar dates, annotations) shows up in the viewport instead of being skipped. Labels become ordinary strokes, so they are fixed, merged and exported like any other geometry (an exported `_fixed.hpgl` contains the text as polylines, not `LB` commands)
- Plotter coordinate tooltip on hover
- Pen-up move visualisation: green = short, orange = long but outside zone, red = will be fixed
- Pen-up smear fix: inserts pen-8 waypoint dots along long pen-up moves, exported as a separate `_fixed.hpgl` file
  - **Threshold** slider: minimum move length to flag/fix (cm)
  - **Waypoint spacing** slider: distance between inserted dots (cm)
  - **Left zone** slider: only fix moves that start within the leftmost X% of the document
- Merge close strokes: combines consecutive same-pen strokes whose gap (end→start) is ≤ the pen's configured width, eliminating unnecessary pen-up/down movements between nearly-adjacent strokes; chains of strokes are merged together
- Split long strokes: breaks any pen-down stroke whose polyline length exceeds **Max stroke length** into multiple shorter consecutive strokes (pen lifts and drops back down at the split point) — useful for ink-flow on long continuous lines
- Simplify collinear points: removes redundant interior points within a stroke — a middle point is dropped when its perpendicular distance to the segment between its neighbours is within **Collinear tol** (mm) AND it projects between them (so back-and-forth on the same axis is preserved)
- Export dots + lines to separate HPGL files: writes `<name>_dots.hpgl` (every single-point or all-coincident-point stroke) and `<name>_lines.hpgl` (everything else) — useful for plotting dots and lines with a different pen on the plotter
- PNG export at 600 DPI (sized from the document's physical dimensions)
- FBO-cached canvas: GPU strokes + pen-up moves render once into an off-screen framebuffer and are sampled back via `ImGui::Image`; the cache is invalidated only when canvas-affecting state changes (pan/zoom/rotation, doc, pen colors/widths, layer colors, threshold, canvas size) so panel-only interactions stay snappy on heavy files

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
| `U` | Unload all files / reset to empty state |

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
meson setup build       # only needed once
ninja -C build
sudo ninja -C build install
```

The `-C build` is required on every command — `build.ninja` lives in `build/`,
not in the project root. Installing also updates the MIME and desktop
databases, so `.hpgl` files open in the viewer from your file manager.

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
- Label font: `src/hpgl_font.cpp` is generated by `tools/gen_hpgl_font.py` from
  `tools/HersheySans1.svg`, a vendored copy of the Hershey Sans 1-stroke font
  (as distributed with Inkscape's Hershey Text extension). It stands in for the
  plotter's built-in stick font, so glyph shapes are close but not identical to
  what an HP plotter draws. Both the font and the generated table are checked
  in — building never needs either, and regenerating needs only Python:

  ```bash
  python3 tools/gen_hpgl_font.py > src/hpgl_font.cpp
  ```

  The Hershey Fonts were originally created by Dr. A. V. Hershey while working
  at the U. S. National Bureau of Standards; the font data format was created
  by James Hurt, Cognition, Inc.
