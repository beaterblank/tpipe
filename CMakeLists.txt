cmake_minimum_required(VERSION 3.16)
project(tpipe)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED jack)

add_executable(tpipe
    src/main.cpp
)

target_include_directories(tpipe PRIVATE src)
target_link_libraries(tpipe
    ${JACK_LIBRARIES}
    dl
)
