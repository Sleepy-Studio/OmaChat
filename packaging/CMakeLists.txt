# Desktop, icon, and systemd integration files.
install(FILES desktop/org.omarchy.OmaChat.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES desktop/org.omarchy.OmaChat.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES systemd/omachat.service DESTINATION lib/systemd/user)
if(OMACHAT_BUILD_SERVER)
    install(FILES server/omachat-server.service DESTINATION lib/systemd/system)
    install(FILES server/omachat-server.sysusers DESTINATION lib/sysusers.d RENAME omachat-server.conf)
    install(FILES server/server.toml.example DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omachat)
endif()
