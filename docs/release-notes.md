Ready-made builds of the **Davidian Black Napkin**, a circuit-modelled OTA filter pedal: a resonant band-pass swept by an envelope follower (attack, decay, range, up or down) or by a clocked sample and hold of transistor noise, with its GUI. LV2 on every platform, VST3 on Windows and Audio Unit on macOS.

| Download | Contains | For |
|---|---|---|
| `…-windows-x64.zip` | LV2, VST3 | Windows 10/11, 64-bit hosts |
| `…-macos-universal.zip` | LV2, Audio Unit | macOS 10.13 or later, Apple silicon and Intel |
| `…-linux-x64.zip` | LV2 | 64-bit Intel/AMD Linux (glibc 2.35 or later) |
| `…-linux-arm64.zip` | LV2 | 64-bit ARM Linux, e.g. Raspberry Pi 4/5 (glibc 2.35 or later) |

**Install:** unzip, copy the plugin folder for your host's format into its plugin folder, and restart your host or rescan:

- VST3 (Windows): `black_napkin.vst3` into `C:\Program Files\Common Files\VST3`
- Audio Unit (macOS): `black_napkin.component` into `~/Library/Audio/Plug-Ins/Components`
- LV2: `black_napkin.lv2` into `%APPDATA%\LV2` (Windows), `~/Library/Audio/Plug-Ins/LV2` (macOS) or `~/.lv2` (Linux)

**macOS:** the plugins aren't notarized, so macOS may block files downloaded from the internet. After copying, run these once in Terminal:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/black_napkin.component
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/black_napkin.lv2
```

If Logic Pro or GarageBand doesn't list the Audio Unit, log out and back in, or restart the Mac.

**Checks:** every build passed the DSP tests, the GUI tests and a load test on its platform in CI. The VST3 passed Steinberg's validator and the Audio Unit passed Apple's `auval -strict`. No listening test in a DAW has been recorded yet.

See the README for the controls, what is modelled and which values are estimates. GPL-3.0-or-later. Made with generative AI (see AI_USAGE.md).
