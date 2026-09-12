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
