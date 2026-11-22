cmake_minimum_required(VERSION 3.25...4.4)

# Builds ffmpeg/ffprobe with vcpkg and ships them next to Mango. Linux can
# turn it off and use the system ffmpeg instead (FFmpegLocator finds it in
# PATH or ffmpegFolder). Must be decided before project(): with it off, vcpkg
# skips the manifest's default "bundled-ffmpeg" feature.
option(MANGO_BUNDLE_FFMPEG "Build ffmpeg with vcpkg and ship it next to Mango" ON)

if(NOT MANGO_BUNDLE_FFMPEG)
    if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "MANGO_BUNDLE_FFMPEG=OFF is for Linux only: macOS and Windows have no other ffmpeg.")
    endif()
    # Other default features of vcpkg.json, if any are added, must be
    # listed in VCPKG_MANIFEST_FEATURES.
    set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON CACHE BOOL "" FORCE)
endif()

project(Mango
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sources
file(GLOB_RECURSE CORE_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

# Platform-specific backends are named by suffix (see src/platform/):
#   *_Windows.cpp  Windows
#   *_Posix.cpp    macOS and Linux (code that is truly shared)
#   *_Apple.cpp    macOS
#   *_Linux.cpp    Linux
# Only the files for the target OS are compiled in. app/ uses the same.
function(mango_keep_platform_sources sources)
    set(kept ${${sources}})
    if(WIN32)
        list(FILTER kept EXCLUDE REGEX ".*_(Posix|Apple|Linux)\\.cpp$")
    elseif(APPLE)
        list(FILTER kept EXCLUDE REGEX ".*_(Windows|Linux)\\.cpp$")
    else()
        list(FILTER kept EXCLUDE REGEX ".*_(Windows|Apple)\\.cpp$")
    endif()
    set(${sources} ${kept} PARENT_SCOPE)
endfunction()

mango_keep_platform_sources(CORE_SOURCES)

# Everything behind Engine (configuration/, file/, pipeline/, server/, …)
add_library(MangoCore STATIC ${CORE_SOURCES})

# Shown in each stream log's header (see src/log/Log.cpp).
target_compile_definitions(MangoCore PRIVATE MANGO_ENGINE_VERSION="${PROJECT_VERSION}")

# Remote streaming's default signaling service (wss://…/v1), used when
# settings.json has no "remoteSignalingUrl". Empty: none, so remote streaming
# stays off until one is configured. Our own builds set it in
# private/MangoPrivate.cmake, which the public repository doesn't have.
if(EXISTS "${PROJECT_SOURCE_DIR}/private/MangoPrivate.cmake")
    include("${PROJECT_SOURCE_DIR}/private/MangoPrivate.cmake")
endif()
set(MANGO_SIGNALING_URL "" CACHE STRING "Default signaling service URL")
# Only RemoteCore.cpp reads it, so changing it rebuilds one file.
set_source_files_properties(src/remote/RemoteCore.cpp PROPERTIES
    COMPILE_DEFINITIONS "MANGO_SIGNALING_URL=\"${MANGO_SIGNALING_URL}\"")

# Executable
# Console server: the engine with terminal commands (see app/console/Console.hpp).
# On Linux also the control socket the service and later terminals share
# (app/control/, see app/LinuxMain.hpp).
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp)
mango_keep_platform_sources(APP_SOURCES)
add_executable(Mango ${APP_SOURCES})

target_link_libraries(Mango PRIVATE MangoCore)
# Includes are relative to app/ (console/…, commands/…, control/…), like src/.
target_include_directories(Mango PRIVATE ${PROJECT_SOURCE_DIR}/app)
# Sent to attached terminals, which warn when theirs differs.
target_compile_definitions(Mango PRIVATE MANGO_APP_VERSION="${PROJECT_VERSION}")

# Dependencies
find_package(glaze CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)
find_package(MbedTLS CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
# Remote streaming (src/remote/): WebRTC data channels and the signaling
# WebSocket. OpenSSL is its TLS backend and also gives src/remote/identity/Crypto
# Ed25519.
find_package(LibDataChannel CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# mdns is header-only and has no CMake config package.
find_path(MDNS_INCLUDE_DIRS "mdns.h" REQUIRED)

target_include_directories(MangoCore PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_include_directories(MangoCore SYSTEM PUBLIC
    ${MDNS_INCLUDE_DIRS}
)

target_link_libraries(MangoCore PUBLIC
    glaze::glaze
    BLAKE3::blake3
    MbedTLS::mbedtls
    httplib::httplib
    LibDataChannel::LibDataChannel
    OpenSSL::Crypto
    Threads::Threads
)

# Compiler warnings
# Select by command-line style, not compiler ID: clang-cl reports ID "Clang"
# but treats -Wall like MSVC's /Wall (= -Weverything).
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # MSVC and clang-cl: /W4 maps to -Wall -Wextra on clang-cl
    set(MANGO_WARNING_FLAGS /W4)
else()
    set(MANGO_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

target_compile_options(MangoCore PRIVATE ${MANGO_WARNING_FLAGS})
target_compile_options(Mango PRIVATE ${MANGO_WARNING_FLAGS})

# Linux: links the C++ runtime in (a few MB), so a build with GCC 14's
# libstdc++ (for <print>) also runs on distributions with an older one
# (Debian 12, Ubuntu 22.04). Off: the .deb requires libstdc++ 14, which
# Ubuntu 24.04+ and Debian 13+ have.
option(MANGO_STATIC_CXX_RUNTIME "Linux: link the C++ runtime statically" OFF)
if(MANGO_STATIC_CXX_RUNTIME AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(Mango PRIVATE -static-libstdc++ -static-libgcc)
endif()

#
# macOS app: the Xcode project links the engine through a generated response
# file (see cmake/XcodeLinkSettings.cmake).
#
if(APPLE)
    include(${PROJECT_SOURCE_DIR}/cmake/XcodeLinkSettings.cmake)
    mango_write_xcode_link_settings()
endif()

#
# Tests (off by default): cmake -DMANGO_BUILD_TESTS=ON, then ctest. Built
# next to Mango so the bundled ffmpeg is found the same way.
#
option(MANGO_BUILD_TESTS "Build the engine tests" OFF)

if(MANGO_BUILD_TESTS)
    enable_testing()

    add_executable(MangoTests tests/MangoTests.cpp tests/SessionTests.cpp tests/RemoteTests.cpp
        tests/ForwardingTests.cpp tests/TrackSelectionTests.cpp tests/FFmpegInstallTests.cpp
        tests/EncoderOptionTests.cpp)
    target_include_directories(MangoTests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_link_libraries(MangoTests PRIVATE MangoCore)

    # Linux: the control socket and the instance lock, without an engine.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(MangoTests PRIVATE tests/ControlTests_Linux.cpp
            app/control/ControlClient_Linux.cpp app/control/ControlServer_Linux.cpp app/control/ControlSocket_Linux.cpp
            app/InstanceLock_Linux.cpp app/console/ConsoleText.cpp app/console/Output.cpp)
        target_include_directories(MangoTests PRIVATE ${PROJECT_SOURCE_DIR}/app)
        target_compile_definitions(MangoTests PRIVATE MANGO_APP_VERSION="${PROJECT_VERSION}")
    endif()
    add_dependencies(MangoTests Mango)

    add_test(NAME MangoTests COMMAND MangoTests)
endif()

#
# MangoCoreInterop: flat C API DLL wrapping MangoCore for the C# app
# (Mango Streaming Server Win), which can't link a C++ static lib directly.
# See "Mango Streaming Server Win/MangoCoreInterop/MangoCoreInterop.h".
# Only where that folder exists: the public engine repository leaves the
# apps out (see scripts/export-public.sh).
#
if(WIN32 AND EXISTS "${PROJECT_SOURCE_DIR}/Mango Streaming Server Win/MangoCoreInterop/MangoCoreInterop.cpp")
    add_library(MangoCoreInterop SHARED
        "Mango Streaming Server Win/MangoCoreInterop/MangoCoreInterop.cpp"
        "Mango Streaming Server Win/MangoCoreInterop/CredentialStore.cpp"
    )

    target_include_directories(MangoCoreInterop PRIVATE
        "Mango Streaming Server Win/MangoCoreInterop"
    )

    target_compile_definitions(MangoCoreInterop PRIVATE MANGOCOREINTEROP_EXPORTS)

    # Advapi32: the Credential Manager (CredReadW / CredWriteW).
    target_link_libraries(MangoCoreInterop PRIVATE MangoCore Advapi32)
endif()


#
# Bundle FFmpeg tools
#
# Expected layout:
#
# Mango
# ├── Mango
# ├── ffmpeg
# └── ffprobe
#

if(MANGO_BUNDLE_FFMPEG AND DEFINED VCPKG_TARGET_TRIPLET)

    set(FFMPEG_EXE "ffmpeg${CMAKE_EXECUTABLE_SUFFIX}")
    set(FFPROBE_EXE "ffprobe${CMAKE_EXECUTABLE_SUFFIX}")

    set(VCPKG_TOOLS_FFMPEG_DIR
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg"
    )


    # Fallback when vcpkg directory is external
    if(NOT EXISTS "${VCPKG_TOOLS_FFMPEG_DIR}/${FFMPEG_EXE}")
        if(DEFINED _VCPKG_INSTALLED_DIR)

            set(VCPKG_TOOLS_FFMPEG_DIR
                "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg"
            )

        endif()
    endif()


    if(EXISTS "${VCPKG_TOOLS_FFMPEG_DIR}/${FFMPEG_EXE}")

        add_custom_command(
            TARGET Mango
            POST_BUILD

            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VCPKG_TOOLS_FFMPEG_DIR}/${FFMPEG_EXE}"
                "$<TARGET_FILE_DIR:Mango>/${FFMPEG_EXE}"

            COMMENT "Copying bundled ffmpeg..."
        )


        if(EXISTS "${VCPKG_TOOLS_FFMPEG_DIR}/${FFPROBE_EXE}")

            add_custom_command(
                TARGET Mango
                POST_BUILD

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VCPKG_TOOLS_FFMPEG_DIR}/${FFPROBE_EXE}"
                    "$<TARGET_FILE_DIR:Mango>/${FFPROBE_EXE}"

                COMMENT "Copying bundled ffprobe..."
            )

        endif()

        # ffmpeg's VAAPI encoder links libva, which only builds as a shared
        # library. Copy it next to ffmpeg, whose $ORIGIN rpath finds it there.
        set(MANGO_BUNDLED_FFMPEG_LIBS "")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            get_filename_component(VCPKG_FFMPEG_LIB_DIR
                "${VCPKG_TOOLS_FFMPEG_DIR}/../../lib" ABSOLUTE)

            foreach(lib libva.so.2 libva-drm.so.2)
                if(EXISTS "${VCPKG_FFMPEG_LIB_DIR}/${lib}")
                    add_custom_command(
                        TARGET Mango
                        POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${VCPKG_FFMPEG_LIB_DIR}/${lib}"
                            "$<TARGET_FILE_DIR:Mango>/${lib}"
                        COMMENT "Copying bundled ${lib}..."
                    )
                    list(APPEND MANGO_BUNDLED_FFMPEG_LIBS "${lib}")
                endif()
            endforeach()
        endif()


    else()

        # macOS and Windows have no other ffmpeg: the build would run but
        # couldn't play anything. Linux can still use the system one.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(MANGO_FFMPEG_MISSING_LEVEL WARNING)
        else()
            set(MANGO_FFMPEG_MISSING_LEVEL FATAL_ERROR)
        endif()
        message(${MANGO_FFMPEG_MISSING_LEVEL}
            "FFmpeg tools not found in ${VCPKG_TOOLS_FFMPEG_DIR}. "
            "Enable ffmpeg tools feature in vcpkg."
        )

    endif()

endif()

#
# Linux: install layout, systemd service and .deb (see cmake/LinuxPackage.cmake)
#
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(${PROJECT_SOURCE_DIR}/cmake/LinuxPackage.cmake)
endif()
