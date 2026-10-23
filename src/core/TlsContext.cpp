#include "landrop/core/TlsContext.hpp"
#include "landrop/utils/SocketCompat.hpp"
#include <mutex>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <csignal>
#include <openssl/x509v3.h>

namespace landrop {

static std::once_flag s_initFlag;

void TlsHelper::initOpenSsl() {
    std::call_once(s_initFlag, []() {
#ifdef SIGPIPE
        signal(SIGPIPE, SIG_IGN);
#endif
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
    });
}


TlsCertificate TlsHelper::generateCertificate(const std::string& commonName, int daysValid) {
    initOpenSsl();
    TlsCertificate result;

    EVP_PKEY* pkey = EVP_EC_gen("P-256");
    if (!pkey) {
        return result;
    }

    X509* x509 = X509_new();
    if (!x509) {
        EVP_PKEY_free(pkey);
        return result;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), static_cast<long>(daysValid) * 86400L);
    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("UA"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("LAN-Drop"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(commonName.c_str()), -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // Add X509v3 extensions: Basic Constraints & Subject Alternative Name (SAN)
    X509V3_CTX extCtx;
    X509V3_set_ctx_nodb(&extCtx);
    X509V3_set_ctx(&extCtx, x509, x509, nullptr, nullptr, 0);

    X509_EXTENSION* extBc = X509V3_EXT_conf_nid(nullptr, &extCtx, NID_basic_constraints, "critical,CA:TRUE");
    if (extBc) {
        X509_add_ext(x509, extBc, -1);
        X509_EXTENSION_free(extBc);
    }

    std::string sanValue;
    sockaddr_in dummyAddr{};
    if (inet_pton(AF_INET, commonName.c_str(), &dummyAddr.sin_addr) == 1) {
        sanValue = "IP:" + commonName;
    } else {
        sanValue = "DNS:" + commonName;
    }
    X509_EXTENSION* extSan = X509V3_EXT_conf_nid(nullptr, &extCtx, NID_subject_alt_name, sanValue.c_str());
    if (extSan) {
        X509_add_ext(x509, extSan, -1);
        X509_EXTENSION_free(extSan);
    }

    if (X509_sign(x509, pkey, EVP_sha256()) <= 0) {
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return result;
    }

    result.fingerprintSha256 = computeFingerprint(x509);

    // Export certificate to PEM
    BIO* certBio = BIO_new(BIO_s_mem());
    if (certBio) {
        if (PEM_write_bio_X509(certBio, x509) > 0) {
            char* data = nullptr;
            long len = BIO_get_mem_data(certBio, &data);
            if (len > 0 && data) {
                result.certPem.assign(data, static_cast<size_t>(len));
            }
        }
        BIO_free(certBio);
    }

    // Export private key to PEM
    BIO* keyBio = BIO_new(BIO_s_mem());
    if (keyBio) {
        if (PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr) > 0) {
            char* data = nullptr;
            long len = BIO_get_mem_data(keyBio, &data);
            if (len > 0 && data) {
                result.keyPem.assign(data, static_cast<size_t>(len));
            }
        }
        BIO_free(keyBio);
    }

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return result;
}

std::string TlsHelper::computeFingerprint(X509* cert) {
    if (!cert) return "";
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    if (X509_digest(cert, EVP_sha256(), md, &mdLen) <= 0) {
        return "";
    }
    std::ostringstream oss;
    for (unsigned int i = 0; i < mdLen; ++i) {
        if (i > 0) oss << ":";
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(md[i]);
    }
    return oss.str();
}

std::string TlsHelper::computeFingerprint(const std::string& certPem) {
    if (certPem.empty()) return "";
    BIO* bio = BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size()));
    if (!bio) return "";
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) return "";
    std::string fp = computeFingerprint(cert);
    X509_free(cert);
    return fp;
}

bool TlsHelper::fingerprintsMatch(const std::string& fp1, const std::string& fp2) {
    auto normalize = [](const std::string& s) {
        std::string res;
        res.reserve(s.size());
        for (char c : s) {
            if (c != ':' && c != ' ' && c != '-') {
                res.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            }
        }
        return res;
    };
    std::string n1 = normalize(fp1);
    std::string n2 = normalize(fp2);
    return !n1.empty() && n1 == n2;
}

SSL_CTX* TlsHelper::createServerContext(const std::string& certPem, const std::string& keyPem, const std::string& cipherSuites) {
    initOpenSsl();
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) return nullptr;

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    const std::string ciphers = cipherSuites.empty()
        ? "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"
        : cipherSuites;
    SSL_CTX_set_ciphersuites(ctx, ciphers.c_str());

    if (!certPem.empty()) {
        BIO* bio = BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size()));
        if (bio) {
            X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            if (cert) {
                SSL_CTX_use_certificate(ctx, cert);
                X509_free(cert);
            }
            BIO_free(bio);
        }
    }

    if (!keyPem.empty()) {
        BIO* bio = BIO_new_mem_buf(keyPem.data(), static_cast<int>(keyPem.size()));
        if (bio) {
            EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
            if (pkey) {
                SSL_CTX_use_PrivateKey(ctx, pkey);
                EVP_PKEY_free(pkey);
            }
            BIO_free(bio);
        }
    }

    if (SSL_CTX_check_private_key(ctx) <= 0) {
        // Warning: private key does not match cert or missing
    }

    return ctx;
}

SSL_CTX* TlsHelper::createClientContext(const std::string& caCertPem, const std::string& cipherSuites) {
    initOpenSsl();
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return nullptr;

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    const std::string ciphers = cipherSuites.empty()
        ? "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"
        : cipherSuites;
    SSL_CTX_set_ciphersuites(ctx, ciphers.c_str());

    // Load default system trusted CA certificates
    SSL_CTX_set_default_verify_paths(ctx);

    if (!caCertPem.empty()) {
        BIO* bio = BIO_new_mem_buf(caCertPem.data(), static_cast<int>(caCertPem.size()));
        if (bio) {
            X509_STORE* store = SSL_CTX_get_cert_store(ctx);
            if (store) {
                while (X509* caCert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) {
                    X509_STORE_add_cert(store, caCert);
                    X509_free(caCert);
                }
                X509_STORE_set_flags(store, X509_V_FLAG_PARTIAL_CHAIN);
            }
            BIO_free(bio);
        }
    }

    return ctx;
}

const char* TlsHelper::statusToString(TlsStatus status) {
    switch (status) {
        case TlsStatus::None: return "None";
        case TlsStatus::Connected: return "Connected";
        case TlsStatus::HandshakeFailed: return "HandshakeFailed";
        case TlsStatus::ProtocolMismatch: return "ProtocolMismatch";
        case TlsStatus::UntrustedCertificate: return "UntrustedCertificate";
        case TlsStatus::FingerprintMismatch: return "FingerprintMismatch";
        case TlsStatus::HostnameMismatch: return "HostnameMismatch";
        case TlsStatus::CertificateExpired: return "CertificateExpired";
        case TlsStatus::SocketError: return "SocketError";
        default: return "Unknown";
    }
}

} // namespace landrop
