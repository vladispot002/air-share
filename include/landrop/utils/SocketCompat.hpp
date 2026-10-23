#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #include <process.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")

    using socket_handle_t = SOCKET;
    using socklen_compat_t = int;
    #ifndef socklen_t
    using socklen_t = int;
    #endif
    #ifndef _SSIZE_T_DEFINED
    #ifndef ssize_t
    using ssize_t = ptrdiff_t;
    #define _SSIZE_T_DEFINED
    #endif
    #endif

    constexpr socket_handle_t INVALID_SOCKET_HANDLE = INVALID_SOCKET;
    constexpr socket_handle_t INVALID_SOCKET_VALUE = INVALID_SOCKET;

    #ifndef SHUT_RDWR
        #define SHUT_RDWR SD_BOTH
    #endif

    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif

    #ifndef AF_UNIX
        #define AF_UNIX 1
    #endif

    inline bool isValidSocket(socket_handle_t fd) {
        return fd != INVALID_SOCKET;
    }

    inline int closeSocket(socket_handle_t fd) {
        if (fd == INVALID_SOCKET) return 0;
        return ::closesocket(fd);
    }

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

    inline int setSocketNonBlocking(socket_handle_t sock, bool nonBlocking) {
        u_long mode = nonBlocking ? 1 : 0;
        return ::ioctlsocket(sock, FIONBIO, &mode);
    }

    inline int setSocketTimeout(socket_handle_t sock, int timeoutSec) {
        DWORD timeoutMs = static_cast<DWORD>(timeoutSec > 0 ? (timeoutSec * 1000) : 5000);
        int r1 = ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        int r2 = ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        return (r1 == 0 && r2 == 0) ? 0 : -1;
    }

    inline int socketpair(int domain, int type, int protocol, socket_handle_t sv[2]) {
        (void)domain; (void)type; (void)protocol;
        SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) return -1;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        int reuse = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            ::closesocket(listener);
            return -1;
        }

        int addrLen = sizeof(addr);
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrLen) == SOCKET_ERROR) {
            ::closesocket(listener);
            return -1;
        }

        if (::listen(listener, 1) == SOCKET_ERROR) {
            ::closesocket(listener);
            return -1;
        }

        SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET) {
            ::closesocket(listener);
            return -1;
        }

        if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            ::closesocket(client);
            ::closesocket(listener);
            return -1;
        }

        SOCKET server = ::accept(listener, nullptr, nullptr);
        ::closesocket(listener);
        if (server == INVALID_SOCKET) {
            ::closesocket(client);
            return -1;
        }

        sv[0] = server;
        sv[1] = client;
        return 0;
    }

    inline int socketpair(int domain, int type, int protocol, int sv[2]) {
        socket_handle_t handles[2];
        int res = socketpair(domain, type, protocol, handles);
        if (res == 0) {
            sv[0] = static_cast<int>(handles[0]);
            sv[1] = static_cast<int>(handles[1]);
        }
        return res;
    }
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>

    using socket_handle_t = int;
    using socklen_compat_t = socklen_t;

    constexpr socket_handle_t INVALID_SOCKET_HANDLE = -1;
    constexpr socket_handle_t INVALID_SOCKET_VALUE = -1;

    inline bool isValidSocket(socket_handle_t fd) {
        return fd >= 0;
    }

    inline int closeSocket(socket_handle_t fd) {
        if (fd < 0) return 0;
        return ::close(fd);
    }

    inline bool initSocketSubsystem() {
        return true;
    }

    inline void cleanupSocketSubsystem() {}

    inline int setSocketNonBlocking(socket_handle_t sock, bool nonBlocking) {
        int flags = ::fcntl(sock, F_GETFL, 0);
        if (flags < 0) return -1;
        return ::fcntl(sock, F_SETFL, nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
    }

    inline int setSocketTimeout(socket_handle_t sock, int timeoutSec) {
        struct timeval tv{};
        tv.tv_sec = timeoutSec > 0 ? timeoutSec : 5;
        tv.tv_usec = 0;
        int r1 = ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
        int r2 = ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
        return (r1 == 0 && r2 == 0) ? 0 : -1;
    }
#endif
