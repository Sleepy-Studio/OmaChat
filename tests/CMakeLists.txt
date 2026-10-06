find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)
if(OMACHAT_BUILD_CLIENT)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Multimedia Quick)
endif()
include(GoogleTest)

# Unit tests: pure logic, no network, no audio hardware.
add_executable(omachat_unit_tests
    unit/test_main.cpp
    unit/test_snowflake.cpp
    unit/test_permissions.cpp
    unit/test_framing.cpp
    unit/test_media_packet.cpp
    unit/test_rate_limit.cpp
    unit/test_validation.cpp
    unit/test_oauth_profile.cpp
    unit/test_config.cpp
    unit/test_jitter_buffer.cpp
    unit/test_opus.cpp
    unit/test_audio_processor.cpp
    unit/test_spsc_ring.cpp
    unit/test_client_state.cpp
    unit/test_server_state.cpp
    unit/test_server_config_env.cpp
    unit/test_store.cpp
    unit/test_video.cpp
    unit/test_e2e.cpp
)
if(OMACHAT_BUILD_CLIENT)
    target_sources(omachat_unit_tests PRIVATE unit/test_markdown.cpp unit/test_commands.cpp unit/test_paste.cpp
        unit/test_artwork_crop.cpp ../client/src/platform/ArtworkCrop.cpp
        unit/test_artwork_cache.cpp ../client/src/platform/ArtworkCache.cpp
        unit/test_audio_levels.cpp unit/test_message_list_model.cpp
        ../client/src/views/AudioLevels.cpp ../client/src/models/MessageListModel.cpp)
    target_include_directories(omachat_unit_tests PRIVATE ../client/src)
    target_link_libraries(omachat_unit_tests PRIVATE omachat_client_text Qt6::Multimedia)
endif()
omachat_set_warnings(omachat_unit_tests)
target_link_libraries(omachat_unit_tests PRIVATE
    omachat_shared omachat_daemon_core omachat_server_core GTest::gtest Qt6::Test)
gtest_discover_tests(omachat_unit_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS unit)

# The real QML singleton factory must reuse the instance configured by main().
if(OMACHAT_BUILD_CLIENT)
    qt_policy(SET QTP0004 NEW)
    qt_add_executable(omachat_theme_tests gui/test_theme.cpp)
    set_source_files_properties(../client/qml/Metrics.qml PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS Metrics.qml)
    qt_add_qml_module(omachat_theme_tests
        URI OmaChatThemeTest
        VERSION 1.0
        SOURCES ../client/src/platform/ThemeProvider.cpp ../client/src/platform/ThemeProvider.hpp
        QML_FILES ../client/qml/Metrics.qml)
    target_include_directories(omachat_theme_tests PRIVATE ../client/src ../client/src/platform)
    target_link_libraries(omachat_theme_tests PRIVATE
        GTest::gtest Qt6::Quick omachat_shared)
    omachat_set_warnings(omachat_theme_tests)
    gtest_discover_tests(omachat_theme_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS gui)

    qt_add_executable(omachat_theme_state_tests gui/test_theme_states.cpp
        ../client/src/platform/ThemeProvider.cpp ../client/src/platform/ThemeProvider.hpp)
    target_include_directories(omachat_theme_state_tests PRIVATE ../client/src)
    target_compile_definitions(omachat_theme_state_tests PRIVATE OMACHAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(omachat_theme_state_tests PRIVATE
        GTest::gtest Qt6::Quick Qt6::Test omachat_shared)
    omachat_set_warnings(omachat_theme_state_tests)
    gtest_discover_tests(omachat_theme_state_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS gui)
endif()

# Integration tests: real server, real TLS, real daemons (null audio device).
add_executable(omachat_integration_tests
    integration/test_main.cpp
    integration/Harness.cpp
    integration/test_protocol.cpp
    integration/test_daemon.cpp
    integration/test_voice.cpp
    integration/test_channel_placement.cpp
    integration/test_category_delete.cpp
)
omachat_set_warnings(omachat_integration_tests)
target_include_directories(omachat_integration_tests PRIVATE integration)
target_link_libraries(omachat_integration_tests PRIVATE
    omachat_shared omachat_daemon_core omachat_server_core GTest::gtest Qt6::Test)
gtest_discover_tests(omachat_integration_tests DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS integration TIMEOUT 120)

# Parser robustness: feeds random/mutated input to every untrusted-data
# parser. Runs as a normal test; libFuzzer entry points are built when
# OMACHAT_ENABLE_FUZZERS=ON with clang.
add_executable(omachat_fuzz_smoke fuzz/fuzz_smoke.cpp fuzz/FuzzTargets.cpp)
omachat_set_warnings(omachat_fuzz_smoke)
target_link_libraries(omachat_fuzz_smoke PRIVATE omachat_shared omachat_daemon_core GTest::gtest GTest::gtest_main)
gtest_discover_tests(omachat_fuzz_smoke PROPERTIES LABELS fuzz)

if(OMACHAT_ENABLE_FUZZERS)
    foreach(target framing media_header envelope ipc_line video_fragments e2e_payload)
        add_executable(fuzz_${target} fuzz/libfuzzer_${target}.cpp fuzz/FuzzTargets.cpp)
        target_compile_options(fuzz_${target} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_libraries(fuzz_${target} PRIVATE omachat_shared omachat_daemon_core)
    endforeach()
endif()

if(OMACHAT_BUILD_CLIENT)
    qt_add_executable(omachat_operation_tests gui/test_operations.cpp integration/Harness.cpp)
    qt_add_executable(omachat_permission_review_tests gui/test_permission_review.cpp integration/Harness.cpp)
    foreach(operation_target omachat_operation_tests omachat_permission_review_tests)
        target_sources(${operation_target} PRIVATE ../client/src/platform/ArtworkCache.cpp)
        target_sources(${operation_target} PRIVATE ../client/src/platform/ArtworkCrop.cpp)
        file(GLOB operation_icons "${CMAKE_SOURCE_DIR}/client/icons/*.svg")
        qt_add_resources(${operation_target} operation_icons
            PREFIX "/qt/qml/OmaChat" BASE "${CMAKE_SOURCE_DIR}/client" FILES ${operation_icons}
            "${CMAKE_SOURCE_DIR}/client/data/emoji.json")
        target_sources(${operation_target} PRIVATE
            ../client/src/application/DaemonLink.cpp ../client/src/application/DaemonLink.hpp
            ../client/src/controllers/AppController.cpp ../client/src/controllers/AppController.hpp
            ../client/src/controllers/AppActions.cpp ../client/src/controllers/AttachmentActions.cpp
            ../client/src/controllers/EmojiActions.cpp ../client/src/controllers/RoleActions.cpp
            ../client/src/models/RowListModel.cpp ../client/src/models/RowListModel.hpp
            ../client/src/models/MessageListModel.cpp ../client/src/models/MessageListModel.hpp
            ../client/src/views/VideoFrameItem.cpp ../client/src/views/VideoFrameItem.hpp
            ../client/src/views/AudioLevels.cpp ../client/src/views/AudioLevels.hpp
            ../client/src/platform/ThemeProvider.cpp ../client/src/platform/ThemeProvider.hpp)
        target_compile_definitions(${operation_target} PRIVATE OMACHAT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
        target_include_directories(${operation_target} PRIVATE ../client/src integration)
        target_link_libraries(${operation_target} PRIVATE omachat_client_text omachat_shared
            omachat_daemon_core omachat_server_core GTest::gtest Qt6::Quick Qt6::DBus Qt6::Test Qt6::Multimedia)
        omachat_set_warnings(${operation_target})
        gtest_discover_tests(${operation_target} DISCOVERY_TIMEOUT 30
            PROPERTIES LABELS "gui;integration" TIMEOUT 120)
    endforeach()
endif()
