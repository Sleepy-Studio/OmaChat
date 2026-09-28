cmake_minimum_required(VERSION 3.25)

project(OmaChat
    VERSION 0.1.0
    DESCRIPTION "Native voice, text, and screen streaming chat for Omarchy"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(OMACHAT_BUILD_CLIENT "Build the Qt Quick GUI client" ON)
option(OMACHAT_BUILD_DAEMON "Build the omachatd communications daemon" ON)
option(OMACHAT_BUILD_SERVER "Build the self-hostable omachat-server" ON)
option(OMACHAT_BUILD_CLI "Build the omachatctl command-line interface" ON)
option(OMACHAT_BUILD_TESTS "Build unit and integration tests" ON)
option(OMACHAT_ENABLE_FUZZERS "Build libFuzzer targets (requires clang)" OFF)
option(OMACHAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(GNUInstallDirs)
include(cmake/OmaChatWarnings.cmake)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Sql)
find_package(Protobuf CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
find_package(tomlplusplus REQUIRED)
find_package(OpenSSL REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

add_subdirectory(protocol)
add_subdirectory(shared)

if(OMACHAT_BUILD_SERVER)
    add_subdirectory(server)
endif()
if(OMACHAT_BUILD_DAEMON)
    add_subdirectory(daemon)
endif()
if(OMACHAT_BUILD_CLI)
    add_subdirectory(cli)
endif()
if(OMACHAT_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if(OMACHAT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(packaging)
