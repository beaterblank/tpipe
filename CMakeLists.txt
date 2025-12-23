cmake_minimum_required(VERSION 3.15)
project(tpipe VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

set(SOURCES
    src/AppConfig.cpp
    src/Ducker.cpp
    src/VoiceIndoorFilter.cpp
    src/LadspaLoader.cpp
    src/AudioEngine.cpp
    src/main.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        PkgConfig::JACK
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES default.conf
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/${PROJECT_NAME}
    RENAME config.conf.default
)
