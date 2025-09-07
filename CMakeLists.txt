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


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
  set(CURSES_INCLUDE_PATH "/opt/homebrew/opt/ncurses/include")
  set(CURSES_LIBRARY "/opt/homebrew/opt/ncurses/lib/libncurses.dylib")
endif()

set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

add_executable(json-view src/json-view.cpp)
target_include_directories(json-view PRIVATE ${CMAKE_SOURCE_DIR}/include ${CURSES_INCLUDE_DIRS})
target_compile_definitions(json-view PRIVATE JSON_VIEW_VERSION="${PROJECT_VERSION}")

target_link_libraries(json-view PRIVATE ${CURSES_LIBRARIES})

install(TARGETS json-view DESTINATION bin)
install(FILES doc/json-view.1 DESTINATION share/man/man1)
install(FILES doc/json-view.texi DESTINATION share/info)

set(CPACK_PACKAGE_NAME "json-view")
set(CPACK_PACKAGE_VENDOR "json-view")
set(CPACK_PACKAGE_CONTACT "json-view maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminal JSON viewer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "json-view maintainers")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libncursesw5 (>= 6.2) | libncurses6")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
