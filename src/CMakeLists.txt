qt_add_library(qpm_core STATIC
    core/Error.h
    core/Logging.h
    core/Logging.cpp
    core/Version.h
    core/Version.cpp
    core/codec/AtText.h
    core/codec/AtText.cpp
    core/codec/CsvTable.h
    core/codec/CsvTable.cpp
    core/codec/Limits.h
    core/codec/Limits.cpp
    core/codec/PbMetadata.h
    core/codec/PbMetadata.cpp
    core/codec/PhoneTime.h
    core/codec/PhoneTime.cpp
    core/codec/PhoneVCard.h
    core/codec/PhoneVCard.cpp
    core/codec/SafeXml.h
    core/codec/SafeXml.cpp
    core/codec/VCardInterchange.h
    core/codec/VCardInterchange.cpp
    core/protocol/AdbBridge.h
    core/protocol/AdbBridge.cpp
    core/protocol/AtSession.h
    core/protocol/AtSession.cpp
    core/protocol/Discovery.h
    core/protocol/Discovery.cpp
    core/protocol/ObexClient.h
    core/protocol/ObexClient.cpp
    core/protocol/ObexPacket.h
    core/protocol/ObexPacket.cpp
    core/protocol/PhoneConnection.h
    core/protocol/PhoneConnection.cpp
    core/protocol/PinAuth.h
    core/protocol/PinAuth.cpp
    core/protocol/TcpTransport.h
    core/protocol/TcpTransport.cpp
    core/protocol/Transport.h
    core/service/ContactsService.h
    core/service/ContactsService.cpp
    core/service/DeviceStatusService.h
    core/service/DeviceStatusService.cpp
    core/sync/IrmcSync.h
    core/sync/IrmcSync.cpp
    core/store/ContactStore.h
    core/store/ContactStore.cpp
    core/store/ProfileStore.h
    core/store/ProfileStore.cpp
    core/sync/SyncStoreAdapter.h
)
target_include_directories(qpm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(qpm_core PRIVATE "QPM_VERSION=\"${PROJECT_VERSION}\"")
target_link_libraries(qpm_core
    PUBLIC
        Qt6::Core
        Qt6::Network
    PRIVATE
        Qt6::Concurrent
        Qt6::Sql
)
qpm_set_project_warnings(qpm_core)

qt_add_library(qpm_bluetooth STATIC
    bluetooth/BluetoothDeviceFinder.h
    bluetooth/BluetoothDeviceFinder.cpp
    bluetooth/RfcommTransport.h
    bluetooth/RfcommTransport.cpp
    bluetooth/SerialPortProfile.h
    bluetooth/SerialPortProfile.cpp
)
target_include_directories(qpm_bluetooth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qpm_bluetooth
    PUBLIC
        qpm_core
        Qt6::Bluetooth
)
qpm_set_project_warnings(qpm_bluetooth)

qt_add_library(qpm_platform STATIC
    platform/SecretServiceTypes.h
    platform/SecretServiceTypes.cpp
    platform/SecretStore.h
    platform/SecretStore.cpp
    platform/SessionBus.h
    platform/SessionBus.cpp
)
target_include_directories(qpm_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qpm_platform
    PUBLIC
        qpm_core
        Qt6::Core
        Qt6::DBus
    PRIVATE
        Qt6::Widgets
)
qpm_set_project_warnings(qpm_platform)

qt_add_library(qpm_ui STATIC)
set(qpm_ui_qml_dir "${PROJECT_SOURCE_DIR}/qml/QPhoneManager/Ui")
set(qpm_ui_qml_files
    Main.qml
    Theme.qml
    TypeScale.qml
    components/AppHeader.qml
    components/AppShortcuts.qml
    components/ContactAvatar.qml
    components/DisconnectedBanner.qml
    components/EmptyState.qml
    components/FocusRing.qml
    components/FoundPhoneList.qml
    components/InfoRow.qml
    components/ListDetailView.qml
    components/MessageBar.qml
    components/OverviewCard.qml
    components/SectionEntry.qml
    components/SectionPage.qml
    components/Sections.qml
    components/SignalBars.qml
    components/Sidebar.qml
    components/SidebarButton.qml
    components/StatusBar.qml
    dialogs/ConflictDialog.qml
    dialogs/ConnectDialog.qml
    dialogs/DuplicatesDialog.qml
    dialogs/PhotoCropDialog.qml
    dialogs/PinDialog.qml
    dialogs/ProfilesDialog.qml
    dialogs/QrConnectDialog.qml
    dialogs/SettingsDialog.qml
    pages/OverviewPage.qml
    pages/contacts/ContactDetail.qml
    pages/contacts/ContactForm.qml
    pages/contacts/ContactList.qml
    pages/contacts/ContactValueSection.qml
    pages/contacts/ContactsPage.qml
)
set(qpm_ui_qml_sources "")
foreach(file IN LISTS qpm_ui_qml_files)
    # The QML sources live outside this directory; the alias keeps their path
    # inside the module the same as below qml/QPhoneManager/Ui.
    set_source_files_properties("${qpm_ui_qml_dir}/${file}"
        PROPERTIES QT_RESOURCE_ALIAS "${file}"
    )
    list(APPEND qpm_ui_qml_sources "${qpm_ui_qml_dir}/${file}")
endforeach()
set_source_files_properties(
    "${qpm_ui_qml_dir}/Theme.qml"
    "${qpm_ui_qml_dir}/TypeScale.qml"
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)
qt_add_qml_module(qpm_ui
    URI QPhoneManager.Ui
    VERSION 1.0
    QML_FILES
        ${qpm_ui_qml_sources}
    SOURCES
        ui/AppController.h
        ui/AppController.cpp
        ui/DeviceStatusModel.h
        ui/DeviceStatusModel.cpp
        ui/FoundPhoneModel.h
        ui/FoundPhoneModel.cpp
        ui/ProfileListModel.h
        ui/ProfileListModel.cpp
        ui/QrCodeImage.h
        ui/QrCodeImage.cpp
        ui/QrCodeProvider.h
        ui/QrCodeProvider.cpp
        ui/Settings.h
        ui/Settings.cpp
        ui/TintedIcon.h
        ui/TintedIcon.cpp
        ui/contacts/ContactDetailModel.h
        ui/contacts/ContactDetailModel.cpp
        ui/contacts/ContactEditor.h
        ui/contacts/ContactEditor.cpp
        ui/contacts/ContactListModel.h
        ui/contacts/ContactListModel.cpp
        ui/contacts/ContactsArea.h
        ui/contacts/ContactsArea.cpp
        ui/contacts/ContactTypes.h
        ui/contacts/ContactTypes.cpp
        ui/contacts/ContactValueModel.h
        ui/contacts/ContactValueModel.cpp
        ui/contacts/DuplicateListModel.h
        ui/contacts/DuplicateListModel.cpp
        ui/contacts/PhoneImageProvider.h
        ui/contacts/PhoneImageProvider.cpp
)

set(qpm_ui_icons
    app/qphonemanager.svg
    tabler/address-book.svg
    tabler/alert-triangle.svg
    tabler/antenna-bars-5.svg
    tabler/apps.svg
    tabler/battery.svg
    tabler/bell.svg
    tabler/bluetooth.svg
    tabler/calendar.svg
    tabler/check.svg
    tabler/checklist.svg
    tabler/chevron-left.svg
    tabler/copy.svg
    tabler/cpu.svg
    tabler/database.svg
    tabler/device-mobile.svg
    tabler/download.svg
    tabler/edit.svg
    tabler/folder.svg
    tabler/help.svg
    tabler/home.svg
    tabler/info-circle.svg
    tabler/message.svg
    tabler/messages.svg
    tabler/notes.svg
    tabler/phone.svg
    tabler/photo.svg
    tabler/plug-connected-x.svg
    tabler/plug-connected.svg
    tabler/plus.svg
    tabler/qrcode.svg
    tabler/refresh.svg
    tabler/screen-share.svg
    tabler/settings.svg
    tabler/trash.svg
    tabler/upload.svg
    tabler/usb.svg
    tabler/user.svg
    tabler/users.svg
    tabler/wifi.svg
    tabler/x.svg
)
list(TRANSFORM qpm_ui_icons PREPEND "${PROJECT_SOURCE_DIR}/res/icons/")
qt_add_resources(qpm_ui "qpm_ui_icons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/res/icons"
    FILES ${qpm_ui_icons}
)
# The generated QML type registration includes headers by their base name only.
target_include_directories(qpm_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/contacts
)
target_link_libraries(qpm_ui
    PUBLIC
        qpm_core
        qpm_bluetooth
        qpm_platform
        Qt6::Qml
        Qt6::Quick
    PRIVATE
        qrcodegen
        Qt6::Concurrent
        Qt6::Svg
)
qpm_set_project_warnings(qpm_ui)
qpm_set_project_warnings(qpm_uiplugin)

qt_add_executable(qphonemanager
    main.cpp
)
target_link_libraries(qphonemanager
    PRIVATE
        qpm_core
        qpm_bluetooth
        qpm_platform
        qpm_ui
        qpm_uiplugin
        Qt6::Widgets
        Qt6::Qml
)
qpm_set_project_warnings(qphonemanager)

qt_add_resources(qphonemanager "qphonemanager_config"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/res"
    FILES "${PROJECT_SOURCE_DIR}/res/qtquickcontrols2.conf"
)

set(qt_translations_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
set(qt_catalogues
    qtbase_de.qm
    qtbase_en.qm
    qtdeclarative_de.qm
    qtdeclarative_en.qm
)
set(qt_catalogue_files "")
foreach(catalogue IN LISTS qt_catalogues)
    if(NOT EXISTS "${qt_translations_dir}/${catalogue}")
        message(FATAL_ERROR "Qt translation catalogue ${catalogue} not found in ${qt_translations_dir}")
    endif()
    list(APPEND qt_catalogue_files "${qt_translations_dir}/${catalogue}")
endforeach()
qt_add_resources(qphonemanager "qt_translations"
    PREFIX "/i18n"
    BASE "${qt_translations_dir}"
    FILES ${qt_catalogue_files}
)

qt_add_translations(TARGETS qphonemanager
    TS_FILES
        "${PROJECT_SOURCE_DIR}/res/i18n/qphonemanager_de.ts"
        "${PROJECT_SOURCE_DIR}/res/i18n/qphonemanager_en.ts"
    NO_GENERATE_PLURALS_TS_FILE
)

install(TARGETS qphonemanager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
