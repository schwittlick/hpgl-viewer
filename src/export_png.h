#pragma once

#include "hpgl_parser.h"
#include "renderer.h"   // PenStyle

#include <string>

// Render doc to a PNG file at the given path.
// widthPx sets the output width; height is derived from the doc aspect ratio.
// pens[0..7] supplies the pen colours and thicknesses.
// Returns true on success.
bool exportPng(const HpglDoc &doc, const PenStyle pens[8],
               const std::string &path, int widthPx = 2000);
