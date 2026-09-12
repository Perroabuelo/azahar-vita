cmake_minimum_required(VERSION 3.16)

project(azahar_dyncom_host_probe LANGUAGES C CXX)

get_filename_component(AZAHAR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${AZAHAR_ROOT}/externals/fmt" "${CMAKE_CURRENT_BINARY_DIR}/fmt"
                 EXCLUDE_FROM_ALL)

add_executable(azahar_dyncom_host_probe
    "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_dec.cpp"
    "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_interpreter.cpp"
    "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_thumb.cpp"
    "${AZAHAR_ROOT}/src/core/arm/dyncom/arm_dyncom_trans.cpp"
    "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armstate.cpp"
    "${AZAHAR_ROOT}/src/core/arm/skyeye_common/armsupp.cpp"
    "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfp.cpp"
    "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpdouble.cpp"
    "${AZAHAR_ROOT}/src/core/arm/skyeye_common/vfp/vfpsingle.cpp"
    "${AZAHAR_ROOT}/vita/src/dyncom_corpus.cpp"
    "${AZAHAR_ROOT}/vita/src/dyncom_host_logging.cpp"
    "${AZAHAR_ROOT}/vita/src/dyncom_host_main.cpp"
)
target_compile_features(azahar_dyncom_host_probe PRIVATE cxx_std_20)
target_compile_definitions(azahar_dyncom_host_probe PRIVATE
    FMT_HEADER_ONLY=1
    MICROPROFILE_ENABLED=0
)
target_compile_options(azahar_dyncom_host_probe PRIVATE -Wall -Wextra)
target_include_directories(azahar_dyncom_host_probe PRIVATE
    "${AZAHAR_ROOT}/src"
    "${AZAHAR_ROOT}/externals/boost"
    "${AZAHAR_ROOT}/externals/microprofile"
    "${AZAHAR_ROOT}/vita/src"
)
target_link_libraries(azahar_dyncom_host_probe PRIVATE fmt::fmt-header-only)

# Milestone 3: loader + memory + kernel HLE host reference.
#
# common/settings.h needs the generated common/setting_keys.h; SettingKeysList.cmake (shared with
# the root build via GenerateSettingKeys.cmake) only populates the key-list variables, so this
# configure_file call and its output directory are ours to control.
include("${AZAHAR_ROOT}/CMakeModules/SettingKeysList.cmake")
string(LENGTH "${SETTING_KEY_LIST}" AZAHAR_SETTING_KEY_LIST_LENGTH)
math(EXPR AZAHAR_SETTING_KEY_LIST_NEW_LENGTH "${AZAHAR_SETTING_KEY_LIST_LENGTH} - 1")
string(SUBSTRING "${SETTING_KEY_LIST}" 0 ${AZAHAR_SETTING_KEY_LIST_NEW_LENGTH} SETTING_KEY_LIST)
configure_file("${AZAHAR_ROOT}/src/common/setting_keys.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/common/setting_keys.h" @ONLY)

add_executable(azahar_system_host_probe
    "${AZAHAR_ROOT}/src/common/error.cpp"
    "${AZAHAR_ROOT}/src/common/file_util.cpp"
    "${AZAHAR_ROOT}/src/common/settings.cpp"
    "${AZAHAR_ROOT}/src/common/string_util.cpp"
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
    "${AZAHAR_ROOT}/vita/src/dyncom_host_logging.cpp"
    "${AZAHAR_ROOT}/vita/src/system_corpus.cpp"
    "${AZAHAR_ROOT}/vita/src/system_host_main.cpp"
)
target_compile_features(azahar_system_host_probe PRIVATE cxx_std_20)
target_compile_definitions(azahar_system_host_probe PRIVATE
    AZAHAR_VITA=1
    FMT_HEADER_ONLY=1
    MICROPROFILE_ENABLED=0
    ENABLE_SOFTWARE_RENDERER=1
)
target_compile_options(azahar_system_host_probe PRIVATE -Wall -Wextra)
target_include_directories(azahar_system_host_probe PRIVATE
    "${AZAHAR_ROOT}/src"
    "${AZAHAR_ROOT}/externals/boost"
    "${AZAHAR_ROOT}/externals/microprofile"
    "${AZAHAR_ROOT}/vita/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(azahar_system_host_probe PRIVATE fmt::fmt-header-only pthread)
