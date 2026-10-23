#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #pragma comment(lib, "ws2_32.lib")

    using socket_handle_t = SOCKET;
    using socklen_compat_t = int;

    #ifndef SHUT_RDWR
        #define SHUT_RDWR SD_BOTH
    #endif

    inline int closeSocket(int fd) {
        if (fd < 0) return 0;
        return ::closesocket(static_cast<SOCKET>(fd));
    }

    inline bool initSocketSubsystem() {
        WSADATA wsa;
        return ::WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }

    inline void cleanupSocketSubsystem() {
        ::WSACleanup();
    }
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>

    using socket_handle_t = int;
    using socklen_compat_t = socklen_t;

    inline int closeSocket(int fd) {
        if (fd < 0) return 0;
        return ::close(fd);
    }

    inline bool initSocketSubsystem() {
        return true;
    }

    inline void cleanupSocketSubsystem() {}
#endif
