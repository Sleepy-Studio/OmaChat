add_executable(omachatctl src/main.cpp)
omachat_set_warnings(omachatctl)
target_link_libraries(omachatctl PRIVATE omachat_shared Qt6::Core Qt6::Network)
install(TARGETS omachatctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
