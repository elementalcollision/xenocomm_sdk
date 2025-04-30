# XenoComm SDK Examples

This directory contains example applications and code snippets demonstrating the usage of XenoComm SDK.

## Examples Overview

### Basic Examples (`basic/`)
- Simple agent-to-agent communication
- Basic capability discovery
- Protocol negotiation examples
- Data encoding/decoding samples

### Advanced Examples (Coming Soon)
- Multi-agent communication networks
- Custom protocol evolution
- Performance optimization patterns
- Security implementation examples

### Benchmarks (Coming Soon)
- Performance comparison with HTTP/REST
- Latency measurements
- Resource utilization tests
- Protocol efficiency analysis

## Running the Examples

Each example directory contains its own README with specific instructions, but generally:

```bash
# Build all examples
cmake -B build -DBUILD_EXAMPLES=ON
cmake --build build

# Run a specific example
./build/examples/basic/simple_communication

# Run all examples
cd build/examples
ctest --output-on-failure
```

## Example Requirements

- Built XenoComm SDK
- C++17 compatible compiler
- CMake 3.15 or newer
- Python 3.8+ (for Python examples)

## Contributing Examples

We welcome new examples! When contributing:

1. Create a new directory for your example
2. Include a README.md explaining:
   - Purpose of the example
   - How to build and run
   - Expected output
   - Any special requirements
3. Keep the code well-documented
4. Include test cases
5. Follow our [Contributing Guidelines](../CONTRIBUTING.md)

## Example TODOs

- [ ] Add basic communication example
- [ ] Add capability discovery example
- [ ] Add protocol negotiation example
- [ ] Add data encoding example
- [ ] Add benchmark suite
- [ ] Add Python binding examples
- [ ] Add security implementation examples 