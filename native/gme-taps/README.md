# libgme changes

SPDX-License-Identifier: LGPL-2.1-or-later

Copyright (c) 2026 Bit Music Visualizer contributors.

All files in this directory, including patches.json, are under the GNU Lesser
General Public License, version 2.1 or (at your option) any later version.
See ../../LICENSES/LGPL-2.1.txt. Modified upstream files retain their original
copyright notices. These changes provide channel taps, timestamped NES oscillator
pitch/gate history and the YM2413 adapter. No Hoot source is used.

The POKEY adapter wraps the existing Sap_Apu core with four Blip_Buffer outputs.
All oscillators remain connected; application-side muting changes only the final
mix, preserving coupled dividers/high-pass state. Its small C API is exported in
shared builds as well as static development builds.
