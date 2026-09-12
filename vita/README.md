# Azahar Vita bring-up

This directory contains the Vita-specific bring-up code. It deliberately builds independently from
Azahar while the platform assumptions are tested.

The default CMake build is the milestone 1 common-library probe. The milestone 0 hardware probe is
kept as an optional regression target and in the standalone Makefile. The milestone 2 DynCom probe
is also selected explicitly so existing build commands remain stable.

## Milestone 0: hardware probe

The first VPK verifies:

- VitaSDK compiles `std::span` and the required C++20 language mode.
- The output is a 32-bit ARM EABI executable that can be packaged and launched.
- A 960x544 framebuffer can be allocated in 256 KiB-aligned CDRAM.
- Display, VBlank, and controller APIs work and report successful return values.
- Deterministic diagnostics can be written to `ux0:data/azahar-vita/boot.log`.

The success path displays six horizontal color bands until **START** is pressed. A detected failure
displays alternating magenta and black bands for five seconds, writes a `FAIL` record when possible,
and exits with a non-zero status.

## WSL2 toolchain setup

The validated host is Ubuntu 24.04 under WSL2 with the stable VitaSDK `2026.08` channel. Install the
SDK without root privileges under the WSL user's home directory:

```sh
sudo apt-get update
sudo apt-get install -y cmake make git python3 curl bzip2
git clone --depth 1 https://github.com/vitasdk/vdpm /tmp/vdpm
VITASDK="$HOME/vitasdk" VITASDK_CHANNEL=2026.08 /tmp/vdpm/bootstrap-vitasdk.sh

export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$PATH"
vdpm status
```

The `VITASDK` and `PATH` exports may be added to `~/.bashrc` after installation.

## Milestone 1: minimal common library

The second VPK links a reduced `citra_common` target and verifies:

- fixed-width types, bit fields, scalar math, and ARMv7 atomic operations;
- the Azahar logger writing synchronously to `ux0:data/azahar-vita/boot.log`;
- `ParamPackage` serialization and escaping;
- basic timing and filesystem round trips;
- Vita system-memory pools and initial heap use.

Only fmt in header-only mode and Boost headers are retained. Qt, renderers, audio, networking,
Crypto++, zstd, compiled Boost libraries, settings, and advanced debugging remain excluded.

## Build and inspect Hito 1

CMake is the canonical build. Keep the normal and diagnostic variants in different directories:

```sh
cmake -S vita -B build-vita/hito1-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita/hito1-cmake --parallel

cmake -S vita -B build-vita/hito1-failure -DCMAKE_BUILD_TYPE=Release \
    -DAZAHAR_VITA_COMMON_FORCE_FAILURE=ON
cmake --build build-vita/hito1-failure --parallel
```

Validate the canonical artifact with:

```sh
vita/scripts/validate.sh build-vita/hito1-cmake
```

The validator requires `VITASDK` and checks the ELF architecture, VPK contents, SFO metadata, map
file, excluded symbols, executable size, and checksum. The ELF must be ARM ELF32 with the hard-float
EABI. The VPK uses title ID `AZHV00002`, version `00.01`, and title
`Azahar Vita Common Probe`.

## Rebuild the Hito 0 regression probe

The original CMake probe remains available explicitly:

```sh
cmake -S vita -B build-vita/hito0-cmake -DCMAKE_BUILD_TYPE=Release \
    -DAZAHAR_VITA_BUILD_HARDWARE_PROBE=ON
cmake --build build-vita/hito0-cmake --parallel

cmake -S vita -B build-vita/hito0-failure -DCMAKE_BUILD_TYPE=Release \
    -DAZAHAR_VITA_BUILD_HARDWARE_PROBE=ON \
    -DAZAHAR_VITA_PROBE_FORCE_FAILURE=ON
cmake --build build-vita/hito0-failure --parallel
```

The self-contained Makefile also remains available and always recompiles the small probe so that
changing `FORCE_FAILURE` cannot reuse a stale object:

```sh
make -f vita/Makefile BUILD_DIR="$PWD/build-vita/make"
make -f vita/Makefile BUILD_DIR="$PWD/build-vita/make-failure" FORCE_FAILURE=1
make -f vita/Makefile BUILD_DIR="$PWD/build-vita/make" size
```

## Hito 0 physical validation

Use a homebrew-enabled Vita with VitaShell and USB access to `ux0`:

1. Copy `build-vita/hito0-failure/azahar_vita_probe.vpk` to the mounted Vita and install it in
   VitaShell.
2. Launch it and verify alternating magenta/black bands followed by automatic exit after five
   seconds. Its log must contain `FAIL forced_failure code=0xA0000001` and `RESULT FAIL`.
3. Install `build-vita/hito0-cmake/azahar_vita_probe.vpk` over the diagnostic version and launch it.
4. Verify six complete horizontal bands, press **START**, and confirm a clean return to LiveArea.
5. Reconnect VitaShell USB and copy `ux0:data/azahar-vita/boot.log` into
   `build-vita/evidence/boot.log` on the host.

The final log is truncated on every launch and must have this shape, with no `FAIL` records:

```text
probe_version=00.02
PASS log path=ux0:data/azahar-vita/boot.log
PASS cxx20 standard=202002 pointer_bits=32 pattern_checksum=0x486CC51D
PASS cdram bytes=2097152 alignment=262144
PASS display width=960 height=544 pitch=960
PASS controller mode=analog previous_mode=...
PASS controller input=start
PASS exit reason=start
PASS cleanup framebuffer=released
RESULT PASS
```

Milestone 0 is complete only after the normal VPK passes this physical test and its VPK checksum,
`vdpm status` output, ELF header, and recovered boot log are retained under `build-vita/evidence`.

This validation was completed on 2026-09-11; the retained evidence is the canonical record.

## Hito 1 physical validation

1. Install `build-vita/hito1-failure/azahar_vita_common_probe.vpk` and launch it. It must show
   alternating magenta/black bands, log `FAIL forced_failure code=0xA1000001` and `RESULT FAIL`, and
   exit automatically after five seconds.
2. Install `build-vita/hito1-cmake/azahar_vita_common_probe.vpk` and launch it. It must show six
   complete color bands and remain responsive until **START** is pressed.
3. Launch the normal probe at least three times to detect stale files, unreleased framebuffers, or
   logger shutdown failures.
4. Recover `ux0:data/azahar-vita/boot.log`. It must contain `PASS` records for logger, common types,
   serialization, timer, filesystem, memory, controller, and cleanup, followed by `RESULT PASS` and
   no `FAIL` record.
5. Retain the normal and forced logs plus SDK version, commit, ELF size, VPK hashes, and validator
   output under `build-vita/evidence/hito-1`.

This validation was completed on 2026-09-11. The forced-failure path produced the expected
`0xA1000001` result, and the normal probe passed three consecutive launches with every common-library
check recorded as `PASS`. The retained logs and build record are the canonical evidence.

## Hito 2: ARM11 DynCom interpreter

The third VPK executes the same deterministic DynCom corpus as the native desktop reference. It
covers ARM arithmetic, logic, flags, branches, calls and memory operations; Thumb arithmetic,
branches, memory and isolated SVC dispatch; register state; and a shared-memory handoff between two
emulated cores. The probe uses a 2 MiB translation cache instead of DynCom's desktop-sized 128 MiB
default.

Build and run the desktop reference first:

```sh
cmake -S vita/tests -B build-vita/hito2-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita/hito2-host --parallel
build-vita/hito2-host/azahar_dyncom_host_probe
```

Build normal and diagnostic Vita variants in separate directories:

```sh
cmake -S vita -B build-vita/hito2-cmake -DCMAKE_BUILD_TYPE=Release \
    -DAZAHAR_VITA_BUILD_DYNCOM_PROBE=ON
cmake --build build-vita/hito2-cmake --parallel

cmake -S vita -B build-vita/hito2-failure -DCMAKE_BUILD_TYPE=Release \
    -DAZAHAR_VITA_BUILD_DYNCOM_PROBE=ON \
    -DAZAHAR_VITA_DYNCOM_FORCE_FAILURE=ON
cmake --build build-vita/hito2-failure --parallel

vita/scripts/validate-hito2.sh build-vita/hito2-cmake \
    build-vita/hito2-host/azahar_dyncom_host_probe
```

The VPK uses title ID `AZHV00003`, version `00.01`, and title
`Azahar Vita DynCom Probe`.

## Hito 2 physical validation

1. Install `build-vita/hito2-failure/azahar_vita_dyncom_probe.vpk`. It must show alternating
   magenta/black bands, log `FAIL forced_failure code=0xA2000001` and `RESULT FAIL`, then exit after
   five seconds.
2. Install `build-vita/hito2-cmake/azahar_vita_dyncom_probe.vpk`. It must show six color bands and
   remain responsive until **START** is pressed.
3. Launch the normal probe three times. Every run must contain seven `PASS group=` records with the
   same signatures as the desktop reference, `PASS memory bounds=clean`, a non-zero
   `instructions_per_second`, and `RESULT PASS instructions=34` without any `FAIL` record.
4. Recover `ux0:data/azahar-vita/boot.log` and retain it with the forced log, desktop output, SDK
   version, commit, sizes, hashes, and validator output under `build-vita/evidence/hito-2`.

This validation was completed on 2026-09-12. The forced path produced `0xA2000001`, and the normal
probe passed three consecutive launches with all seven desktop-reference signatures and no invalid
memory access. The retained run reported stable user memory and 1,469,795 interpreted instructions
per second before a clean exit through **START**.

## Hito 3: loader, memory and kernel HLE

The fourth VPK links the real `Memory::MemorySystem`, `Kernel::KernelSystem` and
`Loader::Load3DSXImage`, driven by a `Core::ARM_DynCom` built directly over a `Core::
DynComEnvironment` (no `Core::System`). It loads a synthetic 3DSX homebrew generated entirely in
code, reaches the process's real entry point through `Kernel::ThreadManager::Reschedule()`, and
exercises a small hand-written SVC table (`ConnectToPort`, `SendSyncRequest`, `OutputDebugString`,
`ExitProcess`) including one IPC round trip against a `probe:test` service. FCRAM is sized for an
Old 3DS (128 MiB) rather than New 3DS (256 MiB) to fit the platform's memory budget.

Build and run the desktop reference first:

```sh
cmake -S vita/tests -B build-vita/hito3-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita/hito3-host --parallel
build-vita/hito3-host/azahar_system_host_probe
```

Build normal and diagnostic Vita variants in separate directories:

```sh
cmake -S vita -B build-vita/hito3-cmake -DCMAKE_BUILD_TYPE=Release \
    -DAZAHAR_VITA_BUILD_SYSTEM_PROBE=ON
cmake --build build-vita/hito3-cmake --parallel

cmake -S vita -B build-vita/hito3-failure -DCMAKE_BUILD_TYPE=Release \
    -DAZAHAR_VITA_BUILD_SYSTEM_PROBE=ON \
    -DAZAHAR_VITA_SYSTEM_FORCE_FAILURE=ON
cmake --build build-vita/hito3-failure --parallel

vita/scripts/validate-hito3.sh build-vita/hito3-cmake \
    build-vita/hito3-host/azahar_system_host_probe
```

The VPK uses title ID `AZHV00004`, version `00.01`, and title
`Azahar Vita System Probe`.

## Hito 3 physical validation

1. Install `build-vita/hito3-failure/azahar_vita_system_probe.vpk`. It must show alternating
   magenta/black bands, log `FAIL forced_failure code=0xA3000001` and `RESULT FAIL`, then exit
   after five seconds.
2. Install `build-vita/hito3-cmake/azahar_vita_system_probe.vpk`. It must show six color bands and
   remain responsive until **START** is pressed.
3. Launch the normal probe three times. Every run must contain five `PASS group=` records
   (`loader_identify`, `memory_and_process`, `entry_point`, `svc_and_service_ipc`, `diagnostics`)
   with the same signatures as the desktop reference, and `RESULT PASS groups=5` without any `FAIL`
   record.
4. Recover `ux0:data/azahar-vita/boot.log` and retain it with the forced log, desktop output, SDK
   version, commit, sizes, hashes, and validator output under `build-vita/evidence/hito-3/`.

The first physical attempt crashed natively on all three launches (confirmed by three
`psp2core-*.psp2dmp` dumps, not a controlled failure): `boot.log` stopped right after the logger
check. VitaSDK's newlib heap defaults to 128 MiB, and `Memory::MemorySystem` alone requests
~138.5 MiB via plain heap allocations (128 MiB FCRAM, 6 MiB VRAM, 4 MiB N3DS extra RAM, 0.5 MiB DSP
RAM), plus a ~5 MiB per-process page table on top - short by roughly 15 MiB. The fix was to define
`_newlib_heap_size_user = 192 MiB` in `system_main.cpp`, VitaSDK's documented mechanism for exactly
this (`share/gcc-arm-vita-eabi/samples/newlib_heapsize_ctrl`), not the "system mode app" budget
`vita_create_self`'s `MEMSIZE` option exposes, which is a different, unrelated knob. The rebuilt VPK
then passed three consecutive launches.

This validation was completed on 2026-09-12; the retained evidence is the canonical record.

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
