claude should read this file regularly

while i update it with ideas

it should just pick these up and work on them

and update this document too

todo:
-

done:
- [x] render coordinate system — adaptive grid overlay with mm/cm labels and doc bounding box
- [x] pen styles: add drop down for 0.3, 0.4, 0.5, 0.6, 0.8, 1.0 pen widths
- [x] add export image function, that renders the current hpgl as png
- [x] add multiple hpgls to the same view, to preview various layers
- [x] scaling with mouse wheel gets slower the closer i get — zoom factor now proportional to scroll amount via powf(1.1, delta), so fast scrolling zooms faster and trackpads feel smooth
- [x] viewport culling: per-stroke bounding boxes computed at parse time; drawHpgl skips strokes entirely outside the visible HPGL area — O(1) cull per stroke instead of submitting every segment to ImGui
- [x] GPU stroke renderer: pen-down strokes moved from ImGui CPU draw list (900k AddLine calls/frame) to a VBO with up to 8 glDrawArrays calls per frame — StrokeRenderer in renderer.h/.cpp, uploaded on load/fix, drawn via ImGui draw callback
