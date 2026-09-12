// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <span>
#include <string>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "common/atomic_ops.h"
#include "common/bit_field.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/memory_detect.h"
#include "common/param_package.h"
#include "common/timer.h"

namespace {

constexpr int ScreenWidth = 960;
constexpr int ScreenHeight = 544;
constexpr int ScreenPitch = 960;
constexpr int FramebufferBytes = ScreenPitch * ScreenHeight * sizeof(std::uint32_t);
constexpr int CdramBlockAlignment = 256 * 1024;
constexpr int FramebufferAllocationBytes =
    (FramebufferBytes + CdramBlockAlignment - 1) / CdramBlockAlignment * CdramBlockAlignment;
constexpr unsigned int FailureDisplayTimeMicroseconds = 5'000'000;
constexpr const char* ProbeVersion = "01.00";
constexpr const char* LogPath = "ux0:data/azahar-vita/boot.log";
constexpr const char* TestDirectory = "ux0:data/azahar-vita/hito-1/";
constexpr const char* TestPath = "ux0:data/azahar-vita/hito-1/common.tmp";
constexpr const char* RenamedTestPath = "ux0:data/azahar-vita/hito-1/common-renamed.tmp";

constexpr std::array<std::uint32_t, 6> SuccessColors{
    0xFF202020, 0xFF2774E6, 0xFF2ECC71, 0xFFF1C40F, 0xFFE67E22, 0xFFE74C3C,
};
constexpr std::array<std::uint32_t, 6> FailureColors{
    0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000,
};

static_assert(__cplusplus >= 202002L, "The Vita common probe requires C++20");
static_assert(sizeof(void*) == 4, "The Vita common probe must be built for a 32-bit target");
static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8);

struct MemorySnapshot {
    SceKernelFreeMemorySizeInfo free_memory{};
    struct mallinfo heap {};
    int result{};
};

MemorySnapshot CaptureMemory() {
    MemorySnapshot snapshot{};
    snapshot.free_memory.size = sizeof(snapshot.free_memory);
    snapshot.result = sceKernelGetFreeMemorySize(&snapshot.free_memory);
    snapshot.heap = mallinfo();
    return snapshot;
}

class Framebuffer {
public:
    int Allocate() {
        uid = sceKernelAllocMemBlock("azahar-vita-common-framebuffer",
                                     SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                     FramebufferAllocationBytes, nullptr);
        if (uid < 0) {
            return uid;
        }
        return sceKernelGetMemBlockBase(uid, reinterpret_cast<void**>(&pixels));
    }

    void DrawBands(std::span<const std::uint32_t, 6> colors) const {
        for (int y = 0; y < ScreenHeight; ++y) {
            const auto index = std::min(static_cast<std::size_t>(y * colors.size() / ScreenHeight),
                                        colors.size() - 1);
            std::fill_n(pixels + y * ScreenPitch, ScreenWidth, colors[index]);
        }
    }

    int Present() {
        SceDisplayFrameBuf frame{};
        frame.size = sizeof(frame);
        frame.base = pixels;
        frame.pitch = ScreenPitch;
        frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        frame.width = ScreenWidth;
        frame.height = ScreenHeight;
        const int result = sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
        presented = result >= 0;
        return result;
    }

    int Shutdown() {
        int result = 0;
        if (presented) {
            result = sceDisplaySetFrameBuf(nullptr, SCE_DISPLAY_SETBUF_NEXTFRAME);
            if (result >= 0) {
                result = sceDisplayWaitVblankStart();
            }
            presented = false;
        }
        if (uid >= 0) {
            const int free_result = sceKernelFreeMemBlock(uid);
            if (result >= 0) {
                result = free_result;
            }
            uid = -1;
            pixels = nullptr;
        }
        return result;
    }

    bool IsAllocated() const {
        return pixels != nullptr;
    }

    bool IsPresented() const {
        return presented;
    }

    ~Framebuffer() {
        Shutdown();
    }

private:
    SceUID uid{-1};
    std::uint32_t* pixels{};
    bool presented{};
};

int Fail(Framebuffer* framebuffer, const char* stage, int error) {
    LOG_ERROR(Common, "FAIL {} code=0x{:08X}", stage, static_cast<std::uint32_t>(error));
    LOG_ERROR(Common, "RESULT FAIL");
    if (framebuffer != nullptr && framebuffer->IsAllocated()) {
        framebuffer->DrawBands(FailureColors);
        if (!framebuffer->IsPresented()) {
            static_cast<void>(framebuffer->Present());
        }
        static_cast<void>(sceDisplayWaitVblankStart());
        static_cast<void>(sceKernelDelayThread(FailureDisplayTimeMicroseconds));
    }
    Common::Log::Stop();
    return 1;
}

bool TestUtilities() {
    union TestRegister {
        u32 raw;
        BitField<4, 8, u32> field;
    };
    TestRegister reg{};
    reg.field.Assign(0xA5);

    volatile u32 atomic_value = 7;
    u32 actual = 0;
    const bool atomic_ok = Common::AtomicCompareAndSwap(&atomic_value, 11U, 7U, actual);

    constexpr std::array<u8, 5> values{9, 2, 7, 4, 8};
    const auto [minimum, maximum] = Common::FindMinMax(std::span<const u8>{values});
    return reg.raw == 0xA50 && atomic_ok && actual == 7 && atomic_value == 11 && minimum == 2 &&
           maximum == 9;
}

bool TestParamPackage() {
    Common::ParamPackage original{{"name", "vita:common,probe"}, {"count", "32"}};
    const Common::ParamPackage restored{original.Serialize()};
    return restored.Get("name", "") == "vita:common,probe" && restored.Get("count", 0) == 32;
}

bool TestTimer(std::chrono::milliseconds& elapsed) {
    Common::Timer timer;
    timer.Start();
    if (sceKernelDelayThread(20'000) < 0) {
        return false;
    }
    timer.Stop();
    elapsed = timer.GetTimeElapsed();
    return elapsed >= std::chrono::milliseconds{10} && elapsed < std::chrono::seconds{1};
}

bool TestFilesystem() {
    constexpr std::string_view contents = "azahar-vita-common-roundtrip";
    static_cast<void>(FileUtil::Delete(TestPath));
    static_cast<void>(FileUtil::Delete(RenamedTestPath));
    if (!FileUtil::CreateFullPath(TestDirectory) ||
        FileUtil::WriteStringToFile(false, TestPath, contents) != contents.size()) {
        return false;
    }

    std::string restored;
    const bool read_ok = FileUtil::ReadFileToString(false, TestPath, restored) == contents.size() &&
                         restored == contents;
    const bool rename_ok = FileUtil::Rename(TestPath, RenamedTestPath) &&
                           FileUtil::Exists(RenamedTestPath) && !FileUtil::Exists(TestPath);
    const bool delete_ok = FileUtil::Delete(RenamedTestPath) && !FileUtil::Exists(RenamedTestPath);
    return read_ok && rename_ok && delete_ok;
}

} // namespace

int main() {
    const MemorySnapshot memory_before = CaptureMemory();
    FileUtil::SetUserPath();
    Common::Log::Initialize("boot.log");
    Common::Log::Start();
    LOG_INFO(Common, "common_probe_version={}", ProbeVersion);

    Framebuffer framebuffer;
    int result = framebuffer.Allocate();
    if (result < 0) {
        return Fail(nullptr, "cdram_allocate", result);
    }
    framebuffer.DrawBands(SuccessColors);
    result = framebuffer.Present();
    if (result < 0) {
        return Fail(&framebuffer, "display_present", result);
    }
    result = sceDisplayWaitVblankStart();
    if (result < 0) {
        return Fail(&framebuffer, "display_vblank", result);
    }

    if (!FileUtil::Exists(LogPath)) {
        return Fail(&framebuffer, "azahar_logger", -1);
    }
    LOG_INFO(Common, "PASS logger backend=vita path={}", LogPath);

#if defined(AZAHAR_VITA_COMMON_FORCE_FAILURE)
    return Fail(&framebuffer, "forced_failure", static_cast<int>(0xA1000001U));
#endif

    if (!TestUtilities()) {
        return Fail(&framebuffer, "common_utilities", -1);
    }
    LOG_INFO(Common, "PASS common types=ok atomic_bits=32 math=ok");

    if (!TestParamPackage()) {
        return Fail(&framebuffer, "param_package", -1);
    }
    LOG_INFO(Common, "PASS serialization type=ParamPackage escaping=ok");

    std::chrono::milliseconds elapsed{};
    if (!TestTimer(elapsed)) {
        return Fail(&framebuffer, "timer", static_cast<int>(elapsed.count()));
    }
    LOG_INFO(Common, "PASS timer elapsed_ms={}", elapsed.count());

    if (!TestFilesystem()) {
        return Fail(&framebuffer, "filesystem_roundtrip", -1);
    }
    LOG_INFO(Common_Filesystem, "PASS filesystem roundtrip=create-read-rename-delete");

    const MemorySnapshot memory_after = CaptureMemory();
    if (memory_before.result < 0 || memory_after.result < 0) {
        return Fail(&framebuffer, "memory_snapshot",
                    memory_after.result < 0 ? memory_after.result : memory_before.result);
    }
    const auto memory_info = Common::GetMemInfo();
    LOG_INFO(Common_Memory,
             "PASS memory total={} page={} heap_used={} user_free_before={} user_free_after={} "
             "cdram_free_before={} cdram_free_after={} phycont_free={}",
             memory_info.total_physical_memory, Common::GetPageSize(), memory_after.heap.uordblks,
             memory_before.free_memory.size_user, memory_after.free_memory.size_user,
             memory_before.free_memory.size_cdram, memory_after.free_memory.size_cdram,
             memory_after.free_memory.size_phycont);

    result = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    if (result < 0) {
        return Fail(&framebuffer, "controller_mode", result);
    }
    while (true) {
        SceCtrlData controls{};
        result = sceCtrlPeekBufferPositive(0, &controls, 1);
        if (result != 1) {
            return Fail(&framebuffer, "controller_read", result != 0 ? result : -1);
        }
        if ((controls.buttons & SCE_CTRL_START) != 0) {
            break;
        }
        if (sceDisplayWaitVblankStart() < 0) {
            return Fail(&framebuffer, "display_vblank", -1);
        }
    }

    LOG_INFO(Common, "PASS controller input=start");
    result = framebuffer.Shutdown();
    if (result < 0) {
        return Fail(nullptr, "framebuffer_shutdown", result);
    }
    LOG_INFO(Common, "PASS cleanup framebuffer=released");
    LOG_INFO(Common, "RESULT PASS");
    Common::Log::Stop();
    return 0;
}
