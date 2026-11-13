qt_add_library(qpm_core STATIC
    core/Error.h
    core/Holidays.h
    core/Holidays.cpp
    core/Logging.h
    core/Logging.cpp
    core/Version.h
    core/Version.cpp
    core/backup/BackupArchive.h
    core/backup/BackupArchive.cpp
    core/backup/BackupService.h
    core/backup/BackupService.cpp
    core/backup/ZipStore.h
    core/backup/ZipStore.cpp
    core/codec/AppListCodec.h
    core/codec/AppListCodec.cpp
    core/codec/AtText.h
    core/codec/AtText.cpp
    core/codec/CalMetadata.h
    core/codec/CalMetadata.cpp
    core/codec/CallLogCodec.h
    core/codec/CallLogCodec.cpp
    core/codec/ContentLines.h
    core/codec/ContentLines.cpp
    core/codec/CsvTable.h
    core/codec/CsvTable.cpp
    core/codec/FolderListing.h
    core/codec/FolderListing.cpp
    core/codec/ICalendar.h
    core/codec/ICalendar.cpp
    core/codec/Limits.h
    core/codec/Limits.cpp
    core/codec/MessageExport.h
    core/codec/MessageExport.cpp
    core/codec/NotificationCodec.h
    core/codec/NotificationCodec.cpp
    core/codec/PbMetadata.h
    core/codec/PbMetadata.cpp
    core/codec/PhoneTime.h
    core/codec/PhoneTime.cpp
    core/codec/PhoneVCal.h
    core/codec/PhoneVCal.cpp
    core/codec/PhoneVCard.h
    core/codec/PhoneVCard.cpp
    core/codec/Recurrence.h
    core/codec/Recurrence.cpp
    core/codec/SafeXml.h
    core/codec/SafeXml.cpp
    core/codec/SmsLength.h
    core/codec/SmsLength.cpp
    core/codec/VCardInterchange.h
    core/codec/VCardInterchange.cpp
    core/codec/VMessage.h
    core/codec/VMessage.cpp
    core/codec/VNote.h
    core/codec/VNote.cpp
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
    core/service/AlarmService.h
    core/service/AlarmService.cpp
    core/service/AppsService.h
    core/service/AppsService.cpp
    core/service/CalendarService.h
    core/service/CalendarService.cpp
    core/service/CallLogService.h
    core/service/CallLogService.cpp
    core/service/CallMonitor.h
    core/service/CallMonitor.cpp
    core/service/ClipboardService.h
    core/service/ClipboardService.cpp
    core/service/ContactsService.h
    core/service/ContactsService.cpp
    core/service/DeviceStatusService.h
    core/service/DeviceStatusService.cpp
    core/service/FileService.h
    core/service/FileService.cpp
    core/service/MessagesService.h
    core/service/MessagesService.cpp
    core/service/NotesService.h
    core/service/NotesService.cpp
    core/service/NotificationService.h
    core/service/NotificationService.cpp
    core/service/OrganizerSession.h
    core/service/OrganizerSession.cpp
    core/service/SaveMode.h
    core/service/ScreenMirrorClient.h
    core/service/ScreenMirrorClient.cpp
    core/service/ThumbnailChannel.h
    core/service/ThumbnailChannel.cpp
    core/sync/IrmcSync.h
    core/sync/IrmcSync.cpp
    core/store/CallStore.h
    core/store/CallStore.cpp
    core/store/ContactStore.h
    core/store/ContactStore.cpp
    core/store/MessageStore.h
    core/store/MessageStore.cpp
    core/store/OrganizerStore.h
    core/store/OrganizerStore.cpp
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
    platform/DesktopNotifier.h
    platform/DesktopNotifier.cpp
    platform/SecretServiceTypes.h
    platform/SecretServiceTypes.cpp
    platform/SecretStore.h
    platform/SecretStore.cpp
    platform/SessionBus.h
    platform/SessionBus.cpp
    platform/TrayIcon.h
    platform/TrayIcon.cpp
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
    components/Scrim.qml
    components/SectionEntry.qml
    components/SectionPage.qml
    components/Sections.qml
    components/SignalBars.qml
    components/Sidebar.qml
    components/SidebarButton.qml
    components/StatusBar.qml
    dialogs/AboutDialog.qml
    dialogs/AlarmsDialog.qml
    dialogs/BackupDialog.qml
    dialogs/CallWindow.qml
    dialogs/CalendarsDialog.qml
    dialogs/CategoriesDialog.qml
    dialogs/ConfirmDeleteDialog.qml
    dialogs/ConflictDialog.qml
    dialogs/ConnectDialog.qml
    dialogs/DuplicatesDialog.qml
    dialogs/PhotoCropDialog.qml
    dialogs/PinDialog.qml
    dialogs/ProfilesDialog.qml
    dialogs/QrConnectDialog.qml
    dialogs/SettingsDialog.qml
    dialogs/VersionConflictDialog.qml
    pages/HelpPage.qml
    pages/OverviewPage.qml
    pages/apps/AppsPage.qml
    pages/calls/CallDetail.qml
    pages/calls/CallList.qml
    pages/calls/CallsPage.qml
    pages/contacts/ContactDetail.qml
    pages/contacts/ContactForm.qml
    pages/contacts/ContactList.qml
    pages/contacts/ContactValueSection.qml
    pages/contacts/ContactsPage.qml
    pages/files/FileConflictDialog.qml
    pages/files/FileNameDialog.qml
    pages/files/FilesPage.qml
    pages/files/TransferPanel.qml
    pages/messages/ComposeView.qml
    pages/messages/ConversationList.qml
    pages/messages/ConversationView.qml
    pages/messages/MessagesPage.qml
    pages/messages/ScheduledList.qml
    pages/notifications/NotificationsPage.qml
    pages/organizer/CalendarGrid.qml
    pages/organizer/CalendarPage.qml
    pages/organizer/EventDetail.qml
    pages/organizer/EventForm.qml
    pages/organizer/EventList.qml
    pages/organizer/NoteForm.qml
    pages/organizer/NoteList.qml
    pages/organizer/NotesPage.qml
    pages/organizer/TaskForm.qml
    pages/organizer/TaskList.qml
    pages/organizer/TasksPage.qml
    pages/screen/ScreenPage.qml
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
        ui/PhoneNumberText.h
        ui/PhoneNumberText.cpp
        ui/ProfileListModel.h
        ui/ProfileListModel.cpp
        ui/QrCodeImage.h
        ui/QrCodeImage.cpp
        ui/QrCodeProvider.h
        ui/QrCodeProvider.cpp
        ui/ReducedMotionFilter.h
        ui/ReducedMotionFilter.cpp
        ui/Settings.h
        ui/Settings.cpp
        ui/TintedIcon.h
        ui/TintedIcon.cpp
        ui/apps/AppListModel.h
        ui/apps/AppListModel.cpp
        ui/backup/BackupController.h
        ui/backup/BackupController.cpp
        ui/backup/BackupSectionModel.h
        ui/backup/BackupSectionModel.cpp
        ui/calls/CallListModel.h
        ui/calls/CallListModel.cpp
        ui/calls/CallsArea.h
        ui/calls/CallsArea.cpp
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
        ui/desktop/CallControl.h
        ui/desktop/CallControl.cpp
        ui/desktop/DesktopIntegration.h
        ui/desktop/DesktopIntegration.cpp
        ui/desktop/NotificationListModel.h
        ui/desktop/NotificationListModel.cpp
        ui/files/FileBrowserModel.h
        ui/files/FileBrowserModel.cpp
        ui/files/TransferQueue.h
        ui/files/TransferQueue.cpp
        ui/help/AboutInfo.h
        ui/help/AboutInfo.cpp
        ui/help/HelpController.h
        ui/help/HelpController.cpp
        ui/help/HelpTopicModel.h
        ui/help/HelpTopicModel.cpp
        ui/messages/ConversationListModel.h
        ui/messages/ConversationListModel.cpp
        ui/messages/MessageComposer.h
        ui/messages/MessageComposer.cpp
        ui/messages/MessageListModel.h
        ui/messages/MessageListModel.cpp
        ui/messages/MessagesArea.h
        ui/messages/MessagesArea.cpp
        ui/messages/ScheduledListModel.h
        ui/messages/ScheduledListModel.cpp
        ui/organizer/AlarmListModel.h
        ui/organizer/AlarmListModel.cpp
        ui/organizer/CalendarArea.h
        ui/organizer/CalendarArea.cpp
        ui/organizer/CalendarDaysModel.h
        ui/organizer/CalendarDaysModel.cpp
        ui/organizer/CalendarListModel.h
        ui/organizer/CalendarListModel.cpp
        ui/organizer/CalendarModel.h
        ui/organizer/CalendarModel.cpp
        ui/organizer/CategoryModel.h
        ui/organizer/CategoryModel.cpp
        ui/organizer/EventEditor.h
        ui/organizer/EventEditor.cpp
        ui/organizer/NoteEditor.h
        ui/organizer/NoteEditor.cpp
        ui/organizer/NoteListModel.h
        ui/organizer/NoteListModel.cpp
        ui/organizer/NotesArea.h
        ui/organizer/NotesArea.cpp
        ui/organizer/OrganizerText.h
        ui/organizer/OrganizerText.cpp
        ui/organizer/TaskEditor.h
        ui/organizer/TaskEditor.cpp
        ui/organizer/TaskListModel.h
        ui/organizer/TaskListModel.cpp
        ui/organizer/TasksArea.h
        ui/organizer/TasksArea.cpp
        ui/screen/ScreenMirror.h
        ui/screen/ScreenMirror.cpp
        ui/screen/ScreenMirrorItem.h
        ui/screen/ScreenMirrorItem.cpp
)

set(qpm_ui_icons
    app/qphonemanager.svg
    tabler/address-book.svg
    tabler/alert-triangle.svg
    tabler/antenna-bars-5.svg
    tabler/apps.svg
    tabler/archive.svg
    tabler/arrow-back-up.svg
    tabler/arrows-maximize.svg
    tabler/battery.svg
    tabler/bell-off.svg
    tabler/bell.svg
    tabler/bluetooth.svg
    tabler/calendar.svg
    tabler/camera.svg
    tabler/check.svg
    tabler/checklist.svg
    tabler/chevron-left.svg
    tabler/chevron-right.svg
    tabler/clipboard.svg
    tabler/clock.svg
    tabler/copy.svg
    tabler/cpu.svg
    tabler/database.svg
    tabler/device-floppy.svg
    tabler/device-mobile.svg
    tabler/device-sd-card.svg
    tabler/download.svg
    tabler/edit.svg
    tabler/external-link.svg
    tabler/filter.svg
    tabler/file.svg
    tabler/folder-plus.svg
    tabler/folder.svg
    tabler/help.svg
    tabler/home.svg
    tabler/info-circle.svg
    tabler/keyboard.svg
    tabler/layout-grid.svg
    tabler/list.svg
    tabler/message.svg
    tabler/microphone-off.svg
    tabler/messages.svg
    tabler/notes.svg
    tabler/phone-call.svg
    tabler/phone-incoming.svg
    tabler/phone-off.svg
    tabler/phone-outgoing.svg
    tabler/phone-x.svg
    tabler/phone.svg
    tabler/photo.svg
    tabler/plug-connected-x.svg
    tabler/plug-connected.svg
    tabler/player-play.svg
    tabler/player-stop.svg
    tabler/plus.svg
    tabler/qrcode.svg
    tabler/refresh.svg
    tabler/rotate-clockwise.svg
    tabler/screen-share.svg
    tabler/send.svg
    tabler/settings.svg
    tabler/square.svg
    tabler/trash.svg
    tabler/upload.svg
    tabler/usb.svg
    tabler/user.svg
    tabler/users.svg
    tabler/volume.svg
    tabler/wifi.svg
    tabler/x.svg
)
list(TRANSFORM qpm_ui_icons PREPEND "${PROJECT_SOURCE_DIR}/res/icons/")
qt_add_resources(qpm_ui "qpm_ui_icons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/res/icons"
    FILES ${qpm_ui_icons}
)

# The license texts the About dialog shows, unchanged from the repository.
set(qpm_license_texts
    "${PROJECT_SOURCE_DIR}/LICENSE|QPhoneManager.txt"
    "${PROJECT_SOURCE_DIR}/res/icons/tabler/LICENSE|tabler-icons.txt"
    "${PROJECT_SOURCE_DIR}/third_party/qrcodegen/LICENSE|qrcodegen.txt"
)
set(qpm_license_files "")
foreach(entry IN LISTS qpm_license_texts)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 file)
    list(GET parts 1 alias)
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${alias}")
    list(APPEND qpm_license_files "${file}")
endforeach()
qt_add_resources(qpm_ui "qpm_ui_licenses"
    PREFIX "/licenses"
    FILES ${qpm_license_files}
)

# The help: one compressed help file per language, built from the Markdown
# handbook. The topics are the areas of the program in the order of the
# table of contents; F1 looks a chapter up by its topic.
set(qpm_help_topics
    getting-started
    overview
    contacts
    calls
    messages
    calendar
    tasks
    notes
    files
    apps
    screen
    notifications
    backup
    settings
)
list(JOIN qpm_help_topics "," qpm_help_topic_list)
# Only the Qt installation of the build is searched, never PATH.
find_program(QPM_QHELPGENERATOR qhelpgenerator
    PATHS
        "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBEXECS}"
        "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
    NO_DEFAULT_PATH
    REQUIRED
)
set(qpm_help_dir "${CMAKE_CURRENT_BINARY_DIR}/help")
set(qpm_help_files "")
foreach(language IN ITEMS de en)
    set(source "${PROJECT_SOURCE_DIR}/doc/handbook/${language}")
    set(chapters "${qpm_help_topics}")
    list(TRANSFORM chapters PREPEND "${source}/")
    list(TRANSFORM chapters APPEND ".md")
    set(output "${qpm_help_dir}/qphonemanager_${language}.qch")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND helpgen
            --language ${language}
            --source "${source}"
            --topics "${qpm_help_topic_list}"
            --qhelpgenerator "${QPM_QHELPGENERATOR}"
            --work "${qpm_help_dir}/${language}"
            --output "${output}"
            --depfile "${qpm_help_dir}/qphonemanager_${language}.d"
        DEPENDS helpgen ${chapters}
        DEPFILE "${qpm_help_dir}/qphonemanager_${language}.d"
        COMMENT "Building the help file for ${language}"
        VERBATIM
    )
    list(APPEND qpm_help_files "${output}")
endforeach()
qt_add_resources(qpm_ui "qpm_ui_help"
    PREFIX "/help"
    BASE "${qpm_help_dir}"
    FILES ${qpm_help_files}
)
qpm_add_handbook_pdf_target(TOPICS ${qpm_help_topics})
# The generated QML type registration includes headers by their base name only.
target_include_directories(qpm_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/apps
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/backup
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/calls
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/contacts
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/desktop
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/files
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/help
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/messages
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/organizer
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/screen
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
        Qt6::Help
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
