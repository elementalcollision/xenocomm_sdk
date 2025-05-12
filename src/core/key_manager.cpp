#include "xenocomm/core/key_manager.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/dh.h>
#include <openssl/ec.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <ctime>
#include <uuid/uuid.h>

namespace xenocomm {
namespace core {

// Forward declaration of implementation class
class KeyManagerImpl {
public:
    explicit KeyManagerImpl(const SecurityConfig& config);
    ~KeyManagerImpl();

    Result<std::vector<uint8_t>> generateSymmetricKey(size_t keySize);
    Result<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> generateAsymmetricKeyPair(size_t keySize);
    Result<std::vector<uint8_t>> performDHKeyExchange(const std::vector<uint8_t>& peerPublicKey);
    Result<std::vector<uint8_t>> performECDHKeyExchange(const std::vector<uint8_t>& peerPublicKey);

private:
    const SecurityConfig& config_;
    EVP_PKEY_CTX* dhCtx_;
    EVP_PKEY_CTX* ecdhCtx_;
    
    void initializeOpenSSL();
    void cleanupOpenSSL();
};

KeyManagerImpl::KeyManagerImpl(const SecurityConfig& config) 
    : config_(config), dhCtx_(nullptr), ecdhCtx_(nullptr) {
    initializeOpenSSL();
}

KeyManagerImpl::~KeyManagerImpl() {
    cleanupOpenSSL();
}

void KeyManagerImpl::initializeOpenSSL() {
    // Initialize OpenSSL
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
}

void KeyManagerImpl::cleanupOpenSSL() {
    if (dhCtx_) {
        EVP_PKEY_CTX_free(dhCtx_);
        dhCtx_ = nullptr;
    }
    if (ecdhCtx_) {
        EVP_PKEY_CTX_free(ecdhCtx_);
        ecdhCtx_ = nullptr;
    }
    EVP_cleanup();
    ERR_free_strings();
}

Result<std::vector<uint8_t>> KeyManagerImpl::generateSymmetricKey(size_t keySize) {
    std::vector<uint8_t> key(keySize / 8);
    if (RAND_bytes(key.data(), key.size()) != 1) {
        return Result<std::vector<uint8_t>>(std::string("Failed to generate symmetric key"));
    }
    return Result<std::vector<uint8_t>>(std::move(key));
}

Result<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> 
KeyManagerImpl::generateAsymmetricKeyPair(size_t keySize) {
    (void)keySize; // Suppress unused parameter warning
    
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) {
        return Result<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>(
            std::string("Failed to create key generation context"));
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return Result<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>(
            std::string("Failed to initialize key generation"));
    }

    EVP_PKEY* key = nullptr;
    if (EVP_PKEY_keygen(ctx, &key) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return Result<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>(
            std::string("Failed to generate key pair"));
    }

    // Extract public and private keys
    BIO* pubBio = BIO_new(BIO_s_mem());
    BIO* privBio = BIO_new(BIO_s_mem());
    
    PEM_write_bio_PUBKEY(pubBio, key);
    PEM_write_bio_PrivateKey(privBio, key, nullptr, nullptr, 0, nullptr, nullptr);

    char* pubData;
    char* privData;
    long pubLen = BIO_get_mem_data(pubBio, &pubData);
    long privLen = BIO_get_mem_data(privBio, &privData);

    std::vector<uint8_t> publicKey(pubData, pubData + pubLen);
    std::vector<uint8_t> privateKey(privData, privData + privLen);

    BIO_free(pubBio);
    BIO_free(privBio);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(ctx);

    return Result<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>(
        std::make_pair(std::move(publicKey), std::move(privateKey)));
}

Result<std::vector<uint8_t>> KeyManagerImpl::performDHKeyExchange(
    const std::vector<uint8_t>& peerPublicKey) {
    // Implementation of Diffie-Hellman key exchange
    EVP_PKEY* params = nullptr;
    EVP_PKEY* privKey = nullptr;
    EVP_PKEY* peerKey = nullptr;
    EVP_PKEY_CTX* ctx = nullptr;
    unsigned char* secret = nullptr;
    size_t secretLen;
    Result<std::vector<uint8_t>> result(std::string("DH key exchange failed"));

    do {
        // Generate DH parameters
        if (!(ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_DH, nullptr))) break;
        if (EVP_PKEY_paramgen_init(ctx) <= 0) break;
        if (EVP_PKEY_CTX_set_dh_paramgen_prime_len(ctx, 2048) <= 0) break;
        if (EVP_PKEY_paramgen(ctx, &params) <= 0) break;

        // Generate private key
        EVP_PKEY_CTX_free(ctx);
        if (!(ctx = EVP_PKEY_CTX_new(params, nullptr))) break;
        if (EVP_PKEY_keygen_init(ctx) <= 0) break;
        if (EVP_PKEY_keygen(ctx, &privKey) <= 0) break;

        // Load peer's public key
        BIO* peerBio = BIO_new_mem_buf(peerPublicKey.data(), static_cast<int>(peerPublicKey.size()));
        if (!peerBio) break;
        
        // The correct way to use PEM_read_bio_* functions is to pass NULL for the second arg if we want a new object
        peerKey = nullptr;
        peerKey = PEM_read_bio_PUBKEY(peerBio, &peerKey, nullptr, nullptr);
        BIO_free(peerBio);
        if (!peerKey) break;

        // Derive shared secret
        EVP_PKEY_CTX_free(ctx);
        if (!(ctx = EVP_PKEY_CTX_new(privKey, nullptr))) break;
        if (EVP_PKEY_derive_init(ctx) <= 0) break;
        if (EVP_PKEY_derive_set_peer(ctx, peerKey) <= 0) break;
        if (EVP_PKEY_derive(ctx, nullptr, &secretLen) <= 0) break;
        
        secret = static_cast<unsigned char*>(OPENSSL_malloc(secretLen));
        if (!secret) break;
        
        if (EVP_PKEY_derive(ctx, secret, &secretLen) <= 0) break;

        // Success - create result
        result = Result<std::vector<uint8_t>>(
            std::vector<uint8_t>(secret, secret + secretLen));
        
    } while (false);

    // Cleanup
    if (secret) OPENSSL_free(secret);
    if (ctx) EVP_PKEY_CTX_free(ctx);
    if (privKey) EVP_PKEY_free(privKey);
    if (peerKey) EVP_PKEY_free(peerKey);
    if (params) EVP_PKEY_free(params);

    return result;
}

Result<std::vector<uint8_t>> KeyManagerImpl::performECDHKeyExchange(
    const std::vector<uint8_t>& peerPublicKey) {
    // Implementation of ECDH key exchange
    EVP_PKEY* privKey = nullptr;
    EVP_PKEY* peerKey = nullptr;
    EVP_PKEY_CTX* ctx = nullptr;
    unsigned char* secret = nullptr;
    size_t secretLen;
    Result<std::vector<uint8_t>> result(std::string("ECDH key exchange failed"));

    do {
        // Generate EC key pair
        if (!(ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr))) break;
        if (EVP_PKEY_paramgen_init(ctx) <= 0) break;
        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0) break;
        if (EVP_PKEY_keygen_init(ctx) <= 0) break;
        if (EVP_PKEY_keygen(ctx, &privKey) <= 0) break;

        // Load peer's public key
        BIO* peerBio = BIO_new_mem_buf(peerPublicKey.data(), static_cast<int>(peerPublicKey.size()));
        if (!peerBio) break;
        
        // The correct way to use PEM_read_bio_* functions is to pass NULL for the second arg if we want a new object
        peerKey = nullptr;
        peerKey = PEM_read_bio_PUBKEY(peerBio, &peerKey, nullptr, nullptr);
        BIO_free(peerBio);
        if (!peerKey) break;

        // Derive shared secret
        EVP_PKEY_CTX_free(ctx);
        if (!(ctx = EVP_PKEY_CTX_new(privKey, nullptr))) break;
        if (EVP_PKEY_derive_init(ctx) <= 0) break;
        if (EVP_PKEY_derive_set_peer(ctx, peerKey) <= 0) break;
        if (EVP_PKEY_derive(ctx, nullptr, &secretLen) <= 0) break;
        
        secret = static_cast<unsigned char*>(OPENSSL_malloc(secretLen));
        if (!secret) break;
        
        if (EVP_PKEY_derive(ctx, secret, &secretLen) <= 0) break;

        // Success - create result
        result = Result<std::vector<uint8_t>>(
            std::vector<uint8_t>(secret, secret + secretLen));
        
    } while (false);

    // Cleanup
    if (secret) OPENSSL_free(secret);
    if (ctx) EVP_PKEY_CTX_free(ctx);
    if (privKey) EVP_PKEY_free(privKey);
    if (peerKey) EVP_PKEY_free(peerKey);

    return result;
}

// KeyManager implementation

KeyManager::KeyManager(const SecurityConfig& config)
    : config_(config), impl_(std::make_unique<KeyManagerImpl>(config)) {
}

Result<KeyData> KeyManager::generateKey(const KeyGenParams& params) {
    auto validationResult = validateKeyParams(params);
    if (validationResult.has_error()) {
        return Result<KeyData>(validationResult.error());
    }

    KeyData keyData;
    keyData.type = params.type;
    keyData.keyId = generateKeyId();
    keyData.creationTime = std::chrono::system_clock::now();
    keyData.expiryTime = keyData.creationTime + params.validity;
    keyData.purpose = params.purpose;

    if (params.type == KeyType::SYMMETRIC) {
        auto result = impl_->generateSymmetricKey(params.keySize);
        if (result.has_error()) {
            return Result<KeyData>(result.error());
        }
        keyData.keyMaterial = std::move(result.value());
    } else {
        auto result = impl_->generateAsymmetricKeyPair(params.keySize);
        if (result.has_error()) {
            return Result<KeyData>(result.error());
        }
        if (params.type == KeyType::ASYMMETRIC_PUB) {
            keyData.keyMaterial = std::move(result.value().first);
        } else {
            keyData.keyMaterial = std::move(result.value().second);
        }
    }

    auto storeResult = storeKey(keyData);
    if (storeResult.has_error()) {
        return Result<KeyData>(storeResult.error());
    }

    return Result<KeyData>(std::move(keyData));
}

Result<KeyExchangeResult> KeyManager::initiateKeyExchange(const KeyExchangeParams& params) {
    // Generate ephemeral key pair for the exchange
    KeyGenParams keyParams{
        KeyType::ASYMMETRIC_PRIV,
        params.keySize,
        params.validity,
        std::string("Key exchange with ") + params.peerId
    };

    auto keyResult = generateKey(keyParams);
    if (!keyResult.has_value()) {
        return Result<KeyExchangeResult>(keyResult.error());
    }

    // Perform key exchange using ECDH
    auto exchangeResult = impl_->performECDHKeyExchange(keyResult.value().keyMaterial);
    if (!exchangeResult.has_value()) {
        return Result<KeyExchangeResult>(exchangeResult.error());
    }

    // Create and store the shared key
    KeyData sharedKeyData;
    sharedKeyData.type = KeyType::SYMMETRIC;
    sharedKeyData.keyId = generateKeyId();
    sharedKeyData.creationTime = std::chrono::system_clock::now();
    sharedKeyData.expiryTime = sharedKeyData.creationTime + params.validity;
    sharedKeyData.keyMaterial = std::move(exchangeResult.value());
    sharedKeyData.purpose = std::string("Shared key with ") + params.peerId;

    auto storeResult = storeKey(sharedKeyData);
    if (!storeResult.has_value()) {
        return Result<KeyExchangeResult>(storeResult.error());
    }

    KeyExchangeResult result{
        sharedKeyData.keyId,
        std::move(sharedKeyData),
        params.peerId
    };

    return Result<KeyExchangeResult>(std::move(result));
}

Result<KeyExchangeResult> KeyManager::respondToKeyExchange(
    const std::string& exchangeId, bool accept) {
    if (!accept) {
        return Result<KeyExchangeResult>(std::string("Key exchange rejected"));
    }

    auto keyResult = getKey(exchangeId);
    if (!keyResult.has_value()) {
        return Result<KeyExchangeResult>(keyResult.error());
    }

    // Verify the key is valid for exchange
    if (keyResult.value().type != KeyType::ASYMMETRIC_PUB) {
        return Result<KeyExchangeResult>(std::string("Invalid key type for exchange"));
    }

    // Perform ECDH key exchange
    auto exchangeResult = impl_->performECDHKeyExchange(keyResult.value().keyMaterial);
    if (!exchangeResult.has_value()) {
        return Result<KeyExchangeResult>(exchangeResult.error());
    }

    // Create and store the shared key
    KeyData sharedKeyData;
    sharedKeyData.type = KeyType::SYMMETRIC;
    sharedKeyData.keyId = generateKeyId();
    sharedKeyData.creationTime = std::chrono::system_clock::now();
    sharedKeyData.expiryTime = keyResult.value().expiryTime;
    sharedKeyData.keyMaterial = std::move(exchangeResult.value());
    sharedKeyData.purpose = "Shared key from exchange " + exchangeId;

    auto storeResult = storeKey(sharedKeyData);
    if (!storeResult.has_value()) {
        return Result<KeyExchangeResult>(storeResult.error());
    }

    KeyExchangeResult result{
        sharedKeyData.keyId,
        std::move(sharedKeyData),
        exchangeId
    };

    return Result<KeyExchangeResult>(std::move(result));
}

Result<KeyData> KeyManager::rotateKey(const std::string& keyId, const KeyGenParams& params) {
    std::lock_guard<std::mutex> lock(keyStoreMutex_);
    
    auto it = keyStore_.find(keyId);
    if (it == keyStore_.end()) {
        return Result<KeyData>(std::string("Key not found"));
    }

    // Generate new key
    auto newKeyResult = generateKey(params);
    if (!newKeyResult.has_value()) {
        return newKeyResult;
    }

    // Mark old key for expiry
    it->second.expiryTime = std::chrono::system_clock::now() + std::chrono::hours(24);

    return newKeyResult;
}

Result<void> KeyManager::revokeKey(const std::string& keyId, 
    const std::optional<std::string>& reason) {
    std::lock_guard<std::mutex> lock(keyStoreMutex_);
    
    auto it = keyStore_.find(keyId);
    if (it == keyStore_.end()) {
        return Result<void>(std::string("Key not found"));
    }

    it->second.isRevoked = true;
    it->second.expiryTime = std::chrono::system_clock::now();
    if (reason) {
        it->second.purpose = *reason;
    }

    return Result<void>(std::string("Key revoked"));
}

Result<KeyData> KeyManager::getKey(const std::string& keyId) const {
    std::lock_guard<std::mutex> lock(keyStoreMutex_);
    
    auto it = keyStore_.find(keyId);
    if (it == keyStore_.end()) {
        return Result<KeyData>(std::string("Key not found"));
    }

    if (it->second.isRevoked) {
        return Result<KeyData>(std::string("Key is revoked"));
    }

    if (it->second.expiryTime <= std::chrono::system_clock::now()) {
        return Result<KeyData>(std::string("Key is expired"));
    }

    return Result<KeyData>(it->second);
}

std::vector<KeyData> KeyManager::listActiveKeys() const {
    std::lock_guard<std::mutex> lock(keyStoreMutex_);
    std::vector<KeyData> activeKeys;
    auto now = std::chrono::system_clock::now();

    for (const auto& pair : keyStore_) {
        if (!pair.second.isRevoked && pair.second.expiryTime > now) {
            activeKeys.push_back(pair.second);
        }
    }

    return activeKeys;
}

Result<bool> KeyManager::verifyKey(const std::string& keyId) const {
    auto keyResult = getKey(keyId);
    if (!keyResult.has_value()) {
        return Result<bool>(false);
    }

    // Additional verification could be added here
    return Result<bool>(true);
}

Result<std::vector<uint8_t>> KeyManager::exportKey(
    const std::string& keyId, const std::string& format) const {
    auto keyResult = getKey(keyId);
    if (!keyResult.has_value()) {
        return Result<std::vector<uint8_t>>(keyResult.error());
    }

    // For now, just return the raw key material
    // In a real implementation, we would format according to the requested format
    return Result<std::vector<uint8_t>>(keyResult.value().keyMaterial);
}

Result<KeyData> KeyManager::importKey(
    const std::vector<uint8_t>& keyData,
    const std::string& format,
    KeyType type) {
    // Create key data structure
    KeyData key;
    key.type = type;
    key.keyId = generateKeyId();
    key.creationTime = std::chrono::system_clock::now();
    key.expiryTime = key.creationTime + std::chrono::hours(24); // Default 24h validity
    key.keyMaterial = keyData;

    auto result = storeKey(key);
    if (!result.has_value()) {
        return Result<KeyData>(result.error());
    }

    return Result<KeyData>(std::move(key));
}

std::string KeyManager::generateKeyId() const {
    uuid_t uuid;
    uuid_generate(uuid);
    
    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);
    
    return std::string(uuid_str);
}

Result<void> KeyManager::validateKeyParams(const KeyGenParams& params) const {
    if (params.keySize == 0) {
        return Result<void>(std::string("Key size cannot be zero"));
    }

    if (params.validity.count() <= 0) {
        return Result<void>(std::string("Key validity period must be positive"));
    }

    switch (params.type) {
        case KeyType::SYMMETRIC:
            if (params.keySize != 128 && params.keySize != 192 && params.keySize != 256) {
                return Result<void>(std::string("Invalid symmetric key size"));
            }
            break;
        case KeyType::ASYMMETRIC_PUB:
        case KeyType::ASYMMETRIC_PRIV:
            if (params.keySize < 2048) {
                return Result<void>(std::string("Asymmetric key size must be at least 2048 bits"));
            }
            break;
    }

    return Result<void>(std::string("Key parameters are valid"));
}

Result<void> KeyManager::storeKey(const KeyData& keyData) {
    std::lock_guard<std::mutex> lock(keyStoreMutex_);
    keyStore_[keyData.keyId] = keyData;
    return Result<void>(std::string("Key stored successfully"));
}

void KeyManager::cleanupKeys() {
    std::lock_guard<std::mutex> lock(keyStoreMutex_);
    auto now = std::chrono::system_clock::now();
    
    for (auto it = keyStore_.begin(); it != keyStore_.end();) {
        if (it->second.isRevoked || it->second.expiryTime <= now) {
            it = keyStore_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace core
} // namespace xenocomm 