#!/usr/bin/env bash
# Copyright 2026 Azahar Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

set -euo pipefail

build_dir="${1:-build-vita/hito4-cmake}"
host_probe="${2:-build-vita/hito4-host/azahar_budget_host_probe}"
elf="${build_dir}/azahar_vita_budget_probe"
vpk="${build_dir}/azahar_vita_budget_probe.vpk"
sfo="${build_dir}/azahar_vita_budget_probe.vpk_param.sfo"
map="${build_dir}/azahar_vita_budget_probe.map"
tool_prefix="${VITASDK:?Set VITASDK before validating}/bin/arm-vita-eabi-"

for artifact in "${elf}" "${vpk}" "${sfo}" "${map}" "${host_probe}"; do
    if [[ ! -f "${artifact}" ]]; then
        echo "FAIL missing artifact: ${artifact}" >&2
        exit 1
    fi
done

header="$(${tool_prefix}readelf -h "${elf}")"
grep -q 'Class:.*ELF32' <<<"${header}"
grep -q 'Machine:.*ARM' <<<"${header}"
grep -q 'hard-float ABI' <<<"${header}"

contents="$(python3 -m zipfile -l "${vpk}")"
grep -q 'sce_sys/param.sfo' <<<"${contents}"
grep -q 'eboot.bin' <<<"${contents}"

sfo_strings="$(strings "${sfo}")"
grep -q 'AZHV00005' <<<"${sfo_strings}"
grep -q '00.01' <<<"${sfo_strings}"
grep -q 'Azahar Vita Memory Probe' <<<"${sfo_strings}"

symbols="$(${tool_prefix}nm -S -C "${elf}")"
grep -q 'Loader::Load3DSXImage' <<<"${symbols}"
grep -q 'Kernel::KernelSystem::KernelSystem' <<<"${symbols}"
grep -q 'Memory::MemorySystem::MemorySystem' <<<"${symbols}"
grep -q 'Memory::MemorySystem::IsInitialized' <<<"${symbols}"
grep -q 'Memory::MemorySystem::GetAllocatedBytes' <<<"${symbols}"
grep -q 'InterpreterMainLoop(ARMul_State\*)' <<<"${symbols}"
grep -q 'trans_cache_buf$' <<<"${symbols}"
grep -q '_newlib_heap_size_user$' <<<"${symbols}"

cache_size_hex="$(awk '$4 == "trans_cache_buf" { print $2 }' <<<"${symbols}")"
cache_size=$((16#${cache_size_hex}))
if [[ "${cache_size}" -ne 2097152 ]]; then
    echo "FAIL unexpected translation cache size: ${cache_size}" >&2
    exit 1
fi

bss_size="$(${tool_prefix}size "${elf}" | awk 'NR == 2 { print $3 }')"
if [[ "${bss_size}" -gt 8388608 ]]; then
    echo "FAIL BSS exceeds 8 MiB probe budget: ${bss_size}" >&2
    exit 1
fi

# Same dependency shape as milestone 3 (see validate-hito3.sh): the real HLE kernel and memory
# system are expected; Core::System and the desktop-only subsystems it drags in are not.
banned_regex_ci='Qt|Vulkan|OpenGL|Android|Windows|Discord|CryptoPP|ZSTD'
banned_regex_cs='boost::iostreams|boost::archive|Core::System::|GDBStub::|PLGLDR::'
if grep -Eiq "${banned_regex_ci}" <<<"${symbols}" || grep -Eq "${banned_regex_cs}" <<<"${symbols}"; then
    echo 'FAIL forbidden desktop or excluded dependency found in ELF' >&2
    exit 1
fi

host_output="$("${host_probe}" 2>/dev/null)"
grep -q '^PASS group=budget_plan ' <<<"${host_output}"
grep -q '^PASS group=region_accounting ' <<<"${host_output}"
grep -q '^PASS group=oom_recovery ' <<<"${host_output}"
grep -q '^PASS group=load_release_cycles ' <<<"${host_output}"
grep -q '^PASS group=renderer_headroom ' <<<"${host_output}"
grep -q '^RESULT PASS groups=5$' <<<"${host_output}"

echo 'PASS elf class=ELF32 machine=ARM abi=hard-float'
echo 'PASS package title_id=AZHV00005 version=00.01 contents=valid'
echo "PASS budget symbols=present translation_cache_bytes=${cache_size} bss_bytes=${bss_size}"
echo 'PASS dependency_audit forbidden_symbols=0'
echo 'PASS desktop_reference groups=5'
${tool_prefix}size "${elf}"
sha256sum "${vpk}"
