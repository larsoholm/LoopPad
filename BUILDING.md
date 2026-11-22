# Building LoopPad

LoopPad is cross-compiled on Windows for the Akai MPC Sample's AArch64 Linux environment.

## Prerequisites

- Git
- Zig 0.17
- Python 3
- TinyALSA source
- mpc-ui-toolkit source, including its Git submodules

Suggested local layout:

```text
C:\Users\<you>\akai\
├── looppad\
│   ├── assets\
│   │   └── looppad_splash.png
│   ├── src\
│   │   ├── looppad.c
│   │   └── version.h
│   ├── tools\
│   │   └── generate_splash.py
│   └── tinyalsa\
└── mpc-ui-toolkit\
```

## Get dependencies

Clone TinyALSA:

```powershell
git clone https://github.com/tinyalsa/tinyalsa.git
```

Clone mpc-ui-toolkit:

```powershell
git clone https://github.com/garrettjwilke/mpc-ui-toolkit.git

cd .\mpc-ui-toolkit
git submodule update --init --recursive
```

Place TinyALSA in the LoopPad working directory as:

```text
tinyalsa\
```

or adjust the paths in the build command.

## Tested mpc-ui-toolkit version

LoopPad v0.6.1-alpha has been tested with:

```text
mpc-ui-toolkit
0c162ff88940445c6c9977fd60d99fac9192eb7b
```

Other revisions may work, but this is the current known-good development baseline.

To check the currently checked-out toolkit revision:

```powershell
cd ..\mpc-ui-toolkit
git rev-parse HEAD
```

To build against the known-good revision:

```powershell
git checkout 0c162ff88940445c6c9977fd60d99fac9192eb7b
```

Return to the LoopPad repository:

```powershell
cd ..\looppad
```

## Generate the embedded splash screen

The master splash image is stored in:

```text
assets\looppad_splash.png
```

The image used by LoopPad is 320×240 pixels.

Before compiling, generate the embedded C header:

```powershell
py .\tools\generate_splash.py
```

This creates:

```text
src\looppad_splash_data.h
```

The generated header is intentionally excluded from Git because it can be regenerated from the PNG.

The application version itself is stored separately in:

```text
src\version.h
```

This allows the splash artwork to remain version-independent.

## Cross-compile

From the LoopPad repository root:

```powershell
$ui = "..\mpc-ui-toolkit"

zig cc -target aarch64-linux-musl -O2 -D_DEFAULT_SOURCE -Wall -Wextra -std=c11 `
    -Itinyalsa/include `
    "-I.\src" `
    "-I$ui\src" `
    "-I$ui\examples" `
    "-I$ui\deps\md4c\src" `
    "-I$ui\deps" `
    -static -s -o .\looppad `
    .\src\looppad.c `
    "$ui\src\app.c" `
    "$ui\src\screen.c" `
    "$ui\src\surface.c" `
    "$ui\src\ui.c" `
    "$ui\src\widgets.c" `
    "$ui\src\filebrowser.c" `
    "$ui\src\usb_sd.c" `
    "$ui\src\input.c" `
    "$ui\src\help.c" `
    "$ui\src\image.c" `
    "$ui\deps\md4c\src\md4c.c" `
    (Get-ChildItem .\tinyalsa\src\*.c).FullName `
    -lm -lpthread
`*`

The expected output is a static*AArch64 Linux executable:

```text*looppad
```

Warnings from third-p*rty headers such as `stb_image.h` *bout unused functions may appear.
*Treat actual compiler lines contai*ing:

```text
error:
```

as build*failures.

## MPC audio target cur*ently used

LoopPad's current test*d audio configuration is:

```text*ALSA card/device: 2,0
Sample rate:*     44100 Hz
Format:           S3*_LE
Channels:         2
Period:   *       128 frames
```

This config*ration was determined experimental*y on the development MPC Sample an* should not be assumed to describe*every possible firmware or hardwar* configuration.

## Installation f*r testing

The supported developme*t workflow currently uses MPC Boot*Thingy.

1. Connect the MPC Sample*to the computer over USB.
2. Open *he MPC Boot Thingy web interface:
*   `http://192.168.155.1:8080`

3. Keep **Stock** as the default application.
4. Select **Install App**.
5. Select the compiled `looppad` binary.
6. Leave **Args** blank.
7. Launch LoopPad manually with **Boot now**.

## Important: exit and recovery

LoopPad currently does not implement its own hardware exit or power-off function.

The MPC Sample physical power button is not currently handled directly by LoopPad.

Keep MPC Boot Thingy available for recovery and reboot while testing.

Do **not** configure LoopPad as the default boot application while this limitation remains.

## Current validation baseline

LoopPad v0.6.1-alpha has been hardware-tested with:

- 4-loop detailed mode
- 8-loop compact mode
- eight simultaneous loops
- mixed loop lengths
- BAR / BEAT / OFF quantization
- phase-locked stop/start
- per-loop levels
- waveform display
- hardware VU meters
- startup mute/fade
- embedded splash screen
- `XRUN 0` during the current eight-loop stress test

## Developer safety

Never commit or distribute:

- private SSH keys
- personalized `authorized_keys`
- personalized `autorun` folders
- generated host SSH private keys
- Akai firmware
- proprietary Akai binaries

LoopPad's own source is licensed separately from its third-party dependencies. See:

- `LICENSE`
- `THIRD_PARTY_NOTICES.md`