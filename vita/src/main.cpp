#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

namespace {

constexpr int ScreenWidth = 960;
constexpr int ScreenHeight = 544;
constexpr int ScreenPitch = 960;
constexpr int FramebufferBytes = ScreenPitch * ScreenHeight * sizeof(std::uint32_t);
constexpr int CdramBlockAlignment = 256 * 1024;
constexpr int FramebufferAllocationBytes =
    (FramebufferBytes + CdramBlockAlignment - 1) / CdramBlockAlignment * CdramBlockAlignment;
constexpr unsigned int FailureDisplayTimeMicroseconds = 5'000'000;
constexpr const char* ProbeVersion = "00.02";
constexpr const char* LogDirectory = "ux0:data/azahar-vita";
constexpr const char* LogPath = "ux0:data/azahar-vita/boot.log";

constexpr std::array<std::uint32_t, 6> SuccessColors{
    0xFF202020, 0xFF2774E6, 0xFF2ECC71, 0xFFF1C40F, 0xFFE67E22, 0xFFE74C3C,
};
constexpr std::array<std::uint32_t, 6> FailureColors{
    0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000,
};

constexpr std::uint32_t PatternChecksum(std::span<const std::uint32_t> colors) {
    std::uint32_t checksum = 2166136261U;
    for (const auto color : colors) {
        checksum = (checksum ^ color) * 16777619U;
    }
    return checksum;
}

static_assert(__cplusplus >= 202002L, "The Vita probe requires C++20");
static_assert(sizeof(void*) == 4, "The Vita probe must be built for a 32-bit target");
static_assert(PatternChecksum(SuccessColors) == 0x486CC51DU);

class ProbeLog {
public:
    bool Initialize() {
        sceIoMkdir(LogDirectory, 0777);
        file = sceIoOpen(LogPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        last_error = file < 0 ? file : 0;
        return file >= 0;
    }

    bool Write(const char* format, ...) {
        std::array<char, 256> buffer{};
        va_list arguments;
        va_start(arguments, format);
        const int length = std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
        va_end(arguments);

        if (length < 0 || static_cast<std::size_t>(length) >= buffer.size()) {
            last_error = -1;
            return false;
        }

        std::size_t written = 0;
        while (written < static_cast<std::size_t>(length)) {
            const int result = sceIoWrite(file, buffer.data() + written,
                                          static_cast<SceSize>(length - written));
            if (result <= 0) {
                last_error = result < 0 ? result : -1;
                return false;
            }
            written += static_cast<std::size_t>(result);
        }
        return true;
    }

    bool Close() {
        if (file < 0) {
            return true;
        }
        const int result = sceIoClose(file);
        file = -1;
        last_error = result;
        return result >= 0;
    }

    int LastError() const {
        return last_error;
    }

    ~ProbeLog() {
        Close();
    }

private:
    SceUID file{-1};
    int last_error{};
};

class Framebuffer {
public:
    int Allocate() {
        uid = sceKernelAllocMemBlock("azahar-vita-framebuffer",
                                     SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                     FramebufferAllocationBytes, nullptr);
        if (uid < 0) {
            return uid;
        }
        return sceKernelGetMemBlockBase(uid, reinterpret_cast<void**>(&pixels));
    }

    void DrawBands(std::span<const std::uint32_t, 6> colors) const {
        for (int y = 0; y < ScreenHeight; ++y) {
            const auto color = colors[std::min(static_cast<std::size_t>(y * colors.size() /
                                                                        ScreenHeight),
                                                colors.size() - 1)];
            std::fill_n(pixels + y * ScreenPitch, ScreenWidth, color);
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

bool Record(ProbeLog& log, const char* format, ...) {
    std::array<char, 256> buffer{};
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    if (length < 0 || static_cast<std::size_t>(length) >= buffer.size()) {
        return false;
    }
    return log.Write("%s", buffer.data());
}

int Fail(ProbeLog* log, Framebuffer* framebuffer, const char* stage, int error) {
    if (log != nullptr) {
        Record(*log, "FAIL %s code=0x%08X\n", stage, static_cast<std::uint32_t>(error));
        Record(*log, "RESULT FAIL\n");
    }

    if (framebuffer != nullptr && framebuffer->IsAllocated()) {
        framebuffer->DrawBands(FailureColors);
        if (!framebuffer->IsPresented()) {
            framebuffer->Present();
        }
        sceDisplayWaitVblankStart();
        sceKernelDelayThread(FailureDisplayTimeMicroseconds);
    }
    return 1;
}

} // namespace

int main() {
    ProbeLog log;
    const bool log_initialized = log.Initialize();

    Framebuffer framebuffer;
    int result = framebuffer.Allocate();
    if (result < 0) {
        return Fail(log_initialized ? &log : nullptr, nullptr, "cdram_allocate", result);
    }

    framebuffer.DrawBands(SuccessColors);
    result = framebuffer.Present();
    if (result < 0) {
        return Fail(log_initialized ? &log : nullptr, &framebuffer, "display_present", result);
    }
    result = sceDisplayWaitVblankStart();
    if (result < 0) {
        return Fail(log_initialized ? &log : nullptr, &framebuffer, "display_vblank", result);
    }

    if (!log_initialized) {
        return Fail(nullptr, &framebuffer, "log_open", log.LastError());
    }
    if (!Record(log, "probe_version=%s\n", ProbeVersion) ||
        !Record(log, "PASS log path=%s\n", LogPath) ||
        !Record(log, "PASS cxx20 standard=%ld pointer_bits=%u pattern_checksum=0x%08X\n",
                static_cast<long>(__cplusplus), static_cast<unsigned int>(sizeof(void*) * 8),
                PatternChecksum(SuccessColors)) ||
        !Record(log, "PASS cdram bytes=%d alignment=%d\n", FramebufferAllocationBytes,
                CdramBlockAlignment) ||
        !Record(log, "PASS display width=%d height=%d pitch=%d\n", ScreenWidth, ScreenHeight,
                ScreenPitch)) {
        return Fail(&log, &framebuffer, "log_write", log.LastError());
    }

#if defined(AZAHAR_VITA_PROBE_FORCE_FAILURE)
    return Fail(&log, &framebuffer, "forced_failure", static_cast<int>(0xA0000001U));
#endif

    result = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    if (result < 0) {
        return Fail(&log, &framebuffer, "controller_mode", result);
    }
    if (!Record(log, "PASS controller mode=analog previous_mode=%d\n", result)) {
        return Fail(&log, &framebuffer, "log_write", log.LastError());
    }

    while (true) {
        SceCtrlData controls{};
        result = sceCtrlPeekBufferPositive(0, &controls, 1);
        if (result != 1) {
            return Fail(&log, &framebuffer, "controller_read", result != 0 ? result : -1);
        }
        if ((controls.buttons & SCE_CTRL_START) != 0) {
            break;
        }
        result = sceDisplayWaitVblankStart();
        if (result < 0) {
            return Fail(&log, &framebuffer, "display_vblank", result);
        }
    }

    if (!Record(log, "PASS controller input=start\n") ||
        !Record(log, "PASS exit reason=start\n")) {
        return Fail(&log, &framebuffer, "log_write", log.LastError());
    }

    result = framebuffer.Shutdown();
    if (result < 0) {
        return Fail(&log, nullptr, "framebuffer_shutdown", result);
    }
    if (!Record(log, "PASS cleanup framebuffer=released\n") || !Record(log, "RESULT PASS\n")) {
        return Fail(&log, nullptr, "log_write", log.LastError());
    }
    return log.Close() ? 0 : 1;
}
