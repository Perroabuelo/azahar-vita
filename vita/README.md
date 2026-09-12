# Azahar Vita bring-up

This directory contains the Vita-specific bring-up code. It deliberately builds independently from
Azahar while the platform assumptions are tested.

The default CMake build is the milestone 1 common-library probe. The milestone 0 hardware probe is
kept as an optional regression target and in the standalone Makefile.

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

Milestone 1 remains in physical validation until this evidence is committed.

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
