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
