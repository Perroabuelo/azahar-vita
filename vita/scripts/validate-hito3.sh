#!/usr/bin/env bash
# Copyright 2026 Azahar Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

set -euo pipefail

build_dir="${1:-build-vita/hito3-cmake}"
host_probe="${2:-build-vita/hito3-host/azahar_system_host_probe}"
elf="${build_dir}/azahar_vita_system_probe"
vpk="${build_dir}/azahar_vita_system_probe.vpk"
sfo="${build_dir}/azahar_vita_system_probe.vpk_param.sfo"
map="${build_dir}/azahar_vita_system_probe.map"
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
grep -q 'AZHV00004' <<<"${sfo_strings}"
grep -q '00.01' <<<"${sfo_strings}"
grep -q 'Azahar Vita System Probe' <<<"${sfo_strings}"

symbols="$(${tool_prefix}nm -S -C "${elf}")"
grep -q 'Loader::Load3DSXImage' <<<"${symbols}"
grep -q 'Kernel::KernelSystem::KernelSystem' <<<"${symbols}"
grep -q 'Memory::MemorySystem::MemorySystem' <<<"${symbols}"
grep -q 'InterpreterMainLoop(ARMul_State\*)' <<<"${symbols}"
grep -q 'trans_cache_buf$' <<<"${symbols}"

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

# Unlike milestone 2, Kernel:: and Core::MemorySystem symbols are expected here - this probe links
# the real HLE kernel and memory system. Core::System and the desktop-only subsystems it drags in
# (renderer, audio, network, savestates, GDB stub, plugin loader) must still be absent.
banned_regex_ci='Qt|Vulkan|OpenGL|Android|Windows|Discord|CryptoPP|ZSTD'
# Case-sensitive and namespace-qualified: Settings::Keys::use_gdbstub and similar setting-name data
# symbols contain the plain word "gdbstub" without ever calling into the real GDBStub:: namespace,
# so a loose case-insensitive match on these produces false positives.
banned_regex_cs='boost::iostreams|boost::archive|Core::System::|GDBStub::|PLGLDR::'
if grep -Eiq "${banned_regex_ci}" <<<"${symbols}" || grep -Eq "${banned_regex_cs}" <<<"${symbols}"; then
    echo 'FAIL forbidden desktop or excluded dependency found in ELF' >&2
    exit 1
fi

host_output="$("${host_probe}" 2>/dev/null)"
grep -q '^PASS group=loader_identify ' <<<"${host_output}"
grep -q '^PASS group=memory_and_process ' <<<"${host_output}"
grep -q '^PASS group=entry_point ' <<<"${host_output}"
grep -q '^PASS group=svc_and_service_ipc ' <<<"${host_output}"
grep -q '^PASS group=diagnostics ' <<<"${host_output}"
grep -q '^RESULT PASS groups=5$' <<<"${host_output}"

echo 'PASS elf class=ELF32 machine=ARM abi=hard-float'
echo 'PASS package title_id=AZHV00004 version=00.01 contents=valid'
echo "PASS system symbols=present translation_cache_bytes=${cache_size} bss_bytes=${bss_size}"
echo 'PASS dependency_audit forbidden_symbols=0'
echo 'PASS desktop_reference groups=5'
${tool_prefix}size "${elf}"
sha256sum "${vpk}"
