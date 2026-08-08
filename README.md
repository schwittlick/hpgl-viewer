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
- Plotter bounds overlay (`B`): pick a plotter and the viewer draws its plotting envelope over the drawing, so you can see whether the file will actually land on paper. Three nested rectangles, all in the file's own HPGL unit space (no transform — HPGL files are authored in native plotter units):
  - **paper** — the physical sheet, faintly filled. Drawn dashed while its offset is unverified (see below)
  - **max area** — the furthest the pen can physically reach; anything outside is clipped by the machine
  - **plot area** — max area inset by the plotter's deliberate per-edge margin; this is what a drawing should stay inside. Drawn green when the drawing fits, red when it does not
  - Per-edge clearance in mm is labelled on the canvas and listed in the sidebar, negative and red where the drawing overruns that edge
  - The plotter is auto-selected from the filename (files named `…_hpdm_sx_a1_…`, `…_hp7550a_a3_…` etc. match the registry's `name` field); turn this off with **Detect plotter from filename**
  - **Fit includes bounds** makes fit-to-window (`C`) frame the union of the drawing and the bounds, so a drawing that lands off the sheet stays visible next to it
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
| `B` | Toggle the plotter bounds overlay |
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

## Plotter registry

The plotter bounds overlay reads `data/plotters.json`. Field names match the
shared registry in the `cursor` project so entries can be copied between them.
It currently covers the HP 7550A, the HP Draftmaster family (DM II / SX /
RX-Plus), the Mutoh XP-500 and the Roland DXY family; adding a machine is one
JSON object.

The viewer takes the first of these that exists:

1. `$HPGL_VIEWER_PLOTTERS`
2. `~/.config/hpgl-viewer/plotters.json` (per-user, survives reinstalls)
3. `<prefix>/share/hpgl-viewer/plotters.json` (installed copy)
4. `data/plotters.json` relative to the working directory (running from a source tree)

**Reload registry** in the sidebar re-reads the file, so entries can be edited
and checked without restarting.

### What is measured and what is guessed

`max_area` and `margin` come from plotter documentation and are trustworthy —
the effective plot area is `max_area` inset by `margin` (positive shrinks that
edge inward, negative extends past the maximum).

The **paper** block is not. No plotter manual states where the sheet sits
relative to the plot area, so every entry ships with a placeholder derived by
centring the plot area on the sheet — correct only for machines whose
`max_area` is symmetric about the origin, and wrong for the ones whose origin
is the lower-left of the plot area (the HP 7550A and the Roland DXYs). Those
entries carry `"verified": false` and are drawn with a **dashed** sheet outline.

To fix one: plot a file, measure the gap from the paper edge to the furthest
the pen reached, dial the **left**/**bottom** offsets in the sidebar until the
overlay matches, press **Copy paper JSON**, and paste the block into the
plotter's entry. The offsets mean:

```jsonc
"paper": {
  "size_mm":   { "w": 841, "h": 594 },   // the sheet
  "offset_mm": { "left": 18.0, "bottom": 7.0 },
  // ^ mm from the sheet's left/bottom edge to max_area.x / max_area.y,
  //   i.e. how far in from the paper edge the pen can first reach.
  //   Negative means the plot area runs off the sheet.
  "verified": true                        // stops the dashed outline
}
```

## Notes

- HPGL Y-axis: the viewer treats Y as-is (no flip). If your plotter files
  appear upside-down, tick the "Flip Y" checkbox (easy to add).
- Pen numbers beyond 8 are clamped to pen 8's style.
- The bounds overlay is drawn on the CPU alongside the grid, so toggling it
  never invalidates the cached canvas FBO. It is a viewport aid only — it is
  not included in PNG export.
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
