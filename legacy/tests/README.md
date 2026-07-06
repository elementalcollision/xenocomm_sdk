# XenoComm SDK Tests

This directory contains the test suite for XenoComm SDK, including unit tests, integration tests, and performance tests.

## Test Structure

### Unit Tests (`unit/`)
- Tests for individual components and classes
- Mock-based testing of network operations
- Error handling verification
- Edge case testing

### Integration Tests (`integration/`)
- End-to-end communication tests
- Multi-module interaction tests
- Protocol negotiation tests
- Error recovery scenarios

### Performance Tests (`performance/`)
- Latency measurements
- Throughput testing
- Resource utilization monitoring
- Comparative benchmarks

## Running Tests

```bash
# Build tests
cmake -B build -DBUILD_TESTING=ON
cmake --build build

# Run all tests
cd build
ctest --output-on-failure

# Run specific test category
ctest -L unit --output-on-failure
ctest -L integration --output-on-failure
ctest -L performance --output-on-failure

# Run tests with coverage
cmake -B build -DBUILD_TESTING=ON -DENABLE_COVERAGE=ON
cmake --build build
cd build && ctest
gcovr -r .. .
```

## Test Requirements

- Google Test framework
- CMake 3.15 or newer
- Python 3.8+ (for test utilities)
- gcovr (for coverage reports)

## Writing Tests

### Guidelines
1. Test both success and failure cases
2. Mock external dependencies
3. Use meaningful test names
4. Keep tests focused and simple
5. Document test purpose and setup

### Example Test Structure
```cpp
TEST(ComponentName, OperationBeingTested) {
    // Setup
    ComponentName component;
    
    // Exercise
    auto result = component.operation();
    
    // Verify
    EXPECT_EQ(result, expected_value);
    
    // Cleanup (if needed)
}
```

## Test Coverage Goals

- Minimum 90% line coverage
- 100% coverage of public APIs
- All error paths tested
- All protocol states tested

## Contributing Tests

When adding new tests:
1. Follow existing test patterns
2. Include both positive and negative cases
3. Document any complex test setups
4. Ensure tests are deterministic
5. Add appropriate test labels

## Test TODOs

- [ ] Add basic unit tests for core modules
- [ ] Create integration test framework
- [ ] Set up performance test suite
- [ ] Add network simulation tests
- [ ] Implement coverage reporting
- [ ] Add stress tests
- [ ] Create test utilities 