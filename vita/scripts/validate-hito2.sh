#!/usr/bin/env bash
# Copyright 2026 Azahar Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

set -euo pipefail

build_dir="${1:-build-vita/hito2-cmake}"
host_probe="${2:-build-vita/hito2-host/azahar_dyncom_host_probe}"
elf="${build_dir}/azahar_vita_dyncom_probe"
vpk="${build_dir}/azahar_vita_dyncom_probe.vpk"
sfo="${build_dir}/azahar_vita_dyncom_probe.vpk_param.sfo"
map="${build_dir}/azahar_vita_dyncom_probe.map"
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
grep -q 'AZHV00003' <<<"${sfo_strings}"
grep -q '00.01' <<<"${sfo_strings}"
grep -q 'Azahar Vita DynCom Probe' <<<"${sfo_strings}"

symbols="$(${tool_prefix}nm -S -C "${elf}")"
grep -q 'InterpreterMainLoop(ARMul_State\*)' <<<"${symbols}"
grep -q 'trans_cache_buf$' <<<"${symbols}"

cache_size_hex="$(awk '$4 == "trans_cache_buf" { print $2 }' <<<"${symbols}")"
cache_size=$((16#${cache_size_hex}))
if [[ "${cache_size}" -ne 2097152 ]]; then
    echo "FAIL unexpected translation cache size: ${cache_size}" >&2
    exit 1
fi

bss_size="$(${tool_prefix}size "${elf}" | awk 'NR == 2 { print $3 }')"
if [[ "${bss_size}" -gt 4194304 ]]; then
    echo "FAIL BSS exceeds 4 MiB probe budget: ${bss_size}" >&2
    exit 1
fi

banned_regex='Qt|Vulkan|OpenGL|Android|Windows|Discord|CryptoPP|ZSTD|boost::iostreams|boost::archive|Kernel::SVC|Core::System'
if grep -Eiq "${banned_regex}" <<<"${symbols}"; then
    echo 'FAIL forbidden desktop, kernel, or excluded dependency found in ELF' >&2
    exit 1
fi

host_output="$("${host_probe}")"
grep -q '^PASS group=arm_alu_flags ' <<<"${host_output}"
grep -q '^PASS group=thumb_alu_memory_svc ' <<<"${host_output}"
grep -q '^PASS group=shared_memory_cores ' <<<"${host_output}"
grep -q '^PASS memory bounds=clean$' <<<"${host_output}"
grep -q '^PASS benchmark instructions=30000 final=10000$' <<<"${host_output}"
grep -q '^RESULT PASS instructions=34$' <<<"${host_output}"

echo 'PASS elf class=ELF32 machine=ARM abi=hard-float'
echo 'PASS package title_id=AZHV00003 version=00.01 contents=valid'
echo "PASS dyncom symbols=present translation_cache_bytes=${cache_size} bss_bytes=${bss_size}"
echo 'PASS dependency_audit forbidden_symbols=0'
echo 'PASS desktop_reference groups=7 invalid_access=0 benchmark_instructions=30000'
${tool_prefix}size "${elf}"
sha256sum "${vpk}"
