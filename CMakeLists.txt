cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

project(DeviousEditorRemastered VERSION 1.0.0)

file(GLOB_RECURSE SOURCES
    src/*.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable.")
else()
    message(STATUS "Found Geode SDK at $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

target_link_libraries(${PROJECT_NAME} geode-sdk)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} ws2_32 iphlpapi)

    # The networking .cpp files must include winsock2.h before any Windows
    # headers. Geode's precompiled header already contains windows.h (which
    # pulls in the legacy winsock.h), so we skip the PCH for these files and
    # let them control the include order themselves.
    set_source_files_properties(
        src/network/Discovery.cpp
        src/network/Session.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )
endif()

setup_geode_mod(${PROJECT_NAME})
