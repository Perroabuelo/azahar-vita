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

set(VITA_APP_NAME "Azahar Vita Probe")
set(VITA_TITLEID "AZHV00001")
set(VITA_VERSION "00.01")

add_executable(${PROJECT_NAME} src/main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -O2
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    SceCtrl_stub
    SceDisplay_stub
    SceIofilemgr_stub
    SceKernelThreadMgr_stub
    SceSysmem_stub
)

vita_create_self(eboot.bin ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
)
