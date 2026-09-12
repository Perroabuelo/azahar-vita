// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "common/common_types.h"

namespace FileUtil {
class IOFileBase;
}

namespace Kernel {
class CodeSet;
class KernelSystem;
} // namespace Kernel

namespace Loader {

enum class ThreeDSXResult {
    Success,
    ErrorFileNotOpen,
    ErrorRead,
};

// 3DSX file header. Shared with AppLoader_THREEDSX (3dsx.cpp), which also parses it directly to
// read the RomFS and SMDH sections that Load3DSXImage below does not need.
#pragma pack(1)
struct THREEDSX_Header {
    u32 magic;
    u16 header_size, reloc_hdr_size;
    u32 format_ver;
    u32 flags;

    // Sizes of the code, rodata and data segments +
    // size of the BSS section (uninitialized latter half of the data segment)
    u32 code_seg_size, rodata_seg_size, data_seg_size, bss_size;
    // offset and size of smdh
    u32 smdh_offset, smdh_size;
    // offset to filesystem
    u32 fs_offset;
};
#pragma pack()

/**
 * Returns true if `file` begins with the plain (uncompressed) 3DSX magic. This does not recognize
 * the Z3DS zstd-wrapped variant that AppLoader_THREEDSX::IdentifyType additionally accepts on
 * desktop; zstd is outside the Vita port's dependency graph (see docs/vita-port.md).
 */
bool Identify3DSXImage(FileUtil::IOFileBase* file);

/**
 * Parses a 3DSX homebrew image from `file`, lays its code/rodata/data segments out starting at
 * `base_addr`, applies its relocation tables, and returns the resulting CodeSet (built via
 * `kernel.CreateCodeSet()`) through `out_codeset`.
 *
 * This is the platform-independent core of AppLoader_THREEDSX::Load(): it needs only a
 * Kernel::KernelSystem, not a full Core::System, FS service, or ArchiveManager, so it can be
 * linked into a minimal harness such as the Vita port.
 */
ThreeDSXResult Load3DSXImage(Kernel::KernelSystem& kernel, FileUtil::IOFileBase* file, u32 base_addr,
                             std::shared_ptr<Kernel::CodeSet>* out_codeset);

} // namespace Loader
