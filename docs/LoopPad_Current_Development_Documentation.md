LoopPad

Akai MPC Sample Fixed-Length Live Looper

Current development documentation - v0.6.0 development line

Known-good base before v0.6: v0.5.3.  v0.6.0 adds selectable 4/8-loop mode and is currently under validation.

## 1. Current status

Project intent: a Loopy-style hardware live looper that runs completely on the MPC Sample, using the MPC display, pads, knobs, fader, built-in microphone/rear inputs and hardware level LEDs.

## 2. Proven hardware and software architecture

## 3. Current feature set

## 4. Controls and UI

### 4.1 Loop state colors

### 4.2 v0.6 layout behavior

- 4 LOOP mode: detailed 2x2 waveform view for Pads 1-4.
- 8 LOOP mode: compact 2x4 loop view for Pads 1-8; Pads 9-16 remain unused/off initially.
- PAD BANK toggles 4/8 only in SETUP. 4 -> 8 is always allowed.
- 8 -> 4 is blocked until Pads 5-8 are completely empty, preventing hidden active loops.
- The underlying engine has eight clip buffers; layout changes the active/control view, not the audio architecture.
## 5. Typical workflow

## 6. Development milestones

## 7. Known limitations / development notes

- The control-surface toolkit receives Main Volume, K1-K3 and fader as MIDI CC events; it does not currently query untouched physical positions at application startup.
- The Main Volume value is therefore established after the physical main-volume control sends CC19. The startup audio path is muted/faded for safety.
- Only one clip is armed/recording at a time in the current architecture; existing loops continue playing while another clip records.
- Current per-pad length range is 1-8 bars. Changing length is a SETUP action for an EMPTY slot; it does not reinterpret existing audio.
- v0.6 4/8 mode is newer than the well-tested 4-loop baseline and should be stress-tested with eight mixed-length simultaneous loops while watching XRUN.
## 8. Coming features / roadmap

## 9. Developer quick reference

Working tree: C:\Users\laoh\akai\looppad
UI toolkit:   C:\Users\laoh\akai\mpc-ui-toolkit
Target:       aarch64-linux-musl
Audio:        hw:2,0 / 44100 / S32_LE / stereo / period 128
Launcher:     MPC Boot Thingy
MPC USB IP:   192.168.155.1

Build pattern: zig cc -target aarch64-linux-musl -O2 ... -static -s -o looppad-<version> ... -lm -lpthread

Distribution note: Do not redistribute Akai firmware or private SSH material. Before publishing LoopPad, verify licenses/notices for TinyALSA, mpc-ui-toolkit and any incorporated third-party code.

## Reference tables

### Table 1

| Area | Status | Notes |
| --- | --- | --- |
| Platform | Working | Standalone app on MPC Sample through MPC Boot Thingy. |
| Audio | Working | Internal I2S hw:2,0; 44.1 kHz; stereo; S32_LE; 128-frame periods. |
| 4-loop engine | Validated | Independent fixed-length loops, phase-locked playback, per-loop levels and waveforms. |
| 4/8 mode | In validation | v0.6.0 uses an 8-clip engine with selectable detailed 4-loop and compact 8-loop views. |
| Safety baseline | Working | Stock remains default boot; LoopPad has clean startup mute/fade and clear confirmation. |

### Table 2

| Component | Current implementation |
| --- | --- |
| Target | Akai MPC Sample AC50/AZ07 development target used in testing. |
| Launcher | MPC Boot Thingy; Stock remains Default during development. |
| Language/toolchain | C; Zig cross-compiles static aarch64-linux-musl binaries on Windows. |
| Audio API | TinyALSA linked statically into LoopPad. |
| Internal PCM | ALSA card 2 / device 0 (I2S / AK4558 path). |
| PCM format | 44,100 Hz; S32_LE; stereo; period 128 frames. |
| Control/UI | mpc-ui-toolkit: 320x240 screen, RGB pads, B1-B3, K1-K3, fader, main volume, encoder, hardware meters. |
| Input routing | MIC: Builtin Microphone Switch=1, TRS Switch=0. REAR reverses those values. |

### Table 3

| Feature | Behavior |
| --- | --- |
| SETUP / LIVE | SAMPLE SELECT toggles configuration and performance modes. SETUP prevents accidental recording while choosing pad parameters. |
| Per-pad length | Each loop stores its own 1-8 bar length. K1 edits the selected EMPTY pad in SETUP. |
| Count-in | SHIFT+K3: NEXT, 1BAR, NOW. NEXT records at next bar; 1BAR adds a full preparation bar; NOW records immediately. |
| Playback quantize | SHIFT+K2: BAR, BEAT, OFF. Recorded loops stay phase-locked to the permanent master timeline. |
| Per-loop mix | Physical fader controls selected loop with soft takeover/pickup. Pad levels survive stop/restart and audio clear. |
| Master/click | Main Volume is software master after CC19 is received. SHIFT+K1 controls dedicated click level. B2 toggles click. |
| Waveforms | Compact waveform summaries with moving playhead; visual-only waveform gain makes normal mic audio readable. |
| Input/VU | K3 selects MIC/REAR. MPC side LEDs act as live L/R input level meters. |
| Clear safety | B3 once requests clear; B3 again within about 2 s confirms. ARMED+B3 cancels arm immediately. |
| Startup safety | Playback is primed with silence; ~50 ms mute and ~100 ms fade-in avoid startup transient. |

### Table 4

| Physical control | Normal function | SHIFT/secondary |
| --- | --- | --- |
| Pads 1-4 / 1-8 | Select/configure in SETUP; arm/cancel/start/stop in LIVE | State shown by RGB LEDs. |
| B1 | Global transport | - |
| B2 | Metronome on/off | - |
| B3 | Clear selected pad with confirmation | ARMED: cancel arm. |
| K1 | Selected pad length in SETUP | Metronome volume. |
| K2 | BPM | Quantize: BAR / BEAT / OFF. |
| K3 | MIC / REAR | Count-in: NEXT / 1BAR / NOW. |
| Fader | Selected pad level with soft takeover | - |
| SAMPLE SELECT | SETUP <-> LIVE | - |
| PAD BANK | v0.6: 4 <-> 8 loops in SETUP | Blocked in LIVE; 8->4 blocked if P5-P8 contain loops. |

### Table 5

| State | Pad/UI color | Meaning |
| --- | --- | --- |
| EMPTY | Dim gray / cyan when selected in SETUP | No recorded audio. |
| ARMED / count-in | Flashing amber | Waiting for scheduled recording start; countdown shown. |
| REC | Solid red | Recording fixed-length audio. |
| PLAY | Green | Loop audible and phase-locked. |
| STOP | Blue | Loop muted but its master-clock phase continues advancing. |
| Queued start/stop | Blue/green variants | Action waiting for BAR/BEAT quantize boundary. |

### Table 6

| Step | Example |
| --- | --- |
| 1. Configure | Enter SETUP. Select P1 and choose 1B; P2 6B; P3 2B; P4 4B using K1. |
| 2. Choose input | K3 = MIC for built-in microphone or REAR for rear input. |
| 3. Choose timing | SHIFT+K3 selects count-in. SHIFT+K2 selects stop/start quantization. |
| 4. Perform | SAMPLE SELECT -> LIVE. Tap an empty loop pad to arm it. Amber countdown -> red REC -> green PLAY. |
| 5. Mix | Select a pad and move the fader. PICKUP protects its stored level until the physical fader crosses it. |
| 6. Arrange | Tap running/stopped loops; BAR/BEAT/OFF determines when the audible state changes. |
| 7. Clear safely | Select the pad, B3 once -> confirmation, B3 again -> audio cleared while length/level remain. |

### Table 7

| Version | Milestone |
| --- | --- |
| v0.1 | Static AArch64 TinyALSA prototype; discovered real MPC audio format/hardware ownership. |
| v0.2.x | MPC display/pads; fixed scheduled record timing; continuous UI wake; enabled built-in microphone route. |
| v0.3.x | MIC/REAR switching, input meters, MPC hardware VU LEDs. |
| v0.4.x | Four independent loops, mixer/fader pickup, quantized stop/start, permanent phase-lock, waveforms, per-pad lengths. |
| v0.5.x | Count-in modes, SETUP/LIVE workflow, clean startup mute/fade, armed countdown, clear confirmation. |
| v0.6.0 | Eight-clip engine plus selectable 4-loop detailed and 8-loop compact layouts; current development/validation target. |

### Table 8

| Priority | Feature | Proposed direction |
| --- | --- | --- |
| High | 8-loop validation | Stress-test P1-P8 with mixed 1/2/4/6/8-bar loops, switching, clear protection and XRUN=0 target. |
| High | Overdub | Layer new audio onto an existing loop while the old loop plays. Reserve Pad 16 as possible Overdub mode control. |
| High | Undo / Redo | Use the MPC physical UNDO button for the most recent overdub/action; maintain a practical undo buffer. |
| High | Session Save / Load | Persist loop audio plus BPM, pad lengths, levels, input, count-in and quantize settings to SD. |
| Medium | Performance controls | Use Pads 9-16 for Stop All, Play All, Solo/Mute modes, input/click/clear/overdub feedback as appropriate. |
| Medium | Solo / Mute | Fast arrangement of eight loops without clearing or stopping their phase timeline. |
| Medium | MIDI clock sync | Synchronize master timing with external synths, drum machines or another clock source. |
| Medium | Loop-boundary polish | Tune fades/crossfades and clipping/headroom rules for dense multi-loop mixes. |
| Later | Creative playback | Reverse, half-speed and other loop transformations after core live performance workflow is stable. |
| Release | GitHub packaging | Clean repository structure, licenses, README/install guide, Boot Thingy package and downloadable AArch64 release. |
