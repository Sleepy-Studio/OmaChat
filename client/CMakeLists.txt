find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick QuickControls2 DBus Svg)

# Pure text processing (markdown sanitizer, command parser), unit tested.
add_library(omachat_client_text STATIC
    src/text/MarkdownRenderer.cpp
    src/text/CommandParser.cpp
)
omachat_set_warnings(omachat_client_text)
target_include_directories(omachat_client_text PUBLIC src)
target_link_libraries(omachat_client_text PUBLIC Qt6::Core Qt6::Gui)

qt_policy(SET QTP0004 NEW)

qt_add_executable(omachat
    src/main.cpp
)

qt_add_qml_module(omachat
    URI OmaChat
    VERSION 1.0
    SOURCES
        src/application/DaemonLink.hpp src/application/DaemonLink.cpp
        src/controllers/AppController.hpp src/controllers/AppController.cpp
        src/controllers/AppActions.cpp
        src/models/RowListModel.hpp src/models/RowListModel.cpp
        src/models/MessageListModel.hpp src/models/MessageListModel.cpp
        src/platform/ThemeProvider.hpp src/platform/ThemeProvider.cpp
        src/platform/SingleInstance.hpp src/platform/SingleInstance.cpp
    QML_FILES
        qml/Main.qml
        qml/components/Icon.qml
        qml/components/IconButton.qml
        qml/components/Avatar.qml
        qml/components/PresenceDot.qml
        qml/components/Badge.qml
        qml/components/FlatButton.qml
        qml/components/Field.qml
        qml/components/SectionLabel.qml
        qml/components/MenuPopup.qml
        qml/components/MenuAction.qml
        qml/pages/DaemonPage.qml
        qml/pages/LoginPage.qml
        qml/pages/CertificatePage.qml
        qml/pages/ConnectingPage.qml
        qml/views/MainView.qml
        qml/views/ServerRail.qml
        qml/views/ChannelSidebar.qml
        qml/views/ChatPane.qml
        qml/views/MessageDelegate.qml
        qml/views/Composer.qml
        qml/views/MemberList.qml
        qml/views/BottomBar.qml
        qml/views/StatusBanner.qml
        qml/views/EmptyState.qml
        qml/dialogs/Dialog.qml
        qml/dialogs/QuickSwitcher.qml
        qml/dialogs/SettingsDialog.qml
        qml/dialogs/TextPromptDialog.qml
        qml/dialogs/CreateChannelDialog.qml
        qml/dialogs/CommandHelp.qml
        qml/dialogs/SearchPanel.qml
        qml/dialogs/ConfirmDialog.qml
    RESOURCES
        icons/app.svg
        icons/hash.svg icons/speaker.svg icons/mic.svg icons/mic-off.svg icons/headphones.svg
        icons/headphones-off.svg icons/settings.svg icons/plus.svg icons/x.svg icons/reply.svg
        icons/edit.svg icons/trash.svg icons/copy.svg icons/search.svg icons/lock.svg icons/at.svg
        icons/chevron-down.svg icons/chevron-right.svg icons/hangup.svg icons/message.svg
        icons/smile.svg icons/link.svg icons/logout.svg icons/bell-off.svg icons/users.svg
        icons/server.svg icons/alert.svg icons/shield.svg icons/crown.svg
)

omachat_set_warnings(omachat)
# qmltyperegistrar includes QML-exposed headers by bare file name.
target_include_directories(omachat PRIVATE src src/controllers src/platform src/models)
target_link_libraries(omachat PRIVATE
    omachat_shared omachat_client_text
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Svg
    tomlplusplus::tomlplusplus)
set_target_properties(omachat PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)
install(TARGETS omachat RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
