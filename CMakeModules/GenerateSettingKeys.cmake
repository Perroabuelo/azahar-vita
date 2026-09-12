## Populates SETTING_KEY_LIST/SETTING_KEY_DEFINITIONS/JNI_SETTING_KEY_DEFINITIONS (see
## SettingKeysList.cmake for the actual key list) and configures the generated headers for the
## root build. Included from src/CMakeLists.txt, where CMAKE_CURRENT_SOURCE_DIR/_BINARY_DIR are
## src/ and <build>/src respectively.

include("${CMAKE_CURRENT_LIST_DIR}/SettingKeysList.cmake")

# Trim trailing comma and newline from SETTING_KEY_LIST
string(LENGTH "${SETTING_KEY_LIST}" SETTING_KEY_LIST_LENGTH)
math(EXPR SETTING_KEY_LIST_NEW_LENGTH "${SETTING_KEY_LIST_LENGTH} - 1")
string(SUBSTRING "${SETTING_KEY_LIST}" 0 ${SETTING_KEY_LIST_NEW_LENGTH} SETTING_KEY_LIST)

# Configure files
configure_file("common/setting_keys.h.in" "common/setting_keys.h" @ONLY)
if (ENABLE_QT)
    configure_file("citra_qt/setting_qkeys.h.in" "citra_qt/setting_qkeys.h" @ONLY)
endif()
if (ANDROID AND NOT ENABLE_LIBRETRO)
    configure_file("android/app/src/main/jni/jni_setting_keys.cpp.in" "android/app/src/main/jni/jni_setting_keys.cpp" @ONLY)
endif()
