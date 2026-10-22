cmake_minimum_required(VERSION 3.20)
project(lan_drop VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Core Library Sources (Independent of GUI framework)
set(LANDROP_CORE_SOURCES
    src/core/FileChunkHasher.cpp
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
target_compile_options(landrop_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(landrop_core PUBLIC pthread ${OPENSSL_TARGETS})


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

# Native Qt 6 Executable
add_executable(lan_drop
    src/main.cpp
    ${LANDROP_QT_SOURCES}
    ${LANDROP_QT_HEADERS}
)
target_compile_definitions(lan_drop PRIVATE LANDROP_BUILD_QT)
target_compile_options(lan_drop PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lan_drop PRIVATE
    landrop_core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    pthread
)

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

# Automated Unit Tests Target
enable_testing()
add_executable(lan_drop_tests tests/unit_tests.cpp)
target_compile_options(lan_drop_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lan_drop_tests PRIVATE
    landrop_core
    pthread
)
add_test(NAME ComprehensiveTests COMMAND lan_drop_tests)

# Automated Qt 6 GUI Tests Target (Runs headless via offscreen platform)
add_executable(lan_drop_gui_tests
    tests/test_qt_gui.cpp
    ${LANDROP_QT_SOURCES}
    ${LANDROP_QT_HEADERS}
)
target_compile_definitions(lan_drop_gui_tests PRIVATE LANDROP_BUILD_QT)
target_compile_options(lan_drop_gui_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lan_drop_gui_tests PRIVATE
    landrop_core
    Qt6::Widgets
    Qt6::Test
    Qt6::Network
    Qt6::Concurrent
    pthread
)
add_test(NAME QtGuiTests COMMAND lan_drop_gui_tests)

