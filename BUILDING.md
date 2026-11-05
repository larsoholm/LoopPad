# Building LoopPad

LoopPad is cross-compiled on Windows for the Akai MPC Sample's AArch64 Linux environment.

## Prerequisites

- Git
- Zig (the current development setup uses Zig 0.17)
- TinyALSA source
- mpc-ui-toolkit source, including its git submodules

Suggested local layout:

```text
C:\Users\<you>\akai\
├── looppad\
│   ├── src\looppad.c
│   └── tinyalsa\
└── mpc-ui-toolkit\
```

## Get dependencies

```powershell
git clone https://github.com/tinyalsa/tinyalsa.git

git clone https://github.com/garrettjwilke/mpc-ui-toolkit.git
cd .\mpc-ui-toolkit
git submodule update --init --recursive
```

Place TinyALSA in the LoopPad working directory as `tinyalsa`, or adjust the paths in the build command.

## Cross-compile

From the LoopPad working directory:

```powershell
$ui = "..\mpc-ui-toolkit"

zig cc -target aarch64-linux-musl -O2 -D_DEFAULT_SOURCE -Wall -Wextra -std=c11 `
    -Itinyalsa/include `
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
```

The expected output is a static AArch64 Linux executable named `looppad`.

Warnings from third-party headers such as `stb_image.h` about unused functions may appear. Treat actual compiler `error:` messages as build failures.

## MPC audio target currently used

LoopPad's current tested audio configuration is:

```text
ALSA card/device: 2,0
Sample rate:      44100 Hz
Format:           S32_LE
Channels:         2
Period:           128 frames
```

This was determined experimentally on the development MPC Sample and should not be assumed to describe every possible firmware/hardware configuration.

## Installation for testing

The supported development workflow currently uses MPC Boot Thingy:

1. Keep Stock as the default application.
2. Open the MPC Boot Thingy web interface over USB.
3. Use **Install App** and select the compiled `looppad` binary.
4. Leave Args blank.
5. Launch LoopPad manually with **Boot now**.

LoopPad currently does not implement its own hardware exit/power-off path. Keep MPC Boot Thingy available for recovery/reboot while testing.

## Developer safety

Never commit or distribute:

- private SSH keys
- personalized `authorized_keys`
- personalized `autorun` folders
- Akai firmware or proprietary Akai binaries
- generated host SSH private keys

