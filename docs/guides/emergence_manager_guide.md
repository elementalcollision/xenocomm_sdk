# EmergenceManager User Guide

## Introduction

The EmergenceManager is a sophisticated component of the XenoComm SDK that enables protocol evolution through dynamic adaptation and learning. This guide will walk you through the key concepts, setup, and best practices for using the EmergenceManager in your applications.

## Key Concepts

### Protocol Variants

Protocol variants represent different configurations or implementations of your communication protocol. Each variant can include:
- Modified timeout values
- Different retry strategies
- Alternative encoding schemes
- Custom error handling approaches
- Performance optimizations

### Safeguard Mechanisms

The EmergenceManager includes three key safeguard components:

1. **RollbackManager**
   - Creates state snapshots before variant changes
   - Enables quick recovery from problematic variants
   - Maintains verifiable state history
   - Optimized for large state objects

2. **CircuitBreaker**
   - Prevents cascade failures
   - Automatically detects problematic patterns
   - Provides graceful degradation
   - Supports custom failure criteria

3. **CanaryDeployment**
   - Enables gradual variant adoption
   - Monitors variant performance
   - Supports automatic rollback
   - Configurable deployment strategies

## Getting Started

### Installation

The EmergenceManager is included in the XenoComm SDK. Ensure you have the following dependencies:
```cpp
#include <xenocomm/extensions/emergence_manager.hpp>
#include <xenocomm/core/circuit_breaker.hpp>
#include <xenocomm/extensions/rollback_manager.hpp>
```

### Basic Configuration

```cpp
// Create configuration
EmergenceConfig config;
config.maxVariants = 50;  // Maximum number of active variants
config.enableIncrementalAdoption = true;
config.storagePath = "variants/";  // Where to store variant data

// Configure safeguards
config.rollbackConfig.maxPoints = 100;  // Maximum rollback points
config.rollbackConfig.compressionEnabled = true;

config.circuitConfig.failureThreshold = 5;
config.circuitConfig.resetTimeout = std::chrono::seconds(30);

config.canaryConfig.initialPercentage = 10;
config.canaryConfig.rampUpSteps = 5;

// Initialize manager
EmergenceManager manager(config);
```

## Working with Variants

### Creating a New Variant

```cpp
// Define variant changes
ProtocolVariant variant;
variant.changes = {
    {"timeout", 5000},
    {"retries", 3},
    {"encoding", "protobuf"},
    {"compression", true}
};
variant.description = "Optimized for high-latency networks";

// Propose the variant
std::string variantId = manager.proposeVariant(variant, "Performance optimization");
```

### Monitoring Performance

```cpp
// Log performance metrics
PerformanceMetrics metrics;
metrics.successRate = 0.99;
metrics.latencyMs = 45.0;
metrics.throughput = 1000.0;
metrics.customMetrics = {
    {"compression_ratio", 0.65},
    {"memory_usage_mb", 256.0}
};

manager.logPerformance(variantId, metrics);

// Get performance history
auto history = manager.getVariantPerformance(variantId);
```

### Agent Integration

```cpp
// Register an agent
AgentContext context;
context.capabilities = {
    {"feature1", "enabled"},
    {"compression", "supported"},
    {"max_message_size", "1048576"}
};
context.preferences = {
    {"latency", 0.8},
    {"throughput", 0.2}
};

manager.registerAgent("agent1", context);

// Get recommended variants
auto recommendations = manager.getRecommendedVariants("agent1", 3);

// Report experience
manager.reportVariantExperience("agent1", variantId, true);
```

## Best Practices

### State Management

1. **Regular Snapshots**
   ```cpp
   // Create rollback points at key state changes
   std::string rollbackId = manager.getRollbackManager().createRollbackPoint(
       variantId,
       currentState
   );
   ```

2. **Verify State Integrity**
   ```cpp
   // Verify rollback points periodically
   bool isValid = manager.getRollbackManager().verifyRollbackPoint(rollbackId);
   ```

### Circuit Breaker Usage

1. **Request Protection**
   ```cpp
   if (manager.getCircuitBreaker().allowRequest()) {
       try {
           // Attempt operation
           performOperation();
           manager.getCircuitBreaker().recordSuccess();
       } catch (const std::exception& e) {
           manager.getCircuitBreaker().recordFailure();
           // Handle failure
       }
   }
   ```

2. **Custom Failure Criteria**
   ```cpp
   CircuitBreakerConfig config;
   config.failurePredicate = [](const OperationResult& result) {
       return result.latency > 1000 || !result.success;
   };
   ```

### Canary Deployment

1. **Gradual Rollout**
   ```cpp
   if (manager.getCanaryDeployment().shouldUseCanary()) {
       // Use new variant
       useVariant(newVariantId);
   } else {
       // Use stable variant
       useVariant(stableVariantId);
   }
   ```

2. **Health Monitoring**
   ```cpp
   // Record metrics for canary monitoring
   manager.getCanaryDeployment().recordMetrics(metrics);
   
   if (!manager.getCanaryDeployment().isCanaryHealthy()) {
       // Trigger rollback
       manager.getRollbackManager().restoreToPoint(lastStablePoint);
   }
   ```

## Error Handling

```cpp
try {
    manager.proposeVariant(variant, "New optimization");
} catch (const InvalidVariantError& e) {
    // Handle invalid variant configuration
} catch (const StateError& e) {
    // Handle state management issues
} catch (const ConfigurationError& e) {
    // Handle configuration problems
}
```

## Performance Optimization

1. **State Storage**
   - Use compression for large states
   - Configure appropriate chunk sizes
   - Enable memory mapping for large datasets

2. **Caching**
   - Configure cache sizes based on memory availability
   - Use selective caching for frequently accessed data
   - Monitor cache hit rates

3. **Monitoring**
   - Track key performance indicators
   - Set up alerting for anomalies
   - Maintain performance logs

## Troubleshooting

### Common Issues

1. **High Memory Usage**
   - Adjust cache sizes
   - Enable compression
   - Review state retention policies

2. **Slow Variant Switching**
   - Optimize state serialization
   - Use incremental state updates
   - Configure appropriate chunk sizes

3. **Circuit Breaker Triggers**
   - Review failure thresholds
   - Analyze failure patterns
   - Adjust reset timeouts

### Debugging

1. **Enable Debug Logging**
   ```cpp
   EmergenceConfig config;
   config.logLevel = LogLevel::Debug;
   config.logFile = "emergence_debug.log";
   ```

2. **Performance Profiling**
   ```cpp
   // Enable performance tracking
   config.enableProfiling = true;
   config.profilingInterval = std::chrono::seconds(1);
   ```

## Advanced Topics

### Custom Metrics

```cpp
// Define custom metrics
struct CustomMetrics : public PerformanceMetrics {
    double compressionRatio;
    double messageSize;
    double processingTime;
};

// Log custom metrics
CustomMetrics metrics;
metrics.compressionRatio = 0.75;
metrics.messageSize = 1024;
metrics.processingTime = 0.05;

manager.logPerformance(variantId, metrics);
```

### State Migration

```cpp
// Define migration strategy
auto migrationStrategy = [](const nlohmann::json& oldState) {
    nlohmann::json newState = oldState;
    // Perform state transformation
    return newState;
};

// Register migration
manager.registerStateMigration(
    "v1", "v2",
    migrationStrategy
);
```

### Custom Variant Selection

```cpp
// Define custom selection strategy
auto selector = [](const std::vector<ProtocolVariant>& variants,
                  const AgentContext& context) {
    // Implement custom selection logic
    return selectedVariant;
};

manager.setVariantSelector(selector);
```

## Conclusion

The EmergenceManager provides a powerful framework for protocol evolution while maintaining system stability. By following these guidelines and best practices, you can effectively leverage its capabilities to create robust and adaptive communication systems. 