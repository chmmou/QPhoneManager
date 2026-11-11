qt_add_library(qrcodegen STATIC
    qrcodegen.hpp
    qrcodegen.cpp
)
target_include_directories(qrcodegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(qrcodegen PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
