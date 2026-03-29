#!/usr/bin/env bash
set -e

BUILD_DIR="build"

if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    meson setup "$BUILD_DIR"
fi

meson compile -C "$BUILD_DIR"
sudo meson install -C "$BUILD_DIR"
