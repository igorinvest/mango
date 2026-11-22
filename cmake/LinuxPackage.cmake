# Linux install layout, the systemd service and the .deb / tarball (CPack).
#
#   /usr/lib/mango/Mango            the engine with its console
#   /usr/lib/mango/ffmpeg, ffprobe  when MANGO_BUNDLE_FFMPEG is ON
#   /usr/lib/mango/libva*.so.2      the bundled ffmpeg's libva (VAAPI)
#   /usr/bin/mango -> ../lib/mango/Mango
#   /usr/lib/systemd/user/mango.service  a user service: runs as whoever enables it
#
# ExecutableDirectory() resolves /proc/self/exe through the symlink, so the
# bundled ffmpeg is found next to the real binary.
#
#   cmake --preset linux-x64-release && cmake --build --preset linux-x64-release
#   cd build-linux-x64-release && cpack

include(GNUInstallDirs)

set(MANGO_LIB_DIR "lib/mango")

install(TARGETS Mango RUNTIME DESTINATION ${MANGO_LIB_DIR})

if(MANGO_BUNDLE_FFMPEG)
    install(PROGRAMS
        "$<TARGET_FILE_DIR:Mango>/ffmpeg"
        "$<TARGET_FILE_DIR:Mango>/ffprobe"
        DESTINATION ${MANGO_LIB_DIR})

    # libva for ffmpeg's VAAPI encoder (see the ffmpeg bundling in CMakeLists.txt).
    foreach(lib IN LISTS MANGO_BUNDLED_FFMPEG_LIBS)
        install(FILES "$<TARGET_FILE_DIR:Mango>/${lib}" DESTINATION ${MANGO_LIB_DIR})
    endforeach()
endif()

# The /usr/bin/mango symlink: made in the build folder, installed as a link.
set(MANGO_BIN_LINK "${CMAKE_BINARY_DIR}/linux-package/mango")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/linux-package")
file(CREATE_LINK "../${MANGO_LIB_DIR}/Mango" "${MANGO_BIN_LINK}" SYMBOLIC)
install(FILES "${MANGO_BIN_LINK}" DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/mango.service"
    DESTINATION lib/systemd/user)

#
# CPack
#
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_PACKAGE_NAME "mango")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Mango")
set(CPACK_PACKAGE_CONTACT "Mango developers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Streams local videos to Chromecast and AirPlay TVs (headless server)")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# The system ffmpeg brings libx264, the software fallback when there's no
# hardware encoder. Without a bundled one it's the only ffmpeg.
if(MANGO_BUNDLE_FFMPEG)
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "ffmpeg")
    # Lets dpkg-shlibdeps find the bundled libva, which no system package owns.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${CMAKE_BINARY_DIR}")
else()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "ffmpeg")
endif()

include(CPack)
