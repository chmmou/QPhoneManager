qt_add_library(qpm_fakephone STATIC
    FakePhone.h
    FakePhone.cpp
    WaitFor.h
)
target_include_directories(qpm_fakephone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qpm_fakephone
    PUBLIC
        qpm_core
        Qt6::Network
        Qt6::Test
)
qpm_set_project_warnings(qpm_fakephone)
