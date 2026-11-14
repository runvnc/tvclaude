cmake_minimum_required(VERSION 3.5...3.30)

option(TVTERM_BUILD_APP "Build main application" ON)
option(TVTERM_USE_SYSTEM_TVISION "Use system-wide Turbo Vision instead of the submodule" OFF)
option(TVTERM_USE_SYSTEM_LIBVTERM "Use system-wide libvterm instead of the submodule" OFF)
option(TVTERM_OPTIMIZE_BUILD "Enable build optimizations (Unity Build, Precompiled Headers)" ON)

project(tvterm)

function(tvterm_set_warnings t)
    if (NOT ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC"))
        target_compile_options(${t} PRIVATE
            -Wall
        )
    endif()
endfunction()

# Dependencies

include(deps/CMakeLists.txt)

# Target 'tvterm-core'

file(GLOB_RECURSE TVTERM_CORE_SRC "${CMAKE_CURRENT_LIST_DIR}/source/tvterm-core/*.cc")
add_library(tvterm-core ${TVTERM_CORE_SRC})

tvterm_set_warnings(tvterm-core)
if (HAVE_VTERMSTRINGFRAGMENT)
    target_compile_definitions(tvterm-core PRIVATE HAVE_VTERMSTRINGFRAGMENT)
endif()
target_include_directories(tvterm-core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(tvterm-core PUBLIC
    tvision
    vterm
    ${SYSTEM_DEPS}
)
install(TARGETS vterm ${SYSTEM_DEPS} EXPORT tvterm-config)
install(TARGETS tvterm-core
    EXPORT tvterm-config
    ARCHIVE DESTINATION lib
    COMPONENT library
)
install(EXPORT tvterm-config
    DESTINATION lib/cmake/tvterm
    NAMESPACE tvterm::
    FILE tvterm-config.cmake
    COMPONENT library
)
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/tvterm" DESTINATION include)

# Target 'tvclaude'

if (TVTERM_BUILD_APP)
    file(GLOB_RECURSE TVTERM_SRC "${CMAKE_CURRENT_LIST_DIR}/source/tvterm/*.cc")
    # Build number (commit count), commit and date, shown in Help > About.
    set(BUILDINFO_CC "${CMAKE_CURRENT_BINARY_DIR}/buildinfo.cc")
    add_custom_target(buildinfo
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_LIST_DIR} -DOUT=${BUILDINFO_CC}
                -P ${CMAKE_CURRENT_LIST_DIR}/cmake/buildinfo.cmake
        BYPRODUCTS ${BUILDINFO_CC}
        VERBATIM)
    add_executable(tvclaude ${TVTERM_SRC} ${BUILDINFO_CC})
    add_dependencies(tvclaude buildinfo)
    set_source_files_properties(${BUILDINFO_CC} PROPERTIES
        GENERATED TRUE SKIP_UNITY_BUILD_INCLUSION TRUE SKIP_PRECOMPILE_HEADERS TRUE)
    tvterm_set_warnings(tvclaude)
    target_link_libraries(tvclaude PRIVATE
        tvterm-core
    )
    install(TARGETS tvclaude RUNTIME DESTINATION bin)
endif()

# Build optimization

if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.16.0")
    if (TVTERM_OPTIMIZE_BUILD)
        foreach (t tvclaude tvterm-core)
            set_target_properties(${t} PROPERTIES UNITY_BUILD ON)
            target_precompile_headers(${t} PRIVATE
                "${CMAKE_CURRENT_LIST_DIR}/include/pch.h"
            )
        endforeach()
    endif()
endif()
