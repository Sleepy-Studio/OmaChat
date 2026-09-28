# Generated protobuf code for the reliable network protocol.
add_library(omachat_proto STATIC network.proto)
set_target_properties(omachat_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(omachat_proto PUBLIC protobuf::libprotobuf-lite)
target_include_directories(omachat_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate(TARGET omachat_proto LANGUAGE cpp)
