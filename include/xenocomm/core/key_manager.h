#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <optional>
#include <unordered_map>
#include "xenocomm/utils/result.h"
#include "xenocomm/core/security_config.hpp"

namespace xenocomm {
namespace core {

/**
 * @brief Supported key types for key exchange
 */
enum class KeyType {
    SYMMETRIC,      // For symmetric encryption (e.g., AES keys)
    ASYMMETRIC_PUB, // Public key for asymmetric encryption
    ASYMMETRIC_PRIV // Private key for asymmetric encryption
};

/**
 * @brief Key metadata and storage
 */
struct KeyData {
    std::vector<uint8_t> keyMaterial;  // The actual key bytes
    KeyType type;                      // Type of the key
    std::chrono::system_clock::time_point creationTime;  // When the key was created
    std::chrono::system_clock::time_point expiryTime;    // When the key expires
    std::string keyId;                 // Unique identifier for the key
    bool isRevoked = false;            // Whether the key has been revoked
    std::optional<std::string> purpose;  // Optional purpose/usage of the key
};

/**
 * @brief Parameters for key generation
 */
struct KeyGenParams {
    KeyType type;
    size_t keySize;  // Size in bits
    std::chrono::seconds validity;  // How long the key should be valid
    std::optional<std::string> purpose;  // Optional purpose/usage
};

/**
 * @brief Parameters for key exchange
 */
struct KeyExchangeParams {
    std::string peerId;  // ID of the peer to exchange keys with
    KeyType keyType;     // Type of key to exchange
    size_t keySize;      // Size of the key in bits
    std::chrono::seconds validity;  // Validity period
};

/**
 * @brief Result of a key exchange operation
 */
struct KeyExchangeResult {
    std::string keyId;   // ID of the exchanged key
    KeyData keyData;     // The exchanged key data
    std::string peerId;  // ID of the peer
};

/**
 * @brief Manages cryptographic keys including generation, exchange, and lifecycle
 */
class KeyManager {
public:
    /**
     * @brief Creates a KeyManager instance
     * 
     * @param config Security configuration
     */
    explicit KeyManager(const SecurityConfig& config);
    virtual ~KeyManager() = default;

    /**
     * @brief Generates a new key pair or symmetric key
     * 
     * @param params Parameters for key generation
     * @return Result<KeyData> Generated key or error
     */
    virtual Result<KeyData> generateKey(const KeyGenParams& params);

    /**
     * @brief Initiates a key exchange with a peer
     * 
     * @param params Parameters for the key exchange
     * @return Result<KeyExchangeResult> Exchange result or error
     */
    virtual Result<KeyExchangeResult> initiateKeyExchange(const KeyExchangeParams& params);

    /**
     * @brief Responds to a key exchange request
     * 
     * @param exchangeId ID of the exchange request
     * @param accept Whether to accept the exchange
     * @return Result<KeyExchangeResult> Exchange result or error
     */
    virtual Result<KeyExchangeResult> respondToKeyExchange(
        const std::string& exchangeId, 
        bool accept
    );

    /**
     * @brief Rotates a key, generating a new one and marking the old one for expiry
     * 
     * @param keyId ID of the key to rotate
     * @param params Parameters for the new key
     * @return Result<KeyData> New key data or error
     */
    virtual Result<KeyData> rotateKey(
        const std::string& keyId,
        const KeyGenParams& params
    );

    /**
     * @brief Revokes a key immediately
     * 
     * @param keyId ID of the key to revoke
     * @param reason Optional reason for revocation
     * @return Result<void> Success or error
     */
    virtual Result<void> revokeKey(
        const std::string& keyId,
        const std::optional<std::string>& reason = std::nullopt
    );

    /**
     * @brief Gets a key by its ID
     * 
     * @param keyId ID of the key to retrieve
     * @return Result<KeyData> Key data or error
     */
    virtual Result<KeyData> getKey(const std::string& keyId) const;

    /**
     * @brief Lists all active keys
     * 
     * @return std::vector<KeyData> List of active keys
     */
    virtual std::vector<KeyData> listActiveKeys() const;

    /**
     * @brief Verifies a key's authenticity and status
     * 
     * @param keyId ID of the key to verify
     * @return Result<bool> True if key is valid, false if not, or error
     */
    virtual Result<bool> verifyKey(const std::string& keyId) const;

    /**
     * @brief Exports a key in a secure format
     * 
     * @param keyId ID of the key to export
     * @param format Format to export in (e.g., PEM, DER)
     * @return Result<std::vector<uint8_t>> Exported key data or error
     */
    virtual Result<std::vector<uint8_t>> exportKey(
        const std::string& keyId,
        const std::string& format
    ) const;

    /**
     * @brief Imports a key from external data
     * 
     * @param keyData Raw key data
     * @param format Format of the key data
     * @param type Type of the key being imported
     * @return Result<KeyData> Imported key or error
     */
    virtual Result<KeyData> importKey(
        const std::vector<uint8_t>& keyData,
        const std::string& format,
        KeyType type
    );

protected:
    /**
     * @brief Generates a unique key ID
     * 
     * @return std::string Unique key ID
     */
    virtual std::string generateKeyId() const;

    /**
     * @brief Validates key parameters before generation
     * 
     * @param params Parameters to validate
     * @return Result<void> Success or error with reason
     */
    virtual Result<void> validateKeyParams(const KeyGenParams& params) const;

    /**
     * @brief Securely stores a key
     * 
     * @param keyData Key to store
     * @return Result<void> Success or error
     */
    virtual Result<void> storeKey(const KeyData& keyData);

    /**
     * @brief Performs cleanup of expired and revoked keys
     */
    virtual void cleanupKeys();

private:
    SecurityConfig config_;
    std::unordered_map<std::string, KeyData> keyStore_;
    mutable std::mutex keyStoreMutex_;
    std::unique_ptr<class KeyManagerImpl> impl_;
};

} // namespace core
} // namespace xenocomm 