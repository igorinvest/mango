# Link settings for the macOS app's Xcode project.
#
# Writes <build>/xcode/engine-dependencies.rsp: a clang response file with
# the full path of every library MangoCore pulls in, in link order. The
# Xcode project links "@<that file>" (see
# "Mango Streaming Server/MangoEngine.xcconfig"), so a dependency added with
# target_link_libraries reaches the app without editing Xcode.
#
# libMangoCore.a itself isn't listed: the "Build engine" phase stages it
# into the products folder and the Frameworks phase links it from there, so
# Xcode sees it as a link input and relinks whenever the engine changes.
#
# A response file rather than a generated .xcconfig: Xcode reads its build
# settings before any build phase runs, so on a clean clone a generated
# .xcconfig wouldn't exist yet when the "Build engine" phase creates it.
# clang reads the response file only when it links, after that phase.

# Appends target's link dependencies (recursively, through
# INTERFACE_LINK_LIBRARIES) to the list named by outVar.
function(_mango_collect_link_items target outVar)
    set(items ${${outVar}})

    get_target_property(direct ${target} INTERFACE_LINK_LIBRARIES)
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT target STREQUAL "MangoCore")
        # A built library's own private dependencies matter too.
        get_target_property(private ${target} LINK_LIBRARIES)
        if(private)
            list(APPEND direct ${private})
        endif()
    endif()

    foreach(item IN LISTS direct)
        if(NOT item)
            continue()
        endif()
        # $<LINK_ONLY:X> is what CMake wraps private dependencies of a
        # static library in.
        string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" item "${item}")
        if(NOT item)
            continue()  # e.g. libdatachannel's empty $<LINK_ONLY:>
        endif()

        list(APPEND items "${item}")
        if(TARGET "${item}")
            _mango_collect_link_items("${item}" items)
        endif()
    endforeach()

    set(${outVar} ${items} PARENT_SCOPE)
endfunction()

function(mango_write_xcode_link_settings)
    set(items "")
    _mango_collect_link_items(MangoCore items)
    get_target_property(mangoCoreLinks MangoCore LINK_LIBRARIES)
    foreach(item IN LISTS mangoCoreLinks)
        list(APPEND items "${item}")
        if(TARGET "${item}")
            _mango_collect_link_items("${item}" items)
        endif()
    endforeach()

    # Keeping each item's last occurrence keeps every library after all the
    # libraries that need it.
    list(REVERSE items)
    list(REMOVE_DUPLICATES items)
    list(REVERSE items)

    set(lines "")
    foreach(item IN LISTS items)
        if(TARGET "${item}")
            get_target_property(type ${item} TYPE)
            if(type STREQUAL "INTERFACE_LIBRARY")
                continue()  # header-only, e.g. glaze
            endif()
            string(APPEND lines "\"$<TARGET_LINKER_FILE:${item}>\"\n")
        elseif(item MATCHES "^-")
            string(APPEND lines "${item}\n")  # a linker flag
        elseif(item MATCHES "^\\$<")
            # A generator expression (e.g. httplib's
            # $<$<PLATFORM_ID:Darwin>:-framework CFNetwork ...>): unquoted, so
            # one that evaluates to nothing is a blank line and one that
            # evaluates to several flags splits into separate arguments.
            string(APPEND lines "${item}\n")
        elseif(NOT item MATCHES "[/\\.]")
            string(APPEND lines "-l${item}\n")  # a system library by name, e.g. dl
        else()
            string(APPEND lines "\"${item}\"\n")
        endif()
    endforeach()

    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/xcode/engine-dependencies.rsp" CONTENT "${lines}")
endfunction()
