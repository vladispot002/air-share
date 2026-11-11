# CMake module for libdatachannel integration in LAN-Drop
# Configures optional WebRTC DataChannel capabilities while preserving lightweight default builds.

option(LANDROP_ENABLE_WEBRTC "Enable WebRTC DataChannel transport using libdatachannel" OFF)

if (LANDROP_ENABLE_WEBRTC)
    message(STATUS "[LAN-Drop] WebRTC DataChannel transport enabled")
    
    # 1. Try finding pre-installed system package
    find_package(LibDataChannel CONFIG QUIET)
    
    # 2. Fall back to FetchContent if system library is not available
    if (NOT LibDataChannel_FOUND)
        message(STATUS "[LAN-Drop] Fetching libdatachannel v0.22.2 via FetchContent...")
        include(FetchContent)
        
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for submodules" FORCE)
        set(NO_EXAMPLES ON CACHE BOOL "Disable libdatachannel examples" FORCE)
        set(NO_TESTS ON CACHE BOOL "Disable libdatachannel tests" FORCE)
        set(PREFER_SYSTEM_LIB OFF CACHE BOOL "Prefer system libraries for libdatachannel dependencies" FORCE)
        set(NO_MEDIA ON CACHE BOOL "Disable libdatachannel media transport" FORCE)
        
        FetchContent_Declare(
            LibDataChannel
            GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
            GIT_TAG v0.22.2
            GIT_SHALLOW TRUE
            EXCLUDE_FROM_ALL
        )
        FetchContent_MakeAvailable(LibDataChannel)
    endif()

    add_compile_definitions(LANDROP_HAVE_WEBRTC=1)
else()
    message(STATUS "[LAN-Drop] WebRTC DataChannel transport disabled (Stage 1 modular baseline)")
endif()
