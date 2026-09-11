#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
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

class Framebuffer {
public:
    bool Initialize() {
        uid = sceKernelAllocMemBlock("azahar-vita-framebuffer", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                     FramebufferAllocationBytes, nullptr);
        if (uid < 0 || sceKernelGetMemBlockBase(uid, reinterpret_cast<void**>(&pixels)) < 0) {
            return false;
        }

        SceDisplayFrameBuf frame{};
        frame.size = sizeof(frame);
        frame.base = pixels;
        frame.pitch = ScreenPitch;
        frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        frame.width = ScreenWidth;
        frame.height = ScreenHeight;
        return sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME) >= 0;
    }

    ~Framebuffer() {
        if (uid >= 0) {
            sceKernelFreeMemBlock(uid);
        }
    }

    void DrawProbePattern() const {
        constexpr std::array<std::uint32_t, 6> colors{
            0xFF202020, 0xFF2774E6, 0xFF2ECC71, 0xFFF1C40F, 0xFFE67E22, 0xFFE74C3C,
        };
        for (int y = 0; y < ScreenHeight; ++y) {
            const auto color = colors[std::min(static_cast<std::size_t>(y * colors.size() /
                                                                        ScreenHeight),
                                                colors.size() - 1)];
            std::fill_n(pixels + y * ScreenPitch, ScreenWidth, color);
        }
    }

private:
    SceUID uid{-1};
    std::uint32_t* pixels{};
};

void WriteBootLog(const char* message) {
    sceIoMkdir("ux0:data/azahar-vita", 0777);
    const auto file = sceIoOpen("ux0:data/azahar-vita/boot.log",
                                SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (file >= 0) {
        sceIoWrite(file, message, static_cast<SceSize>(std::strlen(message)));
        sceIoClose(file);
    }
}

} // namespace

int main() {
    WriteBootLog("Azahar Vita probe started\n");

    Framebuffer framebuffer;
    if (!framebuffer.Initialize()) {
        WriteBootLog("Framebuffer initialization failed\n");
        return 1;
    }
    framebuffer.DrawProbePattern();
    WriteBootLog("C++20 and CDRAM framebuffer initialized; press START to exit\n");

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    while (true) {
        SceCtrlData controls{};
        sceCtrlPeekBufferPositive(0, &controls, 1);
        if ((controls.buttons & SCE_CTRL_START) != 0) {
            break;
        }
        sceDisplayWaitVblankStart();
    }

    WriteBootLog("Azahar Vita probe exited cleanly\n");
    sceKernelExitProcess(0);
    return 0;
}
