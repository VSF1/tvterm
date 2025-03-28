function(tvterm_find_library name)
    find_library(${name}_LIB ${name})
    if (NOT ${name}_LIB)
        message(FATAL_ERROR "Library '${name}' not found")
    else()
        message(STATUS "Found '${name}': ${${name}_LIB}")
    endif()
    add_library(${name} INTERFACE)
    target_link_libraries(${name} INTERFACE ${${name}_LIB})
endfunction()
function(tvterm_find_library_and_header name header)
    tvterm_find_library(${name})
    find_path(${name}_INCLUDE ${header})
    if (NOT ${name}_INCLUDE)
        message(FATAL_ERROR "'${name}' development headers not found: missing '${header}'")
    endif()
    target_include_directories(${name} INTERFACE ${${name}_INCLUDE})
endfunction()

set(SYSTEM_DEPS)

if (NOT WIN32)
    tvterm_find_library(util)
    tvterm_find_library(pthread)
    set(SYSTEM_DEPS util pthread)
endif()

if (TVTERM_USE_SYSTEM_LIBVTERM)
    tvterm_find_library_and_header(vterm "vterm.h")
else()
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/vterm" EXCLUDE_FROM_ALL)
endif()

set(TV_OPTIMIZE_BUILD ${TVTERM_OPTIMIZE_BUILD})
if (TVTERM_USE_SYSTEM_TVISION)
    find_package(tvision CONFIG REQUIRED)
    add_library(tvision ALIAS tvision::tvision)
    get_target_property(_TVISION tvision LOCATION)
    message(STATUS "Found 'tvision': ${_TVISION}")
else()
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/tvision" EXCLUDE_FROM_ALL)
endif()
