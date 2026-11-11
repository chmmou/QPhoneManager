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
qpm_add_core_test(tst_nothirdpartyservices)
target_compile_definitions(tst_nothirdpartyservices PRIVATE
    "QPM_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\""
)

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

qt_add_executable(tst_accessibility ui/tst_accessibility.cpp)
target_link_libraries(tst_accessibility PRIVATE qpm_ui qpm_uiplugin Qt6::Quick Qt6::Test)
qt_add_resources(tst_accessibility "tst_accessibility_config"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/res"
    FILES "${PROJECT_SOURCE_DIR}/res/qtquickcontrols2.conf"
)
qpm_set_project_warnings(tst_accessibility)
add_test(NAME tst_accessibility COMMAND tst_accessibility)
set_tests_properties(tst_accessibility PROPERTIES TIMEOUT 120)
