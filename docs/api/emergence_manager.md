# EmergenceManager API Reference

## Overview

The EmergenceManager module provides a robust framework for protocol evolution based on feedback and performance metrics. It enables dynamic protocol adaptation while maintaining system stability through various safeguard mechanisms.

## Core Classes

### EmergenceManager

Main class responsible for managing protocol variants and their evolution.

```cpp
class EmergenceManager {
public:
    EmergenceManager(const EmergenceConfig& config);
    
    // Variant Management
    std::string proposeVariant(const ProtocolVariant& variant, const std::string& description);
    std::optional<ProtocolVariant> getVariant(const std::string& id) const;
    std::vector<ProtocolVariant> listVariants(VariantStatus status = ANY) const;
    
    // Performance Tracking
    void logPerformance(const std::string& variantId, const PerformanceMetrics& metrics);
    PerformanceHistory getVariantPerformance(const std::string& variantId) const;
    
    // Agent Interface
    void registerAgent(const std::string& agentId, const AgentContext& context);
    std::vector<std::string> getRecommendedVariants(const std::string& agentId, size_t maxResults = 5);
    void reportVariantExperience(const std::string& agentId, const std::string& variantId, bool success);
};
```

### ProtocolVariant

Represents a specific variant of the protocol with its associated changes and metadata.

```cpp
struct ProtocolVariant {
    std::string id;                    // Unique identifier
    std::string description;           // Human-readable description
    nlohmann::json changes;            // Protocol modifications
    std::map<std::string, std::string> metadata;  // Additional metadata
    VariantStatus status;              // Current status
};
```

### Performance Metrics

Structure for tracking variant performance metrics.

```cpp
struct PerformanceMetrics {
    double successRate;       // Percentage of successful operations
    double latencyMs;        // Average operation latency
    double resourceUsage;    // Normalized resource consumption
    double throughput;       // Operations per second
    std::map<std::string, double> customMetrics;  // Protocol-specific metrics
};
```

## Safeguard Components

### RollbackManager

Manages protocol state snapshots and rollback capabilities.

```cpp
class RollbackManager {
public:
    RollbackManager(const RollbackConfig& config);
    
    std::string createRollbackPoint(const std::string& variantId, const nlohmann::json& state);
    bool restoreToPoint(const std::string& rollbackId);
    bool verifyRollbackPoint(const std::string& rollbackId) const;
};
```

### CircuitBreaker

Implements circuit breaker pattern for automatic fault detection.

```cpp
class CircuitBreaker {
public:
    CircuitBreaker(const CircuitBreakerConfig& config);
    
    bool isOpen() const;
    void recordSuccess();
    void recordFailure();
    bool allowRequest();
};
```

### CanaryDeployment

Manages gradual deployment and testing of new variants.

```cpp
class CanaryDeployment {
public:
    CanaryDeployment(const CanaryConfig& config);
    
    bool shouldUseCanary();
    void recordMetrics(const PerformanceMetrics& metrics);
    bool isCanaryHealthy() const;
};
```

## Configuration

### EmergenceConfig

```cpp
struct EmergenceConfig {
    size_t maxVariants = 100;              // Maximum number of active variants
    bool enableIncrementalAdoption = true; // Enable gradual variant adoption
    std::string storagePath = "variants/"; // Path for variant storage
    
    RollbackConfig rollbackConfig;         // RollbackManager configuration
    CircuitBreakerConfig circuitConfig;    // CircuitBreaker configuration
    CanaryConfig canaryConfig;             // CanaryDeployment configuration
};
```

## Error Handling

The EmergenceManager uses exceptions to indicate error conditions:

- `VariantException`: Base class for variant-related errors
- `InvalidVariantError`: Thrown when a variant is invalid
- `VariantNotFoundError`: Thrown when a variant cannot be found
- `StateError`: Thrown for state-related issues
- `ConfigurationError`: Thrown for configuration problems

## Thread Safety

The EmergenceManager and its components are thread-safe and can be safely used from multiple threads. Internal synchronization is handled using mutexes and atomic operations where appropriate.

## Performance Considerations

- RollbackManager optimized for large state objects (>100MB)
- B-tree indexing for efficient rollback point lookup
- Configurable caching for frequently accessed data
- Chunked storage with compression for large states
- Memory-mapped file support for efficient access

## Example Usage

```cpp
// Initialize EmergenceManager
EmergenceConfig config;
config.maxVariants = 50;
config.enableIncrementalAdoption = true;

EmergenceManager manager(config);

// Propose a new variant
ProtocolVariant variant;
variant.changes = {{"timeout", 5000}, {"retries", 3}};
variant.description = "Increased timeout and retries";

std::string variantId = manager.proposeVariant(variant, "Performance improvement");

// Log performance metrics
PerformanceMetrics metrics;
metrics.successRate = 0.99;
metrics.latencyMs = 45.0;
metrics.throughput = 1000.0;

manager.logPerformance(variantId, metrics);

// Register an agent
AgentContext context;
context.capabilities = {{"feature1", "enabled"}};
context.preferences = {{"latency", 0.8}, {"throughput", 0.2}};

manager.registerAgent("agent1", context);

// Get recommended variants for the agent
auto recommendations = manager.getRecommendedVariants("agent1", 3);
``` 