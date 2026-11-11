qt_add_library(qpm_core STATIC
    core/Error.h
    core/Logging.h
    core/Logging.cpp
    core/Version.h
    core/Version.cpp
    core/codec/AtText.h
    core/codec/AtText.cpp
    core/codec/Limits.h
    core/codec/Limits.cpp
    core/codec/PhoneTime.h
    core/codec/PhoneTime.cpp
    core/codec/SafeXml.h
    core/codec/SafeXml.cpp
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
    core/sync/IrmcSync.h
    core/sync/IrmcSync.cpp
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
    platform/SessionBus.h
    platform/SessionBus.cpp
)
target_include_directories(qpm_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qpm_platform
    PUBLIC
        Qt6::Core
    PRIVATE
        Qt6::DBus
        Qt6::Widgets
)
qpm_set_project_warnings(qpm_platform)

qt_add_library(qpm_ui STATIC)
set(qpm_ui_qml_dir "${PROJECT_SOURCE_DIR}/qml/QPhoneManager/Ui")
# The QML sources live outside this directory; the alias keeps their path
# inside the module the same as below qml/QPhoneManager/Ui.
set_source_files_properties("${qpm_ui_qml_dir}/Main.qml"
    PROPERTIES QT_RESOURCE_ALIAS Main.qml
)
qt_add_qml_module(qpm_ui
    URI QPhoneManager.Ui
    VERSION 1.0
    QML_FILES
        "${qpm_ui_qml_dir}/Main.qml"
)
# The generated QML type registration includes headers by their base name only.
target_include_directories(qpm_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui
)
target_link_libraries(qpm_ui
    PUBLIC
        Qt6::Qml
        Qt6::Quick
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
