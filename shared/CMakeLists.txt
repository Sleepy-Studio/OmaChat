configure_file(src/omachat/core/Version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/omachat/core/Version.hpp @ONLY)

add_library(omachat_shared STATIC
    src/omachat/crypto/MessageCrypto.cpp
    src/omachat/crypto/FileCrypto.cpp
    src/omachat/core/Log.cpp
    src/omachat/core/Paths.cpp
    src/omachat/core/Snowflake.cpp
    src/omachat/core/Validation.cpp
    src/omachat/core/Permissions.cpp
    src/omachat/protocol/Framing.cpp
    src/omachat/protocol/ProtocolConnection.cpp
    src/omachat/media/MediaPacket.cpp
    src/omachat/media/VideoFragments.cpp
    src/omachat/media/FrameBuffer.cpp
    src/omachat/ipc/IpcMessage.cpp
    src/omachat/ipc/IpcClient.cpp
    src/omachat/config/ClientConfig.cpp
)
omachat_set_warnings(omachat_shared)
set_target_properties(omachat_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(omachat_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(omachat_shared
    PUBLIC Qt6::Core Qt6::Network omachat_proto PkgConfig::SODIUM
    PRIVATE tomlplusplus::tomlplusplus)
