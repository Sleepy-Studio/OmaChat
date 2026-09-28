find_package(Qt6 REQUIRED COMPONENTS DBus)
find_package(Qt6Keychain REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(RNNOISE IMPORTED_TARGET rnnoise)

add_library(omachat_daemon_core STATIC
    src/application/Daemon.cpp
    src/application/DaemonMethods.cpp
    src/storage/LocalStore.cpp
    src/platform/CredentialStore.cpp
    src/networking/ClientState.cpp
    src/networking/ServerConnection.cpp
    src/networking/FileTransfers.cpp
    src/ipc/IpcServer.cpp
    src/notifications/Notifier.cpp
    src/audio/AudioBackend.hpp
    src/audio/PipeWireBackend.cpp
    src/voice/AudioProcessor.cpp
    src/voice/JitterBuffer.cpp
    src/voice/OpusCodec.cpp
    src/voice/MediaTransport.cpp
    src/voice/VoiceEngine.cpp
)
omachat_set_warnings(omachat_daemon_core)
target_include_directories(omachat_daemon_core PUBLIC src)
target_link_libraries(omachat_daemon_core
    PUBLIC omachat_shared Qt6::Core Qt6::Network Qt6::Sql Qt6::DBus
           qt6keychain PkgConfig::PIPEWIRE PkgConfig::OPUS)
if(RNNOISE_FOUND)
    target_link_libraries(omachat_daemon_core PUBLIC PkgConfig::RNNOISE)
    target_compile_definitions(omachat_daemon_core PUBLIC OMACHAT_HAVE_RNNOISE=1)
    message(STATUS "omachatd: RNNoise noise suppression enabled")
else()
    target_compile_definitions(omachat_daemon_core PUBLIC OMACHAT_HAVE_RNNOISE=0)
    message(STATUS "omachatd: RNNoise not found, noise suppression disabled")
endif()

add_executable(omachatd src/main.cpp)
omachat_set_warnings(omachatd)
target_link_libraries(omachatd PRIVATE omachat_daemon_core)
install(TARGETS omachatd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
