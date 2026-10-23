#include "landrop/core/NetworkSession.hpp"
#include "landrop/utils/SocketCompat.hpp"
#include <cstring>
#include <utility>
#include <algorithm>

namespace landrop {

NetworkSession::NetworkSession(std::string sessionId, std::string remoteIp, uint16_t remotePort)
    : m_sessionId(std::move(sessionId)),
      m_remoteIp(std::move(remoteIp)),
      m_remotePort(remotePort),
      m_connectedAt(std::chrono::steady_clock::now()),
      m_lastActivityAt(std::chrono::steady_clock::now()) {}

NetworkSession::~NetworkSession() {
    closeSession();
}

NetworkSession::NetworkSession(NetworkSession&& other) noexcept
    : m_sessionId(std::move(other.m_sessionId)),
      m_remoteIp(std::move(other.m_remoteIp)),
      m_remotePort(other.m_remotePort),
      m_socketFd(other.m_socketFd),
      m_listenFd(other.m_listenFd),
      m_state(other.m_state),
      m_bytesSent(other.m_bytesSent),
      m_bytesReceived(other.m_bytesReceived),
      m_timeoutSeconds(other.m_timeoutSeconds),
      m_connectedAt(other.m_connectedAt),
      m_lastActivityAt(other.m_lastActivityAt),
      m_tlsCipherSuite(std::move(other.m_tlsCipherSuite)),
      m_isEncrypted(other.m_isEncrypted),
      m_tlsEnabled(other.m_tlsEnabled),
      m_tlsConfig(std::move(other.m_tlsConfig)),
      m_tlsStatus(other.m_tlsStatus),
      m_peerFingerprint(std::move(other.m_peerFingerprint)),
      m_sslCtx(other.m_sslCtx),
      m_ssl(other.m_ssl) {
    other.m_socketFd = INVALID_SOCKET_VALUE;
    other.m_listenFd = INVALID_SOCKET_VALUE;
    other.m_sslCtx = nullptr;
    other.m_ssl = nullptr;
    other.m_tlsEnabled = false;
    other.m_tlsStatus = TlsStatus::None;
    other.m_state = ConnectionState::Disconnected;
}

NetworkSession& NetworkSession::operator=(NetworkSession&& other) noexcept {
    if (this != &other) {
        closeSession();
        m_sessionId = std::move(other.m_sessionId);
        m_remoteIp = std::move(other.m_remoteIp);
        m_remotePort = other.m_remotePort;
        m_socketFd = other.m_socketFd;
        m_listenFd = other.m_listenFd;
        m_state = other.m_state;
        m_bytesSent = other.m_bytesSent;
        m_bytesReceived = other.m_bytesReceived;
        m_timeoutSeconds = other.m_timeoutSeconds;
        m_connectedAt = other.m_connectedAt;
        m_lastActivityAt = other.m_lastActivityAt;
        m_tlsCipherSuite = std::move(other.m_tlsCipherSuite);
        m_isEncrypted = other.m_isEncrypted;
        m_tlsEnabled = other.m_tlsEnabled;
        m_tlsConfig = std::move(other.m_tlsConfig);
        m_tlsStatus = other.m_tlsStatus;
        m_peerFingerprint = std::move(other.m_peerFingerprint);
        m_sslCtx = other.m_sslCtx;
        m_ssl = other.m_ssl;

        other.m_socketFd = INVALID_SOCKET_VALUE;
        other.m_listenFd = INVALID_SOCKET_VALUE;
        other.m_sslCtx = nullptr;
        other.m_ssl = nullptr;
        other.m_tlsEnabled = false;
        other.m_tlsStatus = TlsStatus::None;
        other.m_state = ConnectionState::Disconnected;
    }
    return *this;
}

bool NetworkSession::attachSocket(socket_handle_t fd, ConnectionState state) {
    closeSession();
    m_socketFd = fd;
    m_state = state;
    m_connectedAt = std::chrono::steady_clock::now();
    m_lastActivityAt = m_connectedAt;
    return true;
}

bool NetworkSession::enableTls(const TlsConfig& config) {
    m_tlsConfig = config;
    m_tlsEnabled = true;
    m_tlsStatus = TlsStatus::None;
    return true;
}

TlsStatus NetworkSession::performTlsHandshake() {
    if (!isValidSocket(m_socketFd)) {
        m_tlsStatus = TlsStatus::SocketError;
        return m_tlsStatus;
    }

    TlsHelper::initOpenSsl();

    if (!m_sslCtx) {
        if (m_tlsConfig.role == TlsRole::Server) {
            m_sslCtx = TlsHelper::createServerContext(m_tlsConfig.certPem, m_tlsConfig.keyPem, m_tlsConfig.cipherSuites);
        } else {
            m_sslCtx = TlsHelper::createClientContext(m_tlsConfig.caCertPem, m_tlsConfig.cipherSuites);
        }
    }

    if (!m_sslCtx) {
        m_tlsStatus = TlsStatus::HandshakeFailed;
        return m_tlsStatus;
    }

    if (!m_ssl) {
        m_ssl = SSL_new(m_sslCtx);
        if (!m_ssl) {
            m_tlsStatus = TlsStatus::HandshakeFailed;
            return m_tlsStatus;
        }
        SSL_set_fd(m_ssl, static_cast<int>(m_socketFd));
    }

    if (m_tlsConfig.role == TlsRole::Server) {
        int ret = SSL_accept(m_ssl);
        if (ret <= 0) {
            unsigned long osslErr = ERR_get_error();
            char errBuf[256] = {0};
            ERR_error_string_n(osslErr, errBuf, sizeof(errBuf));
            std::string errStr(errBuf);

            if (errStr.find("http request") != std::string::npos ||
                errStr.find("wrong version") != std::string::npos ||
                errStr.find("unknown protocol") != std::string::npos ||
                errStr.find("unsupported protocol") != std::string::npos ||
                errStr.find("packet length too long") != std::string::npos) {
                m_tlsStatus = TlsStatus::ProtocolMismatch;
            } else {
                m_tlsStatus = TlsStatus::HandshakeFailed;
            }
            closeSession();
            return m_tlsStatus;
        }

        m_isEncrypted = true;
        const char* cipher = SSL_get_cipher_name(m_ssl);
        m_tlsCipherSuite = cipher ? cipher : "TLS_AES_256_GCM_SHA384";
        m_tlsStatus = TlsStatus::Connected;
        return m_tlsStatus;
    } else {
        int ret = SSL_connect(m_ssl);
        if (ret <= 0) {
            unsigned long osslErr = ERR_get_error();
            char errBuf[256] = {0};
            ERR_error_string_n(osslErr, errBuf, sizeof(errBuf));
            std::string errStr(errBuf);

            if (errStr.find("wrong version") != std::string::npos ||
                errStr.find("unknown protocol") != std::string::npos ||
                errStr.find("unsupported protocol") != std::string::npos ||
                errStr.find("packet length too long") != std::string::npos) {
                m_tlsStatus = TlsStatus::ProtocolMismatch;
            } else {
                m_tlsStatus = TlsStatus::HandshakeFailed;
            }
            closeSession();
            return m_tlsStatus;
        }

        // Verify peer certificate
        X509* peerCert = SSL_get_peer_certificate(m_ssl);
        if (!peerCert) {
            m_tlsStatus = TlsStatus::UntrustedCertificate;
            closeSession();
            return m_tlsStatus;
        }

        m_peerFingerprint = TlsHelper::computeFingerprint(peerCert);
        X509_free(peerCert);

        if (m_tlsConfig.verifyMode == TlsVerificationMode::VerifyPinnedFingerprint) {
            // Rule: Self-signed certificates without confirmation or pinning MUST be rejected by default!
            if (m_tlsConfig.expectedFingerprint.empty()) {
                m_tlsStatus = TlsStatus::UntrustedCertificate;
                closeSession();
                return m_tlsStatus;
            }
            if (!TlsHelper::fingerprintsMatch(m_peerFingerprint, m_tlsConfig.expectedFingerprint)) {
                m_tlsStatus = TlsStatus::FingerprintMismatch;
                closeSession();
                return m_tlsStatus;
            }
        } else if (m_tlsConfig.verifyMode == TlsVerificationMode::VerifySystemCa) {
            long verifyRes = SSL_get_verify_result(m_ssl);
            if (verifyRes != X509_V_OK) {
                m_tlsStatus = TlsStatus::UntrustedCertificate;
                closeSession();
                return m_tlsStatus;
            }
        }

        m_isEncrypted = true;
        const char* cipher = SSL_get_cipher_name(m_ssl);
        m_tlsCipherSuite = cipher ? cipher : "TLS_AES_256_GCM_SHA384";
        m_tlsStatus = TlsStatus::Connected;
        return m_tlsStatus;
    }
}

bool NetworkSession::connectTo(const std::string& ip, uint16_t port) {
    closeSession();
    m_remoteIp = ip;
    m_remotePort = port;

    m_socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (!isValidSocket(m_socketFd)) {
        m_state = ConnectionState::Disconnected;
        return false;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
        closeSession();
        return false;
    }

    m_state = ConnectionState::Handshake;
    if (connect(m_socketFd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        closeSession();
        return false;
    }

    m_state = ConnectionState::Connected;
    m_connectedAt = std::chrono::steady_clock::now();
    m_lastActivityAt = m_connectedAt;

    if (m_tlsEnabled) {
        if (performTlsHandshake() != TlsStatus::Connected) {
            closeSession();
            return false;
        }
    }

    return true;
}

bool NetworkSession::listenOn(uint16_t port) {
    closeSession();
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (!isValidSocket(m_listenFd)) {
        return false;
    }

    int opt = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closeSocket(m_listenFd);
        m_listenFd = INVALID_SOCKET_VALUE;
        return false;
    }

    if (listen(m_listenFd, 5) < 0) {
        closeSocket(m_listenFd);
        m_listenFd = INVALID_SOCKET_VALUE;
        return false;
    }

    m_state = ConnectionState::Handshake;
    return true;
}

bool NetworkSession::acceptIncoming() {
    if (!isValidSocket(m_listenFd)) {
        return false;
    }

    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    m_socketFd = accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (!isValidSocket(m_socketFd)) {
        return false;
    }

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    m_remoteIp = ipStr;
    m_remotePort = ntohs(clientAddr.sin_port);

    m_state = ConnectionState::Connected;
    m_connectedAt = std::chrono::steady_clock::now();
    m_lastActivityAt = m_connectedAt;

    if (m_tlsEnabled) {
        if (performTlsHandshake() != TlsStatus::Connected) {
            closeSession();
            return false;
        }
    }

    return true;
}

void NetworkSession::closeSession() {
    if (m_ssl) {
        SSL_shutdown(m_ssl);
        SSL_free(m_ssl);
        m_ssl = nullptr;
    }
    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
    }
    if (isValidSocket(m_socketFd)) {
        closeSocket(m_socketFd);
        m_socketFd = INVALID_SOCKET_VALUE;
    }
    if (isValidSocket(m_listenFd)) {
        closeSocket(m_listenFd);
        m_listenFd = INVALID_SOCKET_VALUE;
    }
    m_state = ConnectionState::Terminated;
    m_isEncrypted = false;
}

bool NetworkSession::sendAll(const uint8_t* data, size_t length) {
    if (!isValidSocket(m_socketFd) || m_state != ConnectionState::Connected) {
        return false;
    }
    size_t total = 0;
    while (total < length) {
        if (m_isEncrypted && m_ssl) {
            int toSend = static_cast<int>(std::min<size_t>(length - total, 32768));
            int bytes = SSL_write(m_ssl, data + total, toSend);
            if (bytes <= 0) {
                closeSession();
                return false;
            }
            total += static_cast<size_t>(bytes);
            m_bytesSent += static_cast<uint64_t>(bytes);
        } else {
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags |= MSG_NOSIGNAL;
#endif
            int toSend = static_cast<int>(std::min<size_t>(length - total, 32768));
            ssize_t bytes = send(m_socketFd, reinterpret_cast<const char*>(data + total), toSend, flags);
            if (bytes <= 0) {
                closeSession();
                return false;
            }
            total += static_cast<size_t>(bytes);
            m_bytesSent += static_cast<uint64_t>(bytes);
        }
    }
    updateActivity();
    return true;
}

bool NetworkSession::sendData(const std::vector<uint8_t>& data) {
    return sendAll(data.data(), data.size());
}

bool NetworkSession::receiveExact(uint8_t* buffer, size_t length) {
    if (!isValidSocket(m_socketFd) || m_state != ConnectionState::Connected) {
        return false;
    }
    size_t total = 0;
    while (total < length) {
        if (m_isEncrypted && m_ssl) {
            int toRead = static_cast<int>(std::min<size_t>(length - total, 32768));
            int bytes = SSL_read(m_ssl, buffer + total, toRead);
            if (bytes <= 0) {
                closeSession();
                return false;
            }
            total += static_cast<size_t>(bytes);
            m_bytesReceived += static_cast<uint64_t>(bytes);
        } else {
            int toRead = static_cast<int>(std::min<size_t>(length - total, 32768));
            ssize_t bytes = recv(m_socketFd, reinterpret_cast<char*>(buffer + total), toRead, 0);
            if (bytes <= 0) {
                closeSession();
                return false;
            }
            total += static_cast<size_t>(bytes);
            m_bytesReceived += static_cast<uint64_t>(bytes);
        }
    }
    updateActivity();
    return true;
}

bool NetworkSession::receiveData(std::vector<uint8_t>& buffer, size_t expectedSize) {
    buffer.resize(expectedSize);
    return receiveExact(buffer.data(), expectedSize);
}

bool NetworkSession::checkTimeout() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastActivityAt).count();
    return elapsed > static_cast<int64_t>(m_timeoutSeconds);
}

void NetworkSession::updateActivity() {
    m_lastActivityAt = std::chrono::steady_clock::now();
}

double NetworkSession::getElapsedSeconds() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - m_connectedAt).count();
}

} // namespace landrop
