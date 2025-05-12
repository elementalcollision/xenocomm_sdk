#include "xenocomm/core/certificate_auth_provider.hpp"
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509_vfy.h>
#include <chrono>
#include <fstream>
#include <sstream>

namespace xenocomm {
namespace core {

namespace {
    // Helper function to convert OpenSSL errors to string
    std::string getOpenSSLError() {
        BIO* bio = BIO_new(BIO_s_mem());
        ERR_print_errors(bio);
        char* buf;
        size_t len = BIO_get_mem_data(bio, &buf);
        std::string errorMsg(buf, len);
        BIO_free(bio);
        return errorMsg;
    }

    // Helper to convert ASN1_TIME to time_t
    // OpenSSL 3.x removed ASN1_TIME_to_time_t, so we need our own implementation
    time_t asn1TimeToTimeT(const ASN1_TIME* asn1Time) {
        int days = 0;
        int seconds = 0;
        
        // Get difference between ASN1_TIME and current time
        auto now = std::chrono::system_clock::now();
        auto nowTimeT = std::chrono::system_clock::to_time_t(now);
        struct tm* nowTm = gmtime(&nowTimeT);
        ASN1_TIME* nowAsn1 = ASN1_TIME_set(nullptr, nowTimeT);
        
        // Calculate difference
        if (!ASN1_TIME_diff(&days, &seconds, nowAsn1, asn1Time)) {
            ASN1_TIME_free(nowAsn1);
            return 0; // Error
        }
        
        ASN1_TIME_free(nowAsn1);
        
        // Convert difference to time_t
        return nowTimeT + days * 24 * 60 * 60 + seconds;
    }

    // Helper to check certificate validity period
    bool checkValidityPeriod(X509* cert, int64_t maxValidityDays) {
        const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
        const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
        
        // Convert ASN1_TIME to time_t
        auto now = std::chrono::system_clock::now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        
        time_t notBeforeTime = asn1TimeToTimeT(notBefore);
        time_t notAfterTime = asn1TimeToTimeT(notAfter);
        
        // Check if certificate is currently valid
        if (nowTime < notBeforeTime || nowTime > notAfterTime) {
            return false;
        }
        
        // Check validity period
        auto validityDays = (notAfterTime - notBeforeTime) / (24 * 60 * 60);
        return validityDays <= maxValidityDays;
    }

    // Helper to check if DN is in allowed list
    bool isAllowedDN(X509* cert, const std::vector<std::string>& allowedDNs) {
        if (allowedDNs.empty()) return true;  // Empty list means all DNs allowed
        
        char subjectName[256];
        X509_NAME_oneline(X509_get_subject_name(cert), subjectName, sizeof(subjectName));
        
        std::string dn(subjectName);
        for (const auto& allowedDN : allowedDNs) {
            if (dn.find(allowedDN) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
}

class CertificateAuthProvider::Impl {
public:
    explicit Impl(CertificateAuthConfig config) 
        : config_(std::move(config)), store_(nullptr) {}
    
    ~Impl() {
        if (store_) {
            X509_STORE_free(store_);
        }
    }

    bool initialize() {
        // Initialize OpenSSL store
        store_ = X509_STORE_new();
        if (!store_) {
            return false;
        }

        // Load CA certificate
        if (!loadCACertificate()) {
            return false;
        }

        // Load CRL if enabled
        if (config_.checkCRL && !config_.crlPath.empty()) {
            if (!loadCRL()) {
                return false;
            }
        }

        return true;
    }

    AuthResult authenticate(const AuthenticationContext& context) {
        // Parse certificate from credentials
        const unsigned char* data = context.credentials.data();
        X509* cert = d2i_X509(nullptr, &data, context.credentials.size());
        if (!cert) {
            return AuthResult::Failure("Invalid certificate format: " + getOpenSSLError());
        }
        
        std::unique_ptr<X509, decltype(&X509_free)> certPtr(cert, X509_free);

        // Create verification context
        X509_STORE_CTX* ctx = X509_STORE_CTX_new();
        if (!ctx) {
            return AuthResult::Failure("Failed to create verification context: " + getOpenSSLError());
        }
        
        std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> 
            ctxPtr(ctx, X509_STORE_CTX_free);

        // Initialize verification context
        if (!X509_STORE_CTX_init(ctx, store_, cert, nullptr)) {
            return AuthResult::Failure("Failed to initialize verification context: " + getOpenSSLError());
        }

        // Set verification flags
        unsigned long flags = X509_V_FLAG_CRL_CHECK;
        if (!config_.allowSelfSigned) {
            flags |= X509_V_FLAG_X509_STRICT;
        }
        X509_STORE_CTX_set_flags(ctx, flags);

        // Verify certificate
        if (X509_verify_cert(ctx) != 1) {
            int err = X509_STORE_CTX_get_error(ctx);
            return AuthResult::Failure("Certificate verification failed: " + 
                                     std::string(X509_verify_cert_error_string(err)));
        }

        // Check validity period
        if (!checkValidityPeriod(cert, config_.maxValidityDays)) {
            return AuthResult::Failure("Certificate validity period check failed");
        }

        // Check DN restrictions
        if (!isAllowedDN(cert, config_.allowedDNs)) {
            return AuthResult::Failure("Certificate DN not in allowed list");
        }

        // Extract agent ID from certificate (using subject CN)
        X509_NAME* subject = X509_get_subject_name(cert);
        char commonName[256];
        X509_NAME_get_text_by_NID(subject, NID_commonName, commonName, sizeof(commonName));

        return AuthResult::Success(std::string(commonName));
    }

    std::string getMethodName() const {
        return "certificate";
    }

private:
    bool loadCACertificate() {
        FILE* fp = fopen(config_.caPath.c_str(), "r");
        if (!fp) {
            return false;
        }
        
        X509* ca = PEM_read_X509(fp, nullptr, nullptr, nullptr);
        fclose(fp);
        
        if (!ca) {
            return false;
        }
        
        std::unique_ptr<X509, decltype(&X509_free)> caPtr(ca, X509_free);
        
        if (X509_STORE_add_cert(store_, ca) != 1) {
            return false;
        }
        
        return true;
    }

    bool loadCRL() {
        FILE* fp = fopen(config_.crlPath.c_str(), "r");
        if (!fp) {
            return false;
        }
        
        X509_CRL* crl = PEM_read_X509_CRL(fp, nullptr, nullptr, nullptr);
        fclose(fp);
        
        if (!crl) {
            return false;
        }
        
        std::unique_ptr<X509_CRL, decltype(&X509_CRL_free)> crlPtr(crl, X509_CRL_free);
        
        if (X509_STORE_add_crl(store_, crl) != 1) {
            return false;
        }
        
        return true;
    }

    CertificateAuthConfig config_;
    X509_STORE* store_;
};

// CertificateAuthProvider implementation
CertificateAuthProvider::CertificateAuthProvider(CertificateAuthConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

CertificateAuthProvider::~CertificateAuthProvider() = default;

bool CertificateAuthProvider::initialize() {
    return impl_->initialize();
}

AuthResult CertificateAuthProvider::authenticate(const AuthenticationContext& context) {
    return impl_->authenticate(context);
}

std::string CertificateAuthProvider::getMethodName() const {
    return impl_->getMethodName();
}

} // namespace core
} // namespace xenocomm 