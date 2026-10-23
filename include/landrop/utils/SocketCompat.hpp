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
    using socklen_t = int;
    using ssize_t = ptrdiff_t;

    #ifndef SHUT_RDWR
        #define SHUT_RDWR SD_BOTH
    #endif

    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif

    #ifndef AF_UNIX
        #define AF_UNIX 1
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

    inline int setSocketNonBlocking(socket_handle_t sock, bool nonBlocking) {
        u_long mode = nonBlocking ? 1 : 0;
        return ::ioctlsocket(sock, FIONBIO, &mode);
    }

    inline int socketpair(int domain, int type, int protocol, int sv[2]) {
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

        sv[0] = static_cast<int>(server);
        sv[1] = static_cast<int>(client);
        return 0;
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

    inline int closeSocket(int fd) {
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
#endif
