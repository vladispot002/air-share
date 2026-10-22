#pragma once

#include <string>
#include <vector>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>

namespace landrop {

enum class TlsRole {
    Server,
    Client
};

enum class TlsVerificationMode {
    VerifyPinnedFingerprint, // Pin specific SHA-256 fingerprint (device authentication / unilateral server auth)
    VerifySystemCa,          // Standard trusted root CAs / PKI (used for HTTPS)
    AllowAnyUntrusted        // Insecure / dev mode (explicit opt-in only)
};

enum class TlsStatus {
    None,
    Connected,
    HandshakeFailed,
    ProtocolMismatch,
    UntrustedCertificate,
    FingerprintMismatch,
    HostnameMismatch,
    CertificateExpired,
    SocketError
};

struct TlsCertificate {
    std::string certPem;
    std::string keyPem;
    std::string fingerprintSha256;
};

struct TlsConfig {
    TlsRole role{TlsRole::Client};
    std::string certPem;
    std::string keyPem;
    TlsVerificationMode verifyMode{TlsVerificationMode::VerifyPinnedFingerprint};
    std::string expectedFingerprint;
    std::string caCertPem;
    std::string cipherSuites{"TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"};
};

class TlsHelper {
public:
    static void initOpenSsl();
    static TlsCertificate generateCertificate(const std::string& commonName = "LAN-Drop Device", int daysValid = 365);
    static std::string computeFingerprint(X509* cert);
    static std::string computeFingerprint(const std::string& certPem);
    static bool fingerprintsMatch(const std::string& fp1, const std::string& fp2);
    static SSL_CTX* createServerContext(const std::string& certPem, const std::string& keyPem, const std::string& cipherSuites = "");
    static SSL_CTX* createClientContext(const std::string& caCertPem = "", const std::string& cipherSuites = "");
    static const char* statusToString(TlsStatus status);
};

} // namespace landrop
