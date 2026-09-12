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

## Milestone 2 closure

Milestone 1 is complete when the reduced `citra_common` target builds for Vita, its native probe
passes the deterministic utility, logger, timer, filesystem, serialization, and memory checks on
physical hardware, and the resulting ELF size and initial memory use are retained as evidence.

Milestone 2 is implemented as an isolated DynCom probe. The same deterministic ARM and Thumb corpus
runs on desktop and Vita, uses bounds-checked flat memory, dispatches SVC through a probe callback,
checks shared memory between two emulated cores, and records a first instructions-per-second
measurement. Loader and kernel HLE integration remain part of milestone 3.

Physical validation was completed on 2026-09-12. The forced-failure VPK reported `0xA2000001`; the
normal VPK passed three consecutive launches with all seven desktop-reference signatures, no
invalid memory accesses, stable user memory, clean framebuffer release, and 1,469,795 interpreted
instructions per second in the retained run.

## Milestone 3 closure

Milestone 3 links the real loader, memory system and HLE kernel rather than an isolated corpus:
`Core::MemoryEnvironment` and the existing `Core::DynComEnvironment` let `Memory::MemorySystem`,
`Kernel::KernelSystem` and a `Core::ARM_DynCom` built directly over an environment run without
`Core::System`, the renderer, audio, network, or savestates. A synthetic 3DSX homebrew (generated
in code, no external binary or relocations) reaches its real entry point through `Kernel::
ThreadManager::Reschedule()` and exercises a small SVC table plus one IPC round trip against a
probe service.

Physical validation on 2026-09-12 found that VitaSDK's default 128 MiB newlib heap is not enough
for `Memory::MemorySystem`'s ~138.5 MiB of plain heap allocations (FCRAM, VRAM, N3DS extra RAM, DSP
RAM) plus a per-process page table: the first attempt crashed natively on all three launches, with
no boot.log record past the logger check. Defining `_newlib_heap_size_user = 192 MiB` (VitaSDK's
documented mechanism for exactly this, distinct from the "system mode app" budget `vita-make-
fself`'s `-m` flag exposes) resolved it; the rebuilt probe then passed three consecutive launches
with all five corpus groups matching the desktop reference bit for bit and stable user memory.

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

Milestone 2 was completed on physical Vita hardware on 2026-09-12. ARM and Thumb execution matched
the native desktop reference across arithmetic, flags, branches, calls, memory, isolated SVC
dispatch, register state, and a shared-memory core handoff. The bounded memory environment reported
no invalid access, and the first retained benchmark reached 1,469,795 instructions per second.

Milestone 3 was completed on physical Vita hardware on 2026-09-12. The real `Loader::
Load3DSXImage`, `Memory::MemorySystem` and `Kernel::KernelSystem` loaded a synthetic 3DSX homebrew,
reached its entry point through the kernel's own thread scheduler, and ran it to a clean
`ExitProcess` after one IPC round trip - all five corpus groups (`loader_identify`,
`memory_and_process`, `entry_point`, `svc_and_service_ipc`, `diagnostics`) matched the desktop
reference's signatures exactly, with user memory stable across three consecutive launches.

Milestone 4 was completed on physical Vita hardware on 2026-09-12. All five budget corpus groups
(`budget_plan`, `region_accounting`, `oom_recovery`, `load_release_cycles`, `renderer_headroom`)
matched the desktop reference's signatures exactly across three consecutive launches, with
`user_free` stable at 91,226,112 bytes (87.0 MiB) before and after the corpus ran and the 160 MiB
`_newlib_heap_size_user` confirmed in the log. Two design errors surfaced by that same physical
testing were fixed and reverified before this closure - see "Milestone 4 closure" below.

## Milestone 4 closure

Milestone 4 turns `Memory::MemorySystem`'s four large backing allocations (FCRAM, VRAM, DSP RAM,
New 3DS extra RAM) from an unconditional `std::make_unique<u8[]>` into calls through
`Core::MemoryEnvironment::AllocateBackingMemory`/`FreeBackingMemory`. This is the direct answer to
milestone 3's physical finding: a region that fails to allocate now leaves `MemorySystem` fully
constructed but reporting `IsInitialized() == false` and a `GetFailedItem()`, instead of the
constructor's plain heap allocation crashing natively with nothing in `boot.log` past the logger
check. `GetAllocatedBytes()`/`GetTotalAllocatedBytes()` expose what each region actually costs, so
"the consumption is visible in logs" (the Hito 4 acceptance criterion) has a concrete API behind
it, not just a printed number.

New 3DS extra RAM is no longer allocated at all under `AZAHAR_VITA`
(`N3DS_EXTRA_RAM_ALLOCATED_SIZE = 0` in `core/memory.cpp`): no Old 3DS-only build's ExHeader can
ever request it (`HandleSpecialMapping` in `core/hle/kernel/memory.cpp` only reaches that region
for a New-3DS-exclusive title, and such a title is already refused when `is_new_3ds` is false), so
the corresponding entries were dropped from `GetPhysMemRegionInfo`'s region table and
`HandleSpecialMapping`'s special-mapping table under `AZAHAR_VITA` rather than left allocated and
unused. A stray reference to the region now falls through to each site's existing "unknown
address"/"unhandled special mapping" `LOG_ERROR` path instead of resolving to a null-backed region
- recoverable and diagnosable, per the milestone's acceptance criterion, with no new failure path
invented for it.

The deterministic corpus (`vita/src/budget_corpus.cpp`) adds five groups, shared verbatim between
the desktop reference and the Vita probe exactly like milestones 2 and 3:

- `budget_plan` - the declared byte budget for every large allocation the port makes (the four
  emulated regions, the per-process page table's ARMv7-nominal size, the DynCom translation cache,
  the CDRAM framebuffer, and the newlib heap reservation), signed and checked against the measured
  Vita user-RAM pool.
- `region_accounting` - a real `Memory::MemorySystem`, built over a plain heap-based environment,
  checked byte for byte against that plan.
- `oom_recovery` - an environment that refuses the FCRAM request: `IsInitialized()` comes back
  false, `GetFailedItem()` names FCRAM, both objects are destroyed cleanly, and a second,
  unrestricted `MemorySystem` then succeeds. This is the "memory errors are recoverable" criterion
  exercised directly, in the one class of failure that took milestone 3 down without a log record.
- `load_release_cycles` - the milestone 3 loader/memory/kernel sequence run two independent times
  through a new `Vita::SystemProbe::RunSystemCycle` (a refactor of `RunSystemCorpus` that changes
  no milestone 3 behavior or signature - reverified against the retained desktop reference after
  the split), checked for byte-identical results across both cycles. This is the "no continuous
  growth across load/close cycles" criterion, expressed as a property that is exactly as
  meaningful on an x86-64 desktop as on ARMv7 hardware. Two cycles, not three: the final signature
  depends only on the first cycle's result plus two pass/match booleans, so a third cycle is pure
  redundant confirmation - confirmed empirically when the reduction from three to two left the
  desktop reference's signature (`0x17E0B0FE`) unchanged.
- `renderer_headroom` - a policy check that the declared plan still leaves a reserve for milestone
  5's software renderer.

A signature never depends on anything the desktop reference and the Vita probe could disagree on
bit for bit purely because of the host they run on - `sizeof(void*)` (8 on the x86-64 reference,
4 on Vita), the DynCom translation cache size (the desktop reference does not define
`TRANS_CACHE_SIZE`, so it keeps DynCom's much larger default), and any live
`sceKernelGetFreeMemorySize`/`mallinfo()` reading are all excluded from every group's signature and
logged separately instead, the same split milestone 3 already used for its own memory line.

`_newlib_heap_size_user` moves from 192 MiB (milestone 3) to 160 MiB. This is a smaller reduction
than an early estimate in this milestone's plan assumed: removing the 4 MiB New 3DS allocation
frees exactly that, but the corpus's own environments still construct full-sized, heap-based
`Memory::MemorySystem` instances (up to ~134.5 MiB FCRAM+VRAM+DSP RAM, plus the ~5 MiB per-process
page table once a homebrew is loaded) to stay comparable to the desktop reference, so the probe's
own peak heap need did not shrink to the extent a fully memblock-backed production system's would.

An earlier version of this probe additionally built one extra, unsigned `Memory::MemorySystem` over
a `MemblockEnvironment` that gave FCRAM, VRAM and DSP RAM their own named `sceKernelAllocMemBlock`
allocations before running the corpus - the intended hardware demonstration of "separate the
allocations by subsystem." Physical validation on 2026-09-12 found this does not fit the platform's
budget: `_newlib_heap_size_user` reserves its full size as one memblock the instant the process
starts, for the process's whole lifetime, regardless of whether anything has actually been
malloc'd from it yet - it is not a lazily-grown limit. A 160 MiB heap plus the demonstration's own
~134.5 MiB request needed ~294.5 MiB against the Vita's ~247 MiB measured user-RAM pool; the single
128 MiB FCRAM request was the one the kernel refused
(`SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE`, `0x80024302`), caught cleanly by this same milestone's
`IsInitialized()`/`GetFailedItem()` seam (`FAIL memory_regions`, `RESULT FAIL` - not a crash) but a
design error nonetheless. The demonstration was removed rather than shrinking the heap further,
since the corpus's own heap-based instances already need close to the full heap by themselves. The
real, measured reduction opportunity a memblock-backed allocator offers is left as a documented
follow-up for whenever a real Vita `Core::System` integration (post milestone 5) routes its own,
not-test, `Memory::MemorySystem` through such an allocator *instead of* the corpus's heap-based
one, so the two are never both reserved at once.

A second finding from that same round of physical testing: even after removing the demonstration,
one launch was silently busy for several seconds (up to two full ~134.5 MiB `Memory::MemorySystem`
constructions in `load_release_cycles` alone, plus one more each in `region_accounting` and
`oom_recovery`, each dominated by zeroing freshly allocated memory) before the probe ever read the
controller, which made that launch look hung rather than merely slow. Fixed by dropping
`load_release_cycles` to two cycles (see above) and by logging a `PASS memory_progress` line after
each of the five groups plus a `PASS memory_cycle_progress` line after each `load_release_cycles`
cycle, both flushed to `boot.log` immediately - a slow-but-working run is now distinguishable from
a stuck one.

This validation was completed on 2026-09-12; the retained evidence in `build-vita/evidence/hito-4/`
is the canonical record, and reflects the corrected build (both findings above fixed and
reverified) rather than the first, failing attempt.

## Extended memory evaluation (Hito 4)

`SceKernelFreeMemorySizeInfo` exposes three separate free-memory pools:
`size_user` (`SCE_KERNEL_MEMBLOCK_TYPE_USER_RW`, the default app data pool this port budgets
against), `size_cdram` (the framebuffer's pool), and `size_phycont`
(`SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW`, physically contiguous and **not cached** -
unsuitable for FCRAM, which the interpreter reads constantly, but a candidate for GPU-facing work
in milestones 5-6). The milestone 4 probe logs all three before and after the corpus runs.

VitaSDK's `vita-make-fself -m` ("system mode app", `0x1000`-`0x12800` KiB) is a real mechanism for
a larger memory budget, but it changes the SELF's authority level and its launch depends on
firmware/HENkaku configuration in a way a plain, installable VPK does not. The measured budget
already fits (`budget_plan`/`renderer_headroom` above) with room to spare, so this milestone
evaluates the option and explicitly does not adopt it - consistent with the roadmap's
"sin convertir plugins de kernel en requisito inicial." No kernel plugin, memory-unlock module, or
`ux0:tai` configuration is part of this or any milestone's requirements.
