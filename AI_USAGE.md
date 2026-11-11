# AI usage

This plugin was built with generative AI.

- **Tool:** Claude Code, Anthropic's coding assistant, using Anthropic's Claude models (Opus).
- **Human role:** Davidian set the requirements and the design direction, chose the circuit and the project and decides what is released.
- **What the AI did:**
  - Worked out the circuit's component values and topology from reference material Davidian supplied and published descriptions of the same circuit.
  - Wrote the circuit model, the LV2, VST3 and Audio Unit plugin and GUI code and the panel layout. The stroke font, renderer and colour scheme are the Davidian house style, reused from the Bi-Lumen. The wrappers, build files and tests follow the Humble Overdraft project.
  - Wrote the build files for Windows, macOS and Linux, the CI workflow, the LV2 metadata, the tests and the documentation.
  - Proposed the name "Black Napkin" (from the project folder's name).
- **How it was checked:**
  - Automated tests in `test/`: the band-pass's centre frequency, peak gain and bandwidth against the small-signal formulas, the envelope and clock timing against their RC formulas, both modes and sweep directions, OTA limiting, bypass latency, silence, every sample rate and CPU cost, plus GUI rendering, GUI interaction checks on Windows and macOS, load checks on Linux and macOS, and the lilv LV2 validator. The VST3 and Audio Unit versions have their own host tests (`vst3_check`, `au_check_mac`), and CI runs Steinberg's VST3 validator and Apple's `auval` on them.
  - `build.bat test` (Windows) and `make test` (macOS, Linux) run them, and GitHub Actions runs them on every platform for each push.
  - Some details are estimates (listed in the README). No listening test in a DAW, and no comparison with a built pedal, has been recorded yet.
- **Training data and copyright:** no third-party code was knowingly copied. The only reproduced material is the LV2 interface declarations and Steinberg's VST3 C API header, credited in [NOTICE.md](NOTICE.md). The GUI artwork and font are generated from code, not from images.

The AI's output is released under the project licence, GPL-3.0-or-later (see `LICENSE`). Under the GPL, there is no warranty.
