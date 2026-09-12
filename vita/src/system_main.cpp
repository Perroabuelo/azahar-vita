// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdint>
#include <malloc.h>
#include <span>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "system_corpus.h"

// VitaSDK's newlib startup reads this weak symbol to size the malloc/new heap it requests at
// launch (see share/gcc-arm-vita-eabi/samples/newlib_heapsize_ctrl in the SDK); the default is
// 128 MiB. Memory::MemorySystem alone allocates ~138.5 MiB (128 MiB FCRAM + 6 MiB VRAM + 4 MiB
// N3DS extra RAM + 0.5 MiB DSP RAM) via plain heap allocations, on top of which the kernel adds a
// ~5 MiB page table per process, so the default heap is not enough: a normal run exhausted it and
// crashed (confirmed on hardware, no forced-failure build involved). 192 MiB leaves headroom for
// that plus bookkeeping without yet requesting the exotic "system mode" memory budget documented
// for vita-make-fself's -m flag, which this is not.
extern "C" {
unsigned int _newlib_heap_size_user = SCE_KERNEL_128MiB + SCE_KERNEL_64MiB;
}

namespace {

constexpr int ScreenWidth = 960;
constexpr int ScreenHeight = 544;
constexpr int ScreenPitch = 960;
constexpr int FramebufferBytes = ScreenPitch * ScreenHeight * sizeof(std::uint32_t);
constexpr int CdramBlockAlignment = 256 * 1024;
constexpr int FramebufferAllocationBytes =
    (FramebufferBytes + CdramBlockAlignment - 1) / CdramBlockAlignment * CdramBlockAlignment;
constexpr unsigned int FailureDisplayTimeMicroseconds = 5'000'000;
constexpr const char* ProbeVersion = "03.00";
constexpr const char* LogPath = "ux0:data/azahar-vita/boot.log";
constexpr const char* WorkDir = "ux0:data/azahar-vita/hito-3/";

constexpr std::array<std::uint32_t, 6> SuccessColors{
    0xFF202020, 0xFF2774E6, 0xFF2ECC71, 0xFFF1C40F, 0xFFE67E22, 0xFFE74C3C,
};
constexpr std::array<std::uint32_t, 6> FailureColors{
    0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000,
};

static_assert(__cplusplus >= 202002L, "The Vita system probe requires C++20");
static_assert(sizeof(void*) == 4, "The Vita system probe must be built for a 32-bit target");

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
        uid = sceKernelAllocMemBlock("azahar-vita-system-framebuffer",
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

    [[nodiscard]] bool IsAllocated() const {
        return pixels != nullptr;
    }

    [[nodiscard]] bool IsPresented() const {
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
    LOG_ERROR(Core_ARM11, "FAIL {} code=0x{:08X}", stage, static_cast<std::uint32_t>(error));
    LOG_ERROR(Core_ARM11, "RESULT FAIL");
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

} // namespace

int main() {
    const MemorySnapshot memory_before = CaptureMemory();
    FileUtil::SetUserPath();
    Common::Log::Initialize("boot.log");
    Common::Log::Start();
    LOG_INFO(Core_ARM11, "system_probe_version={} platform=vita", ProbeVersion);

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
    if (sceDisplayWaitVblankStart() < 0) {
        return Fail(&framebuffer, "display_vblank", -1);
    }
    if (!FileUtil::Exists(LogPath)) {
        return Fail(&framebuffer, "azahar_logger", -1);
    }
    LOG_INFO(Core_ARM11, "PASS logger backend=vita path={}", LogPath);

    if (!FileUtil::CreateFullPath(WorkDir)) {
        return Fail(&framebuffer, "work_dir", -1);
    }

#if defined(AZAHAR_VITA_SYSTEM_FORCE_FAILURE)
    return Fail(&framebuffer, "forced_failure", static_cast<int>(0xA3000001U));
#endif

    const Vita::SystemProbe::SystemReport report = Vita::SystemProbe::RunSystemCorpus(WorkDir);
    for (std::size_t i = 0; i < report.group_count; ++i) {
        const auto& group = report.groups[i];
        if (group.passed) {
            LOG_INFO(Core_ARM11, "PASS group={} signature=0x{:08X}", group.name, group.signature);
        } else {
            LOG_ERROR(Core_ARM11, "FAIL group={} signature=0x{:08X}", group.name, group.signature);
        }
    }
    if (!report.Passed()) {
        return Fail(&framebuffer, "system_corpus", -1);
    }

    const MemorySnapshot memory_after = CaptureMemory();
    if (memory_before.result < 0 || memory_after.result < 0) {
        return Fail(&framebuffer, "memory_snapshot",
                    memory_after.result < 0 ? memory_after.result : memory_before.result);
    }
    LOG_INFO(Core_ARM11,
             "PASS memory heap_used={} user_free_before={} user_free_after={} cdram_free_before={} "
             "cdram_free_after={}",
             memory_after.heap.uordblks, memory_before.free_memory.size_user,
             memory_after.free_memory.size_user, memory_before.free_memory.size_cdram,
             memory_after.free_memory.size_cdram);

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

    LOG_INFO(Core_ARM11, "PASS controller input=start");
    result = framebuffer.Shutdown();
    if (result < 0) {
        return Fail(nullptr, "framebuffer_shutdown", result);
    }
    LOG_INFO(Core_ARM11, "PASS cleanup framebuffer=released");
    LOG_INFO(Core_ARM11, "RESULT PASS groups={}", report.group_count);
    Common::Log::Stop();
    return 0;
}
