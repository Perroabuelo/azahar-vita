// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace Core {

class DynComEnvironment {
public:
    virtual ~DynComEnvironment() = default;

    virtual u8 ReadMemory8(VAddr address) = 0;
    virtual u16 ReadMemory16(VAddr address) = 0;
    virtual u32 ReadMemory32(VAddr address) = 0;
    virtual u64 ReadMemory64(VAddr address) = 0;
    virtual void WriteMemory8(VAddr address, u8 value) = 0;
    virtual void WriteMemory16(VAddr address, u16 value) = 0;
    virtual void WriteMemory32(VAddr address, u32 value) = 0;
    virtual void WriteMemory64(VAddr address, u64 value) = 0;

    virtual void AddTicks(u64 ticks) = 0;
    virtual void CallSVC(u32 number) = 0;
};

} // namespace Core
