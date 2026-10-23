cmake_minimum_required(VERSION 3.20)
project(lan_drop VERSION 1.0.0 LANGUAGES CXX)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Core Library Sources (Independent of GUI framework)
set(LANDROP_CORE_SOURCES
    src/core/FileChunkHasher.cpp
    src/core/TransferEngine.cpp
    src/core/MulticastBeacon.cpp
    src/core/NetworkSession.cpp
    src/core/SharedFolderManager.cpp
    src/core/TlsContext.cpp
    src/core/HttpClient.cpp
    src/peers/NetworkPeer.cpp
    src/peers/LocalPeer.cpp
    src/peers/CloudPeer.cpp
    src/tasks/TransferTask.cpp
    src/tasks/P2PTransferTask.cpp
    src/tasks/HttpWebTransferTask.cpp
    src/mobile/AirDropProtocolBridge.cpp
    src/mobile/LandropMobileBridge.cpp
    src/mobile/NfcPairingSession.cpp
    src/ui/DiscoveryPresenter.cpp
    src/ui/TransferPresenter.cpp
    src/ui/StoragePresenter.cpp
    src/ui/SettingsPresenter.cpp
)

find_package(OpenSSL QUIET)
if (OPENSSL_FOUND)
    message(STATUS "Found system OpenSSL ${OPENSSL_VERSION}")
    set(OPENSSL_TARGETS OpenSSL::SSL OpenSSL::Crypto)
else()
    message(STATUS "OpenSSL development package not found, falling back to local include headers and system runtime libraries")
    find_library(SSL_LIB NAMES ssl libssl.so.3 libssl PATHS /lib64 /usr/lib64 /usr/lib ${CMAKE_CURRENT_SOURCE_DIR}/lib)
    find_library(CRYPTO_LIB NAMES crypto libcrypto.so.3 libcrypto PATHS /lib64 /usr/lib64 /usr/lib ${CMAKE_CURRENT_SOURCE_DIR}/lib)
    if (NOT SSL_LIB OR NOT CRYPTO_LIB)
        message(FATAL_ERROR "OpenSSL libraries (libssl, libcrypto) not found. Please install openssl or openssl-devel.")
    endif()
    set(OPENSSL_TARGETS ${SSL_LIB} ${CRYPTO_LIB})
endif()

add_library(landrop_core STATIC ${LANDROP_CORE_SOURCES})
target_include_directories(landrop_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if (MSVC)
    target_compile_options(landrop_core PRIVATE /W4 /EHsc)
else()
    target_compile_options(landrop_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (WIN32)
    target_link_libraries(landrop_core PUBLIC ws2_32 iphlpapi ${OPENSSL_TARGETS})
else()
    target_link_libraries(landrop_core PUBLIC pthread ${OPENSSL_TARGETS})
endif()


# Find Qt 6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Test)

# Qt 6 UI Sources & Headers
set(LANDROP_QT_HEADERS
    include/landrop/ui/qt/QtDesktopApp.hpp
    include/landrop/ui/qt/QtDiscoveryWidget.hpp
    include/landrop/ui/qt/QtTransferWidget.hpp
    include/landrop/ui/qt/QtStorageWidget.hpp
    include/landrop/ui/qt/QtSettingsWidget.hpp
)

set(LANDROP_QT_SOURCES
    src/ui/qt/QtDesktopApp.cpp
    src/ui/qt/QtDiscoveryWidget.cpp
    src/ui/qt/QtTransferWidget.cpp
    src/ui/qt/QtStorageWidget.cpp
    src/ui/qt/QtSettingsWidget.cpp
)

set(LANDROP_RESOURCES
    resources/resources.qrc
)

# Native Qt 6 Executable
add_executable(lan_drop
    src/main.cpp
    ${LANDROP_QT_SOURCES}
    ${LANDROP_QT_HEADERS}
    ${LANDROP_RESOURCES}
)
target_compile_definitions(lan_drop PRIVATE LANDROP_BUILD_QT)
if (MSVC)
    target_compile_options(lan_drop PRIVATE /W4 /EHsc)
else()
    target_compile_options(lan_drop PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (WIN32)
    target_link_libraries(lan_drop PRIVATE
        landrop_core
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        ws2_32
        iphlpapi
    )
else()
    target_link_libraries(lan_drop PRIVATE
        landrop_core
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        pthread
    )
endif()

# Optional GTK3 Executable (if GTK3 is available)
find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(GTK3 QUIET gtk+-3.0)
    if (GTK3_FOUND)
        add_executable(lan_drop_gtk
            src/main.cpp
            src/ui/GtkDesktopApp.cpp
            src/ui/DesktopGuiEngine.cpp
        )
        target_compile_definitions(lan_drop_gtk PRIVATE LANDROP_BUILD_GTK)
        target_compile_options(lan_drop_gtk PRIVATE ${GTK3_CFLAGS} -Wall -Wextra -Wpedantic)
        target_include_directories(lan_drop_gtk PRIVATE ${GTK3_INCLUDE_DIRS})
        target_link_libraries(lan_drop_gtk PRIVATE
            landrop_core
            ${GTK3_LIBRARIES}
            X11
            dl
            pthread
        )
    endif()
endif()

# Testing
include(CTest)
if (BUILD_TESTING)
    add_executable(lan_drop_tests tests/unit_tests.cpp)
    if (MSVC)
        target_compile_options(lan_drop_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(lan_drop_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if (WIN32)
        target_link_libraries(lan_drop_tests PRIVATE landrop_core ws2_32 iphlpapi)
    else()
        target_link_libraries(lan_drop_tests PRIVATE landrop_core pthread)
    endif()
    add_test(NAME ComprehensiveTests COMMAND lan_drop_tests)

    # Automated Qt 6 GUI Tests Target (Runs headless via offscreen platform)
    add_executable(lan_drop_gui_tests
        tests/test_qt_gui.cpp
        ${LANDROP_QT_SOURCES}
        ${LANDROP_QT_HEADERS}
        ${LANDROP_RESOURCES}
    )
    target_compile_definitions(lan_drop_gui_tests PRIVATE LANDROP_BUILD_QT)
    if (MSVC)
        target_compile_options(lan_drop_gui_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(lan_drop_gui_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if (WIN32)
        target_link_libraries(lan_drop_gui_tests PRIVATE
            landrop_core
            Qt6::Widgets
            Qt6::Test
            Qt6::Network
            Qt6::Concurrent
            ws2_32
            iphlpapi
        )
    else()
        target_link_libraries(lan_drop_gui_tests PRIVATE
            landrop_core
            Qt6::Widgets
            Qt6::Test
            Qt6::Network
            Qt6::Concurrent
            pthread
        )
    endif()
    add_test(NAME QtGuiTests COMMAND lan_drop_gui_tests)
    set_tests_properties(QtGuiTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS lan_drop
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

install(FILES resources/lan-drop.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES resources/lan-drop.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

install(FILES resources/styles.qss resources/lan-drop.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/landrop
)

if (TARGET lan_drop_gtk)
    install(TARGETS lan_drop_gtk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
endif()

# Packaging with CPack
set(CPACK_PACKAGE_NAME "landrop")
set(CPACK_PACKAGE_VENDOR "LAN-Drop Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-Platform P2P and Hybrid Cloud File Transfer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_CONTACT "vladis@localhost")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_URL "https://github.com/landrop/lan-drop")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase-gui, qt6-qtbase, openssl-libs")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

set(CPACK_GENERATOR "TGZ;RPM")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/.git/"
    "/build/"
    "/obj/"
    "/bin/"
    "/lib/"
    "/downloads/"
    "/shared_drop/"
    "\\\\.DS_Store$"
    "/node_modules/"
    "/Design LAN-Drop macOS UI/"
)
include(CPack)

