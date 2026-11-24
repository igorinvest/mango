# The Linux desktop app's install layout and its .deb / tarball (CPack), used
# instead of LinuxPackage.cmake when MANGO_BUILD_LINUX_APP is ON.
#
#   /usr/lib/mango-desktop/mango-desktop    the app, with the engine inside
#   /usr/lib/mango-desktop/ffmpeg, ffprobe  when MANGO_BUNDLE_FFMPEG is ON
#   /usr/lib/mango-desktop/libva*.so.2      the bundled ffmpeg's libva (VAAPI)
#   /usr/bin/mango-desktop -> ../lib/mango-desktop/mango-desktop
#   /usr/share/applications/mango-desktop.desktop
#   /usr/share/metainfo/mango-desktop.metainfo.xml
#   /usr/share/icons/hicolor/…/apps/mango-desktop.{svg,png}
#
# Its own folder, so it installs next to the headless "mango" package without
# a file conflict. Both use ~/.local/share/Mango; the data-folder lock lets
# only one run the engine at a time, and the app says so.
#
#   cmake --preset linux-x64-desktop-release && cmake --build --preset linux-x64-desktop-release
#   cd build-linux-x64-desktop-release && cpack

include(GNUInstallDirs)

set(MANGO_DESKTOP_LIB_DIR "lib/mango-desktop")
set(MANGO_DESKTOP_PACKAGING "${PROJECT_SOURCE_DIR}/Mango Streaming Server Linux/packaging")

install(TARGETS MangoDesktop RUNTIME DESTINATION ${MANGO_DESKTOP_LIB_DIR})

if(MANGO_BUNDLE_FFMPEG)
    install(PROGRAMS
        "$<TARGET_FILE_DIR:Mango>/ffmpeg"
        "$<TARGET_FILE_DIR:Mango>/ffprobe"
        DESTINATION ${MANGO_DESKTOP_LIB_DIR})

    foreach(lib IN LISTS MANGO_BUNDLED_FFMPEG_LIBS)
        install(FILES "$<TARGET_FILE_DIR:Mango>/${lib}" DESTINATION ${MANGO_DESKTOP_LIB_DIR})
    endforeach()
endif()

# /usr/bin/mango-desktop: made in the build folder, installed as a link.
set(MANGO_DESKTOP_BIN_LINK "${CMAKE_BINARY_DIR}/linux-package/mango-desktop")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/linux-package")
file(CREATE_LINK "../${MANGO_DESKTOP_LIB_DIR}/mango-desktop" "${MANGO_DESKTOP_BIN_LINK}" SYMBOLIC)
install(FILES "${MANGO_DESKTOP_BIN_LINK}" DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES "${MANGO_DESKTOP_PACKAGING}/mango-desktop.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES "${MANGO_DESKTOP_PACKAGING}/mango-desktop.metainfo.xml"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES "${MANGO_DESKTOP_PACKAGING}/icons/mango-desktop.svg"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
foreach(size 128 256 512)
    install(FILES "${MANGO_DESKTOP_PACKAGING}/icons/mango-desktop-${size}.png"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
        RENAME mango-desktop.png)
endforeach()

#
# CPack
#
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_PACKAGE_NAME "mango-desktop")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Mango")
set(CPACK_PACKAGE_CONTACT "Mango developers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Streams local videos to Chromecast TVs (desktop app)")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
# The Qt 6 libraries come from the distribution's packages (found by
# shlibdeps). The QML modules are loaded at run time, so they're listed.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(MANGO_DESKTOP_QML_DEPENDS
    "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs, qml6-module-qtqml-models, qml6-module-qtquick-templates, qml6-module-qtquick-window, xdg-desktop-portal")

if(MANGO_BUNDLE_FFMPEG)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${MANGO_DESKTOP_QML_DEPENDS}")
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "ffmpeg")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${CMAKE_BINARY_DIR}")
else()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${MANGO_DESKTOP_QML_DEPENDS}, ffmpeg")
endif()

# Native file dialogs on GNOME and in Flatpak go through the portal.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-xdgdesktopportal-platformtheme")

include(CPack)
