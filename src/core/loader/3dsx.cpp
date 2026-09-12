// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>
#include "common/file_derived.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/zstd_compression.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/fs/fs_user.h"
#include "core/loader/3dsx.h"
#include "core/loader/3dsx_image.h"
#include "core/memory.h"

namespace Loader {

using Kernel::CodeSet;

AppLoader_THREEDSX::AppLoader_THREEDSX(Core::System& system_, FileUtil::IOFile&& file,
                                       const std::string& filename, const std::string& filepath)
    : AppLoader(system_, std::move(file)), filename(filename), filepath(filepath) {

    filetype = IdentifyType(this->file.get());

    if (FileUtil::Z3DSReadIOFile::GetUnderlyingFileMagic(this->file.get()) != std::nullopt) {
        this->file = std::make_unique<FileUtil::Z3DSReadIOFile>(std::move(this->file));
    }
}

FileType AppLoader_THREEDSX::IdentifyType(FileUtil::IOFileBase* file) {
    if (Loader::Identify3DSXImage(file)) {
        return FileType::THREEDSX;
    }

    u32 magic{};
    if (file->Seek(0, SEEK_SET) && 1 == file->ReadArray<u32>(&magic, 1)) {
        if (FileUtil::MakeMagic('Z', '3', 'D', 'S') == magic &&
            FileUtil::Z3DSReadIOFile::GetUnderlyingFileMagic(file) ==
                FileUtil::MakeMagic('3', 'D', 'S', 'X'))
            return FileType::THREEDSX;
    }

    return FileType::Error;
}

ResultStatus AppLoader_THREEDSX::Load(std::shared_ptr<Kernel::Process>& process) {
    if (is_loaded)
        return ResultStatus::ErrorAlreadyLoaded;

    if (!file->IsOpen())
        return ResultStatus::Error;

    std::shared_ptr<CodeSet> codeset;
    if (Loader::Load3DSXImage(system.Kernel(), file.get(), Memory::PROCESS_IMAGE_VADDR, &codeset) !=
        Loader::ThreeDSXResult::Success)
        return ResultStatus::Error;
    codeset->name = filename;

    process = system.Kernel().CreateProcess(std::move(codeset));
    process->Set3dsxKernelCaps();

    // Attach the default resource limit (APPLICATION) to the process
    process->resource_limit =
        system.Kernel().ResourceLimit().GetForCategory(Kernel::ResourceLimitCategory::Application);

    process->resource_limit->ApplyAppMaxCPUSetting(process, 1, 89);

    // On real HW this is done with FS:Reg, but we can be lazy
    auto fs_user = system.ServiceManager().GetService<Service::FS::FS_USER>("fs:USER");
    fs_user->RegisterProgramInfo(process->GetObjectId(), process->codeset->program_id, filepath);

    process->Run(48, Kernel::DEFAULT_STACK_SIZE);

    system.ArchiveManager().RegisterSelfNCCH(*this);

    is_loaded = true;
    return ResultStatus::Success;
}

ResultStatus AppLoader_THREEDSX::ReadRomFS(std::shared_ptr<FileSys::RomFSReader>& romfs_file) {
    if (!file->IsOpen())
        return ResultStatus::Error;

    // Reset read pointer in case this file has been read before.
    file->Seek(0, SEEK_SET);

    THREEDSX_Header hdr;
    if (file->ReadBytes(&hdr, sizeof(THREEDSX_Header)) != sizeof(THREEDSX_Header))
        return ResultStatus::Error;

    if (hdr.header_size != sizeof(THREEDSX_Header))
        return ResultStatus::Error;

    // Check if the 3DSX has a RomFS...
    if (hdr.fs_offset != 0) {
        u32 romfs_offset = hdr.fs_offset;
        u32 romfs_size = static_cast<u32>(file->GetSize()) - hdr.fs_offset;

        LOG_DEBUG(Loader, "RomFS offset:           {:#010X}", romfs_offset);
        LOG_DEBUG(Loader, "RomFS size:             {:#010X}", romfs_size);

        // We reopen the file, to allow its position to be independent from file's
        std::unique_ptr<FileUtil::IOFileBase> romfs_file_inner = file->OpenCopy();
        if (!romfs_file_inner->IsOpen())
            return ResultStatus::Error;

        romfs_file =
            std::make_shared<FileSys::DirectRomFSReader>(std::make_unique<FileUtil::SubIOFile>(
                std::move(romfs_file_inner), romfs_offset, romfs_size));

        return ResultStatus::Success;
    }
    LOG_DEBUG(Loader, "3DSX has no RomFS");
    return ResultStatus::ErrorNotUsed;
}

AppLoader::CompressFileInfo AppLoader_THREEDSX::GetCompressFileInfo() {
    CompressFileInfo info;
    info.is_supported = true;
    info.recommended_compressed_extension = "z3dsx";
    info.recommended_uncompressed_extension = "3dsx";
    info.underlying_magic = std::array<u8, 4>({'3', 'D', 'S', 'X'});
    info.is_compressed = file->GetType().HasCompressedType();
    return info;
}

bool AppLoader_THREEDSX::IsFileCompressed() {
    return file->GetType().HasCompressedType();
}

ResultStatus AppLoader_THREEDSX::ReadIcon(std::vector<u8>& buffer) {
    if (!file->IsOpen())
        return ResultStatus::Error;

    // Reset read pointer in case this file has been read before.
    file->Seek(0, SEEK_SET);

    THREEDSX_Header hdr;
    if (file->ReadBytes(&hdr, sizeof(THREEDSX_Header)) != sizeof(THREEDSX_Header))
        return ResultStatus::Error;

    if (hdr.header_size != sizeof(THREEDSX_Header))
        return ResultStatus::Error;

    // Check if the 3DSX has a SMDH...
    if (hdr.smdh_offset != 0) {
        file->Seek(hdr.smdh_offset, SEEK_SET);
        buffer.resize(hdr.smdh_size);

        if (file->ReadBytes(buffer.data(), hdr.smdh_size) != hdr.smdh_size)
            return ResultStatus::Error;

        return ResultStatus::Success;
    }
    return ResultStatus::ErrorNotUsed;
}

} // namespace Loader
