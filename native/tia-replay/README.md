# TIATracker replay

`replay.*` adapts the project structures and sequencing rules from TIATracker
1.3, copyright 2016–2017 Andre "Kylearan" Wichmann, under GPL-2.0-or-later.
Changes by Bit Music Visualizer contributors, 2026-10-17: strict bounded JSON
validation, offline register sequencing and full sequencer-state loop detection;
no editor, SDL, device access, Qt signals or wall-clock timers.

The unchanged `emulation/TIASnd.cpp`, `TIASnd.h` and `bspf.h` from that release
provide the Stella audio core, copyright Bradford W. Mott, Stephen Anthony and
the Stella Team. Both voices advance together. Its split-output mode exposes
the actual voice samples; their sum equals its mono output. The original MIT
`ttttrack.*` adapter integrates these samples onto the host clock, removes DC
per voice, and applies mutes after synthesis. Muted voices keep evolving.

Upstream: <https://bitbucket.org/kylearan/tiatracker>.
The complete 1.3 source mirror and SHA-256 are pinned in `cmake/Tia.cmake` and
`packaging/sources.json`. The source archive accompanies distribution packages.
See `LICENSES/TIATracker-GPL-2.0.txt` and `TIATracker-NOTICES.txt` (Debian's
copyright record, including its GPL-2+ classification). Derived replay code
retains these terms; the top-level MIT license does not relicense it.

The combined application remains GPLv3. Rebuilding replaces this static engine;
there is no additional runtime DLL or ROM requirement.
