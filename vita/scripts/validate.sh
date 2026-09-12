#!/usr/bin/env bash
# Copyright 2026 Azahar Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

set -euo pipefail

build_dir="${1:-build-vita/hito1-cmake}"
elf="${build_dir}/azahar_vita_common_probe"
vpk="${build_dir}/azahar_vita_common_probe.vpk"
sfo="${build_dir}/azahar_vita_common_probe.vpk_param.sfo"
map="${build_dir}/azahar_vita_common_probe.map"
tool_prefix="${VITASDK:?Set VITASDK before validating}/bin/arm-vita-eabi-"

for artifact in "${elf}" "${vpk}" "${sfo}" "${map}"; do
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
grep -q 'AZHV00002' <<<"${sfo_strings}"
grep -q '00.01' <<<"${sfo_strings}"
grep -q 'Azahar Vita Common Probe' <<<"${sfo_strings}"

banned_regex='Qt|Vulkan|OpenGL|Android|Windows|Discord|CryptoPP|ZSTD|boost::iostreams|boost::archive'
if ${tool_prefix}nm -C "${elf}" | grep -Eiq "${banned_regex}"; then
    echo 'FAIL forbidden desktop or excluded dependency found in ELF' >&2
    exit 1
fi

echo 'PASS elf class=ELF32 machine=ARM abi=hard-float'
echo 'PASS package title_id=AZHV00002 version=00.01 contents=valid'
echo 'PASS dependency_audit forbidden_symbols=0'
${tool_prefix}size "${elf}"
sha256sum "${vpk}"
