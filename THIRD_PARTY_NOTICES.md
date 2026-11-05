# Third-Party Notices

The LoopPad MIT License applies to LoopPad's own source code. Third-party projects and components used to build or launch LoopPad remain subject to their own license terms.

## TinyALSA

LoopPad uses TinyALSA for audio I/O and mixer control.

Copyright 2011, The Android Open Source Project  
Copyright (c) 2019, The Linux Foundation.

The TinyALSA checkout used during LoopPad development includes the following redistribution notice:

> Redistribution and use in source and binary forms, with or without modification, are permitted provided that the copyright notice, conditions, and disclaimer are retained as required by the TinyALSA notice. Neither the name of The Android Open Source Project nor contributor names may be used to endorse or promote derived products without specific prior written permission.

For the complete terms and disclaimer, see the `NOTICE` file in the TinyALSA project used to build LoopPad.

Project: https://github.com/tinyalsa/tinyalsa

## mpc-ui-toolkit

LoopPad uses `mpc-ui-toolkit` source code at build time for the MPC Sample display, control surface, pads, LEDs, input handling, and application loop.

Project: https://github.com/garrettjwilke/mpc-ui-toolkit

**License status:** the local `mpc-ui-toolkit` checkout inspected during LoopPad development did not contain a top-level `LICENSE` file. Until the author or repository provides explicit redistribution/license terms, this notice does **not** assert permission to redistribute `mpc-ui-toolkit` source or binaries incorporating that code.

For that reason, LoopPad's source repository should continue to reference `mpc-ui-toolkit` as an external dependency rather than vendoring it. Public redistribution of precompiled LoopPad binaries that incorporate `mpc-ui-toolkit` should wait until redistribution permission is clarified.

## mpc-boot-thingy

`mpc-boot-thingy` is used externally to install, select, launch, and recover from LoopPad during development. Its source code is not included in LoopPad and is not linked into the LoopPad executable by the current build process.

Project: https://github.com/garrettjwilke/mpc-boot-thingy

The local checkout inspected during LoopPad development did not contain a top-level `LICENSE` file. Users should refer to that project's own repository for its terms.

## Akai Professional

LoopPad does not distribute Akai firmware or proprietary Akai application binaries. Akai Professional product names and trademarks belong to their respective owners. LoopPad is an unofficial community project and is not affiliated with or endorsed by Akai Professional.
