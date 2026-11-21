# vcpkg's x64-linux triplet, plus one change for libva (used by the bundled
# ffmpeg's VAAPI encoder). Picked up through VCPKG_OVERLAY_TRIPLETS in the
# Linux presets.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# libva loads the GPU's driver (iHD, radeonsi, ...) at run time from a
# folder fixed at build time, which would otherwise be inside vcpkg's tree.
# Point it at the distributions' own driver folders instead. LIBVA_DRIVERS_PATH
# still overrides it.
if(PORT STREQUAL "libva")
    set(VCPKG_MESON_NATIVE_FILE_RELEASE "${CMAKE_CURRENT_LIST_DIR}/libva-driverdir.ini")
    set(VCPKG_MESON_NATIVE_FILE_DEBUG "${CMAKE_CURRENT_LIST_DIR}/libva-driverdir.ini")
endif()
