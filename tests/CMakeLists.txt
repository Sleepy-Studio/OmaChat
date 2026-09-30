find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)
if(OMACHAT_BUILD_CLIENT)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Multimedia)
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
    unit/test_store.cpp
    unit/test_video.cpp
    unit/test_e2e.cpp
)
if(OMACHAT_BUILD_CLIENT)
    target_sources(omachat_unit_tests PRIVATE unit/test_markdown.cpp unit/test_commands.cpp unit/test_paste.cpp
        unit/test_audio_levels.cpp ../client/src/views/AudioLevels.cpp)
    target_include_directories(omachat_unit_tests PRIVATE ../client/src)
    target_link_libraries(omachat_unit_tests PRIVATE omachat_client_text Qt6::Multimedia)
endif()
omachat_set_warnings(omachat_unit_tests)
target_link_libraries(omachat_unit_tests PRIVATE
    omachat_shared omachat_daemon_core omachat_server_core GTest::gtest Qt6::Test)
gtest_discover_tests(omachat_unit_tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS unit)

# Integration tests: real server, real TLS, real daemons (null audio device).
add_executable(omachat_integration_tests
    integration/test_main.cpp
    integration/Harness.cpp
    integration/test_protocol.cpp
    integration/test_daemon.cpp
    integration/test_voice.cpp
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
