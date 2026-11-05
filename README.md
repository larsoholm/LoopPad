# LoopPad

**LoopPad** is an experimental open-source fixed-length live looper for the **Akai MPC Sample**.

It runs directly on the MPC Sample and turns the hardware into a pad-driven live looping instrument using the built-in display, RGB pads, microphone/rear inputs, knobs, fader, and hardware level meters.

> **Status:** experimental development software. The current development line is **v0.6.0**, adding a selectable 4-loop / 8-loop layout. The 4-loop workflow is the more heavily tested baseline.

## What LoopPad does

LoopPad is designed around a simple workflow:

1. Configure loop slots in **SETUP** mode.
2. Choose a per-pad length from **1 to 8 bars**.
3. Switch to **LIVE** mode.
4. Arm a pad and record from the built-in microphone or rear input.
5. LoopPad records the configured length and immediately plays it back in sync.
6. Mix, stop, restart, clear, and combine independent loops from the MPC hardware.

## Current features

- Fixed-length synchronized live looping
- Detailed **4-loop** mode
- Experimental compact **8-loop** mode
- Independent **1-8 bar** length per loop pad
- **SETUP / LIVE** workflow
- Built-in microphone and rear-input routing
- **NEXT / 1BAR / NOW** recording count-in modes
- **BAR / BEAT / OFF** start/stop quantization
- Phase-locked loops that retain their master-clock position while stopped
- Per-loop playback level using the MPC fader
- Soft takeover / pickup for per-loop level changes
- Independent master volume and metronome level
- Metronome on/off
- Waveform display for recorded loops
- Moving waveform playhead
- Hardware RGB pad state feedback
- MPC side LEDs used as live L/R input meters
- Clear confirmation to reduce accidental loop deletion
- Startup mute/fade to avoid output transients

## Loop states

| State | Typical indication |
|---|---|
| Empty | Dim / selected cyan in SETUP |
| Armed / count-in | Flashing amber |
| Recording | Red |
| Playing | Green |
| Stopped | Blue |
| Queued start/stop | Blue/green variants |

## Controls

| MPC control | LoopPad function |
|---|---|
| Pads 1-4 / 1-8 | Loop slots |
| B1 | Global transport |
| B2 | Metronome on/off |
| B3 | Clear selected loop with confirmation; cancel arm when armed |
| K1 | Selected pad loop length in SETUP |
| Shift + K1 | Metronome volume |
| K2 | BPM |
| Shift + K2 | Quantize: BAR / BEAT / OFF |
| K3 | MIC / REAR input |
| Shift + K3 | Count-in: NEXT / 1BAR / NOW |
| Fader | Selected loop playback level |
| Sample Select | SETUP / LIVE mode |
| Pad Bank | v0.6: switch 4-loop / 8-loop layout in SETUP |

## 4-loop and 8-loop modes

LoopPad v0.6 uses an eight-clip engine underneath two views:

### 4 LOOP

- Pads 1-4 active
- Larger 2x2 clip panels
- Larger waveforms and more detailed feedback

### 8 LOOP

- Pads 1-8 active
- Compact 2x4 clip overview
- Pads 9-16 are currently reserved for future performance functions

Switching from 4 to 8 loops is allowed in SETUP. Switching from 8 back to 4 should only be allowed when Pads 5-8 are empty, avoiding hidden active loops.

## Audio platform

The current MPC Sample test hardware exposes the internal audio interface through ALSA card 2 / device 0.

LoopPad currently uses:

- 44.1 kHz
- Stereo
- Signed 32-bit little-endian PCM
- 128-frame audio periods
- TinyALSA for capture/playback and mixer control

Input routing discovered during development:

- **MIC:** Builtin Microphone Switch = 1, TRS Switch = 0
- **REAR:** Builtin Microphone Switch = 0, TRS Switch = 1

## Build dependencies

LoopPad is written in C and cross-compiled for the MPC Sample's AArch64 Linux environment.

Current development uses:

- [Zig](https://ziglang.org/) as the cross-compiler
- [TinyALSA](https://github.com/tinyalsa/tinyalsa) for audio I/O
- [mpc-ui-toolkit](https://github.com/garrettjwilke/mpc-ui-toolkit) for the MPC Sample display, pads, controls, LEDs, and input handling
- [mpc-boot-thingy](https://github.com/garrettjwilke/mpc-boot-thingy) to launch/install alternative MPC Sample applications

A reproducible public build guide will be added as the project is cleaned up for broader testing.

## Installation

### Recommended: MPC Boot Thingy

The currently supported way to install and launch LoopPad is **MPC Boot Thingy**. LoopPad is a standalone AArch64 application, but Boot Thingy currently provides the practical application-selection and recovery path needed during development.

Typical installation flow:

1. Install MPC Boot Thingy on the MPC Sample.
2. Open the MPC Boot Thingy web interface over USB.
3. Use **Install App** to install the LoopPad AArch64 binary.
4. Keep **Stock** as the default boot application while LoopPad is experimental.
5. Launch LoopPad manually from MPC Boot Thingy when you want to use it.

> **Important: no standalone exit / power-off yet**
>
> LoopPad currently has **no implemented hardware exit or power-off function of its own**. The MPC Sample physical power button is not currently handled by LoopPad. Without MPC Boot Thingy, there is currently no supported clean way to exit LoopPad, reboot the MPC, or return to the stock MPC application from within LoopPad.
>
> For this reason, **MPC Boot Thingy is currently required for the supported installation and recovery workflow**. Its web interface can be used to reboot the MPC and return to Stock. Do **not** configure LoopPad as the default boot application while this limitation remains.

A clean in-app exit/shutdown path is planned before a stable v1.0 release. A standalone SD-card/autorun installation may be considered later, once safe exit and recovery are implemented.

## Current development status

LoopPad is **alpha-quality community software**.

The current development focus is:

- validating eight simultaneous loop slots
- testing mixed loop lengths such as 1B / 2B / 4B / 6B / 8B
- monitoring XRUN behavior under heavier multi-loop playback
- refining the 4/8-loop user interface
- preserving reliable recovery to the stock MPC application

Keep **Stock** as the default application in MPC Boot Thingy while testing development builds.

## Roadmap

### High priority

- Validate and stabilize the 8-loop engine
- Overdub existing loops without consuming another pad
- Physical Undo support for the last overdub/action
- Save and load complete LoopPad sessions from SD card
- Solo and mute workflow for larger performances

### Performance controls

Pads 9-16 may later be used for performance functions such as:

- Stop All
- Play All
- Solo / Mute modes
- Input / metronome shortcuts
- Clear mode
- Overdub mode

### Future ideas

- MIDI clock synchronization
- Additional loop-boundary/crossfade controls
- Reverse playback
- Half-speed playback
- Session/project management
- Packaged Boot Thingy installer
- Reproducible builds and downloadable GitHub Releases

## Development documentation

Detailed development notes covering the current architecture, control mapping, audio configuration, version history, known limitations, and roadmap are maintained separately in the `docs/` directory.

## Safety and disclaimer

LoopPad is an unofficial community project. It is **not affiliated with, sponsored by, or endorsed by Akai Professional**.

LoopPad is experimental software that runs directly on the MPC Sample hardware. Before testing:

- Back up the SD card and important MPC projects.
- Keep the stock MPC application as the default boot option while developing/testing.
- Do not distribute private SSH keys or personalized autorun data.
- Use experimental builds at your own risk.

## Author

LoopPad was created by **Lars Öholm** ([github.com/larsoholm](https://github.com/larsoholm)).

Contributions are welcome through GitHub issues and pull requests.

## License

LoopPad's original source code is licensed under the **MIT License**.

Copyright (c) 2026 Lars Öholm.

See [LICENSE](LICENSE) for the full license terms.

LoopPad uses and depends on third-party software that is covered by separate license terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

### Binary releases

Precompiled LoopPad binaries are not currently provided as public releases.

LoopPad is statically built with third-party components, including `mpc-ui-toolkit`. The version of `mpc-ui-toolkit` currently used for LoopPad does not include an explicit license covering the toolkit's own source code.

Until redistribution rights for `mpc-ui-toolkit` are clarified, LoopPad will be distributed as source code only. Developers can obtain the required dependencies separately and build LoopPad by following [BUILDING.md](BUILDING.md).

TinyALSA is distributed under its own permissive license terms. Required third-party copyright notices and redistribution conditions are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Acknowledgements

LoopPad builds on community work around the MPC Sample, particularly:

- `mpc-ui-toolkit`
- `mpc-boot-thingy`
- TinyALSA

Thanks to the MPC Sample community for documenting and exploring the platform.
