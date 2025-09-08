cmake_minimum_required(VERSION 3.20)

# Derive project version from the latest Git tag.  Fallback to 0.1.0 when tags
# are unavailable (e.g., during development or shallow clones).
execute_process(
  COMMAND git describe --tags --abbrev=0
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE GIT_TAG_RESULT)

if(GIT_TAG_RESULT EQUAL 0)
  string(REGEX REPLACE "^v" "" PROJECT_VERSION "${GIT_TAG}")
else()
  set(PROJECT_VERSION "0.1.0")
endif()

project(json-view VERSION ${PROJECT_VERSION} LANGUAGES CXX)

enable_testing()


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CURSES_NEED_WIDE TRUE)
find_package(Curses QUIET)

if(NOT CURSES_FOUND AND APPLE)
  find_program(BREW brew)
  if(BREW)
    execute_process(
      COMMAND ${BREW} --prefix ncurses
      OUTPUT_VARIABLE NCURSES_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NCURSES_PREFIX)
      list(APPEND CMAKE_PREFIX_PATH "${NCURSES_PREFIX}")
      find_package(Curses REQUIRED)
    endif()
  endif()
endif()

if(NOT CURSES_FOUND)
  message(FATAL_ERROR "Could not find ncurses; install ncurses or set CMAKE_PREFIX_PATH")
endif()

# When building static binaries we prefer linking against the static variant of
# ncurses to avoid hard dependencies on the system's libncursesw. Override the
# library list returned by FindCurses with plain library names so the linker
# can pick the `.a` files.
option(JSON_VIEW_STATIC "Link json-view binaries statically" OFF)
if(JSON_VIEW_STATIC)
  # Resolve static ncurses libraries explicitly to avoid pulling in the
  # dynamic versions via linker scripts.
  set(_orig_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  find_library(NCURSESW_LIB ncursesw REQUIRED)
  find_library(FORMW_LIB formw REQUIRED)
  find_library(TINFO_LIB tinfo REQUIRED)
  set(CURSES_LIBRARIES ${NCURSESW_LIB} ${FORMW_LIB} ${TINFO_LIB})
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${_orig_suffixes})
endif()

# Option to build the Turbo Vision based frontend.
option(BUILD_JSON_VIEW_APP "Build Turbo Vision based json-view-app" ON)

add_executable(json-view src/json-view.cpp)
add_library(json_view_core STATIC src/json_view_core.cpp)
target_include_directories(json_view_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_include_directories(json-view PRIVATE ${CMAKE_SOURCE_DIR}/include ${CURSES_INCLUDE_DIRS})
target_compile_definitions(json-view PRIVATE JSON_VIEW_VERSION="${PROJECT_VERSION}")

target_link_libraries(json-view PRIVATE json_view_core ${CURSES_LIBRARIES})

# Fetch Turbo Vision for the alternate frontend if enabled
if(BUILD_JSON_VIEW_APP)
  include(FetchContent)
  # Disable unnecessary tvision components.
  set(TV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(TV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  if(JSON_VIEW_STATIC)
    set(TV_BUILD_USING_GPM OFF CACHE BOOL "" FORCE)
    # Ensure tvision resolves static ncurses libraries.
    set(_tv_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  endif()
  FetchContent_Declare(
    tvision
    GIT_REPOSITORY https://github.com/magiblot/tvision.git
    GIT_TAG 7ecc590ac59b163a876da50867c69bba605cebfc
  )
  FetchContent_MakeAvailable(tvision)
  if(JSON_VIEW_STATIC)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_tv_suffixes})
    # Replace tvision's ncurses and tinfo dependencies with explicit static
    # libraries resolved above.
    get_target_property(_tv_libs tvision INTERFACE_LINK_LIBRARIES)
    if(_tv_libs)
      list(FILTER _tv_libs EXCLUDE REGEX "ncursesw|tinfo")
    endif()
    list(APPEND _tv_libs ${NCURSESW_LIB} ${FORMW_LIB} ${TINFO_LIB})
    set_target_properties(tvision PROPERTIES INTERFACE_LINK_LIBRARIES "${_tv_libs}")
  endif()

  add_executable(json-view-app src/json-view-app.cpp)
  target_include_directories(json-view-app PRIVATE ${CMAKE_SOURCE_DIR}/include)
  target_compile_definitions(json-view-app PRIVATE JSON_VIEW_VERSION="${PROJECT_VERSION}")
  target_link_libraries(json-view-app PRIVATE json_view_core tvision::tvision ${CURSES_LIBRARIES})
endif()

# Optionally build fully static binaries to avoid runtime dependencies on
# specific versions of glibc or ncurses. This is useful when targeting
# platforms like Amazon Linux 2023 whose system libraries may be older than
# those used to build the package. Enable with `-DJSON_VIEW_STATIC=ON`.
if(JSON_VIEW_STATIC)
  target_link_options(json-view PRIVATE -static -static-libgcc -static-libstdc++)
  if(TARGET json-view-app)
    target_link_options(json-view-app PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()

file(GLOB EXAMPLE_JSON_FILES "${CMAKE_SOURCE_DIR}/examples/*.json")
foreach(json ${EXAMPLE_JSON_FILES})
  get_filename_component(name ${json} NAME_WE)
  if(name STREQUAL "invalid")
    add_test(NAME validate_${name} COMMAND $<TARGET_FILE:json-view> --validate ${json})
    set_tests_properties(validate_${name} PROPERTIES WILL_FAIL TRUE)
  else()
    add_test(NAME validate_${name} COMMAND $<TARGET_FILE:json-view> --validate ${json})
  endif()
endforeach()

install(TARGETS json-view RUNTIME DESTINATION bin)
if(TARGET json-view-app)
  install(TARGETS json-view-app RUNTIME DESTINATION bin)
endif()
install(TARGETS json_view_core ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
configure_file(doc/json-view.1.in ${CMAKE_CURRENT_BINARY_DIR}/doc/json-view.1 @ONLY)
configure_file(doc/json-view.texi.in ${CMAKE_CURRENT_BINARY_DIR}/doc/json-view.texi @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/doc/json-view.1 DESTINATION share/man/man1)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/doc/json-view.texi DESTINATION share/info)

set(CPACK_PACKAGE_NAME "json-view")
set(CPACK_PACKAGE_VENDOR "json-view")
set(CPACK_PACKAGE_CONTACT "json-view maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminal JSON viewer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "json-view maintainers")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libncursesw5 (>= 6.2) | libncurses6, libtvision")
set(CPACK_RPM_PACKAGE_REQUIRES "ncurses, tvision")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})

if(JSON_VIEW_STATIC)
  # Static builds do not require ncurses or tvision at runtime and are
  # distributed under a separate package name to avoid confusion with the
  # dynamically linked RPM.
  set(CPACK_PACKAGE_NAME "json-view-static")
  set(CPACK_RPM_PACKAGE_REQUIRES "")
endif()
include(CPack)
