qt_add_resources(FONT_RESOURCES fonts/fonts.qrc)
qt_add_resources(ICON_RESOURCES icons/icons.qrc)

add_executable(YukiSend
    main.cpp
    ${FONT_RESOURCES}
    ${ICON_RESOURCES}
    app/App.cpp
    network/Discovery.cpp
    network/TransferServer.cpp
    network/TransferClient.cpp
    network/MessageChannel.cpp
    network/ChatServer.cpp
    network/ChatClient.cpp
    ui/MainWindow.cpp
    ui/TrayIcon.cpp
    ui/CloseDialog.cpp
    ui/FingerprintDialog.cpp
    ui/NavBar.cpp
    ui/ChatView.cpp
    ui/ChatInputBar.cpp
    ui/MessageDelegate.cpp
    ui/FolderTreeState.cpp
    ui/MediaViewer.cpp
    ui/PeerListWidget.cpp
    ui/PeerItemDelegate.cpp
    ui/SearchBar.cpp
    ui/Animator.cpp
    ui/SettingsPage.cpp
    ui/PeerPanel.cpp
    theme/Fonts.cpp
    utils/FileUtils.cpp
    utils/ImageUtils.cpp
    utils/ThumbCache.cpp
    model/ChatStore.cpp
    model/AppSettings.cpp
    model/UnreadStore.cpp
    model/PeerStore.cpp
    model/OutboxStore.cpp
    utils/StartupHelper.cpp
)

target_compile_definitions(YukiSend PRIVATE APP_VERSION="1.0.0")

target_include_directories(YukiSend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(YukiSend PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(YukiSend PRIVATE dwmapi)

    # Generate a .rc file with an absolute path so windres finds the icon
    # regardless of where cmake --build is invoked from.
    set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/icons/icon.ico")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in
                   ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    target_sources(YukiSend PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)

    # WIN32_EXECUTABLE suppresses the console window on MSVC.
    # On MinGW it triggers a Qt6EntryPoint link bug (__imp___argc undefined);
    # use the subsystem linker flag directly instead.
    if(MSVC)
        set_target_properties(YukiSend PROPERTIES WIN32_EXECUTABLE TRUE)
    else()
        target_link_options(YukiSend PRIVATE -Wl,-subsystem,windows)
    endif()
endif()
