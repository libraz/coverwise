cmake_minimum_required(VERSION 3.16)
project(coverwise_installed_headers LANGUAGES CXX)

include(CheckCXXSourceCompiles)

if(NOT DEFINED COVERWISE_SOURCE_DIR OR NOT DEFINED COVERWISE_INSTALL_DIR)
  message(FATAL_ERROR "COVERWISE_SOURCE_DIR and COVERWISE_INSTALL_DIR are required")
endif()

find_package(coverwise CONFIG REQUIRED)

get_target_property(coverwise_includes coverwise::coverwise INTERFACE_INCLUDE_DIRECTORIES)
list(GET coverwise_includes 0 include_root)
if(NOT IS_DIRECTORY "${include_root}")
  message(FATAL_ERROR "coverwise exports an include root that does not exist: ${include_root}")
endif()

# The set of headers an embedder can reach from the install prefix, as paths
# relative to the exported include root.
file(GLOB_RECURSE installed_headers RELATIVE "${include_root}" "${include_root}/*.h")
list(SORT installed_headers)

# The set the C++ API reference calls "the full API": everything coverwise.h
# pulls in, transitively, from the source tree.
function(coverwise_header_closure out_var)
  set(pending "coverwise.h")
  set(closure)
  while(pending)
    list(POP_FRONT pending header)
    if(header IN_LIST closure)
      continue()
    endif()
    list(APPEND closure "${header}")
    file(STRINGS "${COVERWISE_SOURCE_DIR}/src/${header}" include_lines
         REGEX "^[ \t]*#[ \t]*include[ \t]+\"")
    foreach(line IN LISTS include_lines)
      string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]+\"([^\"]+)\".*$" "\\1" included "${line}")
      if(EXISTS "${COVERWISE_SOURCE_DIR}/src/${included}")
        list(APPEND pending "${included}")
      endif()
    endforeach()
  endwhile()
  list(SORT closure)
  set(${out_var} "${closure}" PARENT_SCOPE)
endfunction()

coverwise_header_closure(public_headers)

if(NOT installed_headers STREQUAL public_headers)
  message(FATAL_ERROR
    "the installed header set is not the include closure of coverwise.h\n"
    "  installed: ${installed_headers}\n"
    "  closure:   ${public_headers}")
endif()

# Every remaining header in the source tree is internal and must be out of
# reach: including one from the install prefix has to fail to compile.
file(GLOB_RECURSE source_headers RELATIVE "${COVERWISE_SOURCE_DIR}/src"
     "${COVERWISE_SOURCE_DIR}/src/*.h")
set(internal_headers ${source_headers})
list(REMOVE_ITEM internal_headers ${public_headers})
list(FILTER internal_headers EXCLUDE REGEX "^(ts|binding)/")
if(NOT internal_headers)
  message(FATAL_ERROR "no internal header left to check -- the check has stopped proving anything")
endif()

set(CMAKE_REQUIRED_INCLUDES "${include_root}")
set(CMAKE_REQUIRED_QUIET ON)
foreach(header IN LISTS internal_headers)
  string(MAKE_C_IDENTIFIER "reachable_${header}" probe)
  check_cxx_source_compiles("#include \"${header}\"\nint main() { return 0; }" ${probe})
  if(${probe})
    message(FATAL_ERROR "internal header ${header} is reachable from the install prefix")
  endif()
endforeach()
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_QUIET)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE coverwise::coverwise)
