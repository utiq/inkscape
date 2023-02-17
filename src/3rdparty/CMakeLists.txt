add_subdirectory(libuemf)
add_subdirectory(libcroco)
add_subdirectory(libdepixelize)
add_subdirectory(adaptagrams)
add_subdirectory(autotrace)

if(WITH_INTERNAL_2GEOM)
    set(2GEOM_BUILD_SHARED ${BUILD_SHARED_LIBS} CACHE BOOL "")
    add_subdirectory(2geom)
endif()

if(WITH_INTERNAL_CAIRO)
    include(ExternalProject)
    # Used by windows CI/CD/packaging, Linux appimages

    # Change, don't remove options.
    # Otherwise the values are not reset in existing builds.
    set(CAIRO_CONF --buildtype=debugoptimized -Dgtk_doc=false)
    if (NOT WIN32)
    set(CAIRO_CONF ${CAIRO_CONF} --prefix=${CMAKE_INSTALL_PREFIX})
    endif()

    ExternalProject_Add(cairo_ext
        PREFIX cairo
        GIT_REPOSITORY https://gitlab.com/inkscape/deps/cairo.git
        GIT_TAG NAMED_12cd2bcbb260f4ed64a72b64bd2fdd593f683bbc
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cairo
        PATCH_COMMAND git checkout ${CMAKE_CURRENT_SOURCE_DIR}/cairo/src && git apply --unsafe-paths --directory ${CMAKE_CURRENT_SOURCE_DIR}/cairo/ ${CMAKE_SOURCE_DIR}/packaging/cairo.patch
        CONFIGURE_COMMAND meson setup . ${CMAKE_CURRENT_SOURCE_DIR}/cairo && meson setup --reconfigure ${CAIRO_CONF} . ${CMAKE_CURRENT_SOURCE_DIR}/cairo
        BUILD_COMMAND meson compile
        INSTALL_COMMAND meson install
        )
endif()
