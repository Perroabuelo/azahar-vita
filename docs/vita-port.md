# PS Vita port feasibility notes

## Current architecture decision

The initial target is a standalone VPK. Azahar's existing Qt frontend is not portable to Vita, and
using its libretro frontend immediately would hide platform failures behind RetroArch.

| Azahar subsystem | Initial Vita choice | Reason |
| --- | --- | --- |
| CPU | ARM DynCom interpreter | Existing portable fallback for 32-bit hosts |
| GPU | Software renderer, then VitaGL | Establish correctness before shader translation |
| Frontend | Small native Vita shell | Qt is unavailable and too large |
| Audio | Disabled, then native Vita backend | Avoid blocking loader/CPU bring-up |
| Networking | Disabled | Not required to boot local software |
| Filesystem | `ux0:data/azahar-vita` adapter | Gives deterministic writable paths and logs |

## Known hard constraints

- Vita applications are 32-bit ARMv7 while current Azahar's fast Dynarmic path is only compiled for
  x86-64 and ARM64. The portable interpreter should compile but will be much slower.
- The Vita has 512 MiB system RAM and 128 MiB VRAM shared under tighter per-process limits than a
  desktop build. Custom textures, shader caches, telemetry, multiplayer, scripting, debugging, and
  game dumping must remain disabled.
- Vita has no native desktop OpenGL or Vulkan implementation. The existing accelerated renderers
  cannot be linked unchanged.
- Azahar's dependency graph must be reduced; cross-compiling every desktop dependency is neither a
  useful nor realistic first milestone.

## Current milestone

Milestone 1 is complete when the reduced `citra_common` target builds for Vita, its native probe
passes the deterministic utility, logger, timer, filesystem, serialization, and memory checks on
physical hardware, and the resulting ELF size and initial memory use are retained as evidence.

Milestone 2 is implemented as an isolated DynCom probe. The same deterministic ARM and Thumb corpus
runs on desktop and Vita, uses bounds-checked flat memory, dispatches SVC through a probe callback,
checks shared memory between two emulated cores, and records a first instructions-per-second
measurement. Loader and kernel HLE integration remain part of milestone 3.

The implementation is not considered complete until the normal and forced-failure VPKs are
validated on physical Vita hardware and their logs are retained as evidence.

## Validation status

Milestone 0 was completed on physical Vita hardware on 2026-09-11. Both the forced-failure path and
the normal six-band probe were verified with VitaSDK 2026.08; the normal run recorded every expected
check as `PASS` and exited through the START input. The validated VPK SHA-256 is
`fd26bd100970fbaf0fc9ab3d565b7caa4a3096bc993d66576fc1a573eb9d1e6b`.

Milestone 1 was completed on physical Vita hardware on 2026-09-11. The forced-failure probe recorded
the expected `0xA1000001` code, and the normal six-band probe completed three consecutive launches.
The final run passed logger, common types, serialization, timer, filesystem, memory, controller, and
framebuffer cleanup checks without a `FAIL` record. Its dependency graph contains fmt in header-only
mode and Boost headers; desktop frontends, network, audio, renderers, Crypto++, zstd, and compiled
Boost libraries are not linked.
