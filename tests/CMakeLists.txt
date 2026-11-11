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
target_link_libraries(tst_profilestore PRIVATE Qt6::Sql)
qpm_add_core_test(tst_devicestatus)
qpm_add_core_test(tst_phonevcard)
qpm_add_core_test(tst_vcardinterchange)
qpm_add_core_test(tst_pbmetadata)
qpm_add_core_test(tst_contactsservice)
qpm_add_core_test(tst_calllogcodec)
qpm_add_core_test(tst_calllogservice)
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

qt_add_executable(tst_accessibility ui/tst_accessibility.cpp)
target_link_libraries(tst_accessibility PRIVATE qpm_ui qpm_uiplugin qpm_fakephone Qt6::Quick Qt6::Test)
qt_add_resources(tst_accessibility "tst_accessibility_config"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/res"
    FILES "${PROJECT_SOURCE_DIR}/res/qtquickcontrols2.conf"
)
qpm_set_project_warnings(tst_accessibility)
add_test(NAME tst_accessibility COMMAND tst_accessibility)
set_tests_properties(tst_accessibility PROPERTIES TIMEOUT 120)

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
