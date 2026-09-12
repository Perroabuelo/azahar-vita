// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/loader/3dsx_image.h"

#include <algorithm>
#include <cstring>
#include <vector>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"

namespace Loader {

/*
 * File layout:
 * - File header
 * - Code, rodata and data relocation table headers
 * - Code segment
 * - Rodata segment
 * - Loadable (non-BSS) part of the data segment
 * - Code relocation table
 * - Rodata relocation table
 * - Data relocation table
 *
 * Memory layout before relocations are applied:
 * [0..codeSegSize)             -> code segment
 * [codeSegSize..rodataSegSize) -> rodata segment
 * [rodataSegSize..dataSegSize) -> data segment
 *
 * Memory layout after relocations are applied: well, however the loader sets it up :)
 * The entrypoint is always the start of the code segment.
 * The BSS section must be cleared manually by the application.
 */

namespace {

constexpr u32 RELOCBUFSIZE = 512;
constexpr unsigned int NUM_SEGMENTS = 3;

// Relocation header: all fields (even extra unknown fields) are guaranteed to be relocation counts.
#pragma pack(1)
struct THREEDSX_RelocHdr {
    // # of absolute relocations (that is, fix address to post-relocation memory layout)
    u32 cross_segment_absolute;
    // # of cross-segment relative relocations (that is, 32bit signed offsets that need to be
    // patched)
    u32 cross_segment_relative;
    // more?

    // Relocations are written in this order:
    // - Absolute relocations
    // - Relative relocations
};

// Relocation entry: from the current pointer, skip X words and patch Y words
struct THREEDSX_Reloc {
    u16 skip, patch;
};
#pragma pack()

struct THREEloadinfo {
    u8* seg_ptrs[3]; // code, rodata & data
    u32 seg_addrs[3];
    u32 seg_sizes[3];
};

u32 TranslateAddr(u32 addr, const THREEloadinfo* loadinfo, u32* offsets) {
    if (addr < offsets[0])
        return loadinfo->seg_addrs[0] + addr;
    if (addr < offsets[1])
        return loadinfo->seg_addrs[1] + addr - offsets[0];
    return loadinfo->seg_addrs[2] + addr - offsets[1];
}

} // namespace

bool Identify3DSXImage(FileUtil::IOFileBase* file) {
    u32 magic{};
    return file->Seek(0, SEEK_SET) && 1 == file->ReadArray<u32>(&magic, 1) &&
           FileUtil::MakeMagic('3', 'D', 'S', 'X') == magic;
}

ThreeDSXResult Load3DSXImage(Kernel::KernelSystem& kernel, FileUtil::IOFileBase* file, u32 base_addr,
                             std::shared_ptr<Kernel::CodeSet>* out_codeset) {
    if (!file->IsOpen())
        return ThreeDSXResult::ErrorFileNotOpen;

    // Reset read pointer in case this file has been read before.
    file->Seek(0, SEEK_SET);

    THREEDSX_Header hdr;
    if (file->ReadBytes(&hdr, sizeof(hdr)) != sizeof(hdr))
        return ThreeDSXResult::ErrorRead;

    THREEloadinfo loadinfo;
    // loadinfo segments must be a multiple of 0x1000
    loadinfo.seg_sizes[0] = (hdr.code_seg_size + 0xFFF) & ~0xFFF;
    loadinfo.seg_sizes[1] = (hdr.rodata_seg_size + 0xFFF) & ~0xFFF;
    loadinfo.seg_sizes[2] = (hdr.data_seg_size + 0xFFF) & ~0xFFF;
    // prevent integer overflow leading to heap-buffer-overflow
    if (loadinfo.seg_sizes[0] < hdr.code_seg_size || loadinfo.seg_sizes[1] < hdr.rodata_seg_size ||
        loadinfo.seg_sizes[2] < hdr.data_seg_size) {
        return ThreeDSXResult::ErrorRead;
    }
    u32 offsets[2] = {loadinfo.seg_sizes[0], loadinfo.seg_sizes[0] + loadinfo.seg_sizes[1]};
    u32 n_reloc_tables = hdr.reloc_hdr_size / sizeof(u32);
    std::vector<u8> program_image(loadinfo.seg_sizes[0] + loadinfo.seg_sizes[1] +
                                  loadinfo.seg_sizes[2]);

    loadinfo.seg_addrs[0] = base_addr;
    loadinfo.seg_addrs[1] = loadinfo.seg_addrs[0] + loadinfo.seg_sizes[0];
    loadinfo.seg_addrs[2] = loadinfo.seg_addrs[1] + loadinfo.seg_sizes[1];
    loadinfo.seg_ptrs[0] = program_image.data();
    loadinfo.seg_ptrs[1] = loadinfo.seg_ptrs[0] + loadinfo.seg_sizes[0];
    loadinfo.seg_ptrs[2] = loadinfo.seg_ptrs[1] + loadinfo.seg_sizes[1];

    // Skip header for future compatibility
    file->Seek(hdr.header_size, SEEK_SET);

    // Read the relocation headers
    std::vector<u32> relocs(n_reloc_tables * NUM_SEGMENTS);
    for (unsigned int current_segment = 0; current_segment < NUM_SEGMENTS; ++current_segment) {
        std::size_t size = n_reloc_tables * sizeof(u32);
        if (file->ReadBytes(&relocs[current_segment * n_reloc_tables], size) != size)
            return ThreeDSXResult::ErrorRead;
    }

    // Read the segments
    if (file->ReadBytes(loadinfo.seg_ptrs[0], hdr.code_seg_size) != hdr.code_seg_size)
        return ThreeDSXResult::ErrorRead;
    if (file->ReadBytes(loadinfo.seg_ptrs[1], hdr.rodata_seg_size) != hdr.rodata_seg_size)
        return ThreeDSXResult::ErrorRead;
    if (file->ReadBytes(loadinfo.seg_ptrs[2], hdr.data_seg_size - hdr.bss_size) !=
        hdr.data_seg_size - hdr.bss_size)
        return ThreeDSXResult::ErrorRead;

    // BSS clear
    std::memset((char*)loadinfo.seg_ptrs[2] + hdr.data_seg_size - hdr.bss_size, 0, hdr.bss_size);

    // Relocate the segments
    for (unsigned int current_segment = 0; current_segment < NUM_SEGMENTS; ++current_segment) {
        for (unsigned current_segment_reloc_table = 0; current_segment_reloc_table < n_reloc_tables;
             current_segment_reloc_table++) {
            u32 n_relocs = relocs[current_segment * n_reloc_tables + current_segment_reloc_table];
            if (current_segment_reloc_table >= 2) {
                // We are not using this table - ignore it because we don't know what it dose
                file->Seek(n_relocs * sizeof(THREEDSX_Reloc), SEEK_CUR);
                continue;
            }
            THREEDSX_Reloc reloc_table[RELOCBUFSIZE];

            u32* pos = (u32*)loadinfo.seg_ptrs[current_segment];
            const u32* end_pos = pos + (loadinfo.seg_sizes[current_segment] / 4);

            while (n_relocs) {
                u32 remaining = std::min(RELOCBUFSIZE, n_relocs);
                n_relocs -= remaining;

                if (file->ReadBytes(reloc_table, remaining * sizeof(THREEDSX_Reloc)) !=
                    remaining * sizeof(THREEDSX_Reloc))
                    return ThreeDSXResult::ErrorRead;

                for (unsigned current_inprogress = 0;
                     current_inprogress < remaining && pos < end_pos; current_inprogress++) {
                    const auto& table = reloc_table[current_inprogress];
                    LOG_TRACE(Loader, "(t={},skip={},patch={})", current_segment_reloc_table,
                              static_cast<u32>(table.skip), static_cast<u32>(table.patch));
                    pos += table.skip;
                    s32 num_patches = table.patch;
                    while (0 < num_patches && pos < end_pos) {
                        u32 in_addr = base_addr + static_cast<u32>(reinterpret_cast<u8*>(pos) -
                                                                   program_image.data());
                        u32 orig_data = *pos;
                        u32 sub_type = orig_data >> (32 - 4);
                        u32 addr = TranslateAddr(orig_data & ~0xF0000000, &loadinfo, offsets);
                        LOG_TRACE(Loader, "Patching {:08X} <-- rel({:08X},{}) ({:08X})", in_addr,
                                  addr, current_segment_reloc_table, *pos);
                        switch (current_segment_reloc_table) {
                        case 0: {
                            if (sub_type != 0)
                                return ThreeDSXResult::ErrorRead;
                            *pos = addr;
                            break;
                        }
                        case 1: {
                            u32 data = addr - in_addr;
                            switch (sub_type) {
                            case 0: // 32-bit signed offset
                                *pos = data;
                                break;
                            case 1: // 31-bit signed offset
                                *pos = data & ~(1U << 31);
                                break;
                            default:
                                return ThreeDSXResult::ErrorRead;
                            }
                            break;
                        }
                        default:
                            break; // this should never happen
                        }
                        pos++;
                        num_patches--;
                    }
                }
            }
        }
    }

    // Create the CodeSet
    std::shared_ptr<Kernel::CodeSet> code_set = kernel.CreateCodeSet("", 0);

    code_set->CodeSegment().offset = loadinfo.seg_ptrs[0] - program_image.data();
    code_set->CodeSegment().addr = loadinfo.seg_addrs[0];
    code_set->CodeSegment().size = loadinfo.seg_sizes[0];

    code_set->RODataSegment().offset = loadinfo.seg_ptrs[1] - program_image.data();
    code_set->RODataSegment().addr = loadinfo.seg_addrs[1];
    code_set->RODataSegment().size = loadinfo.seg_sizes[1];

    code_set->DataSegment().offset = loadinfo.seg_ptrs[2] - program_image.data();
    code_set->DataSegment().addr = loadinfo.seg_addrs[2];
    code_set->DataSegment().size = loadinfo.seg_sizes[2];

    code_set->entrypoint = code_set->CodeSegment().addr;
    code_set->memory = std::move(program_image);

    LOG_DEBUG(Loader, "code size:   {:#X}", loadinfo.seg_sizes[0]);
    LOG_DEBUG(Loader, "rodata size: {:#X}", loadinfo.seg_sizes[1]);
    LOG_DEBUG(Loader, "data size:   {:#X} (including {:#X} of bss)", loadinfo.seg_sizes[2],
              hdr.bss_size);

    *out_codeset = code_set;
    return ThreeDSXResult::Success;
}

} // namespace Loader
