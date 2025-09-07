cmake_minimum_required(VERSION 3.20)
project(json-view LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CURSES_INCLUDE_PATH "/opt/homebrew/opt/ncurses/include")
set(CURSES_LIBRARY "/opt/homebrew/opt/ncurses/lib/libncurses.dylib")

set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

add_executable(json-view src/json-view.cpp)
target_include_directories(json-view PRIVATE ${CMAKE_SOURCE_DIR}/include ${CURSES_INCLUDE_DIRS})

target_link_libraries(json-view PRIVATE ${CURSES_LIBRARIES})

install(TARGETS json-view DESTINATION bin)
install(FILES doc/json-view.1 DESTINATION share/man/man1)
install(FILES doc/json-view.texi DESTINATION share/info)
