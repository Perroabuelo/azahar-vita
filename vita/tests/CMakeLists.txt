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
