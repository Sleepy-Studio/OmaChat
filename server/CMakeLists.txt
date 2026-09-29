# Server logic lives in a static library so integration tests can run a real
# server in-process.
add_library(omachat_server_core STATIC
    src/config/ServerConfig.cpp
    src/auth/Credentials.cpp
    src/auth/OAuthProviders.cpp
    src/storage/Store.cpp
    src/core/State.cpp
    src/core/ChatServer.cpp
    src/core/AuthHandlers.cpp
    src/core/OAuthHandlers.cpp
    src/core/ServerHandlers.cpp
    src/core/DiscordImport.cpp
    src/core/MessageHandlers.cpp
    src/core/VoiceHandlers.cpp
    src/core/ModerationHandlers.cpp
    src/core/AttachmentHandlers.cpp
    src/transport/Certificates.cpp
    src/voice/MediaRelay.cpp
)
omachat_set_warnings(omachat_server_core)
target_include_directories(omachat_server_core PUBLIC src)
target_link_libraries(omachat_server_core
    PUBLIC omachat_shared Qt6::Core Qt6::Network Qt6::Sql OpenSSL::Crypto
    PRIVATE tomlplusplus::tomlplusplus)

add_executable(omachat-server src/main.cpp)
omachat_set_warnings(omachat-server)
target_link_libraries(omachat-server PRIVATE omachat_server_core)
install(TARGETS omachat-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
