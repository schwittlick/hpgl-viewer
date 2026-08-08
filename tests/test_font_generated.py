#!/usr/bin/env python3
"""Check that src/hpgl_font.cpp is what tools/gen_hpgl_font.py currently emits.

The generated table is checked in so building needs no Python, which means
nothing in the build re-runs the generator — editing tools/stick_font.json
without regenerating would silently leave the viewer drawing the old glyphs.
This test is what catches that.

Usage:
    tests/test_font_generated.py <generator> <font json> <generated cpp>
"""

import difflib
import pathlib
import subprocess
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)

    generator, font, generated = (pathlib.Path(a) for a in sys.argv[1:])

    result = subprocess.run([sys.executable, generator, font],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr.strip())
        sys.exit("FAIL  the generator rejected %s" % font)

    expected = result.stdout
    actual = generated.read_text(encoding="utf-8")

    if actual == expected:
        print("OK    %s is up to date with %s" % (generated.name, font.name))
        return

    diff = difflib.unified_diff(expected.splitlines(), actual.splitlines(),
                                fromfile="regenerated", tofile=str(generated),
                                lineterm="", n=1)
    print("\n".join(list(diff)[:40]))
    sys.exit("\nFAIL  %s is stale. Regenerate it with:\n"
             "        python3 %s > %s" % (generated.name, generator, generated))


if __name__ == "__main__":
    main()
