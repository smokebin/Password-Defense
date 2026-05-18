cmake_minimum_required(VERSION 3.20)
project(PasswordDefense LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================
# Platform detection
# ==============================================================
if(WIN32)
    set(PD_PLATFORM "win32")
elseif(APPLE)
    enable_language(OBJCXX)
    set(PD_PLATFORM "macos")
    set(CMAKE_OBJCXX_STANDARD 20)
else()
    set(PD_PLATFORM "linux")
endif()

# ==============================================================
# Source files — cross-platform core
# ==============================================================
set(CORE_SOURCES
    application.cpp
    app_import_export.cpp
    app_sharing.cpp
    app_display_helpers.cpp

    # UI
    UI.cpp
    ui_widgets.cpp
    ui_views.cpp
    ui_views_threepane.cpp
    ui_views_table.cpp
    ui_controls.cpp
    ui_modals.cpp

    # Credentials
    credentials/vault_db.cpp
    credentials/credential_ops.cpp
    credentials/twofa_ops.cpp
    credentials/crypto/aes_cipher.cpp
    credentials/crypto/vault_crypto.cpp
    credentials/transfer/pwm_file.cpp
    credentials/transfer/kdbx_export.cpp

    # Tools
    tools/save.cpp
    tools/utility.cpp
    tools/totp.cpp
    tools/qrcodegen.cpp

    # Web / Sync
    web/auth_crypto.cpp
    web/sync_manager.cpp
    web/local_server.cpp
)

# ImGui core (platform-independent)
set(IMGUI_CORE_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_stdlib.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
)

# ==============================================================
# Platform-specific sources
# ==============================================================
if(PD_PLATFORM STREQUAL "win32")
    set(PLATFORM_SOURCES
        main.cpp
        GUI.cpp
        favicon.cpp
        third_party/imgui/imgui_impl_dx11.cpp
        third_party/imgui/imgui_impl_win32.cpp
        resources.rc
    )
elseif(PD_PLATFORM STREQUAL "macos")
    set(PLATFORM_SOURCES
        # TODO: Phase 2 — add these as they are created
        # platform/main_mac.mm
        # platform/GUI_mac.mm
        # platform/favicon_mac.mm
        # third_party/imgui/imgui_impl_metal.mm
        # third_party/imgui/imgui_impl_osx.mm
    )
endif()

# ==============================================================
# SQLite3 (compiled from amalgamation)
# ==============================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/../../../libs/sqlite3/sqlite3.c")
    set(SQLITE3_SOURCE "${CMAKE_SOURCE_DIR}/../../../libs/sqlite3/sqlite3.c")
    set(SQLITE3_INCLUDE "${CMAKE_SOURCE_DIR}/../../../libs/sqlite3/include")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c")
    set(SQLITE3_SOURCE "${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c")
    set(SQLITE3_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/sqlite3")
else()
    message(FATAL_ERROR "sqlite3.c not found. Set SQLITE3_SOURCE manually.")
endif()

add_library(sqlite3 STATIC ${SQLITE3_SOURCE})
target_include_directories(sqlite3 PUBLIC ${SQLITE3_INCLUDE})
set_target_properties(sqlite3 PROPERTIES C_STANDARD 11)

# ==============================================================
# libsodium
# ==============================================================
if(PD_PLATFORM STREQUAL "win32")
    # Static lib from prebuilt (adjust paths as needed)
    set(SODIUM_ROOT "${CMAKE_SOURCE_DIR}/../../../libs/libsodium" CACHE PATH "libsodium root")
    set(SODIUM_INCLUDE "${SODIUM_ROOT}/include")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SODIUM_LIB "${SODIUM_ROOT}/x64/Release/v143/static/libsodium.lib")
    else()
        set(SODIUM_LIB "${SODIUM_ROOT}/Win32/Release/v143/static/libsodium.lib")
    endif()

    add_library(sodium STATIC IMPORTED)
    set_target_properties(sodium PROPERTIES
        IMPORTED_LOCATION "${SODIUM_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE}"
    )
elseif(PD_PLATFORM STREQUAL "macos")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
endif()

# ==============================================================
# Executable
# ==============================================================
if(PD_PLATFORM STREQUAL "win32")
    add_executable(PasswordDefense WIN32
        ${CORE_SOURCES}
        ${IMGUI_CORE_SOURCES}
        ${PLATFORM_SOURCES}
    )
elseif(PD_PLATFORM STREQUAL "macos")
    add_executable(PasswordDefense MACOSX_BUNDLE
        ${CORE_SOURCES}
        ${IMGUI_CORE_SOURCES}
        ${PLATFORM_SOURCES}
    )
    set_target_properties(PasswordDefense PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.passworddefense.app"
        MACOSX_BUNDLE_BUNDLE_NAME "Password Defense"
        MACOSX_BUNDLE_BUNDLE_VERSION "2.2.1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "2.2.1"
    )
endif()

# ==============================================================
# Include directories
# ==============================================================
target_include_directories(PasswordDefense PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/web
    ${CMAKE_SOURCE_DIR}/tools
    ${CMAKE_SOURCE_DIR}/icons
    ${CMAKE_SOURCE_DIR}/credentials
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/imgui
)

# ==============================================================
# Preprocessor defines
# ==============================================================
target_compile_definitions(PasswordDefense PRIVATE
    SODIUM_STATIC
    $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<CONFIG:Debug>:_DEBUG>
)

# ==============================================================
# Link libraries
# ==============================================================
target_link_libraries(PasswordDefense PRIVATE sqlite3)

if(PD_PLATFORM STREQUAL "win32")
    target_link_libraries(PasswordDefense PRIVATE
        sodium
        d3d11.lib
        dwmapi.lib
        winhttp.lib
        shell32.lib
        shlwapi.lib
    )
elseif(PD_PLATFORM STREQUAL "macos")
    # OpenSSL (for cpp-httplib HTTPS)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(PasswordDefense PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(PasswordDefense PRIVATE
        PkgConfig::SODIUM
        OpenSSL::SSL
        OpenSSL::Crypto
    )
    target_link_libraries(PasswordDefense PRIVATE
        "-framework Cocoa"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework IOKit"
        "-framework Security"
    )
endif()

# ==============================================================
# Compiler options
# ==============================================================
if(MSVC)
    target_compile_options(PasswordDefense PRIVATE
        /W3 /utf-8
        $<$<CONFIG:Release>:/O2 /GL /Gy>
    )
    target_link_options(PasswordDefense PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
    # Static CRT for Release
    set_property(TARGET PasswordDefense PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    set_property(TARGET sqlite3 PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
elseif(APPLE)
    target_compile_options(PasswordDefense PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        $<$<CONFIG:Release>:-O2>
    )
endif()
