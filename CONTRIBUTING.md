# Contributing to LoopPad

Thanks for helping improve LoopPad.

LoopPad is experimental software for the Akai MPC Sample. Changes should prioritize predictable live-performance behavior, recovery, and preserving the known-good audio path.

## Before contributing

- Test changes with **Stock kept as the default boot application**.
- Keep MPC Boot Thingy available for recovery/reboot.
- Back up important SD-card data and MPC projects.
- Do not include Akai firmware, proprietary Akai binaries, private SSH keys, or personalized autorun data in issues, pull requests, or commits.

## Development principles

1. **Keep loops phase-locked.** Stopping a loop should mute it without resetting its permanent master-clock phase unless a feature explicitly requires otherwise.
2. **Protect user recordings.** Destructive actions should be deliberate and clearly communicated.
3. **Prefer hardware-native UX.** Use MPC pads, RGB feedback, buttons, knobs, fader, display, and meters in ways that match their physical locations.
4. **Preserve rollback paths.** New features should not remove the ability to return to Stock through the supported development workflow.
5. **Separate UI changes from audio-engine changes where practical.** This makes regressions easier to isolate.

## Building

See [BUILDING.md](BUILDING.md).

## Pull requests

Please include:

- what changed
- which LoopPad version/source the change was based on
- MPC/firmware environment used for testing
- exact reproduction/test steps
- whether XRUN remained at zero during the test
- screenshots/photos for UI changes where useful

## Issues

Useful bug reports include:

- LoopPad version
- 4-loop or 8-loop layout
- loop lengths in use
- quantize mode: BAR / BEAT / OFF
- count-in mode: NEXT / 1BAR / NOW
- MIC or REAR input
- exact steps that reproduce the problem
- whether the problem also happens after a clean reboot

