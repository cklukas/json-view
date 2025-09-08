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

# On some systems (notably macOS with Homebrew), FindCurses points
# CURSES_INCLUDE_PATH at the "ncursesw" subdirectory.  tvision expects the
# parent directory to be on the include search path so that
# "#include <ncursesw/ncurses.h>" can resolve its own headers like
# "<ncursesw/unctrl.h>".  Normalize the include path here to avoid nested
# "ncursesw/ncursesw" lookups.
if(CURSES_INCLUDE_PATH MATCHES "/ncursesw$")
  get_filename_component(_curses_parent "${CURSES_INCLUDE_PATH}" DIRECTORY)
  set(CURSES_INCLUDE_PATH "${_curses_parent}" CACHE PATH "" FORCE)
  set(CURSES_INCLUDE_DIR "${_curses_parent}" CACHE PATH "" FORCE)
  set(CURSES_INCLUDE_DIRS "${_curses_parent}" CACHE PATH "" FORCE)
endif()

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
  if(APPLE)
    include_directories(BEFORE ${CMAKE_SOURCE_DIR}/cmake/compat)
  endif()
  FetchContent_Declare(
    tvision
    GIT_REPOSITORY https://github.com/magiblot/tvision.git
    GIT_TAG 7ecc590ac59b163a876da50867c69bba605cebfc
  )
  FetchContent_MakeAvailable(tvision)

  # Ensure tvision uses the normalized ncurses include path on macOS.
  if(APPLE AND TARGET tvision::tvision)
    target_include_directories(tvision::tvision SYSTEM BEFORE PUBLIC ${CURSES_INCLUDE_DIRS})
  endif()

  add_executable(json-view-app src/json-view-app.cpp)
  target_include_directories(json-view-app PRIVATE ${CMAKE_SOURCE_DIR}/include)
  target_compile_definitions(json-view-app PRIVATE JSON_VIEW_VERSION="${PROJECT_VERSION}")
  target_link_libraries(json-view-app PRIVATE json_view_core tvision::tvision ${CURSES_LIBRARIES})
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
include(CPack)
