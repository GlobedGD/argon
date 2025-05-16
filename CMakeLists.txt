cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
elseif (DEFINED ENV{GITHUB_ACTION})
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
else()
    if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        message("Running on Apple Silicon")
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    else()
        message("Running on Intel Mac")
        set(CMAKE_OSX_ARCHITECTURES "x86_64")
    endif()
endif()

project(argon VERSION 1.1.1)

file(GLOB_RECURSE SOURCES
	src/*.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

if (PROJECT_IS_TOP_LEVEL)
    add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE GEODE_MOD_ID="_argon")
target_compile_definitions(${PROJECT_NAME} PRIVATE ARGON_VERSION="${PROJECT_VERSION}")

target_link_libraries(${PROJECT_NAME} geode-sdk)

target_include_directories(${PROJECT_NAME} PUBLIC include)

# Add asp
CPMAddPackage("gh:dankmeme01/asp2#ec0d9fc")
target_link_libraries(${PROJECT_NAME} asp)
