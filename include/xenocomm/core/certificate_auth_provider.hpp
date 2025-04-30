#pragma once

#include "xenocomm/core/authentication_manager.hpp"
#include <openssl/x509.h>
#include <string>
#include <vector>
#include <memory>

namespace xenocomm {
namespace core {

/**
 * @brief Configuration for certificate-based authentication
 */
struct CertificateAuthConfig {
    std::string caPath;                // Path to CA certificate
    std::string crlPath;               // Path to Certificate Revocation List (optional)
    bool checkCRL{true};               // Whether to check CRL
    bool allowSelfSigned{false};       // Whether to allow self-signed certificates
    std::vector<std::string> allowedDNs; // List of allowed Distinguished Names
    int64_t maxValidityDays{365};      // Maximum certificate validity period in days
};

/**
 * @brief Provider for certificate-based authentication
 */
class CertificateAuthProvider : public AuthenticationProvider {
public:
    explicit CertificateAuthProvider(CertificateAuthConfig config);
    ~CertificateAuthProvider() override;

    // AuthenticationProvider interface
    bool initialize() override;
    AuthResult authenticate(const AuthenticationContext& context) override;
    std::string getMethodName() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace core
} // namespace xenocomm 