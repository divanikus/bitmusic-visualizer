# Contributor instructions

This repository owns the native Bit Music Visualizer application.

- Read README.md, docs/BUILDING.md and THIRD-PARTY.md before implementation work.
- Inspect the working tree and preserve unrelated changes. Keep increments small.
- UI and durable documentation are English. Preserve original music metadata.
- Keep real-time audio and scope computation independent of GUI painting.
- Use existing emulation libraries; playback support does not imply independent
  channel waveforms. Document approximations and verify actual signals.
- Never modify source music. Never commit music, binaries, settings, logs,
  credentials, private paths or collection inventories. Use fixtures.py for tests.
- Run native checks appropriate to the change. Update relevant user/build docs.
- Keep release notes short and user-facing: changes, where to find them and downloads.
  Explicitly name what was added or changed and briefly explain its visible effect.
  Leave implementation details and test-coverage reports in development documentation.
- Application code is MIT; native/gme-taps is LGPL-2.1-or-later. Preserve notices.
- native/ymfm-voices is derived from ymfm under BSD-3-Clause; retain its notice
  and sources alongside the pinned upstream core when packaging.
- native/munt-taps and patched libmt32emu are LGPL-2.1-or-later. Keep their
  notices and complete pinned sources in distribution packages.
- native/sid-taps and patched libsidplayfp are GPL-2.0-or-later; SID-enabled
  combined binaries are GPLv3. Retain COPYING and the full corresponding source
  bundle, including application sources and build scripts, when packaging.
- Commit task-owned changes at a meaningful handoff. Do not push or publish unless
  the user has authorized it. Do not create a release from untested artifacts.
