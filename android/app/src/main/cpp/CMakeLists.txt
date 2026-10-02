cmake_minimum_required(VERSION 3.22)
project(omachat_android_crypto LANGUAGES C CXX)
include(ExternalProject)
include(FetchContent)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opus
    URL "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/opus-1.6.1.tar.gz"
    URL_HASH SHA256=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(opus)
# Desktop includes opus/opus.h; upstream build-tree headers live in include/.
file(COPY "${opus_SOURCE_DIR}/include/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/compat/opus"
    FILES_MATCHING PATTERN "*.h")
set(VOICE "${CMAKE_CURRENT_LIST_DIR}/../../../../../daemon/src")
set(SODIUM_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/sodium")
file(MAKE_DIRECTORY "${SODIUM_PREFIX}/include")
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(SODIUM_HOST aarch64-linux-android)
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(SODIUM_HOST x86_64-linux-android)
else()
    message(FATAL_ERROR "Unsupported crypto ABI")
endif()
ExternalProject_Add(sodium_build
    URL "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/libsodium-1.0.22-stable-20260928.tar.gz"
    URL_HASH SHA256=625054219273c74fd204b669d97070fb60cf16b6d68b9909aff7a3530792262f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        "CC=${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET} --sysroot=${CMAKE_SYSROOT}"
        "AR=${CMAKE_AR}" "RANLIB=${CMAKE_RANLIB}" "CFLAGS=-O2 -fPIC"
        <SOURCE_DIR>/configure --host=${SODIUM_HOST} --prefix=${SODIUM_PREFIX}
        --disable-shared --enable-static --disable-pie
    BUILD_COMMAND make -j4
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${SODIUM_PREFIX}/lib/libsodium.a")
add_library(sodium STATIC IMPORTED)
set_target_properties(sodium PROPERTIES IMPORTED_LOCATION "${SODIUM_PREFIX}/lib/libsodium.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_PREFIX}/include")
add_dependencies(sodium sodium_build)
set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../../../../shared/src")
add_library(omachat_crypto SHARED CryptoJni.cpp MediaJni.cpp OpusJni.cpp "${VOICE}/voice/OpusCodec.cpp" "${CORE}/omachat/media/MediaPacket.cpp" "${CORE}/omachat/crypto/MessageCrypto.cpp" "${CORE}/omachat/crypto/FileCrypto.cpp")
target_include_directories(omachat_crypto PRIVATE "${CORE}" "${VOICE}" "${CMAKE_CURRENT_BINARY_DIR}/compat")
target_compile_features(omachat_crypto PRIVATE cxx_std_20)
target_compile_options(omachat_crypto PRIVATE -Wall -Wextra -Werror)
target_link_libraries(omachat_crypto PRIVATE sodium opus)
target_link_options(omachat_crypto PRIVATE "-Wl,-z,max-page-size=16384")
