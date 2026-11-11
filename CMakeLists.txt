cmake_minimum_required(VERSION 3.28)

project(QPhoneManager
    VERSION 1.0.0
    DESCRIPTION "Manage an Android phone from the Linux desktop"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# QML modules are built below this directory in a path that follows their URI,
# which qmllint and the QML import scanner rely on.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core
    Network
    Concurrent
    Sql
    Bluetooth
    Gui
    Svg
    Widgets
    DBus
    Qml
    Quick
    QuickControls2
    QuickDialogs2
    QuickLayouts
    Help
    Test
    QuickTest
    LinguistTools
)
qt_standard_project_setup(REQUIRES 6.9
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES de en
)

# Without this call POSITION_INDEPENDENT_CODE from the presets would not reach
# the link step of the executable.
include(CheckPIESupported)
check_pie_supported()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(ProjectWarnings)
include(Clazy)

enable_testing()

add_subdirectory(third_party/qrcodegen)
add_subdirectory(src)
add_subdirectory(tests)

qpm_add_clazy_target()
