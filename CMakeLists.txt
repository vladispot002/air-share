cmake_minimum_required(VERSION 3.20)
project(lan_drop VERSION 1.0.2 LANGUAGES CXX)

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
    src/core/WireProtocolV2.cpp
    src/core/TcpSocketChannel.cpp
    src/core/WebRtcDataChannel.cpp
    src/core/SignalingClient.cpp
    src/core/TurnConfigClient.cpp
    src/core/WebRtcSessionCoordinator.cpp
    src/core/DoctorEngine.cpp
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
    src/ui/QrCodeGenerator.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Test)

if (NOT DEFINED OPENSSL_ROOT_DIR AND EXISTS "$ENV{HOME}/.local/include/openssl")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")
endif()
find_package(OpenSSL REQUIRED)

add_library(landrop_core STATIC ${LANDROP_CORE_SOURCES})
target_include_directories(landrop_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if (MSVC)
    target_compile_options(landrop_core PRIVATE /W4 /EHsc)
else()
    target_compile_options(landrop_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(landrop_core PUBLIC
    Qt6::Core
    Qt6::Network
    OpenSSL::SSL
    OpenSSL::Crypto
)

if (WIN32)
    target_link_libraries(landrop_core PUBLIC ws2_32 iphlpapi)
else()
    target_link_libraries(landrop_core PUBLIC pthread)
endif()

# Optional WebRTC / libdatachannel integration module
include(cmake/LibDataChannel.cmake)
if (LANDROP_ENABLE_WEBRTC)
    target_link_libraries(landrop_core PUBLIC LibDataChannel::LibDataChannel)
endif()

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
if (WIN32)
    set_target_properties(lan_drop PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
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

install(FILES resources/io.landrop.LAN-Drop.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
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
set(CPACK_PACKAGE_CONTACT "support@landrop.io")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/landrop/lan-drop")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_URL "https://github.com/landrop/lan-drop")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase-gui, qt6-qtbase, openssl-libs")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

# Debian / Ubuntu DEB Properties
set(CPACK_DEBIAN_PACKAGE_NAME "landrop")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/landrop/lan-drop")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.34), libstdc++6 (>= 11), libssl3 (>= 3.0.0), libqt6widgets6, hicolor-icon-theme")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "coturn")

# Architecture detection compatible with any host (does not require dpkg)
if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7.*|armhf")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86|x86")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "riscv64")
    else()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
endif()
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

set(CPACK_GENERATOR "TGZ;RPM;DEB")
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

