cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
            CACHE PATH "VitaSDK toolchain file")
    else()
        message(FATAL_ERROR "Set VITASDK before configuring the Vita probe")
    endif()
endif()

project(azahar_vita_probe LANGUAGES C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

get_filename_component(AZAHAR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(AZAHAR_VITA_PROBE_FORCE_FAILURE "Show and log the hardware probe failure path" OFF)
option(AZAHAR_VITA_BUILD_HARDWARE_PROBE "Build the milestone 0 hardware probe" OFF)
option(AZAHAR_VITA_COMMON_FORCE_FAILURE "Show and log the common probe failure path" OFF)
option(AZAHAR_VITA_BUILD_DYNCOM_PROBE "Build the milestone 2 DynCom probe" OFF)
option(AZAHAR_VITA_DYNCOM_FORCE_FAILURE "Show and log the DynCom probe failure path" OFF)
option(AZAHAR_VITA_BUILD_SYSTEM_PROBE "Build the milestone 3 loader/memory/kernel probe" OFF)
option(AZAHAR_VITA_SYSTEM_FORCE_FAILURE "Show and log the system probe failure path" OFF)
option(AZAHAR_VITA_BUILD_BUDGET_PROBE "Build the milestone 4 memory budget probe" OFF)
option(AZAHAR_VITA_BUDGET_FORCE_FAILURE "Show and log the budget probe failure path" OFF)
option(AZAHAR_VITA_BUILD_RENDER_PROBE "Build the milestone 5 software renderer probe" OFF)
option(AZAHAR_VITA_RENDER_FORCE_FAILURE "Show and log the render probe failure path" OFF)

set(AZAHAR_VITA_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -O2
    -ffunction-sections
    -fdata-sections
)

if(AZAHAR_VITA_BUILD_HARDWARE_PROBE)
    add_executable(azahar_vita_probe src/main.cpp)
    target_compile_features(azahar_vita_probe PRIVATE cxx_std_20)
    target_compile_options(azahar_vita_probe PRIVATE ${AZAHAR_VITA_WARNINGS})
    if(AZAHAR_VITA_PROBE_FORCE_FAILURE)
        target_compile_definitions(azahar_vita_probe PRIVATE AZAHAR_VITA_PROBE_FORCE_FAILURE=1)
    endif()
    target_link_libraries(azahar_vita_probe PRIVATE
        SceCtrl_stub
        SceDisplay_stub
        SceIofilemgr_stub
        SceKernelThreadMgr_stub
        SceSysmem_stub
    )

    vita_create_self(eboot.bin azahar_vita_probe)
    vita_create_vpk(azahar_vita_probe.vpk AZHV00001 eboot.bin
        VERSION 00.02
        NAME "Azahar Vita Probe"
    )
    return()
endif()

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${AZAHAR_ROOT}/externals/fmt" "${CMAKE_CURRENT_BINARY_DIR}/fmt" EXCLUDE_FROM_ALL)

add_library(citra_common STATIC
    "${AZAHAR_ROOT}/src/common/cityhash.cpp"
    "${AZAHAR_ROOT}/src/common/error.cpp"
    "${AZAHAR_ROOT}/src/common/file_util.cpp"
    "${AZAHAR_ROOT}/src/common/logging/backend_vita.cpp"
    "${AZAHAR_ROOT}/src/common/logging/filter.cpp"
    "${AZAHAR_ROOT}/src/common/logging/text_formatter.cpp"
    "${AZAHAR_ROOT}/src/common/math_util.cpp"
    "${AZAHAR_ROOT}/src/common/memory_detect.cpp"
    "${AZAHAR_ROOT}/src/common/param_package.cpp"
    "${AZAHAR_ROOT}/src/common/string_util.cpp"
    "${AZAHAR_ROOT}/src/common/thread.cpp"
    "${AZAHAR_ROOT}/src/common/timer.cpp"
)
target_compile_features(citra_common PUBLIC cxx_std_20)
target_compile_definitions(citra_common PUBLIC
    AZAHAR_VITA=1
    BOOST_ASIO_DISABLE_CONCEPTS
)
target_compile_options(citra_common PRIVATE ${AZAHAR_VITA_WARNINGS})
target_include_directories(citra_common PUBLIC
    "${AZAHAR_ROOT}/src"
    "${AZAHAR_ROOT}/externals/boost"
)
target_link_libraries(citra_common PUBLIC fmt::fmt-header-only)
target_link_libraries(citra_common PRIVATE pthread)

if(AZAHAR_VITA_BUILD_DYNCOM_PROBE)
    add_library(citra_dyncom_probe STATIC
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_dec.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_interpreter.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_thumb.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_trans.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armstate.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armsupp.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfp.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpdouble.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpsingle.cpp"
    )
    target_compile_features(citra_dyncom_probe PUBLIC cxx_std_20)
    target_compile_definitions(citra_dyncom_probe PUBLIC
        AZAHAR_VITA=1
        MICROPROFILE_ENABLED=0
        TRANS_CACHE_SIZE=2097152
    )
    target_compile_options(citra_dyncom_probe PRIVATE
        -Wall
        -Os
        -ffunction-sections
        -fdata-sections
    )
    target_include_directories(citra_dyncom_probe PUBLIC
        "${AZAHAR_ROOT}/src"
        "${AZAHAR_ROOT}/externals/boost"
        "${AZAHAR_ROOT}/externals/microprofile"
    )
    target_link_libraries(citra_dyncom_probe PUBLIC citra_common)

    add_executable(azahar_vita_dyncom_probe
        src/dyncom_corpus.cpp
        src/dyncom_main.cpp
    )
    target_compile_features(azahar_vita_dyncom_probe PRIVATE cxx_std_20)
    target_compile_options(azahar_vita_dyncom_probe PRIVATE ${AZAHAR_VITA_WARNINGS} -Os)
    target_link_options(azahar_vita_dyncom_probe PRIVATE
        "-Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/azahar_vita_dyncom_probe.map"
    )
    if(AZAHAR_VITA_DYNCOM_FORCE_FAILURE)
        target_compile_definitions(azahar_vita_dyncom_probe PRIVATE
            AZAHAR_VITA_DYNCOM_FORCE_FAILURE=1)
    endif()
    target_link_libraries(azahar_vita_dyncom_probe PRIVATE
        citra_dyncom_probe
        SceCtrl_stub
        SceDisplay_stub
        SceIofilemgr_stub
        SceKernelThreadMgr_stub
        SceSysmem_stub
    )

    vita_create_self(eboot.bin azahar_vita_dyncom_probe)
    vita_create_vpk(azahar_vita_dyncom_probe.vpk AZHV00003 eboot.bin
        VERSION 00.01
        NAME "Azahar Vita DynCom Probe"
    )
    return()
endif()

if(AZAHAR_VITA_BUILD_SYSTEM_PROBE)
    # common/settings.h needs the generated common/setting_keys.h. SettingKeysList.cmake (shared
    # with the root build via CMakeModules/GenerateSettingKeys.cmake, and with the desktop
    # reference via vita/tests/CMakeLists.txt) only populates the key-list variables, so this
    # configure_file call and its output directory are ours to control.
    include("${AZAHAR_ROOT}/CMakeModules/SettingKeysList.cmake")
    string(LENGTH "${SETTING_KEY_LIST}" AZAHAR_SETTING_KEY_LIST_LENGTH)
    math(EXPR AZAHAR_SETTING_KEY_LIST_NEW_LENGTH "${AZAHAR_SETTING_KEY_LIST_LENGTH} - 1")
    string(SUBSTRING "${SETTING_KEY_LIST}" 0 ${AZAHAR_SETTING_KEY_LIST_NEW_LENGTH} SETTING_KEY_LIST)
    configure_file("${AZAHAR_ROOT}/src/common/setting_keys.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/common/setting_keys.h" @ONLY)

    add_library(citra_system_probe STATIC
        "${AZAHAR_ROOT}/src/common/settings.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_dec.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_interpreter.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_thumb.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_trans.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armstate.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armsupp.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfp.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpdouble.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpsingle.cpp"
        "${AZAHAR_ROOT}/src/core/core_timing.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/address_arbiter.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/client_port.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/client_session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/config_mem.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/event.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/handle_table.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/hle_ipc.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/ipc.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/ipc_debugger/recorder.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/kernel.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/memory.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/mutex.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/object.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/process.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/resource_limit.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/semaphore.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/server_port.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/server_session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/shared_memory.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/shared_page.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/thread.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/timer.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/vm_manager.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/wait_object.cpp"
        "${AZAHAR_ROOT}/src/core/loader/3dsx_image.cpp"
        "${AZAHAR_ROOT}/src/core/memory.cpp"
    )
    target_compile_features(citra_system_probe PUBLIC cxx_std_20)
    target_compile_definitions(citra_system_probe PUBLIC
        AZAHAR_VITA=1
        MICROPROFILE_ENABLED=0
        ENABLE_SOFTWARE_RENDERER=1
        TRANS_CACHE_SIZE=2097152
    )
    target_compile_options(citra_system_probe PRIVATE
        -Wall
        -Os
        -ffunction-sections
        -fdata-sections
    )
    target_include_directories(citra_system_probe PUBLIC
        "${AZAHAR_ROOT}/src"
        "${AZAHAR_ROOT}/externals/boost"
        "${AZAHAR_ROOT}/externals/microprofile"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(citra_system_probe PUBLIC citra_common)

    add_executable(azahar_vita_system_probe
        src/system_corpus.cpp
        src/system_main.cpp
    )
    target_compile_features(azahar_vita_system_probe PRIVATE cxx_std_20)
    target_compile_options(azahar_vita_system_probe PRIVATE ${AZAHAR_VITA_WARNINGS} -Os)
    target_link_options(azahar_vita_system_probe PRIVATE
        "-Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/azahar_vita_system_probe.map"
    )
    if(AZAHAR_VITA_SYSTEM_FORCE_FAILURE)
        target_compile_definitions(azahar_vita_system_probe PRIVATE
            AZAHAR_VITA_SYSTEM_FORCE_FAILURE=1)
    endif()
    target_link_libraries(azahar_vita_system_probe PRIVATE
        citra_system_probe
        SceCtrl_stub
        SceDisplay_stub
        SceIofilemgr_stub
        SceKernelThreadMgr_stub
        SceSysmem_stub
    )

    vita_create_self(eboot.bin azahar_vita_system_probe)
    vita_create_vpk(azahar_vita_system_probe.vpk AZHV00004 eboot.bin
        VERSION 00.01
        NAME "Azahar Vita System Probe"
    )
    return()
endif()

if(AZAHAR_VITA_BUILD_BUDGET_PROBE)
    # Same generated common/setting_keys.h dance as the milestone 3 block above (see its comment);
    # this block has its own AZAHAR_ROOT-relative output directory, so it is repeated rather than
    # shared across the early-return blocks.
    include("${AZAHAR_ROOT}/CMakeModules/SettingKeysList.cmake")
    string(LENGTH "${SETTING_KEY_LIST}" AZAHAR_SETTING_KEY_LIST_LENGTH)
    math(EXPR AZAHAR_SETTING_KEY_LIST_NEW_LENGTH "${AZAHAR_SETTING_KEY_LIST_LENGTH} - 1")
    string(SUBSTRING "${SETTING_KEY_LIST}" 0 ${AZAHAR_SETTING_KEY_LIST_NEW_LENGTH} SETTING_KEY_LIST)
    configure_file("${AZAHAR_ROOT}/src/common/setting_keys.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/common/setting_keys.h" @ONLY)

    # Same core/ source list as citra_system_probe above: the budget corpus's load_release_cycles
    # group repeats the milestone 3 loader/memory/kernel sequence, so it needs everything that does.
    add_library(citra_budget_probe STATIC
        "${AZAHAR_ROOT}/src/common/settings.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_dec.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_interpreter.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_thumb.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_trans.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armstate.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armsupp.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfp.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpdouble.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpsingle.cpp"
        "${AZAHAR_ROOT}/src/core/core_timing.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/address_arbiter.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/client_port.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/client_session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/config_mem.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/event.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/handle_table.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/hle_ipc.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/ipc.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/ipc_debugger/recorder.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/kernel.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/memory.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/mutex.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/object.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/process.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/resource_limit.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/semaphore.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/server_port.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/server_session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/shared_memory.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/shared_page.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/thread.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/timer.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/vm_manager.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/wait_object.cpp"
        "${AZAHAR_ROOT}/src/core/loader/3dsx_image.cpp"
        "${AZAHAR_ROOT}/src/core/memory.cpp"
    )
    target_compile_features(citra_budget_probe PUBLIC cxx_std_20)
    target_compile_definitions(citra_budget_probe PUBLIC
        AZAHAR_VITA=1
        MICROPROFILE_ENABLED=0
        ENABLE_SOFTWARE_RENDERER=1
        TRANS_CACHE_SIZE=2097152
    )
    target_compile_options(citra_budget_probe PRIVATE
        -Wall
        -Os
        -ffunction-sections
        -fdata-sections
    )
    target_include_directories(citra_budget_probe PUBLIC
        "${AZAHAR_ROOT}/src"
        "${AZAHAR_ROOT}/externals/boost"
        "${AZAHAR_ROOT}/externals/microprofile"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(citra_budget_probe PUBLIC citra_common)

    add_executable(azahar_vita_budget_probe
        src/budget_corpus.cpp
        src/budget_main.cpp
        src/system_corpus.cpp
    )
    target_compile_features(azahar_vita_budget_probe PRIVATE cxx_std_20)
    target_compile_options(azahar_vita_budget_probe PRIVATE ${AZAHAR_VITA_WARNINGS} -Os)
    target_link_options(azahar_vita_budget_probe PRIVATE
        "-Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/azahar_vita_budget_probe.map"
    )
    if(AZAHAR_VITA_BUDGET_FORCE_FAILURE)
        target_compile_definitions(azahar_vita_budget_probe PRIVATE
            AZAHAR_VITA_BUDGET_FORCE_FAILURE=1)
    endif()
    target_link_libraries(azahar_vita_budget_probe PRIVATE
        citra_budget_probe
        SceCtrl_stub
        SceDisplay_stub
        SceIofilemgr_stub
        SceKernelThreadMgr_stub
        SceSysmem_stub
    )

    vita_create_self(eboot.bin azahar_vita_budget_probe)
    vita_create_vpk(azahar_vita_budget_probe.vpk AZHV00005 eboot.bin
        VERSION 00.01
        NAME "Azahar Vita Memory Probe"
    )
    return()
endif()

if(AZAHAR_VITA_BUILD_RENDER_PROBE)
    # Same generated common/setting_keys.h dance as the milestone 3/4 blocks above (see their
    # comment); this block has its own AZAHAR_ROOT-relative output directory, so it is repeated
    # rather than shared across the early-return blocks.
    include("${AZAHAR_ROOT}/CMakeModules/SettingKeysList.cmake")
    string(LENGTH "${SETTING_KEY_LIST}" AZAHAR_SETTING_KEY_LIST_LENGTH)
    math(EXPR AZAHAR_SETTING_KEY_LIST_NEW_LENGTH "${AZAHAR_SETTING_KEY_LIST_LENGTH} - 1")
    string(SUBSTRING "${SETTING_KEY_LIST}" 0 ${AZAHAR_SETTING_KEY_LIST_NEW_LENGTH} SETTING_KEY_LIST)
    configure_file("${AZAHAR_ROOT}/src/common/setting_keys.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/common/setting_keys.h" @ONLY)

    # video_core/pica/shader_setup.h pulls in common/hash.h, which needs xxHash - the same target
    # the root build's citra_common links (src/common/CMakeLists.txt).
    add_subdirectory("${AZAHAR_ROOT}/externals/xxHash/cmake_unofficial"
                     "${CMAKE_CURRENT_BINARY_DIR}/xxHash" EXCLUDE_FROM_ALL)

    # Same core/ source list as citra_system_probe/citra_budget_probe above: the render corpus's
    # guest_frame group repeats the milestone 3 loader/memory/kernel sequence, so it needs everything
    # that does. Adds the software-renderer slice of video_core that ENABLE_SOFTWARE_RENDERER selects
    # in src/video_core/CMakeLists.txt: PicaCore, the geometry/vertex-loading support it drives, the
    # software rasterizer/framebuffer/blitter, the shader interpreter (the PICA vertex shader JIT is
    # x86-64/arm64-only - see src/video_core/shader/shader.cpp - so this ARMv7 build always uses the
    # interpreter and never needs xbyak or the JIT compiler files at all), and texture decoding.
    # video_core.cpp, gpu.cpp, the accelerated rasterizer/cache and the shader generator are
    # deliberately not part of this list - see docs/vita-port.md.
    add_library(citra_render_probe STATIC
        "${AZAHAR_ROOT}/src/common/settings.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_dec.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_interpreter.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_thumb.cpp"
        "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_trans.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armstate.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armsupp.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfp.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpdouble.cpp"
        "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpsingle.cpp"
        "${AZAHAR_ROOT}/src/core/core_timing.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/address_arbiter.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/client_port.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/client_session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/config_mem.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/event.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/handle_table.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/hle_ipc.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/ipc.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/ipc_debugger/recorder.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/kernel.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/memory.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/mutex.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/object.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/process.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/resource_limit.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/semaphore.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/server_port.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/server_session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/session.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/shared_memory.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/shared_page.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/thread.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/timer.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/vm_manager.cpp"
        "${AZAHAR_ROOT}/src/core/hle/kernel/wait_object.cpp"
        "${AZAHAR_ROOT}/src/core/loader/3dsx_image.cpp"
        "${AZAHAR_ROOT}/src/core/memory.cpp"
        "${AZAHAR_ROOT}/src/video_core/debug_utils/debug_utils.cpp"
        "${AZAHAR_ROOT}/src/video_core/pica/geometry_pipeline.cpp"
        "${AZAHAR_ROOT}/src/video_core/pica/output_vertex.cpp"
        "${AZAHAR_ROOT}/src/video_core/pica/pica_core.cpp"
        "${AZAHAR_ROOT}/src/video_core/pica/primitive_assembly.cpp"
        "${AZAHAR_ROOT}/src/video_core/pica/regs_internal.cpp"
        "${AZAHAR_ROOT}/src/video_core/pica/shader_setup.cpp"
        "${AZAHAR_ROOT}/src/video_core/pica/shader_unit.cpp"
        "${AZAHAR_ROOT}/src/video_core/pica/vertex_loader.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_base.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_software/renderer_software.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_software/sw_blitter.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_software/sw_clipper.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_software/sw_framebuffer.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_software/sw_lighting.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_software/sw_proctex.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_software/sw_rasterizer.cpp"
        "${AZAHAR_ROOT}/src/video_core/renderer_software/sw_texturing.cpp"
        "${AZAHAR_ROOT}/src/video_core/shader/shader.cpp"
        "${AZAHAR_ROOT}/src/video_core/shader/shader_interpreter.cpp"
        "${AZAHAR_ROOT}/src/video_core/texture/etc1.cpp"
        "${AZAHAR_ROOT}/src/video_core/texture/texture_decode.cpp"
    )
    target_compile_features(citra_render_probe PUBLIC cxx_std_20)
    target_compile_definitions(citra_render_probe PUBLIC
        AZAHAR_VITA=1
        MICROPROFILE_ENABLED=0
        ENABLE_SOFTWARE_RENDERER=1
        TRANS_CACHE_SIZE=2097152
    )
    target_compile_options(citra_render_probe PRIVATE
        -Wall
        -Os
        -ffunction-sections
        -fdata-sections
    )
    # video_core's plain (non-scoped) register enums, e.g. regs_shader.h's ShaderMode, have no
    # explicit underlying type; ARM EABI's default is to size such an enum to the smallest type that
    # fits its values (here, a single byte), while every other target this corpus's signatures are
    # compared against (the x86-64 desktop reference) sizes it as int. That mismatch fails
    # Common::BitField's own static_assert (bits + position <= 8 * sizeof(T)) the moment such an enum
    # is used as a BitField's value type. PUBLIC so it also applies to azahar_vita_render_probe's own
    # translation units below, which include these same headers directly.
    target_compile_options(citra_render_probe PUBLIC -fno-short-enums)
    target_include_directories(citra_render_probe PUBLIC
        "${AZAHAR_ROOT}/src"
        "${AZAHAR_ROOT}/externals/boost"
        "${AZAHAR_ROOT}/externals/microprofile"
        "${AZAHAR_ROOT}/externals/nihstro/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(citra_render_probe PUBLIC citra_common xxHash::xxhash)

    add_executable(azahar_vita_render_probe
        src/render_corpus.cpp
        src/render_main.cpp
    )
    target_compile_features(azahar_vita_render_probe PRIVATE cxx_std_20)
    # Unlike every earlier milestone's probe executable, render_corpus.cpp/render_main.cpp include
    # video_core and nihstro headers directly (Pica::RegsInternal, nihstro::InlineAsm, ...), not just
    # this probe's own small corpus header - so ${AZAHAR_VITA_WARNINGS}'s -Wpedantic -Werror would
    # fail on upstream code this milestone did not write (anonymous structs, unused virtual-stub
    # parameters, nihstro's own sign-compare/strict-aliasing warnings), exactly why citra_render_probe
    # above already uses the laxer set instead of ${AZAHAR_VITA_WARNINGS}.
    target_compile_options(azahar_vita_render_probe PRIVATE -Wall -Os -ffunction-sections
                           -fdata-sections)
    target_link_options(azahar_vita_render_probe PRIVATE
        "-Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/azahar_vita_render_probe.map"
    )
    if(AZAHAR_VITA_RENDER_FORCE_FAILURE)
        target_compile_definitions(azahar_vita_render_probe PRIVATE
            AZAHAR_VITA_RENDER_FORCE_FAILURE=1)
    endif()
    target_link_libraries(azahar_vita_render_probe PRIVATE
        citra_render_probe
        SceCtrl_stub
        SceDisplay_stub
        SceIofilemgr_stub
        SceKernelThreadMgr_stub
        SceSysmem_stub
    )

    vita_create_self(eboot.bin azahar_vita_render_probe)
    vita_create_vpk(azahar_vita_render_probe.vpk AZHV00006 eboot.bin
        VERSION 00.01
        NAME "Azahar Vita Render Probe"
    )
    return()
endif()

add_executable(azahar_vita_common_probe src/common_main.cpp)
target_compile_features(azahar_vita_common_probe PRIVATE cxx_std_20)
target_compile_options(azahar_vita_common_probe PRIVATE ${AZAHAR_VITA_WARNINGS})
target_link_options(azahar_vita_common_probe PRIVATE
    "-Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/azahar_vita_common_probe.map"
)
if(AZAHAR_VITA_COMMON_FORCE_FAILURE)
    target_compile_definitions(azahar_vita_common_probe PRIVATE AZAHAR_VITA_COMMON_FORCE_FAILURE=1)
endif()
target_link_libraries(azahar_vita_common_probe PRIVATE
    citra_common
    SceCtrl_stub
    SceDisplay_stub
    SceIofilemgr_stub
    SceKernelThreadMgr_stub
    SceSysmem_stub
)

vita_create_self(eboot.bin azahar_vita_common_probe)
vita_create_vpk(azahar_vita_common_probe.vpk AZHV00002 eboot.bin
    VERSION 00.01
    NAME "Azahar Vita Common Probe"
)
