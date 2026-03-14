if(TARGET JasPer::jasper)
    set(JasPer_FOUND ON)
    return()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(JasPer REQUIRED jasper)

add_library(JasPer::jasper INTERFACE IMPORTED)
set_target_properties(JasPer::jasper PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${JasPer_INCLUDE_DIRS}"
    INTERFACE_COMPILE_OPTIONS     "${JasPer_CFLAGS_OTHER}"
    INTERFACE_LINK_LIBRARIES      "${JasPer_LINK_LIBRARIES}"
)
