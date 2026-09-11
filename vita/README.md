# Azahar Vita bring-up

This directory contains the Vita-specific bring-up code. It deliberately builds independently from
Azahar while the platform assumptions are tested.

## Milestone 0: hardware probe

The first VPK verifies:

- VitaSDK can compile the required C++20 subset.
- A 32-bit ARM executable can be packaged and launched.
- A 960x544 framebuffer can be allocated in CDRAM.
- Display and controller APIs work.
- Runtime diagnostics can be written to `ux0:data/azahar-vita/boot.log`.

It displays six horizontal color bands. Press **START** to exit.

## Build

Install VitaSDK, then run:

```sh
export VITASDK=/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S vita -B build-vita -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita --parallel
```

If CMake is unavailable, the same probe has a self-contained Make build:

```sh
export VITASDK=/path/to/vitasdk
make -f vita/Makefile
make -f vita/Makefile size
```

The output is `build-vita/azahar_vita_probe.vpk`.

## Porting order

1. Compile `citra_common` without networking, desktop dynamic-library loading, or platform-specific
   CPU detection.
2. Compile the ARM DynCom interpreter and a small instruction test. Vita is ARMv7/32-bit, so
   Azahar's Dynarmic backend is not selected by the existing build.
3. Add the 3DS loader and memory system without video or audio.
4. Bring up the software renderer for correctness.
5. Add a VitaGL renderer for usable performance.
6. Add audio, input mapping, configuration, and a launcher UI.

Do not treat a successful VPK build as game compatibility. Each milestone must run on physical Vita
hardware and leave a boot log.
