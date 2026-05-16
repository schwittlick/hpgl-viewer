#pragma once

#include "hpgl_parser.h"
#include "renderer.h"   // PenStyle

#include <string>

// Render doc to a PNG file at the given path.
// Output dimensions are derived from the document's physical size at dpi.
// pens[0..9] supplies the pen colours and thicknesses.
// layerStyles, when supplied, overrides the pen palette for strokes whose
// Stroke::layer indexes a layer with a solid-colour override enabled.
// Returns true on success.
bool exportPng(const HpglDoc &doc, const PenStyle pens[10],
               const std::string &path, float dpi = 600.f,
               const LayerStyle *layerStyles = nullptr, int layerCount = 0);
