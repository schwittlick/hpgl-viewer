#pragma once

#include "hpgl_parser.h"
#include "renderer.h"   // PenStyle

#include <string>

// Render doc to a PNG file at the given path.
// Output dimensions are derived from the document's physical size at dpi.
// pens[0..9] supplies the pen colours and thicknesses.
// Returns true on success.
bool exportPng(const HpglDoc &doc, const PenStyle pens[10],
               const std::string &path, float dpi = 300.f);
