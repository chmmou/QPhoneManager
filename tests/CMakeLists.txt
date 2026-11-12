add_subdirectory(fakephone)
add_subdirectory(device)

# Adds the test tests/core/<name>.cpp, linked against the core library and the
# fake phone.
function(qpm_add_core_test name)
    qt_add_executable(${name} core/${name}.cpp)
    target_link_libraries(${name} PRIVATE qpm_core qpm_fakephone Qt6::Test)
    qpm_set_project_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

qpm_add_core_test(tst_version)
qpm_add_core_test(tst_attext)
qpm_add_core_test(tst_phonetime)
qpm_add_core_test(tst_obexpacket)
qpm_add_core_test(tst_discovery)
qpm_add_core_test(tst_pinauth)
qpm_add_core_test(tst_adbbridge)
qpm_add_core_test(tst_limits)
qpm_add_core_test(tst_atsession)
qpm_add_core_test(tst_obexclient)
qpm_add_core_test(tst_phoneconnection)
qpm_add_core_test(tst_irmcsync)
qpm_add_core_test(tst_profilestore)
target_link_libraries(tst_profilestore PRIVATE Qt6::Sql Qt6::Concurrent)
qpm_add_core_test(tst_devicestatus)
qpm_add_core_test(tst_phonevcard)
qpm_add_core_test(tst_vcardinterchange)
qpm_add_core_test(tst_pbmetadata)
qpm_add_core_test(tst_contactsservice)
qpm_add_core_test(tst_calllogcodec)
qpm_add_core_test(tst_calllogservice)
qpm_add_core_test(tst_vmessage)
qpm_add_core_test(tst_messages)
target_link_libraries(tst_messages PRIVATE Qt6::Sql)
qpm_add_core_test(tst_messageexport)
qpm_add_core_test(tst_phonevcal)
qpm_add_core_test(tst_recurrence)
qpm_add_core_test(tst_icalendar)
qpm_add_core_test(tst_calmetadata)
qpm_add_core_test(tst_vnote)
qpm_add_core_test(tst_holidays)
qpm_add_core_test(tst_alarms)
qpm_add_core_test(tst_organizerservice)
qpm_add_core_test(tst_folderlisting)
qpm_add_core_test(tst_files)
qpm_add_core_test(tst_thumbnails)
qpm_add_core_test(tst_applist)
qpm_add_core_test(tst_appsservice)
target_link_libraries(tst_appsservice PRIVATE Qt6::Gui)
qpm_add_core_test(tst_screenmirror)
qpm_add_core_test(tst_notifications)
qpm_add_core_test(tst_clipboard)
qpm_add_core_test(tst_callmonitor)
qpm_add_core_test(tst_zipstore)
qpm_add_core_test(tst_backup)
# Every function syncs all areas with the fake phone first.
set_tests_properties(tst_backup PROPERTIES TIMEOUT 120)
qpm_add_core_test(tst_nothirdpartyservices)
target_compile_definitions(tst_nothirdpartyservices PRIVATE
    "QPM_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\""
)

qt_add_executable(tst_rfcomm bluetooth/tst_rfcomm.cpp)
target_link_libraries(tst_rfcomm PRIVATE qpm_bluetooth qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_rfcomm)
add_test(NAME tst_rfcomm COMMAND tst_rfcomm)
set_tests_properties(tst_rfcomm PROPERTIES TIMEOUT 60)

qt_add_executable(tst_secretstore platform/tst_secretstore.cpp)
target_link_libraries(tst_secretstore PRIVATE qpm_platform qpm_fakephone Qt6::DBus Qt6::Test)
qpm_set_project_warnings(tst_secretstore)
add_test(NAME tst_secretstore COMMAND tst_secretstore)
set_tests_properties(tst_secretstore PROPERTIES TIMEOUT 60)

qt_add_executable(tst_desktopnotifier platform/tst_desktopnotifier.cpp)
target_link_libraries(tst_desktopnotifier PRIVATE qpm_platform qpm_fakephone Qt6::DBus Qt6::Test)
qpm_set_project_warnings(tst_desktopnotifier)
add_test(NAME tst_desktopnotifier COMMAND tst_desktopnotifier)
set_tests_properties(tst_desktopnotifier PROPERTIES TIMEOUT 60)

qt_add_executable(tst_trayicon platform/tst_trayicon.cpp)
target_link_libraries(tst_trayicon PRIVATE qpm_platform Qt6::Widgets Qt6::Test)
qpm_set_project_warnings(tst_trayicon)
add_test(NAME tst_trayicon COMMAND tst_trayicon)
set_tests_properties(tst_trayicon PROPERTIES TIMEOUT 60)

qt_add_executable(tst_settings ui/tst_settings.cpp)
target_link_libraries(tst_settings PRIVATE qpm_ui Qt6::Test)
qpm_set_project_warnings(tst_settings)
add_test(NAME tst_settings COMMAND tst_settings)
set_tests_properties(tst_settings PROPERTIES TIMEOUT 60)

qt_add_executable(tst_appcontroller ui/tst_appcontroller.cpp)
target_link_libraries(tst_appcontroller PRIVATE qpm_ui qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_appcontroller)
add_test(NAME tst_appcontroller COMMAND tst_appcontroller)
set_tests_properties(tst_appcontroller PROPERTIES TIMEOUT 120)

qt_add_executable(tst_contacts ui/tst_contacts.cpp)
target_link_libraries(tst_contacts PRIVATE qpm_ui qpm_fakephone Qt6::Gui Qt6::Test)
qpm_set_project_warnings(tst_contacts)
add_test(NAME tst_contacts COMMAND tst_contacts)
set_tests_properties(tst_contacts PROPERTIES TIMEOUT 120)

qt_add_executable(tst_calls ui/tst_calls.cpp)
target_link_libraries(tst_calls PRIVATE qpm_ui qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_calls)
add_test(NAME tst_calls COMMAND tst_calls)
set_tests_properties(tst_calls PROPERTIES TIMEOUT 120)

qt_add_executable(tst_organizer ui/tst_organizer.cpp)
target_link_libraries(tst_organizer PRIVATE qpm_ui qpm_fakephone Qt6::Test)
# The English catalogue is built along with the program.
add_dependencies(tst_organizer qphonemanager)
target_compile_definitions(tst_organizer PRIVATE
    "QPM_TRANSLATIONS_DIR=\"${PROJECT_BINARY_DIR}/src\""
)
qpm_set_project_warnings(tst_organizer)
add_test(NAME tst_organizer COMMAND tst_organizer)
set_tests_properties(tst_organizer PROPERTIES TIMEOUT 180)

# The core test of the message service has the name tst_messages already.
qt_add_executable(tst_messagearea ui/tst_messages.cpp)
target_link_libraries(tst_messagearea PRIVATE qpm_ui qpm_fakephone Qt6::Test)
# The English catalogue is built along with the program.
add_dependencies(tst_messagearea qphonemanager)
target_compile_definitions(tst_messagearea PRIVATE
    "QPM_TRANSLATIONS_DIR=\"${PROJECT_BINARY_DIR}/src\""
)
qpm_set_project_warnings(tst_messagearea)
add_test(NAME tst_messagearea COMMAND tst_messagearea)
set_tests_properties(tst_messagearea PROPERTIES TIMEOUT 120)

qt_add_executable(tst_transferqueue ui/tst_transferqueue.cpp)
target_link_libraries(tst_transferqueue PRIVATE qpm_ui qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_transferqueue)
add_test(NAME tst_transferqueue COMMAND tst_transferqueue)
set_tests_properties(tst_transferqueue PROPERTIES TIMEOUT 120)

qt_add_executable(tst_filebrowser ui/tst_filebrowser.cpp)
target_link_libraries(tst_filebrowser PRIVATE qpm_ui qpm_fakephone Qt6::Gui Qt6::Test)
qpm_set_project_warnings(tst_filebrowser)
add_test(NAME tst_filebrowser COMMAND tst_filebrowser)
set_tests_properties(tst_filebrowser PROPERTIES TIMEOUT 120)

qt_add_executable(tst_apps ui/tst_apps.cpp)
target_link_libraries(tst_apps PRIVATE qpm_ui qpm_fakephone Qt6::Gui Qt6::Test)
qpm_set_project_warnings(tst_apps)
add_test(NAME tst_apps COMMAND tst_apps)
set_tests_properties(tst_apps PROPERTIES TIMEOUT 120)

# The core test of the screen client has the name tst_screenmirror already.
qt_add_executable(tst_screenview ui/tst_screenmirror.cpp)
target_link_libraries(tst_screenview PRIVATE qpm_ui qpm_fakephone Qt6::Quick Qt6::Test)
qpm_set_project_warnings(tst_screenview)
add_test(NAME tst_screenview COMMAND tst_screenview)
set_tests_properties(tst_screenview PROPERTIES TIMEOUT 120)

qt_add_executable(tst_desktop ui/tst_desktop.cpp)
target_link_libraries(tst_desktop PRIVATE qpm_ui qpm_fakephone Qt6::DBus Qt6::Gui Qt6::Test)
qpm_set_project_warnings(tst_desktop)
add_test(NAME tst_desktop COMMAND tst_desktop)
set_tests_properties(tst_desktop PROPERTIES TIMEOUT 120)

qt_add_executable(tst_backupcontroller ui/tst_backupcontroller.cpp)
target_link_libraries(tst_backupcontroller PRIVATE qpm_ui qpm_fakephone Qt6::Test)
# The English catalogue is built along with the program.
add_dependencies(tst_backupcontroller qphonemanager)
target_compile_definitions(tst_backupcontroller PRIVATE
    "QPM_TRANSLATIONS_DIR=\"${PROJECT_BINARY_DIR}/src\""
)
qpm_set_project_warnings(tst_backupcontroller)
add_test(NAME tst_backupcontroller COMMAND tst_backupcontroller)
set_tests_properties(tst_backupcontroller PROPERTIES TIMEOUT 120)

qt_add_executable(tst_accessibility ui/tst_accessibility.cpp)
target_link_libraries(tst_accessibility PRIVATE qpm_ui qpm_uiplugin qpm_fakephone Qt6::Quick Qt6::Test)
qt_add_resources(tst_accessibility "tst_accessibility_config"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/res"
    FILES "${PROJECT_SOURCE_DIR}/res/qtquickcontrols2.conf"
)
qpm_set_project_warnings(tst_accessibility)
add_test(NAME tst_accessibility COMMAND tst_accessibility)
set_tests_properties(tst_accessibility PROPERTIES TIMEOUT 300)

qt_add_executable(tst_screens ui/tst_screens.cpp)
target_link_libraries(tst_screens PRIVATE qpm_ui qpm_uiplugin qpm_fakephone Qt6::Quick Qt6::Test)
qt_add_resources(tst_screens "tst_screens_config"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/res"
    FILES "${PROJECT_SOURCE_DIR}/res/qtquickcontrols2.conf"
)
# The German catalogue is built along with the program.
add_dependencies(tst_screens qphonemanager)
target_compile_definitions(tst_screens PRIVATE
    "QPM_TRANSLATIONS_DIR=\"${PROJECT_BINARY_DIR}/src\""
)
qpm_set_project_warnings(tst_screens)
add_test(NAME tst_screens COMMAND tst_screens)
set_tests_properties(tst_screens PROPERTIES TIMEOUT 120)

# A QML module with the fake phone, for the Qt Quick tests below.
qt_add_library(qpm_testsupport STATIC)
qt_add_qml_module(qpm_testsupport
    URI QPhoneManager.TestSupport
    VERSION 1.0
    SOURCES
        qml/support/TestPhone.h
        qml/support/TestPhone.cpp
)
target_include_directories(qpm_testsupport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml/support)
target_link_libraries(qpm_testsupport PRIVATE qpm_fakephone Qt6::Qml)
qpm_set_project_warnings(qpm_testsupport)
qpm_set_project_warnings(qpm_testsupportplugin)

qt_add_executable(tst_qml qml/tst_qml.cpp)
target_link_libraries(tst_qml PRIVATE
    qpm_ui
    qpm_uiplugin
    qpm_testsupport
    qpm_testsupportplugin
    Qt6::Quick
    Qt6::QuickTest
)
qt_add_resources(tst_qml "tst_qml_config"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/res"
    FILES "${PROJECT_SOURCE_DIR}/res/qtquickcontrols2.conf"
)
qpm_set_project_warnings(tst_qml)
add_test(NAME tst_qml COMMAND tst_qml -input "${CMAKE_CURRENT_SOURCE_DIR}/qml")
set_tests_properties(tst_qml PROPERTIES TIMEOUT 180)
