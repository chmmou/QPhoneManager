qt_add_library(qpm_devicetest STATIC
    CommandGuard.h
    CommandGuard.cpp
)
target_include_directories(qpm_devicetest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qpm_devicetest PUBLIC qpm_core)
qpm_set_project_warnings(qpm_devicetest)

qt_add_executable(tst_commandguard tst_commandguard.cpp)
target_link_libraries(tst_commandguard PRIVATE qpm_devicetest qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_commandguard)
add_test(NAME tst_commandguard COMMAND tst_commandguard)
set_tests_properties(tst_commandguard PROPERTIES TIMEOUT 60)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_connection tst_device_connection.cpp)
target_link_libraries(tst_device_connection PRIVATE qpm_devicetest qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_device_connection)
add_test(NAME tst_device_connection COMMAND tst_device_connection)
set_tests_properties(tst_device_connection PROPERTIES TIMEOUT 300 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_contacts tst_device_contacts.cpp)
target_link_libraries(tst_device_contacts PRIVATE qpm_devicetest qpm_fakephone Qt6::Gui Qt6::Test)
qpm_set_project_warnings(tst_device_contacts)
add_test(NAME tst_device_contacts COMMAND tst_device_contacts)
set_tests_properties(tst_device_contacts PROPERTIES TIMEOUT 300 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_calls tst_device_calls.cpp)
target_link_libraries(tst_device_calls PRIVATE qpm_devicetest qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_device_calls)
add_test(NAME tst_device_calls COMMAND tst_device_calls)
set_tests_properties(tst_device_calls PROPERTIES TIMEOUT 300 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_messages tst_device_messages.cpp)
target_link_libraries(tst_device_messages PRIVATE qpm_devicetest qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_device_messages)
add_test(NAME tst_device_messages COMMAND tst_device_messages)
set_tests_properties(tst_device_messages PROPERTIES TIMEOUT 300 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_organizer tst_device_organizer.cpp)
target_link_libraries(tst_device_organizer PRIVATE qpm_devicetest qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_device_organizer)
add_test(NAME tst_device_organizer COMMAND tst_device_organizer)
set_tests_properties(tst_device_organizer PROPERTIES TIMEOUT 900 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_files tst_device_files.cpp)
target_link_libraries(tst_device_files PRIVATE qpm_devicetest qpm_fakephone Qt6::Gui Qt6::Test)
qpm_set_project_warnings(tst_device_files)
add_test(NAME tst_device_files COMMAND tst_device_files)
set_tests_properties(tst_device_files PROPERTIES TIMEOUT 900 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_apps tst_device_apps.cpp)
target_link_libraries(tst_device_apps PRIVATE qpm_devicetest qpm_fakephone Qt6::Gui Qt6::Test)
qpm_set_project_warnings(tst_device_apps)
add_test(NAME tst_device_apps COMMAND tst_device_apps)
set_tests_properties(tst_device_apps PROPERTIES TIMEOUT 900 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_screen tst_device_screen.cpp)
target_link_libraries(tst_device_screen PRIVATE qpm_devicetest qpm_fakephone qpm_ui Qt6::Test)
qpm_set_project_warnings(tst_device_screen)
add_test(NAME tst_device_screen COMMAND tst_device_screen)
set_tests_properties(tst_device_screen PROPERTIES TIMEOUT 300 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_desktop tst_device_desktop.cpp)
target_link_libraries(tst_device_desktop PRIVATE qpm_devicetest qpm_fakephone qpm_ui Qt6::Test)
qpm_set_project_warnings(tst_device_desktop)
add_test(NAME tst_device_desktop COMMAND tst_device_desktop)
set_tests_properties(tst_device_desktop PROPERTIES TIMEOUT 300 LABELS device)

# Runs only against a phone, see the README; without one it reports itself as skipped.
qt_add_executable(tst_device_backup tst_device_backup.cpp)
target_link_libraries(tst_device_backup PRIVATE qpm_devicetest qpm_fakephone Qt6::Test)
qpm_set_project_warnings(tst_device_backup)
add_test(NAME tst_device_backup COMMAND tst_device_backup)
set_tests_properties(tst_device_backup PROPERTIES TIMEOUT 900 LABELS device)
