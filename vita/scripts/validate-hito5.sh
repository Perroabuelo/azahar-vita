#!/usr/bin/env bash
# Copyright 2026 Azahar Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

set -euo pipefail

build_dir="${1:-build-vita/hito5-cmake}"
host_probe="${2:-build-vita/hito5-host/azahar_render_host_probe}"
elf="${build_dir}/azahar_vita_render_probe"
vpk="${build_dir}/azahar_vita_render_probe.vpk"
sfo="${build_dir}/azahar_vita_render_probe.vpk_param.sfo"
map="${build_dir}/azahar_vita_render_probe.map"
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
grep -q 'AZHV00006' <<<"${sfo_strings}"
grep -q '00.01' <<<"${sfo_strings}"
grep -q 'Azahar Vita Render Probe' <<<"${sfo_strings}"

# Run from inside build_dir: this toolchain's binutils mis-resolve some absolute paths handed to
# them from outside a WSL2 session (a host quirk unrelated to the ELF itself - readelf/nm above
# still take absolute paths fine, only `size` was observed to need this).
symbols="$(${tool_prefix}nm -S -C "${elf}")"
grep -q 'Loader::Load3DSXImage' <<<"${symbols}"
grep -q 'Kernel::KernelSystem::KernelSystem' <<<"${symbols}"
grep -q 'Memory::MemorySystem::MemorySystem' <<<"${symbols}"
grep -q 'Pica::PicaCore::ProcessCmdList' <<<"${symbols}"
grep -q 'SwRenderer::RendererSoftware::SwapBuffers' <<<"${symbols}"
grep -q 'SwRenderer::RasterizerSoftware::AddTriangle' <<<"${symbols}"
grep -q 'SwRenderer::SwBlitter::TextureCopy' <<<"${symbols}"
grep -q 'SwRenderer::SwBlitter::MemoryFill' <<<"${symbols}"
grep -q 'Pica::Shader::InterpreterEngine' <<<"${symbols}"
grep -q 'trans_cache_buf$' <<<"${symbols}"
grep -q '_newlib_heap_size_user$' <<<"${symbols}"

cache_size_hex="$(awk '$4 == "trans_cache_buf" { print $2 }' <<<"${symbols}")"
cache_size=$((16#${cache_size_hex}))
if [[ "${cache_size}" -ne 2097152 ]]; then
    echo "FAIL unexpected translation cache size: ${cache_size}" >&2
    exit 1
fi

bss_size="$(cd "${build_dir}" && "${tool_prefix}size" "$(basename "${elf}")" | awk 'NR == 2 { print $3 }')"
if [[ "${bss_size}" -gt 8388608 ]]; then
    echo "FAIL BSS exceeds 8 MiB probe budget: ${bss_size}" >&2
    exit 1
fi

# Same shape as milestone 3/4 (see validate-hito3.sh/validate-hito4.sh), with OpenGL and Vulkan
# added explicitly: this milestone links the software renderer only (ENABLE_SOFTWARE_RENDERER, not
# ENABLE_OPENGL/ENABLE_VULKAN - see vita/CMakeLists.txt's citra_render_probe target and
# docs/vita-port.md), so neither accelerated backend's symbols should ever reach this ELF.
banned_regex_ci='Qt|Vulkan|OpenGL|Android|Windows|Discord|CryptoPP|ZSTD'
banned_regex_cs='boost::iostreams|boost::archive|Core::System::|GDBStub::|PLGLDR::|OpenGL::|Vulkan::'
if grep -Eiq "${banned_regex_ci}" <<<"${symbols}" || grep -Eq "${banned_regex_cs}" <<<"${symbols}"; then
    echo 'FAIL forbidden desktop or accelerated-renderer dependency found in ELF' >&2
    exit 1
fi

host_output="$("${host_probe}" 2>/dev/null)"
grep -q '^PASS group=renderer_init ' <<<"${host_output}"
grep -q '^PASS group=color_fill ' <<<"${host_output}"
grep -q '^PASS group=framebuffer_formats ' <<<"${host_output}"
grep -q '^PASS group=transfer_engine ' <<<"${host_output}"
grep -q '^PASS group=triangle_raster ' <<<"${host_output}"
grep -q '^PASS group=textured_quad ' <<<"${host_output}"
grep -q '^PASS group=guest_frame ' <<<"${host_output}"
grep -q '^RESULT PASS groups=7$' <<<"${host_output}"

# The desktop reference writes hito5-top.ppm/hito5-bottom.ppm to its own working directory as a
# side effect of the run above (RunGuestFrameGroup - see render_corpus.cpp). Their hashes are what
# the recovered Vita captures (ux0:data/azahar-vita/hito-5/hito5-{top,bottom}.ppm) must match - see
# vita/README.md's Hito 5 physical validation section.
host_probe_dir="$(dirname "${host_probe}")"
if [[ ! -f "${host_probe_dir}/hito5-top.ppm" || ! -f "${host_probe_dir}/hito5-bottom.ppm" ]]; then
    echo 'FAIL desktop reference did not produce hito5-top.ppm/hito5-bottom.ppm' >&2
    exit 1
fi

echo 'PASS elf class=ELF32 machine=ARM abi=hard-float'
echo 'PASS package title_id=AZHV00006 version=00.01 contents=valid'
echo "PASS render symbols=present translation_cache_bytes=${cache_size} bss_bytes=${bss_size}"
echo 'PASS dependency_audit forbidden_symbols=0'
echo 'PASS desktop_reference groups=7'
echo "PASS desktop_reference_captures top=$(sha256sum "${host_probe_dir}/hito5-top.ppm" | cut -d' ' -f1) bottom=$(sha256sum "${host_probe_dir}/hito5-bottom.ppm" | cut -d' ' -f1)"
(cd "${build_dir}" && "${tool_prefix}size" "$(basename "${elf}")")
sha256sum "${vpk}"
